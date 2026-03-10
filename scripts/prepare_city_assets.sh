#!/usr/bin/env bash
set -euo pipefail

# Prepare a city-ready asset layout for this viewer:
# - Build uncompressed ZIP terrain archives expected by Terrain::load
# - Ensure texture conversion output exists (if converter is available)
# - Create known placeholder textures for missing references
#
# Usage:
#   scripts/prepare_city_assets.sh
#   scripts/prepare_city_assets.sh --city city --data-root oac1osp/data

CITY_NAME="city"
DATA_ROOT="oac1osp/data"
RUN_CONVERT=1
ALL_WORLDS=0

while [[ $# -gt 0 ]]; do
	case "$1" in
	--city)
		CITY_NAME="$2"
		shift 2
		;;
	--data-root)
		DATA_ROOT="$2"
		shift 2
		;;
	--all-worlds)
		ALL_WORLDS=1
		shift
		;;
	--skip-convert)
		RUN_CONVERT=0
		shift
		;;
	*)
		echo "Unknown argument: $1" >&2
		exit 1
		;;
	esac
done

TERRAIN_OUT="$DATA_ROOT/terrain"
mkdir -p "$TERRAIN_OUT"

if [[ "$ALL_WORLDS" -eq 1 ]]; then
	:
else
	if [[ ! -d "$DATA_ROOT/world/$CITY_NAME" ]]; then
		echo "World folder not found: $DATA_ROOT/world/$CITY_NAME" >&2
		exit 1
	fi
fi

python - <<PY
import os, zipfile, sys
data_root = ${DATA_ROOT@Q}
all_worlds = ${ALL_WORLDS}
city_name = ${CITY_NAME@Q}
terrain_out = os.path.join(data_root, "terrain")
os.makedirs(terrain_out, exist_ok=True)

world_root_base = os.path.join(data_root, "world")
if all_worlds:
    world_list = []
    if os.path.isdir(world_root_base):
        for world in sorted(os.listdir(world_root_base)):
            wpath = os.path.join(world_root_base, world)
            if not os.path.isdir(wpath):
                continue
            required = ["terrain", "items", "layers", "navmesh"]
            if all(os.path.isdir(os.path.join(wpath, r)) for r in required):
                world_list.append(world)
    if not world_list:
        print(f"No world folders with complete terrain/items/layers/navmesh sets were found under {world_root_base}", file=sys.stderr)
        raise SystemExit(1)
else:
    world_list = [city_name]

for city in world_list:
    world_root = os.path.join(data_root, "world", city)
    sets = [
        (f"{city}.trn", os.path.join(world_root, "terrain"), [".trn"]),
        (f"{city}.itm", os.path.join(world_root, "items"), [".itm"]),
        (f"{city}.msk", os.path.join(world_root, "layers"), [".msk", ".shw"]),
        (f"{city}.nav", os.path.join(world_root, "navmesh"), [".nav"]),
    ]
    for name, folder, exts in sets:
        if not os.path.isdir(folder):
            print(f"skip: missing folder {folder}")
            continue
        zpath = os.path.join(terrain_out, name)
        with zipfile.ZipFile(zpath, "w", compression=zipfile.ZIP_STORED) as zf:
            for fn in sorted(os.listdir(folder)):
                low = fn.lower()
                if any(low.endswith(ext) for ext in exts):
                    zf.write(os.path.join(folder, fn), arcname=fn)
        print(f"built: {zpath}")

physics_path = os.path.join(terrain_out, "physics.zip")
with zipfile.ZipFile(physics_path, "w", compression=zipfile.ZIP_STORED) as zf:
    zf.writestr(".empty", "")
print(f"built: {physics_path}")
PY

if [[ "$RUN_CONVERT" -eq 1 ]]; then
	if [[ -x "scripts/convert_texture2g_pvr.sh" ]]; then
		scripts/convert_texture2g_pvr.sh --src "$DATA_ROOT/texture2g" --out "$DATA_ROOT/texture_converted" || true
	else
		echo "skip: scripts/convert_texture2g_pvr.sh not found or not executable"
	fi
fi

# Normalize converted texture file names to lowercase aliases for Linux lookup.
python - <<PY
import os, shutil
root = ${DATA_ROOT@Q} + "/texture_converted"
if not os.path.isdir(root):
    print("skip: no texture_converted folder")
    raise SystemExit(0)
for dirpath, _, files in os.walk(root):
    for fn in files:
        low = fn.lower()
        if fn == low:
            continue
        src = os.path.join(dirpath, fn)
        dst = os.path.join(dirpath, low)
        if not os.path.exists(dst):
            shutil.copy2(src, dst)
print("normalized: lowercase aliases created")
PY

# Known missing city texture references; use a neutral city road as fallback.
mkdir -p "$DATA_ROOT/texture_converted/tiles/city"
if [[ -f "$DATA_ROOT/texture_converted/tiles/city/way02.png" ]]; then
	for n in brickround0017_s brickround0033_13_s brickround0050_2_s brickround0092_5_s; do
		cp -f "$DATA_ROOT/texture_converted/tiles/city/way02.png" "$DATA_ROOT/texture_converted/tiles/city/${n}.png"
	done
	echo "patched: placeholder brickround textures"
else
	echo "skip: missing base placeholder texture $DATA_ROOT/texture_converted/tiles/city/way02.png"
fi

echo
if [[ "$ALL_WORLDS" -eq 1 ]]; then
	echo "All world assets prepared."
	echo "Launch with a specific world archive, e.g.:"
	echo "  ./app \"$DATA_ROOT/terrain/city.trn\""
else
	echo "City assets prepared."
	echo "Launch with:"
	echo "  ./app \"$DATA_ROOT/terrain/$CITY_NAME.trn\""
fi
