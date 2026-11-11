#!/usr/bin/env python3
"""Audit Garmin -> OSM POI mappings against an exact OsmAnd poi_types.xml.

No OBF creation. Exits 0 for ordinary data regressions unless --strict is requested.
The existence of a poi_type does NOT guarantee map icon visibility at any zoom.
"""
import argparse,csv,json,collections,sys
from pathlib import Path
from xml.etree import ElementTree as ET

def registry(path):
    root=ET.parse(path).getroot()
    main=collections.defaultdict(list);extras=collections.defaultdict(list)
    for node in root.iter():
        if node.tag not in ('poi_type','poi_additional'):continue
        t=node.attrib.get('tag');v=node.attrib.get('value')
        if not t or v is None:continue
        d=main if node.tag=='poi_type' else extras
        d[f'{t}={v}'].append(node.attrib)
    return main,extras

def interpret(row,main,extra):
    tags=[v.strip() for v in row['osm_tags'].split(';') if '=' in v]
    supported=[];noindex=[];additional=[];missing=[]
    for tv in tags:
        candidates=main.get(tv,[])
        if any(x.get('no_indx')!='true' for x in candidates):supported.append(tv)
        elif candidates:noindex.append(tv)
        elif tv in extra:additional.append(tv)
        else:missing.append(tv)
    if supported:kind='native_indexable_type'
    elif noindex:kind='only_nonindexed_type'
    elif additional:kind='additional_without_primary_type'
    else:kind='no_matching_native_type'
    is_terrain=(row.get('priority')=='terrain' or row.get('code')=='0x6600')
    priority=row.get('priority','')
    cnt=int(row.get('source_count') or 0)
    if is_terrain:severity='terrain_exemption'
    elif kind=='native_indexable_type':severity='ok'
    elif priority=='critical':severity='critical'
    elif cnt>=100:severity='high'
    else:severity='low'
    return {'code':row.get('code',''),'count':cnt,'priority':priority,'legend_bg':row.get('legend_bg',''),
        'osm_tags':row.get('osm_tags',''), 'status':kind, 'severity':severity,
        'indexable_poi_tags':';'.join(supported),'nonindexed_poi_tags':';'.join(noindex),
        'additional_only_tags':';'.join(additional),'unrecognized_tags':';'.join(missing),
        'sample_lat':row.get('example_lat',''),'sample_lon':row.get('example_lon',''),
        'explanation':'Registry compatibility only; does not guarantee rendering zoom or OBF preservation'}

def main(argv=None):
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mapping',required=True,help='Single canonical POI mapping CSV')
    ap.add_argument('--poi-types',required=True,help='OsmAnd official poi_types.xml, preferably pinned version')
    ap.add_argument('--report',default='poi-native-index-audit.csv')
    ap.add_argument('--json',default='poi-native-index-audit.json')
    ap.add_argument('--strict',action='store_true',help='Exit nonzero for unindexable critical/high types')
    args=ap.parse_args(argv)
    native,extra=registry(args.poi_types)
    with open(args.mapping,encoding='utf8-sig',newline='') as f:
        allrows=list(csv.DictReader(f))
    # Name-conditional rows are not additional Garmin types; counting them as
    # independent types would multiply source_count and inflate POI statistics.
    rows=[r for r in allrows if not r.get('name_contains','')]
    out=[interpret(r,native,extra) for r in rows]
    with open(args.report,'w',newline='',encoding='utf8') as f:
        w=csv.DictWriter(f,fieldnames=out[0].keys());w.writeheader();w.writerows(out)
    stats=collections.Counter(r['status'] for r in out)
    weighted=collections.Counter()
    for r in out:weighted[r['status']]+=r['count']
    risk=[r for r in out if r['severity'] in ('critical','high')]
    payload={'type_counts':dict(stats),'weighted_source_counts':dict(weighted),'critical_or_high':len(risk),
         'top_regressions':sorted(risk,key=lambda z:-z['count'])[:30],
         'warning':'POI type registry check ONLY. Does not guarantee OBF map index presence, symbol visibility, zoom or Nearby settings.'}
    Path(args.json).write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf8')
    print(f"Inspected {len(out)} Garmin types: {dict(stats)}, high/critical issues: {len(risk)}")
    print(f"Wrote {args.report} and {args.json}")
    return 2 if args.strict and risk else 0

if __name__=='__main__':sys.exit(main())
