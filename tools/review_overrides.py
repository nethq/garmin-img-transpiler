#!/usr/bin/env python3
"""Rebuild a local-OsmAnd display profile from historical BGMountains CSV + reviewed inventory.

Tagging in this file is NOT approved for OpenStreetMap database import:
uncertain features use an explicit, synthetic tourism=information fallback.
"""
import csv
from pathlib import Path
import argparse

# Additional interpretations backed by Garmin legend or characteristic named samples.
# Uncertain ones are kept descriptive only, with an explicitly synthetic display icon.
INTERPRET = {
 '0x6600':('bgm:spot_height=yes','Височинна кота','legend-ambiguous'),
 '0x6500':('man_made=water_intake','Вода - Каптаж','legend'),
 '0x6614':('natural=rock','Скала','legend'),
 '0x650b':('trailblazed=yes','Зимна маркировка','legend'),
 '0x660b':('trailblazed=yes','Туристическа маркировка','legend'),
 '0x1100':('place=locality','Населено място','uncertain'),
 '0xd00':('place=locality','Селище','uncertain'),
 '0xe00':('place=neighbourhood','Квартал','uncertain'),
 '0x640f':('landuse=cemetery','Гробище','legend'),
 '0x6403':('landuse=cemetery','Гробище','legend'),
 '0x640a':('hazard=yes','Опасност','legend'),
 '0x6502':('ford=yes','Речен брод','legend'),
 '0x2f0b':('leisure=track','Тренировъчна площадка','inferred'),
 '0x661d':('building=agricultural','Стопанска постройка','inferred'),
 '0x2c08':('leisure=horse_riding','Конна база','inferred'),
 '0x2f00':('landuse=industrial','Промишлен обект','inferred'),
 '0x661c':('bgm:landmark=mound','Могила','uncertain'),
 '0x3003':('amenity=townhall','Кметство','inferred'),
 '0x2f01':('amenity=fuel','Бензиностанция','inferred'),
 '0x3002':('emergency=first_aid','Първа помощ','inferred'),
 '0x2c02':('amenity=kindergarten','Детска градина','inferred'),
 '0x2c03':('amenity=community_centre','Читалище','inferred'),
 '0x2e02':('shop=convenience','Хранителен магазин','inferred'),
 '0x2e03':('shop=yes','Магазин','inferred'),
 '0x2e06':('shop=yes','Търговски обект','uncertain'),
 '0x2f06':('amenity=bank','Банка','inferred'),
 '0x6418':('power=generator;generator:source=wind','Вятърен генератор','inferred'),
 '0x6419':('power=generator;generator:source=solar','Фотоволтаична централа','inferred'),
 '0x6405':('building=hut','Ловна хижа','legend'),
 '0x2e0a':('shop=car','Автокъща','inferred'),
 '0x2f03':('shop=car_repair','Автосервиз','inferred'),
 '0x3007':('man_made=pumping_station','Помпена станция','inferred'),
 '0x6601':('bgm:landmark=natural_phenomenon','Природен феномен','legend'),
 '0x6503':('bgm:trail_junction=yes','Кръстопът','legend'),
 '0x2a07':('bgm:landmark=unknown','Спортен обект','uncertain'),
 '0x2d02':('bgm:landmark=unknown','Търговски обект','uncertain'),
 '0x2a0e':('shop=yes','Търговски обект','uncertain'),
 '0x2f08':('bgm:landmark=unknown','Обществен обект','uncertain'),
 '0x4700':('industrial=port','Пристанище','legend'),
 '0x640d':('aerialway=station','Лифт','legend'),
 '0x6402':('building=yes','Сграда','legend'),
 '0x6406':('barrier=gate','Бариера','legend'),
 '0x6401':('man_made=bridge','Мост','legend'),
 '0x6501':('amenity=restaurant','Ресторант','legend'),
 '0x640c':('man_made=mine_shaft','Мина','legend'),
 '0x6602':('tourism=viewpoint','Панорама - Гледка','legend'),
 '0x6511':('natural=spring','Вода - Извор','legend'),
 '0x6414':('amenity=drinking_water','Вода - Чешма','legend'),
 '0x6417':('man_made=water_well','Вода - Кладенец','legend'),
 '0x6616':('natural=peak','Връх','verified'),
 '0x6617':('natural=peak','Връх','legend'),
 '0x6618':('natural=tree','Дърво','legend'),
 '0x6603':('man_made=survey_point','Триангулачна точка','legend'),
}
# Primary categories with stock OsmAnd POI definitions/icons in normal map builds.
STOCK_PREFIXES={'amenity','tourism','historic','shop','emergency','leisure','natural','man_made'}
STOCK_NATURAL={'peak','spring','tree','cave_entrance','saddle'}
STOCK_MAN_MADE={'water_well','survey_point','tower'}
STOCK_LEISURE={'picnic_table','swimming_pool','park','ice_rink','horse_riding'}
STOCK_EMERGENCY={'mountain_rescue','first_aid'}

