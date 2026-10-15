# BGMountains Garmin IMG → OsmAnd OBF (v0.7)

**One command builds the C++ IMG converter, emits a reviewed OSM, generates complete custom OsmAnd POI and map type definitions, validates the intermediate source/OSM data, and invokes OsmAndMapCreator to build an OBF.**

Unlike v0.6's optional `build_obf.sh` wrapper, the main script no longer expects you to create any missing XML files. First-time setup requires access to the official OsmAnd resource files and the OsmAndMapCreator Java tool; the script can download both on demand. Java, Python, CMake, libpng, SQLite3 and a C++ compiler must be installed.

## One-command full Bulgarian map

```bash
sudo apt update
sudo apt install -y build-essential cmake libpng-dev libsqlite3-dev python3 openjdk-21-jre-headless unzip

python3 scripts/build_bgmountains.py \
  --img '/path/to/gmapsupp.img' \
  --out '/path/to/BGMountains-build' \
  --download-creator
```

If you **already have** OsmAndMapCreator, use `--creator /path/to/OsmAndMapCreator` in place of `--download-creator`. You can also `export OSMAND_MAPCREATOR=/path/to/OsmAndMapCreator` and omit `--creator`.

OsmAndMapCreator is ~150 MB and requires extra disk space and Java RAM. By default, the script downloads it **only when** you request `--download-creator`; upstream OsmAnd `poi_types.xml` and `rendering_types.xml` are downloaded or read from your MapCreator directory automatically, then cached in the output folder. Requires Internet for first use unless you supply existing XML files with `--poi-base FILE --rendering-base FILE`.

**Multithreading remains disabled by default (`--jobs 1`).** Pass `--jobs 4` explicitly for the IMG parsing stage. It does not control MapCreator's Java processing.

If the Java process runs out of memory:

```bash
python3 scripts/build_bgmountains.py --img gmapsupp.img --out ./BGMountains-build \
  --creator ~/OsmAndMapCreator --jobs 4 \
  --java-opts '-Xms1G -Xmx12G'
```

The full-country OSM can be large. Allocate substantial disk space for OSM XML, temporary indexing databases, and OBF output. The script retains intermediate files for auditing.

## What is generated

```text
BGMountains-build/
  BGMountains.osm                    # converted source, Garmin type and name kept
  source-pois.csv                    # source point audit manifest
  poi-types.csv                      # observed type counts and example coordinates
  validation.json                   # source-to-OSM counts/mismatches
  mismatches.csv
  converter.log
  validator.log
  upstream/poi_types.xml            # official OsmAnd definitions (if downloaded)
  upstream/rendering_types.xml
  osmand-config/
    poi_types.bgmountains.xml        # FULL base OsmAnd POI rules + 1 custom type per observed Garmin code
    rendering_types.bgmountains.xml  # FULL base render encoding + map encoding for each Garmin code
    BGMountains-custom.render.xml    # OPTIONAL visual style for custom icons in OsmAnd
    type-index-summary.json
  mapcreator.log
  *.obf                              # full map (created by OsmAndMapCreator)
  obf-poi-index.txt                  # dump from inspector.sh, if available
  build-manifest.json
```

For the v0.6 reviewed **national catalog**: 263 decoded types, 410,563 POIs, 87 types with **at least 100** occurrences, 86 with Garmin TYP bitmap extraction. Actual new runs produce their own observed counts. The default threshold is 100; use `--threshold 250` to change it.

### Three different meanings of 'icon'

1. **Native OsmAnd map icon.** Verified type receives proper OSM tags (`natural=spring`, `tourism=hotel`, etc.) from `config/poi-overrides-all-types.csv`. Existing OsmAnd styles choose their built-in symbols.
2. **Custom Garmin type in OBF.** Every observed POI receives `garmin:type` and `bgm_poi=type_0xHEX`; the generated POI and map type XML preserve those types as a dedicated category. Map-index objects at high-frequency types are stored from zoom **15** by default, rare types from zoom **17** (adjustable by `--frequent-zoom`, `--rare-zoom`). Native OsmAnd styles may not draw custom types without a matching style.
3. **Actual Garmin TYP bitmap.** Preserved separately as PNG in `data/poi-icons/`; an OBF **does not embed third-party bitmap resources into the app icon set**. To display these exact symbols requires changing OsmAnd application resources, not just editing XML. A different installed OsmAnd render style may refer to already available built-in symbols.

