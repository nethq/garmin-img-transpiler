#!/usr/bin/env python3
from pathlib import Path
import csv,json,unittest
ROOT=Path(__file__).resolve().parents[1]
def load(file,comments=False):
    with file.open(encoding='utf-8-sig',newline='') as f:
        if comments: content=(line for line in f if not line.startswith('#'))
        else:content=f
        return list(csv.DictReader(content))
class TestBGMLegend(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules={r['code']:r for r in load(ROOT/'tests/fixtures/historical/poi-overrides-legend-reviewed.csv',True)}
        cls.strict={r['code']:r for r in load(ROOT/'data/BGMountains-unique-poi-types-STRICT-reviewed.csv')}
        cls.legacy={r['code']:r for r in load(ROOT/'data/BGMountains-unique-poi-types-reviewed.csv')}
        cls.corrections=load(ROOT/'data/legend-corrections-log.csv')
        cls.names=load(ROOT/'data/extended-label-decoder-changes.csv')
    def test_catalog_counts(self):
        self.assertEqual(len(self.rules),1871)
        self.assertEqual(len(self.legacy),1871)
        self.assertEqual(len(self.strict),263)
        self.assertEqual(sum(int(x['count']) for x in self.strict.values()),410563)
        self.assertEqual(sum(int(x['outside_subdivision_count']) for x in self.legacy.values()),5602)
        self.assertEqual(int(self.strict['0x6608']['count']),2644)
        self.assertNotIn('0x10d1f',self.strict)
    def test_legend_exact_icons(self):
        self.assertEqual(len(self.corrections),60)
        self.assertEqual(len(json.loads((ROOT/"data/legend-mappings.json").read_text(encoding="utf-8"))),67)
        self.assertEqual(len(self.names),117)
        for c in self.corrections:
            rule=self.rules[c['code']]
            self.assertTrue(rule['tags'].startswith('garmin:legend='))
            self.assertEqual(rule['tags'],c['new_override_tags'])
            self.assertEqual(self.strict[c['code']]['review_status'],'icon_legend_verified')
        self.assertIn('tourism=hotel',self.rules['0x6608']['tags'])
        self.assertIn('natural=spring',self.rules['0x6511']['tags'])
        self.assertNotIn('information=guidepost',self.rules['0x6503']['tags'])
        self.assertIn('tourism=camp_site',self.rules['0x6415']['tags'])
    def test_false_nesebar_handled_correctly(self):
        one=self.legacy['0x10d1f']
        self.assertEqual(one['example1_name'],'есебър')
        self.assertEqual(one['strict_count'],'0')
        self.assertTrue(any(r['code']=='0x10d1f' and r['new_name']=='' for r in self.names))
if __name__=='__main__':unittest.main()
