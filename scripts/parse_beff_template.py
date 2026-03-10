#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.off = 0

    def _need(self, n: int) -> None:
        if self.off + n > len(self.data):
            raise ValueError(f"Unexpected EOF at {self.off}, need {n} bytes")

    def i32(self) -> int:
        self._need(4)
        v = struct.unpack_from("<i", self.data, self.off)[0]
        self.off += 4
        return v

    def u32(self) -> int:
        self._need(4)
        v = struct.unpack_from("<I", self.data, self.off)[0]
        self.off += 4
        return v

    def u16(self) -> int:
        self._need(2)
        v = struct.unpack_from("<H", self.data, self.off)[0]
        self.off += 2
        return v

    def f32(self) -> float:
        self._need(4)
        v = struct.unpack_from("<f", self.data, self.off)[0]
        self.off += 4
        return v

    def bytes(self, n: int) -> bytes:
        self._need(n)
        b = self.data[self.off : self.off + n]
        self.off += n
        return b

    def tell(self) -> int:
        return self.off

    def remaining(self) -> int:
        return len(self.data) - self.off


def parse_aligned_string(r: Reader) -> dict:
    at = r.tell()
    length = r.i32()
    text = ""
    if length > 0:
        raw = r.bytes(length)
        text = raw.decode("latin1", errors="ignore").rstrip("\x00")
    return {"offset": at, "length": length, "value": text}


def parse_key_data(r: Reader, kind: str) -> dict:
    start = r.tell()
    key_count = r.u32()
    is_loop = r.i32()
    keys = []
    for _ in range(key_count):
        frame = r.i32()
        if kind == "i":
            data = r.i32()
        elif kind == "f":
            data = r.f32()
        elif kind == "3":
            data = [r.f32(), r.f32(), r.f32()]
        else:
            raise ValueError(f"Unknown key kind: {kind}")
        keys.append({"frame": frame, "data": data})
    return {
        "offset": start,
        "kind": kind,
        "key_count": key_count,
        "is_loop": is_loop,
        "keys": keys,
    }


def parse_emitter_data(r: Reader) -> dict:
    return {
        "type": r.i32(),
        "music": r.u16(),
        "is_billboard": r.u16(),
        "follow_flag": r.i32(),
        "emit_volume_random": r.i32(),
        "emit_time": r.i32(),
        "emit_start_time": r.i32(),
        "interval": r.i32(),
        "interval_random": r.i32(),
        "is_particle_die_with_emitter": r.i32(),
        "is_toward_target": r.i32(),
        "speed_all_random": r.i32(),
        "speed_spe_random": r.i32(),
    }


def parse_particle_data(r: Reader) -> dict:
    return {
        "type": r.i32(),
        "follow_flag": r.i32(),
        "life_time": r.i32(),
        "life_time_random": r.i32(),
        "size_random": r.i32(),
        "is_keep_xy_scale": r.i32(),
        "base_angle_x": r.f32(),
        "base_angle_x_random": r.i32(),
        "base_angle_y": r.f32(),
        "base_angle_y_random": r.i32(),
        "base_angle_z": r.f32(),
        "base_angle_z_random": r.i32(),
        "rotate_x_random": r.i32(),
        "rotate_y_random": r.i32(),
        "rotate_z_random": r.i32(),
        "scale_random": r.i32(),
        "mrtl_type": r.i32(),
        "pivot_x": r.i32(),
        "pivot_y": r.i32(),
        "uv_rot": r.i32(),
        "flip_h": r.u16(),
        "flip_v": r.u16(),
    }


def parse_particle(r: Reader) -> dict:
    return {
        "data": parse_particle_data(r),
        "texture": parse_aligned_string(r),
        "color_r": parse_key_data(r, "i"),
        "color_g": parse_key_data(r, "i"),
        "color_b": parse_key_data(r, "i"),
        "color_a": parse_key_data(r, "i"),
        "base_size_x": parse_key_data(r, "f"),
        "base_size_y": parse_key_data(r, "f"),
        "rotation_x": parse_key_data(r, "f"),
        "rotation_y": parse_key_data(r, "f"),
        "rotation_z": parse_key_data(r, "f"),
        "scale_x": parse_key_data(r, "f"),
        "scale_y": parse_key_data(r, "f"),
    }


def parse_affector(r: Reader) -> dict:
    aff = {"id": r.i32(), "type": r.i32()}
    t = aff["type"]
    if t == 0:  # gravity
        aff["strength"] = parse_key_data(r, "f")
        aff["direction"] = parse_key_data(r, "3")
    elif t == 1:  # accelerate
        aff["strength"] = parse_key_data(r, "f")
    elif t == 2:  # random
        aff["strength_x"] = parse_key_data(r, "f")
        aff["strength_y"] = parse_key_data(r, "f")
        aff["strength_z"] = parse_key_data(r, "f")
        aff["interval"] = parse_key_data(r, "f")
    elif t == 3:  # vortex
        aff["strength"] = parse_key_data(r, "f")
        aff["direction"] = parse_key_data(r, "3")
        aff["affect_type"] = r.i32()
    else:
        raise ValueError(f"Unknown affector type: {t}")
    return aff


def parse_affectors(r: Reader) -> dict:
    count = r.i32()
    items = [parse_affector(r) for _ in range(count)]
    return {"count": count, "items": items}


