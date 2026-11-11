## 0.3.0
- Adds --whole-map and --plan.
- Uses geographical submap coverage interval union instead of one large rectangular export loop.
- Commits completed scanlines atomically and enables efficient continuation with --resume.
- Ensures compatible inputs/options when resuming via a settings fingerprint.
- Tested full Bulgarian IMG on zoom 10: 330 PNG tiles; peak RSS 20 MiB, 6.8 seconds, resumed in 0.02 seconds.
- Planned countrywide 12–16: 1,698,680 candidate tiles (prior global rectangle 1,705,377).
- Full zoom-16 countrywide render has NOT been executed or sized in this environment.

## 0.2.0
- TYP line, polygons, icons, LBL codepage and antialiasing improvements.
- Real-map 2,033-tile Rila test, 36.29 MiB, 34 seconds, peak 28 MiB.

## v0.6 (2026-10-14)
- Reviewed 67 POI code icons directly against supplied BGMountains legend; 48 semantic default updates and a detailed CSV log.
- Corrected extended POI LBL pointer offset (117 changed decoded labels) and added a subdivision-bounds sanity check (5,602 suspicious points filtered).
- Added optional coordinate/name exact-match CSV fixes, no automatic speculative place-name correction.
- Added strict reviewed nationwide type catalog, annotated legacy catalog, 16×16 TYP bitmap offline explorer, and GPXSee QA waypoint file.
- Original OBF has not been validated, and no actual IMG is included in this source release.
