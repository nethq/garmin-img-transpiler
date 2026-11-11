# BGMountains v0.93 — bounded-memory translation and private-map POI fallback

This is a **source patch release**, building on v0.92. All user translation choices live in exactly two files:

1. `config/poi-mapping.csv`: Garmin hex → OSM semantic tags; Bulgarian legend name and old TYP code; `action`, priority, source count and examples.
2. `config/line-mapping.csv`: Garmin line hex → OSM way tags, `osmc:symbol`, route relation choice, marking color and confidence.

`cpp-poi-override.csv` and `cpp-name-labels.csv` are generated **internal intermediates**, not control files; do not edit them.

## Direct diagnosis of the supplied nohup

The same appended logfile shows **different historical attempts**: an early `java.lang.OutOfMemoryError: Java heap space` in OsmAndMapCreator, followed in the last attempt by `java.io.IOException: No space left on device` while `IndexCreator.extractOsmToNodesDB` built the Java/SQLite intermediate database. Earlier `poi-coverage.log` saying `PASS` refers only to OSM semantic checks. It is not a successful OBF creation signal.

## Changes

- Streaming OSM parser clears the XML **root**, preventing an accumulating empty Element for every object.
- Expensive optional `attach_native_routes.py` step is opt-in (`--attach-routes`); default keeps existing routable highway network unchanged, and generates hiking-route relations from Garmin colored overlays. Without attachment, those relations describe overlay geometry; they are not proof of route-based navigation along the existing path ways.
- Unclassified POIs now receive `place=locality`, `bgm:synthetic_fallback=place_locality`, `bgm:classification=unverified`, and a generated Bulgarian name `Неуточнено - <stable hash>` **when the source name was empty or previously synthetic**. Genuine source names remain untouched. Raw `garmin:type` and mapset information are preserved.
- **Exception:** `0x6600` numeric spot heights and other elevation annotations (`ele=*`) are not fabricated as localities. They remain elevations rather than false place icons. Known native tags are never overwritten by the fallback.
- Native categories for water, drinking water, shelters/huts, bridges and more remain sourced from existing two mapping files. No extracted Garmin artwork or phone-side custom stylesheet is required.
- Semantic audit differentiates actual unclassified records, synthetic locality fallbacks and non-POI elevation annotations, and includes example coordinates. `PASS` still does not prove an Android icon renders.
- Conservative free-disk preflight before invoking MapCreator, based on `max(2 GiB, 5*OSM size + 1 GiB)` free in the output filesystem. The estimate is based on the reported >25 GiB scratch need for ~6 GiB OSM; it is *not a guarantee*.
- JVM heap via `JAVA_TOOL_OPTIONS` as well as `JAVA_OPTS`, for MapCreator builds that ignore the latter; use e.g. `--java-opts '-Xms512M -Xmx6G'` (subject to host RAM). **A large heap cannot solve out-of-disk.**
- OBF success only if a newly generated nonempty `.obf` exists, not a stale file from an earlier run.

## Quick regional test — no Java, no large output

```sh
python3 scripts/build_bgmountains.py \
  --img '/path/to/gmapsupp.img' \
  --bbox '23.30,41.67,23.50,41.84' \
  --out './regional-test' \
  --prepare-only

cat regional-test/semantic-audit.json
cat regional-test/route-report.json
```

`--jobs 1` is the default. `--jobs 4` is optional; on the source IMG, serial and parallel runs had equal POI counts and candidate colored overlay counts but a slight discrepancy in connected route groups (78 vs 82). **Do not assume identical routing topology with parallel extraction yet.**

To test a truly unfamiliar map and generate configuration templates:

```sh
python3 scripts/build_bgmountains.py --img another.img --bbox 'W,S,E,N' \
  --out ./new-map --template
# Review: new-map/poi-mapping.TEMPLATE.csv and line-mapping.TEMPLATE.csv
```

The templates are intentionally blank for unknown source meanings; when used by the translation pipeline, unknown POIs will receive the private `place=locality` fallback. A new map should not blindly reuse BGMountains meanings.

## Full OBF build — only on a machine with enough free scratch/RAM

```sh
python3 scripts/build_bgmountains.py \
  --img '/path/to/gmapsupp.img' \
  --out '/large-volume/bgmountains-v093' \
  --creator '/path/to/OsmAndMapCreator' \
  --java-opts '-Xms512M -Xmx8G'
```

Or first use `--prepare-only` and later `--osm '/path/to/IMG-raw.osm'` to skip the C++ extraction **if** that raw original file was saved (`--keep-raw`). `--osm` expects the *raw* IMG-derived OSM, not the already translated native OSM.

The preflight checks output filesystem free space; for shared temp partitions, also check `df -h /tmp "$PWD"` and `df -i`. An OBF may still fail if actual scratch is elsewhere. Only override the preflight (`--ignore-disk-check`) when you have independently confirmed scratch capacity.

## Limitations / honesty

- `place=locality` is a **synthetic label hack chosen for this PRIVATE map**, not a correct definition of all unknown source objects and not suitable for direct uploading into OSM.
- Stock OsmAnd decides icon set, zoom threshold, collision suppression and Nearby POI search category. A native OSM tag increases compatibility but cannot guarantee every icon is shown or listed in Nearby without a mobile test.
- Route colors represented through native OSM hiking relations using `osmc:symbol` for identified Garmin colored overlay types. Color/pattern interpretations with limited source evidence remain explicitly provisional.
- No full-country OBF creation has been performed as part of this patch validation, per the user's storage constraints. The final national MapCreator step remains to be verified on the user's workstation.

## Regression verification

- Unit tests: `python3 -m unittest discover -s tests -p 'test_v093.py' -v`
- Previous v0.92 tests: `python3 -m unittest discover -s tests -p 'test_v092.py' -v`
- Regional real IMG: 1,915 POIs, 296 elevation annotations, 150 synthetic localities, zero unclassified after fallback, 273 route-overlay candidates. Single-worker result has 78 connected route groups. The v0.93 code has not been validated end-to-end against stock OsmAnd on Android.
