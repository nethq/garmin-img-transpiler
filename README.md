# BGMountains Garmin IMG → OSM → OsmAnd OBF v0.8

**This release fixes the v0.7 icon-visibility blind spot and anonymous POIs.**

- Every one of the **263 validated retained Garmin POI types** has at least one conventional OSM semantic/display tag in `config/poi-overrides-all-types.csv`, in addition to raw `bgm_poi`/`garmin:*` provenance tags.
- **87 codes** occur at least 100 times in the source catalog (409,154 source POIs). **46** use conventional feature categories recognized by stock OsmAnd in principle; **41** use an explicitly marked, synthetic `tourism=information;information=board` **local-OsmAnd display fallback**. Some additional lower-frequency codes also need fallback. This DOES NOT assert that the physical object is an information board.
- Missing original IMG names become stable Bulgarian descriptive names, e.g. `Вода - Каптаж - 1THF1SDNTKJ`, with `bgm:generated_name=yes`. Original nonempty names are preserved.
- `--jobs N` enables parallel extraction, **default one thread**.
- New `tools/audit_icon_coverage.py` verifies source OSM classification coverage, names and produces per-type coordinate report. It **does not** prove OBF index presence or visual display; use an OBF inspector and OsmAnd for that.
- Map-derived icon art and datasets remain governed by BGMountains' applicable copyright conditions. These synthetic POI tags are **not appropriate for uploading unchanged to the real OpenStreetMap database**.

## Quick build (Ubuntu/Debian)

```sh
sudo apt update
sudo apt install -y build-essential cmake libpng-dev libsqlite3-dev python3 openjdk-21-jre-headless unzip

python3 scripts/build_bgmountains.py \
  --img /absolute/path/to/gmapsupp.img \
  --out ./BGMountains-out \
  --download-creator
```

`--download-creator` is for the official OsmAndMapCreator, and the pipeline downloads the original official `poi_types.xml` and `rendering_types.xml` the first time as needed. Network access is required for these unless you already supply the files. **This runtime did not have MapCreator, so full OBF compilation was not executed here.**

Already installed OsmAndMapCreator:

```sh
python3 scripts/build_bgmountains.py --img ./gmapsupp.img --out ./BGMountains-out \
  --creator /path/to/OsmAndMapCreator
```

Opt-in parallelism: add `--jobs 4`. The output directory must be fresh: the converter refuses to overwrite an existing OSM or staging files.

Prepare and validate the OSM without invoking the Java OBF builder:

```sh
python3 scripts/build_bgmountains.py --img ./gmapsupp.img --out ./BGMountains-preview \
  --prepare-only
```

**IMPORTANT: You must REBUILD the OSM and OBF.** This update cannot modify an already compiled `.obf` or give v0.7 output its new tags in place.

## What the configuration files mean

| File | Purpose |
|---|---|
| `config/poi-overrides-all-types.csv` | **Active** 1,871-row config, including 263 retained types with revised tags. `mode=replace` replaces old hardcoded semantics, retaining provenance/name. All non-present legacy type rows remain for historical comparison. |
| `config/poi-overrides-all-types-v07-user-upload.csv` | Exact backup of the CSV you supplied. |
| `config/poi-labels-bg.csv` | New 263-row Bulgarian fallback name prefixes, editable. |
| `config/poi-icon-audit.csv` | All 263 types, count, original tags vs new tags, classification confidence and sample coordinates. |
| `config/osmand-icons.csv` | Optional **custom render style** icon names/visibility; never controls the default `tourism=information` fallback injected through the active overrides. |
| `data/BGMountains-frequent-poi-types-100plus-v08.csv` | All 87 frequent types, with reviewed tags, classification mode and coordinates. |
| `tools/audit_icon_coverage.py` | Independent POI OSM field audit; one row per present type. |

**How to edit a type:** find `code=0x6500` in active overrides. Edit the **tags** column, leaving a `key=value;key=value` list. Change `mode=replace` only if you understand how the C++ built-in classifications merge. Set a Bulgarian text prefix in `config/poi-labels-bg.csv`. Run again from scratch to rebuild OSM then OBF.

