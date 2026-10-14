# BGMountains Garmin IMG → OSM → OBF: C++ converter + POI audit toolkit (v0.5)

The source code, PNG icons, full-country inventory, editable 1,871-row POI configuration, source manifest, and comparison tools are included in this archive.

**Important:** The converter source is GPL-3.0-or-later. The original BGMountains map data and any extracted/converted geographic data have **separate** licenses. There is no blanket permission to merge this data into openstreetmap.org. No IMG or OBF binaries are bundled.

## Build

```sh
sudo apt-get install -y cmake g++ make libpng-dev libsqlite3-dev python3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
ctest --test-dir build --output-on-failure
```

## Bundled resources

| File | Purpose |
|---|---|
| `data/BGMountains-unique-poi-types.csv` | **All 1,871 observed POI hex types**, counts, named counts, current default classification, icon availability and five source coordinates/names/mapset IDs per hex type |
| `config/poi-overrides-all-types.csv` | **Editable configurator**: one row per hex, with count, default and an example embedded in the notes. Rules initially have empty tags, therefore do not change output |
| `config/poi-overrides.csv` | Minimal template with explanation and an intentionally commented example |
| `data/BGMountains-source-pois.csv.gz` | Complete extracted inventory (416,165 source records) for audit/provenance comparisons; *this is map-derived data* |
| `data/poi-icons/*.png` | 96 successfully decoded Garmin TYP icons indexed by hex code; facilitates visual comparison with GPXSee |
| `tools/validate_pois.py` | Disk-backed, streaming comparison of source manifest against OSM POIs; optional semantic override assertions; OsmAnd inspector wrapper |
| `tests/demo.img` | Synthetic Garmin fixture suitable for reproducing baseline smoke tests |

## Produce a countrywide OSM export + inventory

Default execution is **serial** (`--jobs 1`, even if not supplied). Set `--jobs 4` or another integer 1–64 to opt into parallel mapset processing. Parallel processing uses independent SQLite stages; all OSM nodes are emitted before ways and node refs are rebased correctly.

```sh
./build/garmin2osm \
  --input '/path/to/gmapsupp.img' \
  --output Bulgaria.osm \
  --poi-overrides config/poi-overrides-all-types.csv \
  --poi-types-out unique-poi-types.csv \
  --poi-manifest source-pois.csv \
  --poi-examples 5 \
  --jobs 4
```

For a regional check before rebuilding the whole country:

```sh
./build/garmin2osm --input '/path/to/gmapsupp.img' \
  --bbox 23.28,42.11,23.47,42.27 --output Rila.osm \
  --poi-overrides config/poi-overrides-all-types.csv \
  --poi-types-out Rila-types.csv --poi-manifest Rila-source.csv
```

Generate **only** a catalog and manifest (no OSM output or staging DB):

```sh
./build/garmin2osm --input '/path/to/gmapsupp.img' \
  --poi-types-out catalog.csv --poi-manifest source.csv --icons-dir my-icons
```

Other original flags remain: `--inspect`, `--contours`, `--all-levels`, `--keep-temp`, `--temp`, `--limit-maps`. Existing OSM, staging, and manifest output paths are never intentionally overwritten. Source inventory is generated even for ignored or `drop`-configured POIs, so intentional exclusions can be audited explicitly.

## Override known defaults and add new POI types

Edit `config/poi-overrides-all-types.csv` in LibreOffice/Excel or a text editor. Columns:

```
code,name_contains,mode,tags,note
0x6414,,replace,man_made=water_well,ONLY IF VERIFIED
0x6503,,append,information=guidepost;hiking=yes,Example: adds another attribute
```

Choose a valid mode: `append`, `replace`, `keep_raw`, or `drop`. `name_contains` is optional and case-sensitive; if populated, the rule applies only to names containing that text. Rule order matters within the same code. Actual `garmin:type`, `garmin:feature`, `garmin:mapset` and `name` cannot be overridden. Use `key=` to delete an existing default key. Unknown hex types will remain raw-only until mapped. **Never infer the meaning of a code solely from its hexadecimal number.**

An immediately usable example that modifies all type `0x6414` points follows:

```
code,name_contains,mode,tags,note
0x6414,,replace,man_made=water_well,TEST ONLY; not evidence of true semantics
```

NOTE: `0x6414` presently has a built-in `amenity=drinking_water` default. Its old duplicate fallback to `water_well` was unreachable and has been removed. The icon and field data need to establish the right category. **The sample replacement is not a verified mapping.**

Coordinates in `data/BGMountains-unique-poi-types.csv` refer to the decoded *original IMG* features. For each type, open the coordinates in GPXSee's Garmin map and inspect both the label and icon. `data/poi-icons/0x6414.png` may provide additional context. The fact that a rendered glyph looks like water is **not** enough to distinguish `natural=spring`, `man_made=water_well` and `amenity=drinking_water`.

