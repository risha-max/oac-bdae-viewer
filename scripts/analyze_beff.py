#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import re
import struct
from collections import Counter
from pathlib import Path


HEADER_U32_FIELDS = {
    "sectionCount": 16,
    "optionA": 32,
    "emitterCount": 40,
    "blendHint": 48,
    "optionB": 56,
}


def u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        return 0
    return struct.unpack_from("<I", data, offset)[0]


def f32(data: bytes, offset: int) -> float:
    if offset + 4 > len(data):
        return 0.0
    return struct.unpack_from("<f", data, offset)[0]


def parse_strings(data: bytes) -> list[dict]:
    out = []
    for match in re.finditer(rb"[\x20-\x7e]{4,}", data):
        text = match.group().decode("latin1", errors="ignore")
        out.append(
            {
                "offset": match.start(),
                "end": match.end(),
                "len": match.end() - match.start(),
                "text": text,
            }
        )
    return out


def looks_sane_float(value: float) -> bool:
    if not math.isfinite(value):
        return False
    if abs(value) < 1e-9:
        return False
    return -10000.0 <= value <= 10000.0


def scan_numeric_window(data: bytes, start: int, end: int) -> dict:
    nonzero_u32 = []
    float_candidates = []

    for off in range(start, min(end, len(data) - 3), 4):
        raw = u32(data, off)
        if raw != 0:
            nonzero_u32.append({"offset": off, "u32": raw})

        value = f32(data, off)
        if looks_sane_float(value):
            # Keep values that look more like authored params than random integer bit patterns.
            # We keep low-ish magnitude values, plus common 2*pi angles.
            if abs(value) <= 360.0 or abs(value - 6.28318) < 0.01:
                float_candidates.append({"offset": off, "f32": round(value, 6)})

    return {
        "start": start,
        "end": min(end, len(data)),
        "nonzero_u32_count": len(nonzero_u32),
        "nonzero_u32_first": nonzero_u32[:80],
        "float_candidate_count": len(float_candidates),
        "float_candidate_first": float_candidates[:120],
    }


def detect_record16_candidates(data: bytes, start: int, end: int) -> list[dict]:
    out = []
    limit = min(end, len(data))
    for off in range(start, limit - 15, 4):
        key = u32(data, off)
        mid_a = u32(data, off + 4)
        mid_b = u32(data, off + 8)
        raw = u32(data, off + 12)
        val = f32(data, off + 12)

        if key == 0:
            continue
        if mid_a != 0 or mid_b != 0:
            continue

        # Common BEFF pattern seen in weapon files:
        # [key:u32][0][0][value:u32/f32]
        if raw == 0:
            continue

        sane = looks_sane_float(val) and abs(val) <= 1000.0
        out.append(
            {
                "offset": off,
                "key": key,
                "raw_u32": raw,
                "value_f32": round(val, 6) if sane else None,
                "value_u32": raw,
            }
        )
    return out


def rank_role_candidates(record16: list[dict], role: str) -> list[dict]:
    ranked = []

    for rec in record16:
        key = rec["key"]
        v = rec["value_f32"]
        u = rec["value_u32"]
        score = 0.0
        why = []

        if v is not None:
            if role == "size":
                if 0.05 <= v <= 32.0:
                    score += 2.0
                    why.append("size-like range")
                if 0.2 <= v <= 8.0:
                    score += 2.0
                    why.append("common sprite size range")
                if key in (15, 23, 30, 97, 255):
                    score += 0.8
                    why.append("common key id")
                if abs(v - 6.28318) < 0.02:
                    score -= 2.0
                    why.append("likely angle constant")

            elif role == "life":
                if 0.03 <= v <= 10.0:
                    score += 2.0
                    why.append("life-like range")
                if 0.2 <= v <= 3.0:
                    score += 2.0
                    why.append("common particle lifetime")
                if key in (15, 23, 30, 76, 97):
                    score += 0.8
                    why.append("common key id")

            elif role == "speed":
                if 0.01 <= v <= 60.0:
                    score += 2.0
                    why.append("speed-like range")
                if 0.05 <= v <= 8.0:
                    score += 2.0
                    why.append("common weapon fx velocity")
                if key in (3, 8, 23, 76, 97):
                    score += 0.8
                    why.append("common key id")
                if abs(v - 6.28318) < 0.02:
                    score -= 1.6
                    why.append("likely angle constant")

            elif role == "spread":
                if 0.001 <= v <= 3.0:
                    score += 2.0
                    why.append("spread-like range")
                if 0.01 <= v <= 1.0:
                    score += 2.0
                    why.append("common spread/jitter range")
                if key in (1, 2, 3, 15, 23):
                    score += 0.8
                    why.append("common key id")
                if abs(v - 6.28318) < 0.02:
                    score -= 1.6
                    why.append("likely angle constant")

            elif role == "rate":
                if 0.1 <= v <= 300.0:
                    score += 1.2
                    why.append("rate-like float range")
                if 1.0 <= v <= 60.0:
                    score += 1.0
                    why.append("common spawn rate range")
                if key in (15, 23, 30, 76, 97, 254, 255):
                    score += 0.8
                    why.append("common key id")

        if role == "rate":
            if 1 <= u <= 300:
                score += 2.0
                why.append("rate-like integer range")
            if key in (15, 23, 30, 76, 97, 254, 255):
                score += 0.5

        if score <= 0:
            continue

        ranked.append(
            {
                "offset": rec["offset"],
                "key": key,
                "score": round(score, 3),
                "value_f32": rec["value_f32"],
                "value_u32": rec["value_u32"],
                "why": ", ".join(why[:3]),
            }
        )

    ranked.sort(key=lambda item: item["score"], reverse=True)
    return ranked[:12]


