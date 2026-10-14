#!/usr/bin/env bash
set -euo pipefail
# Usage: build_obf.sh /path/OsmAndMapCreator map.osm[.gz] [--poi-types full-poi_types.xml] [--rendering-types full-rendering_types.xml]
if (( $# < 2 )); then echo "Usage: $0 CREATOR_DIR MAP.osm[.gz] [--poi-types FILE] [--rendering-types FILE]" >&2;exit 2;fi
creator="$1"; input="$2";shift 2
[[ -f "$creator/utilities.sh" && -f "$input" ]] || { echo 'Missing OsmAnd utilities.sh or map input' >&2;exit 1; }
args=(generate-obf-no-address "$input")
while (( $# > 0 )); do
 case "$1" in
  --poi-types|--rendering-types)
   (( $# >= 2 )) || { echo "Missing file after $1" >&2;exit 2; }
   [[ -f "$2" ]] || { echo "Config not found: $2" >&2;exit 2; }
   args+=("$1=$2");shift 2;;
  *) echo "Unknown option: $1" >&2;exit 2;;
 esac
done
export JAVA_OPTS="${JAVA_OPTS:--Xms256M -Xmx4096M}"
"$creator/utilities.sh" "${args[@]}"