## Automated validation: IMG → OSM

The `--poi-manifest` output is a separately emitted inventory of every parsed IMG point. To compare against the generated OSM:

```sh
python3 tools/validate_pois.py compare \
  --source Rila-source.csv --osm Rila.osm \
  --rules config/poi-overrides-all-types.csv \
  --report Rila-report.json \
  --mismatches Rila-missing-extra.csv
```

Both `.osm` and `.osm.gz` are supported. The validator checks type, exact 7-digit quantized coordinates, name and multiplicity; it separately checks the assertions provided in the override CSV. Its failure exit code is nonzero; `--mismatches` includes direct OSM web links for investigation. Uses temporary SQLite to avoid holding the entire POI set in RAM. A `drop` override is treated as intentional absence, but a matching input record still appears in the source manifest.

**Proof limit:** This is an internal IMG-to-OSM conversion-consistency check, not independent verification of Garmin decoder fidelity or the semantic meaning of a hex code. It cannot by itself prove that your `.obf` contains all the classified features.

## Automated validation: OSM → OBF

Build with the official OsmAndMapCreator distribution (download separately):

```sh
./scripts/build_obf.sh /path/OsmAndMapCreator "$PWD/Rila.osm"
# Custom OsmAnd POI / rendering definitions may be supplied with their FULL respective config XMLs:
./scripts/build_obf.sh /path/OsmAndMapCreator "$PWD/Rila.osm" \
  --poi-types /path/to/full-poi_types.xml \
  --rendering-types /path/to/full-rendering_types.xml
```

Inspect **your actual, externally compiled OBF**:

```sh
python3 tools/validate_pois.py obf-inspect \
  --inspector /path/to/OsmAndMapCreator/inspector.sh \
  --obf /path/to/Rila.obf \
  --bbox 23.28,42.11,23.47,42.27 \
  --report obf-poi-inspection.txt
```

The wrapper invokes OsmAnd BinaryInspector `-vpoi -vpoiobjects` with an optional bounding box and saves the output. It does **not** yet reconcile OBF internal IDs with the Garmin origin; OsmAnd may exclude unsupported tags from its map and POI indexes. For OBF persistence checks, use a POI (known category, name and coordinate) and inspect it via the rendered map / a bounded BinaryInspector dump; unknown types often require extending the **full** OsmAnd `poi_types.xml`. For line colors or custom map tags, modify **full** `rendering_types.xml`, then rebuild the OBF. A CSV rule only affects the OSM export; it does not change OsmAnd's indexes without a rebuild.

## What is tested

- Existing bundled CTest suite: 3/3 tests passed.
- Supplied **441,942,016-byte** Bulgaria IMG: decoded **165 map regions**, **416,165 POI records**, **1,871 unique raw point types**, **96 icons**; full inventory generation completed.
- Rila test bbox `23.28,42.11,23.47,42.27`: source and exported OSM each contained **2,424 POIs**, with **zero missing/extra** after seven-decimal coordinate correction.
- Serial and `--jobs 4` Rila jobs: exactly the same multiset of **16,786 ways and 2,424 POIs** (including way geometry and tags).
- A deliberately overridden test type was exported and assertions passed.
- **Not tested:** user's full final `.obf` (not provided here); comprehensive semantic correspondence of 1,871 Garmin types; original Garmin route-network validity/turn-restrictions; all possible IMG/GMP variants.

## Concurrency caution

The Garmin container file uses a shared `std::ifstream`, protected by a mutex to ensure safe reads. Parser threads work on different `Map` objects with separate SQLite stores. For maximum throughput, `--jobs 4` may help on multi-core CPUs, but the disk read lock, SQLite writes and OSM XML output remain throughput bottlenecks. The default `--jobs 1` is deterministic and safest. Memory and temporary disk use increase with concurrency.

## Add independent POI records (optional)

Use `config/poi-additions.csv` with four CSV fields: `lat,lon,name,tags`.
The program includes them with `garmin:feature=manual_poi` and `source=user_manual`.
They are **not** presented as Garmin-extracted features; the source-POI validator
ignores these intentional extra nodes. Add the flag `--poi-additions config/poi-additions.csv`
to a command with `--output` (empty file is a no-op).

## Reproduce the bundled regional audit without the source IMG

```sh
python3 tools/validate_pois.py compare \
  --source samples/Rila-source-pois.csv \
  --osm samples/Rila-vector.osm.gz \
  --report results.json --mismatches differences.csv
```

For the complete source inventory, use the gzip-compressed CSV directly:
`python3 tools/validate_pois.py compare --source data/BGMountains-source-pois.csv.gz --osm /your/full-map.osm`.
