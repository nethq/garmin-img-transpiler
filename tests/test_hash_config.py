"""Run the same unnamed Garmin point through extraction and OSM translation."""
import csv
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import native_translate as nt

BINARY = sys.argv.pop(1) if __name__ == '__main__' and len(sys.argv) > 1 else None


class HashRules(unittest.TestCase):
    def test_springs_use_six_distinct_characters(self):
        bases = nt.mapping(ROOT / 'config/poi-mapping.csv')
        widths = nt.configured_lengths(bases)
        skip = int(bases['0x6511']['hash_skip'])
        results = []
        for tile, lat, lon in [('02235039','42.9101729','25.0286794'),
                               ('02235041','42.8837156','26.4891744')]:
            node = ET.Element('node', lat=lat, lon=lon)
            results.append(nt.poi_suffix(node, {'garmin:type':'0x6511','garmin:mapset':tile}, widths, skip))
        self.assertEqual(results, ['7QUWS4', 'MQUWS4'])

    def test_invalid_settings(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = pathlib.Path(tmp) / 'hash.csv'
            for body in ('0x1,9', '*,0', '*,14', '*,x', '*,9\n*,6',
                         '*,9\n0x01,6\n0x1,7', '*,11\n0x20000,5'):
                with self.subTest(body=body):
                    config.write_text('code,length\n' + body + '\n')
                    with self.assertRaises(ValueError):
                        nt.read_hash_lengths(config)

    @unittest.skipUnless(BINARY, 'requires compiled garmin2osm')
    def test_extraction_and_reprocessing(self):
        bases, conditions = nt.read_poi_configuration(ROOT / 'config/poi-mapping.csv')
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp)
            # Replace the demo line section with two unnamed classic point records.
            img = bytearray((ROOT / 'tests/demo.img').read_bytes())
            img[8 * 512 + 0xc4 + 3] = 0x10
            record = bytes.fromhex('64 00 00 80 00 00 00 00')
            points = record + b'\x02' + record + b'\x17'
            struct.pack_into('<I', img, 9 * 512 + 0x19, len(points))
            offset = 9 * 512 + 0x7d
            img[offset:offset + len(points)] = points
            (out / 'map.img').write_bytes(img)
            lengths = {'*': 13, '0x6402': 4}
            # A generated label containing Заслон must never trigger the original-name rule.
            bases['0x6402']['label_bg'] = 'Заслон - Сграда'
            nt.make_cpp_rules(bases, out/'rules.csv', out/'labels.csv', conditions, lengths)
            subprocess.run([BINARY, '--input', out/'map.img', '--output', out/'raw.osm',
                            '--poi-labels', out/'labels.csv', '--poi-overrides', out/'rules.csv',
                            '--poi-manifest', out/'manifest.csv', '--poi-types-out', out/'types.csv'], check=True, capture_output=True)
            raw = ET.parse(out/'raw.osm').getroot()
            names = [nt.tagdict(e)['name'] for e in raw.findall('node')]
            self.assertEqual([len(n.rsplit(' - ', 1)[1]) for n in names], [4, 13])
            with (out/'manifest.csv').open() as f:
                self.assertEqual([r['name'] for r in csv.DictReader(f)], names)
            with (out/'types.csv').open() as f:
                self.assertEqual([row['named'] for row in csv.DictReader(f)], ['0', '0'])
            self.assertNotIn('amenity', nt.tagdict(raw[0]))
            nt.transform(out/'raw.osm', out/'map.osm', bases, {}, out/'report.json', lengths=lengths)
            self.assertEqual([nt.tagdict(e)['name'] for e in ET.parse(out/'map.osm').getroot()], names)

    def test_raw_osm_named_known_and_unknown(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp)
            raw = ET.Element('osm')
            for i, (name, semantic) in enumerate([('Garmin обект genuine', False),
                                                   ('', False), ('', True)]):
                node = ET.SubElement(raw, 'node', id=str(i), lat='42', lon='24')
                tags = {'garmin:type':'0x123','garmin:feature':'poi','name':name}
                if semantic:
                    tags['building'] = 'yes'
                nt.set_tags(node, tags)
            ET.ElementTree(raw).write(out/'raw.osm', encoding='utf-8')
            nt.transform(out/'raw.osm', out/'map.osm', {'0x123':{'label_bg':'Сграда'}},
                         {}, out/'report.json', lengths={'*': 11, '0x123': 7})
            real, unknown, known = map(nt.tagdict, ET.parse(out/'map.osm').getroot())
            self.assertEqual(real['name'], 'Garmin обект genuine')
            self.assertNotIn('bgm:generated_name', real)
            self.assertRegex(unknown['name'], r'^Неуточнено - [A-Z0-9]{7}$')
            self.assertRegex(known['name'], r'^Сграда - [A-Z0-9]{7}$')


if __name__ == '__main__':
    unittest.main()
