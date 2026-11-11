#!/usr/bin/env python3
"""Audit IMG-derived POIs for custom indexability and truthfully report native semantic tags.

This measures converted OSM, NOT actual OsmAnd screen visibility nor .obf indexing.
Output inventory always includes Garmin codes and example coordinates.
"""
import argparse,csv,gzip,json
from pathlib import Path
from collections import Counter,defaultdict
from xml.etree.ElementTree import iterparse

CLASS_KEYS=('amenity','tourism','historic','shop','emergency','leisure','natural','man_made','place','highway','landuse','power','waterway','aerialway','industrial','barrier','sport','building')
def main():
 a=argparse.ArgumentParser();a.add_argument('--osm',required=True);a.add_argument('--catalog',required=True);a.add_argument('--csv',required=True);a.add_argument('--json',required=True);a.add_argument('--samples',type=int,default=5);args=a.parse_args()
 with open(args.catalog,encoding='utf-8-sig',newline='') as f:catalog={r['code'].lower():r for r in csv.DictReader(f)}
 stats=defaultdict(Counter); samples=defaultdict(list); counts=Counter(); bad=[]; overview=Counter()
 opener=gzip.open if args.osm.endswith('.gz') else open
 with opener(args.osm,'rb') as f:
  for _,el in iterparse(f,events=('end',)):
   if el.tag not in ('node','way','relation'):continue
   if el.tag!='node':el.clear();continue
   tags={x.get('k'):x.get('v') for x in el.findall('tag') if x.get('k') is not None}
   if tags.get('garmin:feature')!='poi':el.clear();continue
   code=tags.get('garmin:type','').lower();counts[code]+=1;overview['pois']+=1
   name=tags.get('name','');kind=[f'{k}={tags[k]}' for k in CLASS_KEYS if k in tags]
   if not kind:overview['needs_custom_style']+=1
   else:overview['has_native_semantics']+=1
   if not name:overview['nameless']+=1
   if tags.get('bgm:generated_name')=='yes':stats[code]['generated_name']+=1;overview['generated_name']+=1
   if tags.get('bgm:synthetic_icon')=='yes':stats[code]['synthetic_icon']+=1;overview['synthetic_icon']+=1
   if not tags.get('bgm_poi'):overview['missing_raw_type_index_tag']+=1
   else:overview['has_bgm_poi_tag']+=1
   if len(samples[code])<args.samples:samples[code].append((el.get('lat'),el.get('lon'),name, '|'.join(kind)))
   el.clear()
 rows=[]
 for code in sorted(counts,key=lambda x:(-counts[x],x)):
  base=catalog.get(code,{})
  row=dict(code=code,osm_poi_count=counts[code],nationwide_type_count=base.get('count',''),
        icon_strategy=('native+custom' if samples[code][0][3] else 'custom_style_required'),
        synthetic_count=stats[code]['synthetic_icon'],generated_name_count=stats[code]['generated_name'],
        legend=base.get('verified_legend_bg',''),review_status=base.get('review_status',''),
        example_lat=samples[code][0][0],example_lon=samples[code][0][1],
        example_name=samples[code][0][2],example_osm_tags=samples[code][0][3],
        osm_coord_link=f"https://www.openstreetmap.org/?mlat={samples[code][0][0]}&mlon={samples[code][0][1]}#map=18/{samples[code][0][0]}/{samples[code][0][1]}")
  rows.append(row)
 path=Path(args.csv);path.parent.mkdir(parents=True,exist_ok=True)
 with path.open('w',encoding='utf-8',newline='') as f:
  if rows:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
 summary=dict(status='PASS' if not overview['nameless'] and not overview['missing_raw_type_index_tag'] else 'FAIL',
              counts=dict(overview),catalog_types=len(catalog),exported_types=len(counts),
              note='POIs without standard OSM categories are valid for this PRIVATE map because bgm_poi uses custom index and renderer. They require importing the style; this does not prove OBF persistence or screen visibility.',
              errors=bad[:100],missing_types_from_catalog=sorted(set(counts)-set(catalog)))
 Path(args.json).write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(summary,ensure_ascii=False,indent=2))
 return 0 if summary['status']=='PASS' and not summary['missing_types_from_catalog'] else 2
if __name__=='__main__':raise SystemExit(main())
