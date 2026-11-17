# v0.97.5 performance and MapCreator corrections

The source baseline is the uploaded v097-04 project at Git archive commit
8e64536157bb25ca5a25a56884847bca1ee54334. The uploaded results directory is
build-bgmnt-v097-03. Its manifest names poi-reviewed-v3.csv. The project snapshot
also includes config/poi-rev-v04.csv, which was used for the new tests.

## What the uploaded timings establish

| Recorded stage | Seconds | Duration | Share |
| --- | ---: | ---: | ---: |
| Fetch cached MapCreator | 0.002 | under one second | under 0.1% |
| Compile index XML resources | 0.396 | under one second | under 0.1% |
| Python route attachment | 2224.077 | 37m 04s | 34.0% |
| Python semantic audit | 368.635 | 6m 09s | 5.6% |
| Java MapCreator | 3934.765 | 65m 35s | 60.2% |
| OBF inspection | 12.582 | 13s | 0.2% |
| Total | 6540.457 | 1h 49m 00s | 100% |

This run resumed from translated OSM. It contains no extraction or translation
timing. Attachment and audit together consumed 43m 13s. The Java compiler also
needs substantial time, so accelerating Python alone has a finite effect on the
complete build.

The Java log places approximately 15m 23s in XML loading, 26m 14s in processing
ways, 5m 29s in preindexing relations, and 6m 18s in writing the map index.
The exact timestamp intervals are included in the diagnostics JSON. The nested
routing-connectivity task appears separately in those intervals.

## MapCreator messages and their cause

The log contains 8,958 SEVERE messages from calcEntityIdFromRelationId(), affecting
all 3,789 generated hiking relations. Their IDs range from -1 through -3789.

For a message such as `relation -1/0/10 overflow (27/9/6 bits)`, the fields have the
following meanings.

| Field | Logged value | Accepted range | Result |
| --- | ---: | --- | --- |
| Relation ID | -1 | 0 through 134217727 | Invalid |
| Generated segment/search-point counter | 0 | 0 through 511 | Valid |
| MapCreator internal geometric hash | 10 | 0 through 63 | Valid |

The largest counter in this log is 25 and the largest hash is 63. All these
messages are explained by the negative IDs. The six-character POI name suffix
has a separate purpose and representation.

The negative IDs came from the earlier Python relation generator. Its comment
assumed they were necessary because the extractor uses positive IDs. OSM node,
way, and relation IDs have separate namespaces, so generated relations can use
small positive IDs while avoiding existing relation IDs.

The updated generator allocates positive relation IDs below 2^27. On resume, the
postprocessor repairs invalid existing IDs and updates relation-member references.
The route report includes an old-to-new relation_id_changes mapping.

A real MapCreator reproduction with the earlier regional OSM returned exit code
zero while logging 16 SEVERE messages. The corrected regional build logged zero
severe errors and produced an inspected OBF. The builder now checks the log and
records a failed build stage when severe errors, exception headers, or a missing
completion marker appear, even if the launcher exits successfully.

The OVERPASS_URL warning reports optional external relation-fetching support.
The locally generated routes refer to geometry already present in the OSM.
The warning alone does not fail the build.

The initial 0% progress display is not an exception. The log caps its loading
work total at 2147483647, which makes that display unhelpful on a large XML file.

Upstream implementation examined during this work.

https://github.com/osmandapp/OsmAnd-tools/blob/master/java-tools/OsmAndMapCreatorUtilities/src/main/java/net/osmand/obf/preparation/IndexRouteRelationCreator.java#L671

## Changes to expensive stages

The native C++17 tool uses Expat to stream validated XML. It keeps unchanged
geometry in its original bytes, rewrites the tags that translation actually
changes, and avoids building and serializing XML trees for every coordinate node
and every physical way.

Translation collects the selected overlay endpoints directly and groups them
into route components. The previous Python stage staged those endpoints through
SQLite. The generated relation topology, POI labels, hash lengths, hash offsets,
classification rules, and line settings retain the Python implementation's
semantics.

Attachment keeps an index of overlay edges. It records physical ways only when
an edge matches an overlay. It retains the original 90% coverage and 10% overrun
thresholds. Unmatched overlays remain relation members. The output pass audits
POIs at the same time, avoiding a separate full-file audit.

Member deduplication now includes member type and role. A guidepost node sharing
a numeric ID with a way remains a node. Distinct directional roles are retained.

The Python scripts continue to orchestrate commands, validate CSVs, prepare
MapCreator resources, and provide a reference/fallback engine. Their configuration
preparation took less than one second in the uploaded log.

## Measurements from the available IMG

