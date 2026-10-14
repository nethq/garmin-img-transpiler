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
