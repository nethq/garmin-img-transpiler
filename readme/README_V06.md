# BGMountains Garmin IMG → OSM v0.6 (legend-reviewed, spatially validated)

This is an **incremental source + QA release**, based on the v0.5 converter. Original map IMG/OBF binaries are **not bundled**. The source map's license is **not** the converter's GPL license: map-derived data, icons, CSV and examples are for private verification unless the rights holders authorize redistribution.

## Corrected

1. **67 exact extracted TYP icon ↔ supplied BGMountains legend matches**, 60 of them present as decoded POI types in the IMG (7 icons occur only in its TYP catalog), including 48 changes to the previous semantic defaults. Changes appear in `data/legend-corrections-log.csv`, and new mappings are embedded directly in `src/vector.cpp` as safe fallbacks. Every verified icon also gets a `garmin:legend` tag to aid inspection. Optional `config/poi-overrides-legend-reviewed.csv` permits further edits while retaining the original 1,871 type rows.
2. **Extended-point label pointer**: v0.5 read the label pointer at `p+len-3`; Garmin's layout places it at `p+6`, before the optional extra byte. Correcting this fixed or cleared **117 label outcomes**. See `data/extended-label-decoder-changes.csv`, with old/new names/coordinates. The string `есебър` was a decoder artifact at the *wrong geographic location*, not a misspelling to force to `Несебър`. The actual town of Несебър is in the normal classic point data at 42.6599550,27.7197504, mapset 02235056.
3. **Spatial sanity gate**: Extended POIs outside their own TRE subdivision bounds (beyond 0.00005° margin) are no longer emitted. **5,602 suspected/invalid point records** disappear; classic POI count remains **407,820**. This is a conservative defense against malformed extended streams, not independent Garmin specification conformance testing. Retained extended records may still warrant validation. Total kept POIs: **410,563**, distinct types: **263** (113 classic, 150 extended). The old 1,871-code inventory remains available, annotated with exclusion counts.
4. **Optional per-feature POI name corrections**, `--poi-name-fixes config/poi-name-fixes.csv` uses an exact source-map ID, hex code, latitude, longitude, previous name and replacement name. Never blanket-correct all points with a particular substring: it could mask a decoder error. No name corrections are active by default.
5. **Threads are off by default** (`--jobs 1`); use `--jobs 4` to enable four parallel workers. POI rules apply consistently in both cases.

## Files

- `config/poi-overrides-all-types.csv` (same as `config/poi-overrides-legend-reviewed.csv`): **corrected editable 1,871-row override file** (schema `code,name_contains,mode,tags,note`). 60 active POI-code rows corrected (plus 7 extra verified icons embedded in C++ for future occurrences). Unverified rows are left inert and flagged in the notes.
- `config/poi-name-fixes.csv`: name correction template, no active fixes by default.
- `data/BGMountains-unique-poi-types.csv` (same as `data/BGMountains-unique-poi-types-STRICT-reviewed.csv`): **current useful catalog: 263 retained raw types**, current counts, reviewed classification status, up to five real source coordinates and names per type.
- `data/BGMountains-unique-poi-types-reviewed.csv`: **historical 1,871-type catalog**, with v0.5 counts, exclusions and review status; do not mistake rejected coordinates for correct map objects.
- `data/legend-corrections-log.csv`: 60 present-and-verified mappings; `data/all-67-icon-legend-matches.csv`: 67 TYP icon-to-legend matches (7 icon codes not observed as POIs), including original/new OSM semantic tags and one coordinate each.
- `data/extended-label-decoder-changes.csv`: 117 extended-label before/after results (some names vanish because they came from the wrong LBL offset).
- `data/legend-verified-POI-QA-waypoints.gpx`: 179 sample POIs of verified hex codes, for simultaneous visual comparison in GPXSee + original IMG. Exact count is in the GPX file.
- `data/BGMountains-POI-Explorer.html`: no-network/offline searchable icon reference, with copyable sample coordinates and basemap links.
- `data/poi-icons/*.png`: embedded Garmin icons extracted from the original TYP (copyright depends on source data).
- `tools/validate_pois.py`: previously tested source→OSM audit; limitation remains that it is not a third-party ground-truth or OBF assertion.

## Build and validation

```bash
sudo apt-get install -y cmake g++ libpng-dev libsqlite3-dev python3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
ctest --test-dir build --output-on-failure

./build/garmin2osm --input 'gmapsupp.img' \
   --output BGMountains-legend-reviewed.osm \
   --poi-overrides config/poi-overrides-legend-reviewed.csv \
   --poi-name-fixes config/poi-name-fixes.csv \
   --poi-manifest BGMountains-source-reviewed.csv \
   --poi-types-out BGMountains-types-reviewed.csv

python3 tools/validate_pois.py compare \
  --source BGMountains-source-reviewed.csv \
  --osm BGMountains-legend-reviewed.osm \
  --rules config/poi-overrides-legend-reviewed.csv \
  --report validated.json --mismatches mismatches.csv
```

Parallelism: append `--jobs 4` to the converter command if desired; otherwise it uses one worker.

**Adding one verified name correction:** edit `config/poi-name-fixes.csv`, with this schema:

```csv
mapset,code,lat,lon,old_name,new_name,note
# Example (illustrative; this row is NOT a real correction):
# 02234071,0x6609,42.1897745,23.2835913,Х.Иван Вазов,х. Иван Вазов,Check actual Garmin source before use
```

The original `name` and coordinates in the source manifest are quantized to seven decimals for exact matching. A manual name fix affects both OSM and the manifest, so it **cannot count as independent evidence of cartographic correctness**; record your verification separately.

## What this does not prove

The user's full `.obf` was not provided. Compile it again from the updated OSM source and inspect its POI categories/index; neither a successful OSM export nor this audit proves that OsmAnd indexing retains all custom tags. Geometry classification for the map's **area and line legend** remains a separate review (this patch principally covers POIs). Extended types other than those confirmed by icons are **unverified**, even when they pass the subdivision bounds check. Several extended nautical symbols (e.g., navigation lights) appear plausible but have no matching extracted TYP glyph.

## 0x10d1f / Несебър

The v0.5 type list showed `есебър` at **43.0592179,27.7399421**. At that latitude it is ~44 km north of the original Nesebar town POI. The v0.6 label-pointer fix no longer returns `есебър` at that record, and the subdivision bounds check suppresses the spurious record. **No synthetic `Несебър` POI has been inserted.** The real original city/town record is preserved unchanged.