## Assign additional built-in OsmAnd icons yourself

Edit `config/osmand-icons.csv`:

```csv
code,show_in_custom_style,minzoom,osmand_builtin_icon,...
0x650b,yes,16,information,...
```

For common unknowns (>=100 occurrences with no verified standard OSM semantic category) the sample config enables a generic **built-in** `information` icon in an optional style. For verified classes, `show_in_custom_style=no` lets the stock OsmAnd map style draw a more meaningful native icon. The file includes one entry for **all 263 source types**, occurrence counts and sample coordinates. Change the `osmand_builtin_icon` to a valid icon ID supported by your installed OsmAnd version and set `show_in_custom_style=yes` to override the default.

After building, install `BGMountains-custom.render.xml` as a custom OsmAnd rendering style in the application's `rendering` directory, then select the corresponding map style. It depends on OsmAnd's `default` style. It is optional and **not automatically installed** by placing the OBF on the device. Its exact icon rendering has not been device-tested in this release; built-in IDs and application versions may differ. Avoid over-rendering at low zoom because 410k points can be slow.

## Test a region first

```bash
python3 scripts/build_bgmountains.py --img gmapsupp.img \
 --out ./rila-build --bbox 23.22,42.08,23.47,42.29 \
 --creator ~/OsmAndMapCreator
```

Alternatively use `--prepare-only` to stop before Java: this produces the OSM plus generated XML and validation reports.

## Rebuild from existing OSM

Only use an OSM generated by **v0.7 or later** containing `bgm_poi` tags:

```bash
python3 scripts/build_bgmountains.py \
 --osm ./BGMountains-build/BGMountains.osm \
 --catalog ./BGMountains-build/poi-types.csv \
 --creator ~/OsmAndMapCreator \
 --out ./obf-rebuild
```

Original v0.6 OSM exports **lack** the `bgm_poi` classifier. The script rejects those for this path rather than quietly producing an incomplete custom POI index. To reuse old data, rerun the IMG extraction with v0.7.

## How POIs are edited

- `config/poi-overrides-all-types.csv`: reviewed original IMG type → semantic OSM tags (five-column CSV). You can override/add rules, preserving provenance. This includes the earlier 67 legend/icon matches.
- `config/poi-name-fixes.csv`: coordinate-specific correction of verified names, never guessed universally.
- `config/poi-additions.csv`: manually add personally verified POIs.
- `config/osmand-icons.csv`: optional on-screen **stock OsmAnd** symbol selections for custom raw types.
- `data/BGMountains-unique-poi-types.csv`: counts, names, five real sample coordinates per Garmin hex type.
- `data/poi-icons`: 96 extracted TYP bitmaps for reference, not automatically installed into OsmAnd.

### Validation

The C++/Python roundtrip test checks extracted source POI manifest against OSM coordinates, names and tags. `ctest --test-dir build --output-on-failure` runs 5 bundled tests, including checks of full upstream XML augmentation. On the tested source bbox (23.22,42.08,23.47,42.29) a v0.7 extraction produced 3,986 POIs across 60 types and zero source-versus-OSM mismatches. **These tests do not assert OBF persistence** until MapCreator runs on your machine and `obf-poi-index.txt` is reviewed.

OsmAnd has separate indexed data for map rendering and POI search, and an additional app-side style/icons component. A successful OBF build does not prove every icon will be visible. To debug OBF use `~/OsmAndMapCreator/inspector.sh -vpoi /path/to/map.obf` and compare the categories to `osmand-config/type-index-summary.json`.

## Upstream and redistribution

The repository's C++ converter has its own `LICENSE`; BGMountains-derived SVG/PNG icons, data, examples and Garmin IMG/OBF remain subject to map licensors' conditions. Do not redistribute the full data release or generated OBF as unrestricted ODbL/CC0 without additional rights clearance. The script downloads official OsmAnd XML into a private build directory rather than repackaging those resource files in this source ZIP.

Official references:
- https://osmand.net/docs/technical/map-creation/create-offline-maps-yourself/
- https://github.com/osmandapp/OsmAnd-resources/tree/master/poi
- https://github.com/osmandapp/OsmAnd-resources/tree/master/obf_creation
- https://osmand.net/docs/technical/osmand-file-formats/osmand-rendering-style/
