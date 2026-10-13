# Garmin2OsmAnd v0.3 — countrywide conversion

A standalone, low-RAM Garmin IMG → **OsmAnd `.sqlitedb` raster overlay** generator in C++17. Reads the Garmin IMG directly; no Java, Dart, GeoJSON or `ogr2osm` used.

**Test coverage:** Built and tested against the 422 MiB Bulgarian `gmapsupp(2).img` for a countrywide zoom-10 export; tested 2,033 zoom-12–16 real-map tiles in the previous release, and small synthetic classic/GMP regression fixtures. **The full zoom-12–16 nationwide export has not been run here.** Real-map TYP styles, labels and other limitations are unchanged from v0.2.

## Installation on Debian / Ubuntu

```bash
sudo apt install -y build-essential cmake libpng-dev libsqlite3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
ctest --test-dir build --output-on-failure
```

`bin/garmin2osmand` is a compiled Linux/x86-64 binary from the test environment and may need rebuilding on other Linux distributions.

## Plan first: report real geographic coverage

```bash
./build/garmin2osmand \
  --input 'gmapsupp(2).img' \
  --whole-map --min-zoom 12 --max-zoom 16 --plan
```

For this uploaded Bulgarian IMG, the test reported **1,698,680** candidate tiles vs **1,705,377** in the old rectangular approximation. Sparse geographic coverage slightly reduces the count, but *does not guarantee every candidate produces an image*. Empty, transparent results are automatically skipped.

## Export entire IMG

```bash
./build/garmin2osmand \
  --input 'gmapsupp(2).img' \
  --output Bulgaria-Full.sqlitedb \
  --whole-map --min-zoom 12 --max-zoom 16 \
  --max-bytes 50000000000 --aa 2 --verbose
```

`--whole-map` bypasses the previous 100,000 candidate cap automatically and only visits the union of the Garmin submap coverage. `--max-bytes` remains **2 GiB by default** as a disk safety limit: set a larger byte budget as shown (50 decimal GB). Value `0` removes either limit, but is not recommended without disk monitoring. For large exports allow enough free disk space including SQLite journaling. For much smaller output, use max zoom 14 or 15, reserving zoom 16 for hiking regions.

## Resume even if a row contains no image tiles

```bash
./build/garmin2osmand \
  --input 'gmapsupp(2).img' \
  --output Bulgaria-Full.sqlitedb \
  --whole-map --min-zoom 12 --max-zoom 16 \
  --max-bytes 50000000000 --aa 2 --resume --verbose
```

Progress table records committed scanlines, including those where no image tiles were produced. On restart, completed rows are skipped without re-decoding, and already-written tiles within the interrupted row are skipped by primary-key lookup.

The database also stores settings fingerprint (IMG size, bounds, zoom range, fill and antialiasing); mismatches prevent unsafe resume. Changing just `--max-bytes` to increase the storage budget is allowed. v0.2 databases can resume without old-row checkpoints but will recheck empty tiles until reaching newly checkpointed rows.

## Database validation

```bash
python3 - <<'PY'
import sqlite3
with sqlite3.connect('Bulgaria-Full.sqlitedb') as db:
    for query in (
        'pragma integrity_check',
        'select tilenumbering,minzoom,maxzoom from info',
        'select z,count(*) from tiles group by z order by z',
        'select z,y from progress order by z',
    ):
        print(query, db.execute(query).fetchall())
PY
```

Import the `.sqlitedb` into OsmAnd and select it as an overlay or map source. This is a **raster map**, and does not reproduce Garmin routing. A countrywide zoom-16 file may be tens of GiB, because individual transparent PNG tiles often occupy many kilobytes; test available space on phone before copying.

## New in v0.3

- Explicit `--whole-map` mode removes artificial candidate cap for full-map runs, without requiring `--max-tiles 2000000` guesses.
- `--plan` counts coverage candidates and creates no DB.
- Per-row merged geographic coverage ranges; skips uncharted gaps.
- SQLite `progress` checkpoints commit atomically with finished scanlines for fast, repeatable resumes.
- Database settings fingerprint prevents resume with incompatible zoom/extent or AA settings.
- `--max-tiles 0` and `--max-bytes 0` opt out of limits explicitly.

Limitations: renderer still uses a subset of Garmin TYP pattern and icon encodings; not all labels and complex areas are rendered exactly as Garmin software. For a native compact vector routable output, a genuine OBF conversion would require an additional geometry and semantics pipeline.

GPL-3.0-or-later. See LICENSE.

## New: native IMG → OSM vector export

Version 0.4 adds a second executable `garmin2osm` for compact vector data that can be compiled into native OsmAnd `.obf`. **Read [README_VECTOR.md](README_VECTOR.md)** for commands, verification and the limitations of restoring Garmin routing and TYP styling. The existing SQLite raster tool remains available as `garmin2osmand`.
