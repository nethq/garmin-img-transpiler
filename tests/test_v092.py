#!/usr/bin/env python3
"""v0.92 offline tests: mapping discipline, native routes, names, future-map templates."""
import csv,importlib.util,json,pathlib,subprocess,sys,tempfile,unittest,xml.etree.ElementTree as ET
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('translator',ROOT/'scripts/native_translate.py')
t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)
POI=ROOT/'config/BGMountains-native-poi-mapping-v094.csv';LINES=ROOT/'config/line-mapping.csv'
class Native092(unittest.TestCase):
 def test_native_source_semantics(self):
  rows=t.mapping(POI)
  self.assertGreaterEqual(len(rows),263)
  for c,tags in [('0x6414','amenity=drinking_water'),('0x6511','natural=spring'),('0x6500','man_made=water_intake'),('0x660a','amenity=shelter'),('0x6609','tourism=alpine_hut'),('0x6401','man_made=bridge')]:
   self.assertIn(tags,rows[c]['osm_tags'])
  self.assertEqual(rows['0x6402']['label_bg'],'Заслон - Сграда')
  self.assertIn(rows['0x6600']['action'],('append','raw'))
  self.assertFalse(any('leisure=marina' in r['osm_tags'] for r in rows.values()))
  self.assertFalse(any('tourism=information;information=board' in r['osm_tags'] for r in rows.values()))
  self.assertTrue(all('garmin:type' not in r['osm_tags'] for r in rows.values()))
  self.assertTrue(all(int(rows[k]['zoom_hint'])<=12 for k in ('0x6414','0x6511','0x6500','0x660a')))
 def test_config_only_two_and_types(self):
  self.assertEqual(set(p.name for p in (ROOT/'config').glob('*.csv')),{'BGMountains-native-poi-mapping-v094.csv','line-mapping.csv'})
  self.assertTrue(any(x.get('route_mode')=='relation' for x in t.line_mapping(LINES).values()))
  for line in t.line_mapping(LINES).values():
   if line['route_mode']=='relation':self.assertTrue(line['osmc_symbol'])
 def test_native_relation_members_and_poi_provenance(self):
  with tempfile.TemporaryDirectory() as directory:
   d=pathlib.Path(directory);src=d/'sample.osm';dst=d/'native.osm';report=d/'report.json'
   line=next(r for r in t.line_mapping(LINES).values() if r['route_mode']=='relation')
   typ=line['code']
   xml=['<?xml version="1.0" encoding="UTF-8"?><osm version="0.6">']
   for i in range(1,5):xml.append(f'<node id="{i}" lat="41.{i}" lon="23.{i}"/>')
   xml+=['<node id="100" lat="41.7" lon="23.4"><tag k="garmin:type" v="0x6414"/><tag k="garmin:feature" v="poi"/><tag k="name" v="Вода - Чешма - HASH"/><tag k="amenity" v="drinking_water"/><tag k="bgm_poi" v="type_0x6414"/></node>']
   for i,refs in [(10,[1,2]),(11,[2,3])]:
    xml.append(f'<way id="{i}">'+''.join(f'<nd ref="{n}"/>' for n in refs)+f'<tag k="garmin:feature" v="extended_polyline"/><tag k="garmin:type" v="{typ}"/><tag k="bgm_route" v="type_{typ}"/></way>')
   xml.append('</osm>');src.write_text('\n'.join(xml))
   result=t.transform(src,dst,t.mapping(POI),t.line_mapping(LINES),report)
   self.assertEqual(result['route_candidate_ways'],2);self.assertEqual(result['route_relations'],1)
   root=ET.parse(dst).getroot();rel=root.find('relation');tags=t.tagdict(rel)
   self.assertEqual(tags['route'],'hiking');self.assertTrue(tags['osmc:symbol'].endswith('_bar'))
   self.assertEqual(sorted(m.attrib['ref'] for m in rel.findall('member')),['10','11'])
   node=next(n for n in root.findall('node') if n.get('id')=='100');p=t.tagdict(node)
   self.assertEqual(p['amenity'],'drinking_water');self.assertEqual(p['name'],'Вода - Чешма - HASH')
   self.assertEqual(p['garmin:type'],'0x6414');self.assertNotIn('bgm_poi',p)
   for way in root.findall('way'):self.assertNotIn('highway',t.tagdict(way))
 def test_route_attachment_to_real_way_only(self):
  with tempfile.TemporaryDirectory() as directory:
   d=pathlib.Path(directory);src=d/'src.osm';dst=d/'out.osm';rep=d/'report.json'
   typ=next(row['code'] for row in t.line_mapping(LINES).values() if row['route_mode']=='relation')
   parts=['<osm version="0.6">','<node id="1" lat="41" lon="23"/>','<node id="2" lat="41.1" lon="23.1"/>',
     '<node id="3" lat="41.2" lon="23.2"/>','<way id="22"><nd ref="1"/><nd ref="2"/><nd ref="3"/><tag k="highway" v="path"/></way>',
     '<way id="33"><nd ref="1"/><nd ref="2"/><nd ref="3"/><tag k="garmin:type" v="'+typ+'"/><tag k="garmin:feature" v="extended_polyline"/></way>',
     '<relation id="-1"><member type="way" ref="33" role=""/><tag k="route" v="hiking"/><tag k="bgm:source_feature" v="extended_polyline"/></relation></osm>']
   src.write_text('\n'.join(parts))
   subprocess.run([sys.executable,str(ROOT/'scripts/attach_native_routes.py'),'--osm',src,'--out',dst,'--lines',LINES,'--report',rep],check=True,capture_output=True)
   report=json.loads(rep.read_text());self.assertEqual(report['attached_to_existing_highway'],1)
   rel=ET.parse(dst).getroot().find('relation');self.assertEqual(rel.find('member').get('ref'),'22')
 def test_template_for_new_map_without_false_defaults(self):
  with tempfile.TemporaryDirectory() as directory:
   d=pathlib.Path(directory)
   p=d/'inventory.csv';p.write_text('code,count,example1_lat,example1_lon,example1_name\n0x999,2,42.123,24.321,Unknown place\n')
   xml=d/'one.osm';xml.write_text('<osm><node id="1" lat="42" lon="24"/><node id="2" lat="43" lon="25"/><way id="3"><nd ref="1"/><nd ref="2"/><tag k="garmin:feature" v="extended_polyline"/><tag k="garmin:type" v="0x11009"/></way></osm>')
   a=d/'newpoi.csv';b=d/'newlines.csv';stats=t.template(p,a,b,xml)
   self.assertEqual(stats,{'poi_types':1,'line_types':1})
   q=list(t.read_csv(a))[0];self.assertEqual(q['action'],'raw');self.assertEqual(q['osm_tags'],'');self.assertIn('42.123',q['note'])
   r=list(t.read_csv(b))[0];self.assertEqual(r['route_mode'],'preserve');self.assertEqual(r['osmc_symbol'],'')
if __name__=='__main__':unittest.main()