def tagdict(s):return dict(item.split('=',1) for item in s.split(';') if '=' in item)
def tags_text(d):return ';'.join(f'{k}={v}' for k,v in d.items())
def has_standard_icon(tags):
 if 'amenity' in tags or 'tourism' in tags or 'shop' in tags or 'historic' in tags:return True
 if tags.get('natural') in STOCK_NATURAL:return True
 if tags.get('man_made') in STOCK_MAN_MADE:return True
 if tags.get('leisure') in STOCK_LEISURE:return True
 if tags.get('emergency') in STOCK_EMERGENCY:return True
 return False

def main():
 a=argparse.ArgumentParser();a.add_argument('--original',required=True);a.add_argument('--catalog',required=True);a.add_argument('--output-dir',required=True);args=a.parse_args()
 dest=Path(args.output_dir);dest.mkdir(parents=True,exist_ok=True)
 with open(args.original,encoding='utf-8-sig',newline='') as f:
  old=list(csv.DictReader((line for line in f if not line.startswith('#'))))
 assert len(old)>=1800
 originals={r['code'].lower():r for r in old}
 assert len(originals)==len(old)
 with open(args.catalog,encoding='utf-8-sig',newline='') as f:catalog=list(csv.DictReader(f))
 assert len(catalog)>=260
 report=[];labels=[]
 for r in catalog:
  code=r['code'].lower();cur=originals.get(code)
  if not cur:raise SystemExit('Missing type in base CSV: '+code)
  cnt=int(r['count']);raw=tagdict(r.get('reviewed_tags') or r.get('default_tags') or '')
  override=INTERPRET.get(code)
  if override:
   parsed=tagdict(override[0]);label=override[1];confidence=override[2]
  else:
   parsed=raw
   label=r.get('verified_legend_bg') or ''
   confidence='legend' if r.get('review_status')=='icon_legend_verified' else ('source-inferred' if parsed else 'unknown')
  # Extended, unverified POIs use synthetic rendering. Do not claim a real OSM tag.
  if code.startswith('0x1') and int(code,0)>=0x10000:confidence='unverified-extended'
  synth=not has_standard_icon(parsed)
  out=dict(parsed)
  if synth:
   # OsmAnd compatibility shim: real tags are preserved, these extra tags only
   # give local OBF viewers a stock information icon. NEVER upload to OSM.
   out['tourism']='information'
   out['information']='board'
   out['bgm:synthetic_icon']='yes'
  if r.get('verified_legend_en'):
   # Keep original icon-derived legend classification independently of display shim.
   old_legend=tagdict(cur.get('tags','')).get('garmin:legend')
   if old_legend:out['garmin:legend']=old_legend
  # No code remains raw-only: low-frequency categories also get an indexable type.
  if not out:raise SystemExit('Missing display tags for '+code)
  newtags=tags_text(out)
  before=cur['tags'];cur['mode']='replace';cur['tags']=newtags
  cur['note']=(cur.get('note') or '')[:180]+' | V08 local icon mode; confidence='+confidence+(' | SYNTHETIC INFORMATION-BOARD ICON, NOT PUBLIC OSM DATA' if synth else ' | native-tag candidate')
  if not label:label='Garmin обект '+code
  if label.strip().lower() in ('каптаж','reservoir'):label='Вода - Каптаж'
  labels.append({'code':code,'label_bg':label,'confidence':confidence})
  report.append(dict(code=code,count=cnt,old_tags=before,new_tags=newtags,synthetic_fallback='yes' if synth else 'no',confidence=confidence,
    label_bg=label,example_lat=r.get('example1_lat',''),example_lon=r.get('example1_lon',''),example_name=r.get('example1_name',''),
    note='presentation compatibility: cannot guarantee draw at every zoom' if synth else 'stock-tag candidate: verify in OBF'))
 with open(dest/'poi-overrides-all-types.csv','w',encoding='utf-8',newline='') as f:
  f.write('# Local OsmAnd display compatibility rules, NOT valid for public OSM imports.\n')
  f.write('# All 263 retained types have a semantic tag or synthetic board icon; historic rejected types remain inert.\n')
  wr=csv.DictWriter(f,fieldnames=['code','name_contains','mode','tags','note']);wr.writeheader();wr.writerows(old)
 for name,rows in [('poi-labels-bg.csv',labels),('poi-icon-audit.csv',report)]:
  with open(dest/name,'w',encoding='utf-8',newline='') as f:
   wr=csv.DictWriter(f,fieldnames=list(rows[0]));wr.writeheader();wr.writerows(rows)
 print('catalog reviewed',len(report),'frequent',sum(int(x['count'])>=100 for x in report),'synthetic fallback',sum(x['synthetic_fallback']=='yes' for x in report))
 for code in ('0x6500','0x6602','0x650b','0x660b'):
  print(code,next(x for x in report if x['code']==code))

if __name__=='__main__':main()
