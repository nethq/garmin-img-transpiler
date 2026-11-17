# CPP Transpiler Garmin .IMG -> .OSM/.OBF format

**This is the integrated source release.** It replaces the v0.92/v0.93 Python
pipeline and v0.94 standalone CSVs. Do not combine scripts from older ZIPs:
those versions accept different flags and do not apply the current name-conditional
rules. Edit the POI profile and line mapping described in [the configuration guide](config/README.md).

## Quick start (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y cmake g++ make libpng-dev libsqlite3-dev python3 openjdk-21-jre-headless unzip
unzip garmin-img-transpiler-hiking.zip
cd garmin-img-transpiler
```

Use **exactly** this from the release root (where `scripts/` and `config/` are):

```bash
nohup python3 scripts/build_bgmountains.py \
  --img "$HOME/gmapsupp.img" \
  --poi-config config/poi-mapping.csv \
  --line-config config/line-mapping.csv \
  --out bg-mountains-v095-build \
  --download-creator \
  --jobs 8 \
  --java-opts '-Xms512M -Xmx25G' > bg-v095.log 2>&1 &
```

Use `--review-pois` to generate an editable POI CSV from an IMG. Use `--poi-config` and `--line-config` to select conversion rules.
Other supported flags: `--creator /path/to/OsmAndMapCreator`, `--bbox W,S,E,N`,
`--prepare-only`, `--keep-raw`, `--template`, `--attach-routes`,
`--disk-headroom-gb N`, `--ignore-disk-check`, `--jobs N` (default 1).

**Check your actual disk space first.** Full-country MapCreator compilation has
previously required over 25 GiB of temporary disk space in addition to the
source OSM (6+ GiB) on your machine. The script uses a conservative estimate
`max(2 GiB, 5 × native-OSM-size + 1 GiB)`, unless `--disk-headroom-gb`
requires more. These figures are not upper bounds. Choose an output filesystem
with generous free space. `-Xmx25G` controls heap RAM, **not disk usage**.

## Safe regional test, no Java or extra download

```bash
python3 scripts/build_bgmountains.py \
  --img "$HOME/gmapsupp.img" \
  --poi-config config/poi-mapping.csv \
  --line-config config/line-mapping.csv \
  --bbox '25.05,42.43,25.16,42.54' \
  --jobs 8 \
  --out ./regional-test \
  --prepare-only