The available original IMG has SHA-256
`a3f603966ec6f2c3ee9f0eaea0e05c8f217ac38744182abc5ee1e3abc096b879`.
Its extracted OSM is 3,075,439,720 bytes, with 410,563 POIs, 2,294,306 ways,
22,501 candidate overlay ways, and 3,791 generated hiking relations. These counts
differ from the uploaded compilation log. These measurements describe this
workspace and input.

| Operation | Python elapsed | Native elapsed | Measured ratio |
| --- | ---: | ---: | ---: |
| Large-file translation | 304.36 s | 50.94 s, including audit | 5.98x |
| Large-file standalone audit | 90.41 s | 34.76 s | 2.60x |
| Rila attachment plus audit, 17,476,266 bytes | 3.25 s | 0.48 s | 6.74x |

The large native attachment plus fused audit took 73.70 s. Translation plus
attachment plus audit took 124.64 s, excluding the first native compilation.
Of 22,501 overlays, 19,061 attached to physical ways and 3,440 kept their original
geometry. The large-file Python audit and native audit reports were identical.
Translation report counters also matched the Python reference.

Large-file validation jobs overlapped during parts of measurement, including
Python translation and the independent audit. The Rila comparison ran its engines
sequentially. These observations do not establish a full-country OBF speedup on
the user's hardware. The original log's 65-minute Java stage remains a separate
cost. Full-country native preprocessing and regional real OBF generation were
tested. Full-country Java OBF regeneration was not run here.

## Running the update

Extract the update ZIP over the existing project. It contains changed code,
tests, vendored parser headers/license, and this report. Its paths start with
CMakeLists.txt, scripts/, src/, tests/, and docs/. Configuration CSVs are supplied
by the existing project.

From the project root, a fresh build applying the uploaded v04 POI profile uses
this command. Adjust the IMG location and Java heap to the machine.

```bash
python3 scripts/build_bgmountains.py \
  --img /home/opthq/gmapsupp.img \
  --poi-config config/poi-rev-v04.csv \
  --line-config config/line-mapping.csv \
  --out build-bgmnt-v097-05 \
  --attach-routes \
  --engine native \
  --jobs 4 \
  --download-creator \
  --java-opts '-Xms512M -Xmx25G'
```

An existing MapCreator directory can be supplied through --creator instead of
--download-creator.

To repair IDs and rebuild an existing translated checkpoint, provide its path
with --native-osm in place of --img. Keep a separate output directory. Reuse the
POI CSV that produced that checkpoint so the audit and index resources describe
its data. Omit --attach-routes when the checkpoint is already attached.

Resuming a checkpoint preserves its existing POI names and semantic tags. To
apply a changed POI profile, translate the original raw OSM with --osm, or extract
the IMG with --img. This distinction matters because the attached compilation
results used v3 while the project snapshot contains v04.

--engine auto is the default. It builds the native tool when an Expat runtime is
available. The public Expat 2.6.4 headers and MIT license are included, allowing
Linux systems with libexpat.so.1 to build without installing development headers.
The runtime-only discovery path was compiled and tested. If the runtime is
unavailable, auto uses the Python engine. --engine native reports a clear error
if native processing cannot be built. --engine python selects the reference path.
No package installation is performed by the pipeline.

CMake prints individual compiler and linker elapsed times on Make/Ninja builds.
The Python driver writes timings-*.jsonl for configuration, compilation,
extraction, translation, native attachment/audit, MapCreator, and inspection.
Native logs also show index-scan and output/audit pass times. The fused audit is
included in translation or attachment timing. MapCreator errors and its internal
stage intervals are written to mapcreator-diagnostics.json.

## Verification

Native/reference regression tests cover decimal rounding, scientific notation,
Unicode whitespace, Bulgarian and XML-escaped names, per-type suffix lengths,
unknown types, elevation annotations, route grouping, existing relation-ID
collisions, nested relation references, unmatched overlays, directional roles,
node/way ID namespaces, malformed XML, and zero-exit MapCreator failures.

The native and Python reference suites passed, including hash configuration,
legacy vector extraction, configuration/audit checks, orchestration, and
bounded-memory XML parsing. A pre-existing configuration inventory test was
updated to recognize the uploaded poi-rev-v04.csv file.

Every configuration file from the uploaded project was checked byte-for-byte
against the input ZIP. The update ZIP contains no configuration CSV replacements.
Outputs are promoted from unique temporary files after native processing
succeeds. A malformed XML regression confirms preservation of the previous
successful output.

The JSON measurements, MapCreator diagnostics, and corrected regional log are
included in docs/performance-v097-05/.
