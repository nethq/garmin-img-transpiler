# BGMountains v0.9: meaningful POIs, distinct icons and Garmin TYP route colours

## Why v0.8 displayed 'Marina', information boards and orange anchors

The old dataset used unrelated standard tags to force OsmAnd to index symbols. In v0.9 the default `poi-overrides-all-types.csv` no longer injects synthetic `tourism=information;information=board` for arbitrary places. The C++ area type 0x09 was also incorrectly tagged `leisure=marina`, although the BGMountains legend identifies it as shopping area. It is now `landuse=retail`. We found 0x650b winter waymarks clustering on the Vihren route (not marinas). The output is not for public OpenStreetMap imports without source license clearance.

## Build from original IMG to OBF (Linux)

```bash
sudo apt install -y build-essential cmake libpng-dev libsqlite3-dev python3 openjdk-21-jre-headless unzip
python3 scripts/build_bgmountains.py \
   --img '/path/to/gmapsupp.img' \
   --out './BGMountains-output' --download-creator
```

Or use an already installed OsmAndMapCreator:

```bash
python3 scripts/build_bgmountains.py \
  --img '/path/to/gmapsupp.img' --creator /path/to/OsmAndMapCreator \
  --out ./BGMountains-output
```

CLI uses **1 worker by default**, add `--jobs 4` to opt in.

Important: the MapCreator utility is not bundled (large official upstream binary); script downloads it if you specify `--download-creator`. The online upstream OsmAnd `poi_types.xml` and `rendering_types.xml` are fetched too. To work offline provide `--poi-base ... --rendering-base ... --creator ... --offline`.

Generated files:
- `BGMountains.osm` geographic source, custom `bgm_poi` tags, raw Garmin IDs and hashes
- `osmand-config/poi_types.bgmountains.xml`, `rendering_types.bgmountains.xml` for indexing
- `osmand-config/BGMountains-outdoor.render.xml` **MUST BE INSTALLED/SELECTED IN OSMAND** for custom icons and trail colors
- `source-pois.csv`, `poi-types.csv`, `validation.json`, `poi-coverage.csv` for checks
- `.obf`, `mapcreator.log`, `obf-poi-index.txt` **only if MapCreator completed successfully**

### Install on your phone

1. Copy the output `.obf` to the OsmAnd offline maps directory or import through OsmAnd.
2. Import the `.render.xml` style in OsmAnd. On Android: *Menu → Maps & Resources → Local → Rendering styles → Import*, or *Settings → Import from file*. Some versions require placing the style in the app's `rendering/` directory and restarting.
3. Select `BGMountains outdoor v0.9` in *Menu → Configure map → Map rendering → Map style*.
4. Test at zoom 16-18. Very dense points such as winter waymarks and reservoir/catchment markers intentionally begin at zoom 16-17 to prevent the map from being solid with symbols.
5. For search: OsmAnd's POI database/index and on-screen symbols are different. Custom `bgm_poi` entries are added to the build's POI index, but custom POI *categories* and 'Nearby POI' behavior may require app-compatible POI definitions. Search by generated fallback name if Nearby doesn't expose them. We cannot guarantee this until the final OBF is inspected on the device.
6. Remove the older test OBF before loading this one, otherwise the app can display overlapping categories/data.

### How to customise

- `config/poi-overrides-all-types.csv`: **meaning**, stable source Garmin hex → OSM tags. **Do not use false `leisure=marina` or fake boards for icons.**
- `config/osmand-icons.csv`: **appearance**, code → built-in OsmAnd icon and minzoom. Some obscure icon IDs may vary between app builds: validate with your installed version.
- `config/trail-styles.csv`: **hiking overlay appearance**, extended Garmin line type → color/zoom/width. Colors recovered from TYP; 50 line types audited. Exact Garmin pixel/band patterns aren't reproducible from plain OsmAnd XML alone; the extracted original source TYP remains in the IMG.
- `config/poi-labels-bg.csv`: fallback **Bulgarian prefix + deterministic hash** for unnamed POIs; valid existing names untouched.
- `config/poi-name-fixes.csv`: coordinate- and type-specific manual corrections, no fuzzy changes to location names.
- `data/BGMountains-v09-POI-icon-crosswalk.csv`: verified legend, all 263 types, default style icon, counted occurrences and sample location.

### Known limitations

- IMG → OSM regional export fidelity validated, not mobile screen rendering.
- MapCreator couldn't run in the author's offline environment; a new OBF was NOT generated or inspected in these tests.
- Not every Garmin TYP icon has a built-in OsmAnd equivalent. The native application cannot load arbitrary TYP PNGs from an ordinary OBF/style; exact icon artwork requires a custom OsmAnd application build.
- Road/trail colored overlays are not necessarily independently routable trail geometry. **v0.9 no longer creates false routable paths from the decorative extended overlays.** Underlying real road/path features retain their physical geometry.
- Many obscure Garmin types still share the generic built-in info icon, not because they mean 'information board', but because their semantics and replacement art are unverified. Users can edit `config/osmand-icons.csv`; filenames in OsmAnd icon resources can differ between app versions.
- Use this dataset for private testing pending rights clearance from BGMountains and third-party sources.
