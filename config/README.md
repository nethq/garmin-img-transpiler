# BGMountains POI translation review (v0.94, 2026-10-16)

## What was revised

Inputs: the user's `poi-overrides-all-types(3).csv` (1,871 unique Garmin codes), the original strict decoded 410,563-point source inventory and legend, old v18 KMZ/OBF tag-category comparison, and OsmAnd upstream `poi_types.xml` (SHA `f2f284589c8287f9db3a6607265af91c59267788`).

**This is a config revision, not a new converter release or compiled OBF.** It is intended for private map generation. It is **not suitable for direct import into public OpenStreetMap**: it contains private-map compatibility aliases and synthetic classifications.

Two *alternative* POI configuration formats are included because old and new programs read different schemas:

- `BGMountains-POI-overrides-v094-reviewed.csv` — **five columns** (`code,name_contains,mode,tags,note`). Intended for C++ `garmin2osm --poi-overrides`. Contains the 1,871 base rows **plus 79 case-sensitive name-conditional rules**, for 1,950 rules total. Edit this if invoking C++ directly.
- `BGMountains-native-poi-mapping-v094.csv` — **15 columns**. Intended for the previously supplied v0.93 Python `scripts/build_bgmountains.py --poi-config ...` pipeline. Contains 1,871 base rows; it cannot represent `name_contains` overrides in the unmodified v0.93 translator. It retains your existing fallback label prefixes from that v0.93 baseline. **Do not supply the five-column file to `--poi-config`**. Conversely the 15-column file is not accepted by `--poi-overrides`.

**For a Python v0.93 run, use only one POI control file (`--poi-config`) and the existing single line file (`--line-config`).** The pipeline regenerates the intermediate five-column C++ rules. Updating an older `config/poi-overrides-all-types.csv` alone does *not* update a v0.93 Python build.

We could not inspect the current private Gitea branch; if its CLI or schema has since changed, compare its current code with these schemas before installing.

## Important changes, by meaning

| Hex | Previously | Reviewed result | Important caveat |
|---|---|---|---|
| `0x6414` | `amenity=drinking_water` | Add `man_made=water_tap`, matching v18's dual native POI categories | A Garmin fountain icon does not confirm *present-day* water flow. C++ conditional `Сух/сух` name exceptions explicitly mark `drinking_water=no`. |
| `0x6500` | `man_made=water_intake` | Add `amenity=water_point` **as a private OsmAnd compatibility alias**, plus `drinking_water=unknown` | **Not a reliable potable source.** OSM `water_point` normally implies water suitable for refilling tanks, so this alias should not be exported to public OSM; physical water availability/access need confirmation. |
| `0x6511` | `natural=spring` | Preserve | Not automatically potable. |
| `0x6417` | `man_made=water_well` | Preserve | Not automatically potable. |
| `0x6402` | `tourism=wilderness_hut` on **all** buildings | Generic `building=yes`; C++ overrides only when named `заслон` or `хижа` | Source names include post offices, substations, military facilities. An erroneous shelter label is a hiking safety problem. |
| `0x661d` | Agricultural structures advertised as wilderness huts | `building=agricultural`, with name-specific hut/shelter exceptions | Includes many sheepfolds. Overnight access unknown. |
| `0x6405` | Hunting lodges advertised as wilderness huts | `building=hut`, plus explicitly named shelter exceptions | Hunting lodges may be locked/private. |
| `0x6600` | `replace` could erase elevation during C++ processing | `append` preserves numeric `ele=*`; no fake locality | Numeric annotation, not generic POI. |
| `0x6603` | `replace` could erase elevation | `append` preserves source metadata | Survey point label, lower visual priority. |
| `0x2c08` | All sports venues `leisure=horse_riding` | Default sports venue + name-specific `stadium`, `pitch`, `playground`, `ice_rink`, etc. | Mixed Garmin category, so unknown venue subtype remains provisional. |
| `0x2f0b` | `leisure=track` for predominantly parking | `amenity=parking`, with a few non-parking name exceptions | Parking is **non-indexed** in inspected OsmAnd POI definitions even though it has map rendering support. |
| `0x2c06` | Unclassified | `amenity=university`; hospital exception | Based on university/faculty source names. |
| `0x2c07` | Unclassified | `tourism=zoo` | Based on zoo names. |
| `0x2d01` / `0x2d03` | Unclassified | Theatre/cinema with named exceptions | Names distinguish mixed types. |
| `0x2a07` / `0x2a0e` | Unclassified or generic shop | Fast food / cafe with exceptions | Named bistros can be restaurants. |
| `0x2e03` / `0x2e0b` | Generic shop / unclassified | Supermarket / electronics store | Samples include Lidl, Billa, Kaufland, Technopolis. |
| `0x3007` | Every courthouse/pumping station called `man_made=pumping_station` | Courthouse default; exceptions for pumps, treatment plants, prisons | Type group is demonstrably heterogeneous. |
| `0x650b` / `0x660b` | `trailblazed=yes` incorrectly placed on isolated nodes | `tourism=information;information=trail_blaze;hiking=yes` | Accurate OSM trail-marker *point* classification, but `tourism=information` may not be indexed in stock OsmAnd Nearby search; `trailblazed=*` belongs on path ways. |
| `0x4700` | Risk of generating fictitious marinas | `harbour=yes` + source/needs-review metadata | Do not convert unidentified inland marks into `leisure=marina`. |