### Why 0x6500 is not `amenity=drinking_water`

The original legend labels this a **каптаж** (water intake/catchment), not necessarily a drinking-water source. v0.8 records `man_made=water_intake` but adds an explicit *presentation-only* `tourism=information;information=board;bgm:synthetic_icon=yes` for common OsmAnd rendering. The "information board" is NOT a physical claim. The default Bulgarian name prefix is `Вода - Каптаж`. If you want `Вода - Резервоар`, just edit the label CSV; do not misrepresent a small intake as an artificial lake (`natural=water;water=reservoir`).

`0x6602` already has `tourism=viewpoint` and does not use a synthetic board marker. Its failure to appear in a particular existing OBF could be an old build, incomplete POI indexing, or display suppression—only inspecting that actual OBF can distinguish these.

### Stable anonymous names

For an unnamed POI, `name = Bulgarian_label + " - " + FNV1a_base36_11(mapset|type|quantized_lat|quantized_lon)`. The output is deterministic for an identical IMG and coordinates across threads and builds. A very small collision probability remains; the coordinates/mapset/type remain available. These generated names are for an offline map, **not actual surveyed names**.

### Test against your three sample coordinates

| Type | Coordinates | Expected tags | v0.8 generated name |
|---|---|---|---|
| `0x6602` | `42.0346141,24.6528697` | `tourism=viewpoint` | `Панорама - Гледка - 25IVP49H823` |
| `0x6500` | `42.0347643,24.6522689` | `man_made=water_intake;tourism=information;information=board` | `Вода - Каптаж - 1THF1SDNTKJ` |
| `0x6500` | `42.0340991,24.6520114` | Same | `Вода - Каптаж - ED03D455VTZ` |

These examples were directly extracted from the 422MiB IMG into a Plovdiv test OSM and compared to a source manifest. All 57 POIs in that test passed exact semantic-tag checks, with **zero missing and extra**. Serial vs `--jobs 4` POI identities/tags were equal.

## Review before copying `.obf` into OsmAnd

Pipeline artifacts in `--out`:
- `BGMountains.osm` — exact source XML for the OBF builder
- `source-pois.csv`, `poi-types.csv` — extraction manifest and type counts
- `validation.json`, `mismatches.csv` — original-source to OSM audit
- `poi-coverage.csv`, `poi-coverage.json` — export display mapping coverage
- `osmand-config/poi_types.bgmountains.xml`, `rendering_types.bgmountains.xml` — registrations added to **complete upstream XML**
- `osmand-config/BGMountains-custom.render.xml` — optional style; exact Garmin TYP art is NOT embedded into stock OsmAnd
- `*.obf` plus `mapcreator.log`, `obf-poi-index.txt` (only after a successful Java compilation)

The `tourism=information` fallback should give OsmAnd a native searchable POI category in an appropriate compatible build. **Neither an OSM tag nor an indexed OBF proves a symbol will be drawn at every zoom**: the map style, profile, POI filters, icon availability, density/collision and selected offline map still affect visibility. Test at zoom 17–20 with POI layers enabled. Avoid having an old overlapping BGMountains OBF selected at the same time.

To inspect your built file with OsmAndMapCreator:

```sh
/path/to/OsmAndMapCreator/inspector.sh -vpoi -vpoiobjects \
  -bbox=24.64,42.05,24.665,42.02 ./BGMountains-out/*.obf \
  > ./BGMountains-out/plovdiv-obf-inspector.txt
```

Inspect output for the three named POIs. **There is no tested generic parser of OsmAndMapCreator's inspector text in this package**, and we have not verified a full-country OBF built from v0.8 in this runtime. If OSM has the POI but the inspector doesn't, focus on indexing or generated XML; if inspector has it but the screen doesn't, focus on style/POI display/filter/zoom.

### Local-only licensing warning

Do not upload the generated synthetic display tags to OpenStreetMap or assert they accurately describe physical information boards. The original IMG, derived icons and extracts can have third-party copyright restrictions. The source-only code path should be distributed separately from map-derived sample assets until rights are confirmed.
