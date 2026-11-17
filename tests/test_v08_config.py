import csv, unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
def load(p):
 with (R/p).open(encoding='utf8',newline='') as f:return list(csv.DictReader(x for x in f if not x.startswith('#')))
class TestLocalDisplay(unittest.TestCase):
 def test_active_catalog_mappings_have_display_tag_and_fallback_name(self):
  r={x['code']:x for x in load('tests/fixtures/historical/poi-overrides-all-types.csv')}
  names={x['code']:x['label_bg'] for x in load('tests/fixtures/historical/poi-labels-bg.csv')}
  cat=load('data/BGMountains-unique-poi-types.csv')
  self.assertEqual(len(cat),263)
  self.assertEqual(sum(int(x['count'])>=100 for x in cat),87)
  for x in cat:
   c=x['code'];self.assertEqual(r[c]['mode'],'replace',c)
   self.assertTrue(names[c],c)
   t=r[c]['tags']
   self.assertTrue(t,c)  # BGMountains custom POI index handles non-standard types
   self.assertNotIn('tags,,',t)
 def test_key_places(self):
  r={x['code']:x for x in load('tests/fixtures/historical/poi-overrides-all-types.csv')}
  self.assertIn('man_made=water_intake',r['0x6500']['tags'])
  self.assertNotIn('bgm:synthetic_icon=yes',r['0x6500']['tags'])
  self.assertNotIn('tourism=information',r['0x6500']['tags'])
  self.assertIn('tourism=viewpoint',r['0x6602']['tags'])
  self.assertNotIn('bgm:synthetic_icon',r['0x6602']['tags'])
 def test_all_legacy_codes_retained_for_review(self):
  self.assertEqual(len(load('tests/fixtures/historical/poi-overrides-all-types.csv')),1871)
if __name__=='__main__':unittest.main()
