# Hiking POIs in stock OsmAnd

## Generate, edit, build

From the repository root, inventory your original BGMountains IMG before converting it:

```bash
python3 scripts/build_bgmountains.py --img gmapsupp.img --review-pois --out review --jobs 4
# Edit review/poi-review.csv, keeping UTF-8 and quoted comma-containing fields.
python3 scripts/build_bgmountains.py --img gmapsupp.img --poi-config review/poi-review.csv \
  --out hiking-build --creator /path/to/OsmAndMapCreator --jobs 4
```

Use a fresh review directory when regenerating. Counts and base-row examples are read from the IMG. The Bulgarian labels, semantic interpretations, ordered name exceptions and zoom settings come from the editable BGMountains profile. Newly observed codes receive a raw rule for review. Zero-observation rows follow observed rows. Run without a bounding box for a complete inventory. The standalone line/route rules are in `line-mapping.csv`. For unrelated Garmin products, start with the neutral `--template` workflow instead of applying BGMountains interpretations.

| POI column | Role |
|---|---|
| code, action, osm_tags, name_contains | One base rule per code followed by ordered exceptions matched against original names. Re-extract the IMG to apply semantic changes. |
| label_bg, hash_length, hash_skip | Generated Bulgarian prefix and base36 suffix for unnamed points. The skip counts digits from the right. Generated names use `Извор - ABC123`. Genuine names stay intact. |
| hiking_group | Filter the CSV to review related features. A group identifies the intended hiking use; it is not an app-side switch. |
| index_type, index_minzoom | Effective build settings for the native OBF map index. Enter a native tag/value pair such as `natural=spring` and zoom 1–22. Blank cells inherit MapCreator rules. When multiple rows target one native type, their zoom settings must agree. The rule affects every matching object in this OBF, including source-name exceptions that resolve to that type. |
| search_fallback | Optional OBF search entry for a type that otherwise lacked one. For example `man_made/water_intake` gives a catchment an offline name entry without labelling it a drinking-water facility. |
| source_count, example_*, legend_*, meaning_confidence, priority, zoom_hint, note, legacy_icon | Inventory, provenance and older advisory values. Change index_minzoom to adjust index eligibility. Priority is descriptive metadata. |

The builder starts from the XML resources inside the installed MapCreator version and writes generated copies inside its output directory. Only the OBF goes on the phone. Previously supplied hash CSVs can still be passed explicitly via `--hash-config` to override widths. Build the supplied C++ source to enable the new hash window; the historical executable under bin/ predates it.

## How hiking priority was chosen

| Native type | Source inventory examples | First eligible index zoom |
|---|---|---:|
| Mountain rescue | 12 rescue records | 9 |
| Alpine huts | 372 records in the dedicated hut code | 10 |
| Shelters | 275 refuges + 979 shelter/cover records, plus name-specific exceptions | 11 |
| Springs and drinking-water taps | 50,308 springs and 32,922 tap-code records, including named exceptions | 13 |
| Campsites | 264; open campsites are a separate category from weather shelter | 13 |
| Wells and locality detail | 8,868 wells; synthetic unknown localities retained | 15 |
| Rocks | 15,289 | 16 |
| Trees and survey points | 10,176 trees and 26,366 survey points | 17 |

The intake category (31,214) includes installations whose access or potability is unknown. Generic buildings, hunting lodges and farm buildings have searchable names; they are not automatically promoted to public shelters. These map categories do not establish current water flow, building access or opening times. Existing names and numeric elevations are retained. The colored route relations keep their type-derived ref; different components can share this ref.

Stock OsmAnd's rendering style, map detail, icon spacing and optional POI overlay govern the final screen. The OBF provides zoom bands for map features and a separate searchable POI index. Inspect one OBF in isolation when comparing icons. Temporarily disable the POI overlay in Configure map, then enable the desired categories. Do not interpret an icon color alone as a source type. A profile can show the same point through a style and an overlay. Different installed maps can cover the same region.

## Audit

Original IMG SHA-256: `a3f603966ec6f2c3ee9f0eaea0e05c8f217ac38744182abc5ee1e3abc096b879`. The 410,563 source POIs include 172,704 unnamed records, representing 172,586 distinct mapset/type/position keys. Six base36 digits with skip zero produce one same-type collision. With skip one for springs and zero for other types, every distinct key has a unique suffix within its type for this edition. The 118 repeated keys remain indistinguishable from those fields. Recheck new IMG editions; the identifier is not an across-edition guarantee.

The earlier regional OBF indexed 37 taps as both drinking_water and water_tap. The revised tap base has one primary POI classification. Explicitly dry taps retain water_tap and drinking_water=no. All 427 POIs in the central OSM test survived conversion. The Rila preview is a separate regional excerpt around Malyovitsa (23.30,42.12 to 23.47,42.24); original subdivision linework can extend outside the point bounds.

In the delivered Rila–Malyovitsa OBF, Binary Inspector found all 1,065 generated names in the POI index and reported zero double-POI references. The regional extraction contains 1,672 source POIs. Use the six-character suffix when searching, for example `SJRNJM`. The accompanying countrywide review CSV is generated from the entire IMG, independent of the preview bounds.
