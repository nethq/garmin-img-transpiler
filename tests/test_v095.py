#!/usr/bin/env python3
"""Version 0.95 regression checks, including its REAL published CLI contract.

Only mock OsmAndMapCreator is used for the orchestration test, not for proving
OBF binary correctness. The real IMG tests were run separately during release.
"""
import csv,importlib.util,json,pathlib,subprocess,sys,tempfile,unittest,xml.etree.ElementTree as ET
ROOT=pathlib.Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('nt',ROOT/'scripts/native_translate.py')
t=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(t)
POI=ROOT/'config/poi-mapping.csv'
LINES=ROOT/'config/line-mapping.csv'
SCRIPT=ROOT/'scripts/build_bgmountains.py'
class V095(unittest.TestCase):
 def test_mapping_handoff_has_all_named_exceptions(self):
  bases,conditional=t.read_poi_configuration(POI)
  self.assertEqual(len(bases),1871)
  self.assertEqual(len(conditional),80)
  self.assertEqual(sum(int(row['source_count']) for row in bases.values()),410563)
  self.assertEqual(sum(int(row['source_count']) > 0 for row in bases.values()),263)
  self.assertEqual(bases['0x200']['osm_tags'],'place=city')
  self.assertTrue(any(r['code']=='0x200' and r['name_contains']=='София' and r['osm_tags']=='capital=yes' for r in conditional))
  self.assertEqual(len(t.line_mapping(LINES)),50)
  self.assertEqual(sum(x['code']=='0x6414' and x['name_contains']=='Сух' for x in conditional),1)
  with tempfile.TemporaryDirectory() as dd:
   d=pathlib.Path(dd)
   t.make_cpp_rules(bases,d/'cpp.csv',d/'labels.csv',conditional)
   rows=list(csv.DictReader((d/'cpp.csv').open()))
   self.assertEqual(len(rows),1951)
   self.assertEqual(next(x for x in rows if x['code']=='0x6414' and x['name_contains']=='')['tags'].split(';')[:2],['amenity=drinking_water','garmin:legend=drinking_water'])
   self.assertEqual(next(x for x in rows if x['code']=='0x6414' and x['name_contains']=='Сух')['tags'].split(';')[:2],['man_made=water_tap','drinking_water=no'])
 def test_argument_contract_with_users_flags(self):
  helptext=subprocess.run([sys.executable,SCRIPT,'--help'],capture_output=True,text=True,check=True).stdout
  for flag in ('--poi-config','--line-config','--download-creator','--jobs','--java-opts','--bbox','--prepare-only','--creator','--template'):
   self.assertIn(flag,helptext)
  self.assertNotIn('unrecognized arguments',helptext)
 def test_smoke_full_orchestration_with_mock_creator(self):
  with tempfile.TemporaryDirectory() as dd:
   d=pathlib.Path(dd)
   creator=d/'creator';creator.mkdir()
   (creator/'utilities.sh').write_text('#!/bin/sh\nset -eu\n[ "$1" = "generate-obf-no-address" ]\n[ -f "$2" ]\n[ "${JAVA_TOOL_OPTIONS:-}" = "-Xms512M -Xmx25G" ]\nprintf \"INFO: Finish writing binary file\\n\"\n\nprintf MOCK-OBF > "${2%.osm}.obf"\n')
   (creator/'utilities.sh').chmod(0o755)
   (creator/'inspector.sh').write_text('#!/bin/sh\n[ "$1" = "-vpoi" ]\nprintf MOCK-INSPECTION\n')
   (creator/'inspector.sh').chmod(0o755)
   raw=d/'minimal.osm';raw.write_text('''<osm version="0.6"><node id="1" lat="42" lon="24"><tag k="garmin:feature" v="poi"/><tag k="garmin:type" v="0x6414"/><tag k="amenity" v="drinking_water"/><tag k="man_made" v="water_tap"/><tag k="name" v="Чешма"/></node></osm>''',encoding='utf-8')
   # Orchestration fixture uses a legacy profile with no map-compiler overrides.
   legacy=d/'legacy.csv'
   legacy.write_text('code,label_bg,action,osm_tags\n0x6414,Чешма,replace,amenity=drinking_water\n')
   cmd=[sys.executable,str(SCRIPT),'--osm',str(raw),'--poi-config',str(legacy),'--line-config',str(LINES),'--out',str(d/'out'),'--jobs','8','--java-opts','-Xms512M -Xmx25G','--creator',str(creator)]
   run=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
   self.assertEqual(run.returncode,0,run.stderr+'\n'+run.stdout)
   self.assertTrue((d/'out'/'BGMountains-native.obf').is_file())
   self.assertEqual(json.loads((d/'out'/'build-manifest.json').read_text())['version'],'v0.97.5')
   self.assertEqual(json.loads((d/'out'/'semantic-audit.json').read_text())['status'],'PASS')
   tags=t.tagdict(ET.parse(d/'out'/'BGMountains-native.osm').getroot().find('node'))
   self.assertEqual(tags['amenity'],'drinking_water')
   self.assertEqual(tags['man_made'],'water_tap')
 def test_future_template_does_not_require_existing_configs(self):
  with tempfile.TemporaryDirectory() as dd:
   d=pathlib.Path(dd)
   # No fake success from a malformed configuration: expected exit nonzero.
   p=subprocess.run([sys.executable,SCRIPT,'--osm',str(d/'notthere.osm'),'--poi-config',str(d/'missing.csv'),'--line-config',str(LINES),'--prepare-only'],capture_output=True,text=True)
   self.assertNotEqual(p.returncode,0)
   self.assertIn('mapping file not found',p.stderr)
if __name__=='__main__':unittest.main()
