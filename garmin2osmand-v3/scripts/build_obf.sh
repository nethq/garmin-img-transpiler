#!/usr/bin/env bash
set -euo pipefail
# Provide an OsmAndMapCreator folder with utilities.sh (official OsmAnd distribution).
# Usage: ./scripts/build_obf.sh /path/OsmAndMapCreator /path/map.osm[.gz]
if (( $# != 2 )); then
  echo "Usage: $0 /path/OsmAndMapCreator /path/to/map.osm[.gz]" >&2; exit 2
fi
creator="$1"; input="$2"
if [[ ! -f "$creator/utilities.sh" || ! -f "$input" ]]; then echo "Missing OsmAndMapCreator utilities.sh or OSM XML input" >&2; exit 1;fi
export JAVA_OPTS="${JAVA_OPTS:--Xms256M -Xmx4096M}"
"$creator/utilities.sh" generate-obf-no-address "$input"