def infer_parameters(record16: list[dict]) -> dict:
    key_hist = Counter(item["key"] for item in record16)
    key_hist_sorted = sorted(key_hist.items(), key=lambda pair: pair[1], reverse=True)

    return {
        "record16_count": len(record16),
        "top_keys": [{"key": key, "count": count} for key, count in key_hist_sorted[:16]],
        "size_candidates": rank_role_candidates(record16, "size"),
        "life_candidates": rank_role_candidates(record16, "life"),
        "speed_candidates": rank_role_candidates(record16, "speed"),
        "spread_candidates": rank_role_candidates(record16, "spread"),
        "rate_candidates": rank_role_candidates(record16, "rate"),
    }


def derive_blocks(data: bytes, strings: list[dict]) -> list[dict]:
    texture_strings = [
        s
        for s in strings
        if any(ext in s["text"].lower() for ext in [".tga", ".png", ".dds", ".bmp", ".jpg"])
    ]
    texture_strings.sort(key=lambda item: item["offset"])

    blocks = []
    if not texture_strings:
        blocks.append({"name": "whole_file", "start": 0, "end": len(data), "texture": None})
        return blocks

    # Split the file into texture-anchored blocks.
    for idx, tex in enumerate(texture_strings):
        next_offset = texture_strings[idx + 1]["offset"] if idx + 1 < len(texture_strings) else len(data)
        block_start = max(0, tex["offset"] - 32)
        block_end = next_offset
        blocks.append(
            {
                "name": f"texture_block_{idx}",
                "start": block_start,
                "end": block_end,
                "texture": tex["text"],
                "texture_offset": tex["offset"],
                "declared_name_len_u32": u32(data, tex["offset"] - 4) if tex["offset"] >= 4 else None,
            }
        )
    return blocks


def analyze_file(path: Path) -> dict:
    data = path.read_bytes()
    strings = parse_strings(data)

    header = {
        "size": len(data),
        "magic": data[:4].decode("latin1", errors="ignore") if len(data) >= 4 else "",
        "version_i32": struct.unpack_from("<i", data, 4)[0] if len(data) >= 8 else None,
    }
    for key, offset in HEADER_U32_FIELDS.items():
        header[key] = u32(data, offset)

    blocks = derive_blocks(data, strings)
    parsed_blocks = []
    for block in blocks:
        numeric = scan_numeric_window(data, block["start"], block["end"])
        record16 = detect_record16_candidates(data, block["start"], block["end"])
        parsed_blocks.append(
            {
                **block,
                "numeric": numeric,
                "record16_candidates_first": record16[:120],
                "inferred_parameters": infer_parameters(record16),
            }
        )

    whole_record16 = detect_record16_candidates(data, 0, len(data))

    return {
        "file": str(path),
        "header": header,
        "strings": strings,
        "texture_strings": [
            s for s in strings if any(ext in s["text"].lower() for ext in [".tga", ".png", ".dds", ".bmp", ".jpg"])
        ],
        "blocks": parsed_blocks,
        "whole_file_numeric": scan_numeric_window(data, 0, len(data)),
        "whole_file_record16_count": len(whole_record16),
        "whole_file_record16_first": whole_record16[:200],
        "whole_file_inferred_parameters": infer_parameters(whole_record16),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze OAC .beff file structure and candidate numeric fields")
    parser.add_argument("input", nargs="+", help="Path(s) to .beff file")
    parser.add_argument("--out", help="Write JSON output to this file")
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON")
    args = parser.parse_args()

    result = [analyze_file(Path(item)) for item in args.input]
    payload = json.dumps(result if len(result) > 1 else result[0], indent=2 if args.pretty else None)

    if args.out:
        Path(args.out).write_text(payload + "\n", encoding="utf-8")
    else:
        print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
