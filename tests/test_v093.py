"""Regression coverage for large XML handling and no false-locality fallbacks."""
import csv,importlib.util,os,pathlib,sys,tempfile,unittest,xml.etree.ElementTree as ET
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('native_translate', ROOT/'scripts/native_translate.py')
t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)
class Reliability093(unittest.TestCase):
 def test_unknowns_receive_private_locality_and_stable_name(self):
  from tempfile import TemporaryDirectory
  with TemporaryDirectory() as d:
   out=pathlib.Path(d)
   src=out/'input.osm';dst=out/'output.osm';report=out/'report.json'
   src.write_text('''<osm version="0.6"><node id="5" lat="42.1345678" lon="24.2345678"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x9999"/><tag k="garmin:mapset" v="1234"/><tag k="bgm:generated_name" v="yes"/><tag k="name" v="Garmin обект 0x9999 - Q3ABCDEFG9"/></node><node id="6" lat="42.2" lon="24.2"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x6600"/><tag k="ele" v="176"/><tag k="name" v="176"/></node></osm>''')
   t.transform(src,dst,t.mapping(ROOT/'config/poi-mapping.csv'),t.mapping(ROOT/'config/line-mapping.csv'),report)
   nodes=ET.parse(dst).getroot().findall('node');a=t.tagdict(nodes[0]);b=t.tagdict(nodes[1])
   self.assertEqual(a['place'],'locality');self.assertEqual(a['name'],'Неуточнено - Q3ABCDEFG9')
   self.assertEqual(a['garmin:type'],'0x9999');self.assertEqual(a['bgm:classification'],'unverified')
   self.assertNotIn('place',b);self.assertEqual(b['ele'],'176')
   data=t.inspect_counts(dst,t.mapping(ROOT/'config/poi-mapping.csv'),t.mapping(ROOT/'config/line-mapping.csv'),out/'audit.json')
   self.assertEqual(data['synthetic_locality_fallback_total'],1)

 def test_known_bgm_types_are_not_invented_localities(self):
  cfg=t.mapping(ROOT/'config/poi-mapping.csv')
  self.assertIn('bgm:spot_height=yes',cfg['0x6600']['osm_tags']);self.assertEqual(cfg['0x6600']['action'],'append')
  self.assertNotIn('place=locality',cfg['0x6600']['osm_tags'])
  self.assertIn('information=trail_blaze',cfg['0x650b']['osm_tags'])
  self.assertIn('information=trail_blaze',cfg['0x660b']['osm_tags'])
 def test_fallback_does_not_overwrite_known_native_types(self):
  with tempfile.TemporaryDirectory() as d:
   src=pathlib.Path(d)/'before.osm';out=pathlib.Path(d)/'after.osm';rep=pathlib.Path(d)/'rep.json'
   src.write_text('''<osm><node id="1" lat="42" lon="24"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x6400"/><tag k="landuse" v="quarry"/><tag k="name" v="Кариера П"/></node><node id="2" lat="42" lon="24"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x6406"/><tag k="barrier" v="gate"/></node><node id="3" lat="42" lon="24"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x640d"/><tag k="aerialway" v="station"/></node></osm>''')
   t.transform(src,out,t.mapping(ROOT/'config/poi-mapping.csv'),t.mapping(ROOT/'config/line-mapping.csv'),rep)
   nodes=ET.parse(out).getroot().findall('node')
   self.assertTrue(all('place' not in t.tagdict(e) for e in nodes))
   self.assertEqual(t.tagdict(nodes[0])['name'],'Кариера П')
 def test_root_memory_does_not_accumulate(self):
  import tracemalloc
  with tempfile.TemporaryDirectory() as d:
   path=pathlib.Path(d)/'many.osm'
   with path.open('w') as f:
    f.write('<osm version="0.6">\n')
    for i in range(130000):f.write(f'<node id="{i+1}" lat="42.1" lon="24.2"><tag k="x" v="y"/></node>\n')
    f.write('</osm>')
   tracemalloc.start()
   c=0
   with open(path,'rb') as f:
    for e in t.top_level_elements(f):
     c+=1
     self.assertEqual(t.tagdict(e)['x'],'y')
   _,peak=tracemalloc.get_traced_memory();tracemalloc.stop()
   self.assertEqual(c,130000)
   self.assertLess(peak,15_000_000, f'Unbounded XML root growth {peak}')
 def test_audit_spot_height_and_markers(self):
  with tempfile.TemporaryDirectory() as d:
   path=pathlib.Path(d)/'sample.osm';report=pathlib.Path(d)/'report.json'
   path.write_text('''<osm version="0.6"><node id="1" lat="43" lon="22"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x6600"/><tag k="ele" v="132"/></node><node id="2" lat="43" lon="22"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x650b"/><tag k="tourism" v="information"/><tag k="information" v="route_marker"/></node><node id="3" lat="43" lon="22"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x6502"/><tag k="ford" v="yes"/></node></osm>''')
   stats=t.inspect_counts(path,t.mapping(ROOT/'config/poi-mapping.csv'),t.mapping(ROOT/'config/line-mapping.csv'),report)
   self.assertEqual(stats['unclassified_total'],0)
   self.assertEqual(stats['annotations_not_missing_pois']['spot_height'],1)
if __name__=='__main__':unittest.main()