```

Inspect `regional-test/BGMountains-native.osm`, `semantic-audit.json`,
`route-report.json`, `extract.log`, `translate.log`, `audit.log`. The tool
must exit nonzero on a failed conversion subprocess. You can instead pass an
existing MapCreator with `--creator ...` (omit `--prepare-only`) for a small
real OBF build.

## Editable configuration files

### 1. `config/poi-mapping.csv`

1,871 base hex-code entries plus 80 **name-specific** exceptions (1,951 data
rows). Columns include: `code`, `source_count`, `legend_bg`, `legend_en`,
`meaning_confidence`, `osm_tags`, `label_bg`, `priority`, `zoom_hint`, `action`,
`note`, examples, and optional `name_contains`. For conditional matching,
set `name_contains` in an additional row with the same `code`. The matcher uses
case-sensitive substring matching on the **original Garmin name**, with rules
applied in file order after the base rule. `replace` changes the converter's
semantic defaults; `append` retains previous tags; `raw` leaves unspecified
features unclassified until private-map fallback; `drop` discards the feature.

The Python pipeline automatically creates `cpp-poi-override.csv` and
`cpp-name-labels.csv` **inside the build output directory**. These are generated
intermediates. There is no
need to manually supply them or an icon pack to OsmAnd.

Important native examples:

- `0x6414`: one drinking-water POI for an ordinary tap; explicitly dry taps receive water_tap and drinking_water=no.
- `0x6500`: a water intake carries `man_made=water_intake;drinking_water=unknown`. Named fountains have their own rule.
- `0x6511`: `natural=spring` (potability not asserted).
- `0x6402`: `building=yes` by default, explicitly named `Заслон` or `Хижа`
  may become shelter/hut (availability/access not verified).
- `0x661d`: agricultural structures remain agricultural unless specifically
  identified otherwise.
- `0x6600`: keep numeric spot height `ele=*`, **no fake settlement**.
- Unknown POIs: `place=locality`, `Неуточнено - <stable hash>` if unnamed,
  and `bgm:synthetic_fallback=place_locality`; this private-map fallback **does
  not guarantee Nearby POI indexing**.

`priority` and `zoom_hint` are documented classification hints only. They do
**not force stock OsmAnd to show an icon at zoom 12**. Likewise `garmin:type`
remains a provenance tag, not a display rule. Only stock OsmAnd classifications
and rendering apply to the resulting OBF.

### 2. `config/line-mapping.csv`

50 Garmin line types (19 currently have `route_mode=relation`, 31 preserve
source geometry). Columns include Garmin `code`, legend, color,
`osm_way_tags`, `route_mode`, `osmc_symbol`, `network`, confidence and priority.
Selected source overlay segments are grouped into native
`type=route;route=hiking` relations with `osmc:symbol`. Garmin source color is
preserved as relation `colour=*`. The script does not create duplicate routable
paths from decoration lines. Opt-in `--attach-routes` matches overlays to real
existing path geometries, but can use significant memory; it is off by default.
**Existing physical path routing remains in the data.** Exact phone-side color,
icon and at-zoom visibility still depend on stock OsmAnd.

### Generate a comprehensive POI configuration from your BGMountains IMG

```bash
python3 scripts/build_bgmountains.py --img gmapsupp.img --review-pois --out review --jobs 4
# Edit review/poi-review.csv, then build with those settings
python3 scripts/build_bgmountains.py --img gmapsupp.img --poi-config review/poi-review.csv \
  --creator /path/to/OsmAndMapCreator --out hiking-build --jobs 4
```

The POI CSV controls generated labels, hash windows, private search entries and
native OBF map-index zoom bands. See [the hiking profile guide](config/README.md).

## New or unknown IMG maps: generate configuration templates

```bash
python3 scripts/build_bgmountains.py --img /path/to/unknown.img \
  --out ./template-newmap --bbox '23.0,42.0,24.0,43.0' --template
```

Produces `poi-mapping.TEMPLATE.csv` and `line-mapping.TEMPLATE.csv` with
observed types and sample coordinates. Fill them manually and run with
`--poi-config ./template-newmap/poi-mapping.TEMPLATE.csv` and
`--line-config ./template-newmap/line-mapping.TEMPLATE.csv`. **Do not assume
Garmin codes have the same meaning in unrelated maps.** If you need a complete
catalog for a new national map, generate its templates without `--bbox`, but
this can require significant time and intermediate disk space.

## Validation and provenance

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

There is also `tools/audit_osmand_poi_mapping.py`, which takes the official
version-matched OsmAnd `poi_types.xml` and produces a registry compatibility
report, **not a claim of final icon visibility**:

```bash
python3 tools/audit_osmand_poi_mapping.py \
  --mapping config/poi-mapping.csv \
  --poi-types /path/to/poi_types.xml \
  --report native-poi-audit.csv --json native-poi-audit.json
```

Source: https://github.com/osmandapp/OsmAnd-resources/blob/master/poi/poi_types.xml

## Validation

Real BGMountains IMG inventory, regional extractions, real OsmAndMapCreator
compilation and Binary Inspector checks are documented in [the configuration guide](config/README.md).
The stock phone renderer has separate icon and label rules.

License: the source bundle contains historically BGMountains-derived
classification information. Treat that data as private-map material under
applicable source licenses; the package is **not** authorization to republish
BGMountains geography or to import its features into the public OSM database.
