#!/usr/bin/env bash
set -euo pipefail

# Convert texture2g PVR-backed .tga assets into decoded PNG files.
# Requires PVRTexToolCLI from PowerVR SDK:
#   https://docs.imgtec.com/tools-manuals/pvrtextool-manual/html/topics/pvrtextool-cli.html
#
# Usage:
#   scripts/convert_texture2g_pvr.sh
#   scripts/convert_texture2g_pvr.sh --src oac1osp/data/texture2g --out oac1osp/data/texture_converted

SRC_DIR="oac1osp/data/texture2g"
OUT_DIR="oac1osp/data/texture_converted"

while [[ $# -gt 0 ]]; do
	case "$1" in
	--src)
		SRC_DIR="$2"
		shift 2
		;;
	--out)
		OUT_DIR="$2"
		shift 2
		;;
	*)
		echo "Unknown argument: $1" >&2
		exit 1
		;;
	esac
done

if [[ ! -d "$SRC_DIR" ]]; then
	echo "Source directory not found: $SRC_DIR" >&2
	exit 1
fi

PVR_TOOL="${PVRTEXTOOLCLI_BIN:-}"
if [[ -z "$PVR_TOOL" ]]; then
	for bin in PVRTexToolCLI PVRTexToolCLI64 pvrtextoolcli pvr-tex-tool; do
		if command -v "$bin" >/dev/null 2>&1; then
			PVR_TOOL="$bin"
			break
		fi
	done
fi

if [[ -z "$PVR_TOOL" ]]; then
	echo "PVRTexToolCLI not found." >&2
	echo "Install it and ensure it is in PATH, or set PVRTEXTOOLCLI_BIN=/path/to/PVRTexToolCLI" >&2
	exit 1
fi

mkdir -p "$OUT_DIR"

converted=0
failed=0
skipped=0

while IFS= read -r infile; do
	rel="${infile#"$SRC_DIR"/}"
	outfile="$OUT_DIR/${rel%.*}.png"
	outdir="$(dirname "$outfile")"
	mkdir -p "$outdir"

	# Skip if output exists and is newer than input.
	if [[ -f "$outfile" && "$outfile" -nt "$infile" ]]; then
		((skipped+=1))
		continue
	fi

	# Preferred conversion command: decode to a standard PNG.
	if "$PVR_TOOL" -i "$infile" -noout -d "$outfile" >/dev/null 2>&1; then
		((converted+=1))
		continue
	fi

	# Fallback for tool builds that need explicit decode format.
	if "$PVR_TOOL" -i "$infile" -noout -d "$outfile" -f r8g8b8a8 >/dev/null 2>&1; then
		((converted+=1))
		continue
	fi

	# Some files are PVR containers mislabeled as .tga.
	# Re-try via a temporary .pvr file so the tool sniffs by extension.
	tmp_pvr="$(mktemp --suffix=.pvr)"
	cp "$infile" "$tmp_pvr"
	if "$PVR_TOOL" -i "$tmp_pvr" -noout -d "$outfile" >/dev/null 2>&1; then
		rm -f "$tmp_pvr"
		((converted+=1))
		continue
	fi
	if "$PVR_TOOL" -i "$tmp_pvr" -noout -d "$outfile" -f r8g8b8a8 >/dev/null 2>&1; then
		rm -f "$tmp_pvr"
		((converted+=1))
		continue
	fi
	rm -f "$tmp_pvr"

	echo "Failed to convert: $infile" >&2
	((failed+=1))
	done < <(rg --files "$SRC_DIR" | rg -i "\.tga$")

echo "Conversion done."
echo "Converted: $converted"
echo "Skipped:   $skipped"
echo "Failed:    $failed"
