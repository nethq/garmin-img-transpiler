#!/usr/bin/env bash
# Compatibility shim. No manually supplied POI XML files are required anymore.
# Preferred: python3 scripts/build_bgmountains.py --img gmapsupp.img --download-creator --out ./build/out
set -euo pipefail
if (( $# < 2 )); then
  echo "Usage: $0 CREATOR_DIR INPUT_v0.7.osm[.gz] [OUTPUT_DIR]" >&2
  echo "Recommended instead: python3 scripts/build_bgmountains.py --img gmapsupp.img --download-creator --out ./build/out" >&2
  exit 2
fi
creator="$1"; osm="$2"; out="${3:-$(dirname "$osm")/rebuilt-obf}"
root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="$(dirname "$osm")"
args=(--osm "$osm" --creator "$creator" --out "$out")
if [[ -f "$source_dir/poi-types.csv" ]]; then args+=(--catalog "$source_dir/poi-types.csv"); fi
exec python3 "$root/scripts/build_bgmountains.py" "${args[@]}"