Other refinements are in the change log. Existing Garmin provenance `garmin:type`, feature, mapset and real source names are preserved by the tested C++ exporter. This config **does not generate names**; the converter's existing name-label logic does that.

## Visibility and Nearby limitations

The upstream OsmAnd POI registry has distinct cases:
- Indexable: `amenity=drinking_water`, `man_made=water_tap`, `amenity=water_point`, `natural=spring`, `amenity=shelter`, `tourism=alpine_hut`, etc.
- `no_indx=true` in inspected definitions: `place=locality`, `tourism=information`, `man_made=bridge`, `amenity=parking`, `man_made=survey_point`, among others.
- Not registered as a native standalone POI type: `man_made=water_intake`, `building=agricultural`, `building=hut`, `shop=yes`, etc.

**Semantic correctness, presence in the OBF POI index, Nearby search and visibility at a given zoom are separate properties.** Tags such as `bgm:priority=critical` or `zoom_hint=12` do *not* alter stock OsmAnd's minimum zoom rules. Adding two *valid* primary tags may create multiple indexed categories as in the old v18 OBF, but that does not guarantee early rendering. Stock `place=locality` is not a general searchable POI substitute. The detailed native support for all codes still requires validating the exact installed OsmAnd release/`poi_types.xml` and then checking a compiled OBF.

### C++ direct regional test

```bash
./build/garmin2osm --input gmapsupp.img \
  --bbox '25.05,42.43,25.16,42.54' \
  --poi-overrides BGMountains-POI-overrides-v094-reviewed.csv \
  --output regional.osm
```

For a real dry-point test:

```bash
./build/garmin2osm --input gmapsupp.img \
  --bbox '23.985,43.675,23.999,43.688' \
  --poi-overrides BGMountains-POI-overrides-v094-reviewed.csv \
  --output dry-test.osm
```

### v0.93 Python preparation (no OBF)

```bash
python3 scripts/build_bgmountains.py \
  --img gmapsupp.img \
  --poi-config /absolute/path/BGMountains-native-poi-mapping-v094.csv \
  --line-config config/line-mapping.csv \
  --bbox '25.05,42.43,25.16,42.54' \
  --out ./review-regional \
  --prepare-only
```

No extra custom rendering file is required for the revised native categories. To compile a regional OBF later, remove `--prepare-only` and supply your existing OsmAndMapCreator through `--creator`. Do **not** start the whole-country build without sufficient temporary storage (~25+ GiB was previously needed on your machine).

### Changing settings later

In the five-column file, `replace` removes existing semantic defaults but keeps Garmin source and name. `append` preserves defaults/elevation and adds tags. A row with `name_contains` applies only if the POI name contains that **case-sensitive** string; matching rows are applied sequentially in file order. To correct a default for an entire hex type, edit its base row; to specialize named subtypes, add later `name_contains` rows with the same hex. Do not add duplicate keys inside a single `tags` field. Quote CSV fields that contain commas.

In the v0.93 native file edit `osm_tags` and `action`, not `name_contains`: that field does not exist in the old 15-column schema. `priority` and `zoom_hint` are advisory only. Keep the upstream pipeline's `config/line-mapping.csv` alongside this POI control. If you need conditional name rules in that pipeline, its translator must be extended; using these files alone cannot accomplish that.

## Test evidence

- 1,871 input base codes retained. Five-column result has 1,950 CSV rules: 1,871 base + 79 name-specific exceptions. Original inactive 1,608 code entries kept inert (many historically rejected extended-point parser candidates).
- C++ binary loaded all 1,950 rules and extracted **427 POIs** in a real 0.11° × 0.11° regional test. Verified 37 drinking-water points have both `amenity=drinking_water` and `man_made=water_tap`. All 63 checked `0x6600` spot heights preserved `ele`.
- The additional small `'(Сух.)'` IMG feature at ~43.6806107,23.9911795 was correctly marked `man_made=water_tap;drinking_water=no` and had **no** `amenity=drinking_water` tag.
- v0.93 native translator `prepare` and `transform` processed 427 regional POIs, 3,611 ways and 6 hiking relations using the derived 15-column config; 0 missing mapped source types. The native output retains the dual `0x6414` tags and numeric spot heights.
- No entire-country IMG extraction, no new OBF generation or phone-side zoom/POI overlay visual test was run.

## Reference documentation

- OsmAnd native POI registry: https://github.com/osmandapp/OsmAnd-resources/blob/master/poi/poi_types.xml
- OSM `amenity=drinking_water`: https://wiki.openstreetmap.org/wiki/Tag:amenity=drinking_water
- OSM `man_made=water_tap`: https://wiki.openstreetmap.org/wiki/Tag:man_made=water_tap
- OSM `amenity=water_point`: https://wiki.openstreetmap.org/wiki/Tag:amenity=water_point
- OSM `information=trail_blaze`: https://wiki.openstreetmap.org/wiki/Tag:information=trail_blaze
- OSM `tourism=wilderness_hut`: https://wiki.openstreetmap.org/wiki/Tag:tourism=wilderness_hut
