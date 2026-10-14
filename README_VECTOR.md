# Garmin2OsmAnd v0.4 — Native Garmin IMG → OSM vector export

This adds a second C++ executable, `garmin2osm`, to the prior low-RAM `garmin2osmand` tile renderer. The original `garmin2osmand` **SQLiteDB raster exporter is unchanged in purpose**.

## Scope and results

This C++ export uses **no Dart, GeoJSON, `ogr2osm`, or Python runtime**. It shares the Garmin FAT/GMP, TRE/RGN, LBL/LBL6/NET and TYP decoders from the raster project. A disk-backed SQLite staging database deduplicates geographic vertices using 7-decimal-place coordinates, then emits OSM XML with all nodes before ways. The temporary database is deleted after a successful export unless `--keep-temp` is selected. Memory consumption does not scale with overall feature count, but output and staging *disk space* do.

The actual supplied 422 MiB Bulgaria IMG was scanned in full (165 map regions): 1,420,389 polylines, 924,553 area polygons, 416,165 points/POIs, without stopping on malformed geometry. A Rila–Malyovitsa regional OSM XML was parsed and checked for unique node IDs and valid way references.

**NOT VERIFIED:** entire-Bulgaria OSM file generation and compilation into OBF have not been run end-to-end. Road routability is **best effort**; Garmin routing metadata and NOD topology are not reconstructed. The original Garmin TYP bitmap patterns **cannot be represented in ordinary OSM geometry tags**, although `colour=*` and `garmin:type=*` are preserved for extended hiking-route features. Standard OsmAnd rendering might not show these colors; an OsmAnd rendering style or hiking route relation enrichment is a separate project.

The output is for personal/offline processing of maps you are legally allowed to reuse. **Do not upload converted Garmin objects to the public OSM database.** Their licence and OSM provenance are not established.

## Build on Ubuntu/Debian

```sh
sudo apt update
sudo apt install -y cmake g++ make libpng-dev libsqlite3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
ctest --test-dir build --output-on-failure
```

## Inspect the IMG

```sh
./build/garmin2osm --input 'gmapsupp(2).img' --inspect
```

This reads all available subdivisions, prints counts for map features grouped by Garmin types, and shows embedded Garmin TYP color classes. No SQLite or XML output is created.

## Export all Bulgaria

```sh
./build/garmin2osm \
  --input 'gmapsupp(2).img' \
  --output Bulgaria-Garmin.osm
```

Full OSM export could need **several gigabytes of free disk space**, and building a full `.obf` can require significantly more for temporary MapCreator indexes. This is not a completed whole-Bulgaria benchmark. Run `--inspect` first and keep additional storage free.

## Export Rila/Malyovitsa area for testing

```sh
./build/garmin2osm \
  --input 'gmapsupp(2).img' \
  --bbox 23.28,42.11,23.47,42.27 \
  --output Rila-Garmin.osm
```

`--bbox` skips non-overlapping Garmin subdivisions. Polylines that cross the bbox boundary are **not cut at the exact boundary**. Use a small export for validation before exporting a country.

The default **excludes contour line types 0x20–0x25**, as requested; add `--contours` if wanted. `--all-levels` includes less-detailed duplicated Garmin map levels and should normally be avoided. `--limit-maps N` is useful for diagnostic subsets. `--temp staging.sqlite` chooses the temporary database path, and `--keep-temp` retains it. Existing destinations are deliberately not overwritten.

### Vector contents

- Roads, track roads, footpaths and ordinary Garmin line features → OSM ways and `highway=*` where confidently matched.
- Named trails and colored Garmin extended RGN polylines → distinct path ways with `colour=*`, `trailblazed=yes`, and `garmin:route_overlay=yes`.
- Streams and rivers → `waterway=*`.
- Known Garmin polygon types → national parks, woods, lakes, scrub and others, as OSM closed ways.
- Named POIs → separate OSM nodes; specific Bulgarian map codes matched from real data: peaks, drinking water, mountain huts/shelters, signposts, information boards, viewpoints, churches, etc.
- Source codes are recorded as `garmin:type=0x...` for debugging and future tag refinements.

**Precision caveat:** `garmin:route_overlay=yes` ways are **marked-route overlays**, potentially coincident with physical `highway=path` or `highway=track` geometry. They are currently emitted with `highway=path` for basic visibility in OsmAnd. That may introduce duplicate or phantom routable edges. Do not depend on the generated OBF for safety-critical routing until you have independently validated its connectivity and access restrictions.

Unknown Garmin POI types are kept as named OSM nodes with `garmin:type=*`, but they may **not appear** in OsmAnd's POI index until added to its supported `poi_types.xml` or mapped to a verified standard type. Similarly, `colour=*` is not a guarantee of visible hiking stripe styles inside OBF.

## Compile to native OBF

Get **OsmAndMapCreator** from the official [OsmAnd instructions](https://osmand.net/docs/technical/map-creation/create-offline-maps-yourself/).

```sh
wget https://download.osmand.net/latest-night-build/OsmAndMapCreator-main.zip
unzip OsmAndMapCreator-main.zip -d OsmAndMapCreator
export JAVA_OPTS='-Xms256M -Xmx4096M'
./scripts/build_obf.sh "$PWD/OsmAndMapCreator" "$PWD/Rila-Garmin.osm"
```

You can also save storage and I/O bandwidth with `gzip -9 Rila-Garmin.osm` and pass `Rila-Garmin.osm.gz` to the same wrapper. The `generate-obf-no-address` command builds a map without the address index; paths, POIs and map geometry are still indexed based on OsmAnd's supported render/routing/POI tags. **This last compilation step was not run in the container** because the MapCreator binary could not be retrieved; only the native IMG → OSM exporter was compiled and tested.

For large datasets, geographically partition the source and compile separately. OsmAnd recommends `.obf` files below 2 GiB.

## Validate converted OSM XML

```sh
python3 - <<'PY'
import xml.etree.ElementTree as ET
from collections import Counter
counts=Counter()
for _,e in ET.iterparse('Rila-Garmin.osm',events=('end',)):
    if e.tag in ('node','way','relation'):
        counts[e.tag]+=1
        e.clear()
print(counts)
PY
```

See `tests/test_vector.py` for a reference-integrity test of the synthetic Garmin fixture.

### Code and format references

- [QMapShack Garmin implementation](https://github.com/Maproom/qmapshack/blob/dev/src/qmapshack/map/CMapIMG.cpp) — GPLv3 background research
- [cGPSmapper polyline type list](https://www.manualsonline.com/manuals/mfg/garmin/gps_receiver_1_2.html?p=88)
- [Garmin area type documentation](https://wiki.openstreetmap.org/wiki/Editing_OSM_Map_On_Garmin/Area_Types)
- [OsmAnd's official offline map build documentation](https://osmand.net/docs/technical/map-creation/create-offline-maps-yourself/)

Project licence: GPL-3.0-or-later (LICENSE).
