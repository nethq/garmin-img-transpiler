#!/usr/bin/env python3
"""Offline contract tests: upstream XML augmentation and frequent/rare type retention."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

S=Path(__file__).resolve().parents[1]/'scripts'/'build_bgmountains.py'
spec=importlib.util.spec_from_file_location('build_bgmountains',S)
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)

class XmlPipelineTests(unittest.TestCase):
 def test_full_default_preserved_and_all_codes_indexed(self):
  with tempfile.TemporaryDirectory() as td:
   td=Path(td)
   pb=td/'upstream-poi.xml';rb=td/'upstream-render.xml'
   pb.write_text('<poi_types><poi_category name="food"><poi_type name="restaurant" tag="amenity" value="restaurant"/></poi_category></poi_types>')
   rb.write_text('<osmand_types><category name="roads"><type tag="highway" value="path" minzoom="11"/></category><category name="user_defined"/></osmand_types>')
   cc=[dict(code='0x6414',count=32922,has_typ_icon='yes'),dict(code='0x123',count=1,has_typ_icon='no')]
   a,b,s=mod.make_configs(pb,rb,cc,td,100)
   p=ET.parse(a).getroot();r=ET.parse(b).getroot()
   self.assertIsNotNone(p.find(".//poi_type[@name='restaurant']"))
   self.assertEqual(len(p.findall(".//poi_category[@name='bgmountains_raw']/poi_type")),2)
   self.assertIsNotNone(r.find(".//type[@tag='highway']"))
   self.assertEqual(r.find(".//type[@value='type_0x6414']").get('minzoom'),'15')
   self.assertEqual(r.find(".//type[@value='type_0x123']").get('minzoom'),'17')
   self.assertEqual(s['types_at_or_above_threshold'],1)
 def test_real_catalog(self):
  d=mod.catalog_rows(S.parents[1]/'data'/'BGMountains-unique-poi-types.csv')
  self.assertEqual(len(d),263)
  self.assertEqual(sum(x['count'] for x in d),410563)
  self.assertEqual(sum(x['count']>=100 for x in d),87)

if __name__=='__main__':unittest.main()