def parse_emitter(r: Reader) -> dict:
    e = {
        "parent_node": parse_aligned_string(r),
        "sub_emitters": r.i32(),
        "data": parse_emitter_data(r),
        "outer_x": parse_key_data(r, "f"),
        "outer_y": parse_key_data(r, "f"),
        "outer_z": parse_key_data(r, "f"),
        "inner": parse_key_data(r, "i"),
        "rotate_x": parse_key_data(r, "f"),
        "rotate_y": parse_key_data(r, "f"),
        "rotate_z": parse_key_data(r, "f"),
        "translate_x": parse_key_data(r, "f"),
        "translate_y": parse_key_data(r, "f"),
        "translate_z": parse_key_data(r, "f"),
        "emit_volumn": parse_key_data(r, "f"),
        "speed_all": parse_key_data(r, "f"),
        "speed_spe": parse_key_data(r, "f"),
        "speed_spe_angle": parse_key_data(r, "f"),
        "spe_dir": parse_key_data(r, "3"),
        "particle": parse_particle(r),
        "affectors": parse_affectors(r),
        "children": [],
    }
    for _ in range(e["sub_emitters"]):
        e["children"].append(parse_emitter(r))
    return e


def parse_collada_node(r: Reader) -> dict:
    return {
        "node": parse_aligned_string(r),
        "model_loop": r.i32(),
        "alpha_anim_ctrl": parse_key_data(r, "i"),
    }


def summarize_emitter(e: dict) -> dict:
    p = e["particle"]
    return {
        "parent_node": e["parent_node"]["value"],
        "sub_emitters": e["sub_emitters"],
        "emitter_type": e["data"]["type"],
        "emit_time": e["data"]["emit_time"],
        "emit_start_time": e["data"]["emit_start_time"],
        "interval": e["data"]["interval"],
        "interval_random": e["data"]["interval_random"],
        "particle_type": p["data"]["type"],
        "life_time": p["data"]["life_time"],
        "life_time_random": p["data"]["life_time_random"],
        "size_random": p["data"]["size_random"],
        "texture": p["texture"]["value"],
        "base_size_x_keys": p["base_size_x"]["keys"][:4],
        "base_size_y_keys": p["base_size_y"]["keys"][:4],
        "speed_all_keys": e["speed_all"]["keys"][:4],
        "speed_spe_keys": e["speed_spe"]["keys"][:4],
        "affector_count": e["affectors"]["count"],
    }


def parse_beff(path: Path) -> dict:
    data = path.read_bytes()
    r = Reader(data)

    signature = r.bytes(4).decode("latin1", errors="ignore")
    if signature != "beff":
        raise ValueError(f"Bad signature: {signature!r}")

    header = {
        "signature": signature,
        "format_version": r.u32(),
        "delay_time_min": r.i32(),
        "delay_time_max": r.i32(),
        "nodes_count": r.i32(),
    }

    nodes = []
    for _ in range(header["nodes_count"]):
        node_type = r.i32()
        node = {"type": node_type}
        if node_type == 0:
            node["name"] = "Emitter"
            node["data"] = parse_emitter(r)
            node["summary"] = summarize_emitter(node["data"])
        elif node_type == 1:
            node["name"] = "Collada Root"
            node["data"] = parse_collada_node(r)
        elif node_type == 2:
            # The template marks this enum value but doesn't parse it in switch.
            # Best-effort fallback: parse like ColladaNode to keep stream aligned.
            node["name"] = "Collada Node (assumed ColladaRoot layout)"
            node["data"] = parse_collada_node(r)
        else:
            raise ValueError(f"Unknown node type {node_type} at {r.tell() - 4}")
        nodes.append(node)

    return {
        "file": str(path),
        "size": len(data),
        "header": header,
        "nodes": nodes,
        "end_offset": r.tell(),
        "remaining_bytes": r.remaining(),
        "parse_ok": r.remaining() == 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse Gameloft BEFF using template-derived structure")
    parser.add_argument("input", nargs="+", help="Path(s) to .beff")
    parser.add_argument("--out", help="Write JSON output file")
    parser.add_argument("--pretty", action="store_true", help="Pretty JSON")
    parser.add_argument("--summary-only", action="store_true", help="Drop verbose node payloads")
    args = parser.parse_args()

    parsed = [parse_beff(Path(p)) for p in args.input]
    if args.summary_only:
        compact = []
        for item in parsed:
            mini_nodes = []
            for n in item["nodes"]:
                mini = {"type": n["type"], "name": n["name"]}
                if "summary" in n:
                    mini["summary"] = n["summary"]
                mini_nodes.append(mini)
            compact.append(
                {
                    "file": item["file"],
                    "size": item["size"],
                    "header": item["header"],
                    "nodes": mini_nodes,
                    "end_offset": item["end_offset"],
                    "remaining_bytes": item["remaining_bytes"],
                    "parse_ok": item["parse_ok"],
                }
            )
        payload_obj = compact if len(compact) > 1 else compact[0]
    else:
        payload_obj = parsed if len(parsed) > 1 else parsed[0]

    payload = json.dumps(payload_obj, indent=2 if args.pretty else None)
    if args.out:
        Path(args.out).write_text(payload + "\n", encoding="utf-8")
    else:
        print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
