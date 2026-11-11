# BGMountains Garmin IMG → native OSM → OsmAnd OBF — v0.92

**v0.92** fixes the v0.8–v0.9 synthetic marker/marina fallback approach. It aims to create a **native OsmAnd map** without Garmin TYP icon PNGs or a custom `.render.xml` on the phone.

The **only two user-editable translation files** are:

1. `config/poi-mapping.csv` (Garmin point type → native OSM tag(s), Bulgarian naming prefix, legend, priority, historic icon pointer, example coordinates)
2. `config/line-mapping.csv` (Garmin extended line type → OSM way tags, color-marked hiking relation policy and `osmc:symbol`)

Everything under the generated build/output directory is an **intermediate or report**, not a third configuration. You don't have to supply `poi_types.xml`, `rendering_types.xml`, an icons CSV or a custom rendering style.

## Requirements

Ubuntu/Debian: `sudo apt install build-essential cmake libpng-dev libsqlite3-dev python3 openjdk-21-jre-headless unzip`

To **compile an OBF**, provide an official [OsmAndMapCreator](https://osmand.net/docs/technical/map-creation/create-offline-maps-yourself/) installation containing `utilities.sh`, or use the automatic download option (downloads from OsmAnd). **Python, CMake and `make` by themselves do NOT build OBF**.

## Full map: one command

```sh
python3 scripts/build_bgmountains.py \
  --img '/path/to/gmapsupp.img' \
  --out './output' \
  --creator '/path/to/OsmAndMapCreator'
```

If you don't have MapCreator:

```sh
python3 scripts/build_bgmountains.py \
  --img '/path/to/gmapsupp.img' --out './output' --download-creator
```

It compiles C++, extracts the Garmin IMG to OSM, uses the two mapping files, assigns stable Bulgarian hashes to unnamed POIs, produces route relations, conservatively attaches matched overlays to actual `highway=*` ways, performs a semantic QA audit, and runs OsmAndMapCreator. The **only file to copy onto your phone is the resulting `.obf`**. Enable hiking routes and relevant POI filters in OsmAnd as necessary. No third-party icons or renderer file is included or required.

For a fast test that stops before the Java OBF stage:

```sh
python3 scripts/build_bgmountains.py \
  --img '/path/to/gmapsupp.img' \
  --out './vihren-test' --bbox '23.36,41.70,23.42,41.76' --prepare-only
```

Parallelism remains **disabled by default** (`--jobs 1`). For opt-in concurrent IMG extraction append `--jobs 4`. The Python mapping, relation, QA and Java steps are not parallelized by this switch.

Use `--no-route-attachment` to keep source overlay geometries as hiking-relation members instead of attaching matched real path/road ways. `--keep-raw` retains the intermediate IMG-native XML for debugging.

## Editing the 2 CSV files

### `config/poi-mapping.csv`

| Column | What it means |
|---|---|
| `code` | Garmin POI type, in hex; never changed in provenance |
| `source_count` | Count from source inventory; informational only |
| `legend_bg`, `legend_en` | Original BGMountains legend meaning (where verified) |
| `meaning_confidence` | `legend` / `source_inferred`; review unknowns |
| `osm_tags` | `k=v;k=v` for **truthful standard** OSM semantics; no fake category for icons |
| `label_bg` | Prefix for otherwise nameless objects, with deterministic alphanumeric suffix |
| `priority` | `critical`, `normal` or `terrain`; currently emits `bgm:priority` metadata |
| `zoom_hint` | Desirable zoom target for future style/QA work, **not an OsmAnd override** |
| `action` | `replace` removes earlier semantics, `append` keeps them, `raw` keeps provenance only, `drop` discards the POI |
| `legacy_icon` | `TYP:<hex>` reference for older Garmin icon, for auditing only |
| `example_lat`, `example_lon`, `example_name` | Sample place from original source inventory to inspect |

If a source code is known, edit `osm_tags` and set `action=replace`. Example: `0x6414` → `amenity=drinking_water`. If it isn't known, leave it as `raw` until you inspect the old legend and sample coordinates. Nothing will invent a marina, information board or drinking tap for an unknown symbol. You may append multiple compatible tags separated by semicolons.

**Water/shelter priority rules already populated**: `0x6414` drinking tap, `0x6511` spring, `0x6500` water intake, `0x6417` well, `0x6609` hut, `0x660a` basic refuge, `0x661e` shelter and `0x6402` building. For requested naming, unnamed `0x6402` uses the prefix **„Заслон - Сграда“**; its factual OSM classification stays `building=yes` (a generic building must not falsely be declared an actual lifesaving shelter). Verify shelter suitability on the ground before relying on it.

`0x6600` numeric elevation labels preserve `ele=*` instead of being misclassified as peaks, buildings or missing POI categories. `0x650b` winter waymarkers and `0x660b` trail markers remain Garmin-sourced nodes rather than incorrect water/marina/guidepost POIs.

### `config/line-mapping.csv`

| Column | Meaning |
|---|---|
| `code`, `source_count` | Garmin extended polyline code/count |
| `legend_bg`, `legacy_color` | Known description and source TYP base color |
| `osm_way_tags` | Additional truthful `key=value;key=value` tags on source ways |
| `route_mode` | `relation` builds OSM hiking routes; `preserve` keeps raw way without assuming it is a trail |
| `osmc_symbol` | Standard OSMC syntax, e.g. `red:white:red_bar` |
| `network` | `lwn` (local walking), change if there is evidence for a larger network |
| `route_confidence` | `color_only` means inferred from color but pattern not independently established |
| `priority` | Descriptive category only, not stock OsmAnd zoom control |

Colored candidate hiking overlays become `type=route` + `route=hiking` + `osmc:symbol=*` relations. All retain the original `garmin:type` and source line metadata. Matching route overlay lines to existing physical `highway=*` geometry avoids generating fake navigable duplicate paths. A physical road segment is used only if at least 90% of an overlay's segments match and the physical way extends at most 10% beyond it; unmatched overlays remain relation members, flagged in `route-attachment.json`.

**A source color is not conclusive proof of a painted bar pattern**. All automatic bar-style assignments from color-only evidence explicitly say `route_confidence=color_only`; check the original map and choose the right `osmc_symbol` for each code before treating that detail as verified. See [OSM trail markings](https://wiki.openstreetmap.org/wiki/Trail_markings) and [OSMC symbol syntax](https://wiki.openstreetmap.org/wiki/Key:osmc:symbol).

## New IMG / future map: generate blank templates

```sh
python3 scripts/build_bgmountains.py \
  --img '/path/to/another-garmin.img' \
  --out './new-map-template' --template
```

The result contains `poi-mapping.TEMPLATE.csv` with **all discovered types, counts and sample POI coordinates**, and `line-mapping.TEMPLATE.csv` with all discovered extended line types/counts. Unknowns start as `action=raw`, `route_mode=preserve` and **without semantic tags**. You must fill them based on that map's own legend and examples; BGMountains mappings must not be assumed to apply to another Garmin map. To build it, point `--poi-config` / `--line-config` to the new two reviewed files.

## Outputs / validation

| Path | Purpose |
|---|---|
| `BGMountains-native.osm` | Final translated, standard-tagged OSM intermediate |
| `source-pois.csv` | Per-point source type, mapset, coordinates and name |
| `discovered-pois.csv` | Observed type inventory with up to 5 sample coordinates |
| `route-report.json` | Coloured overlays/route relation counts |
| `route-attachment.json` | Reused physical path vs unmatched overlay counts |
| `semantic-audit.json` | Detailed native category statistics, unknown types and example positions |
| `extract.log`, `translate.log`, `audit.log` | Pipeline logs |
| `mapcreator.log` | Java OBF build log when compilation is attempted |
| `obf-inspection.txt` | BinaryInspector POI summary (if inspector is installed) |
| `*.obf` | Final file for OsmAnd (only if MapCreator succeeds) |

Tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

## What native stock OsmAnd cannot guarantee

- OsmAnd's **own rendering style controls icon selection, label collision and minimum zoom**; `zoom_hint=12` does not change it. To force all water/hut icons to zoom 11 or 12, a custom OsmAnd style/app configuration is unavoidable. v0.92 deliberately does **not** install one.
- Not every valid OSM tag has a stock icon, especially water intakes, bridge points, elevation points and trail blazes. We prefer correct/metadata-only tags over false marinas/boards.
- A valid hiking relation is not a guarantee that stock OsmAnd renders every segment. Inspect `route-attachment.json` and verify on-device.
- The OSM file parser and XML/route structural tests do **not** establish that the POIs survived final OBF indexing. Use `mapcreator.log`, `obf-inspection.txt` and actual OsmAnd app checks. We cannot claim a complete nationwide OBF was compiled/tested in this environment unless MapCreator is available.
- Map-derived sample coordinates and legend crosswalk are subject to BGMountains data licensing; do not relicense or import them to public OSM automatically.

**v0.92 does not use the Garmin icon PNGs or `BGMountains-outdoor.render.xml`**. You may remove older BGMountains OBFS from OsmAnd to avoid duplicates before installing the new one.
