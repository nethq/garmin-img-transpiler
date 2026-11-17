"""Compare native stages against Python and catch MapCreator ID/log regressions."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import native_backend as backend
import native_translate as reference
from mapcreator_diagnostics import analyze

TOOL = ROOT / 'build' / 'native' / 'native_postprocess'
POIS, _ = reference.read_poi_configuration(ROOT / 'config' / 'poi-rev-v04.csv')
LINES = reference.line_mapping(ROOT / 'config' / 'line-mapping.csv')


def entities(path):
    return [(e.tag, dict(e.attrib), reference.tagdict(e),
             [(child.tag, dict(child.attrib)) for child in e if child.tag != 'tag'])
            for e in ET.parse(path).getroot()]


@unittest.skipUnless(TOOL.is_file(), 'Build native_postprocess before running native tests')
class NativeParity(unittest.TestCase):
    def test_transform_hash_tags_topology_and_audit_match_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            src = out / 'input.osm'
            root = ET.Element('osm', version='0.6')
            # Decimal boundaries, negative coordinates, exponent notation,
            # Unicode whitespace, genuine source names and escaped XML values.
            for i, (lat, name, code) in enumerate([
                ('42.00000005', '', '0x6414'), ('-0.00000005', '\u2003', '0x6400'),
                ('4.200000005e1', '', '0x6414'), ('-42.11111115', 'Извор & "Име"', '0x6414'),
                ('42', '', '0x6600'), ('42', '', '0x1ffff')]):
                node = ET.SubElement(root, 'node', id=str(i+1), lat=lat, lon='24.12345675')
                tags = {'garmin:feature': 'poi', 'garmin:type': code, 'name': name,
                        'garmin:mapset': 'тест', 'bgm:lookup': 'natural/spring'}
                if code == '0x6600':
                    tags['ele'] = '176'
                    tags['name'] = '176'
                if i == 3:
                    tags['natural'] = 'spring'
                reference.set_tags(node,tags)
            for wid, refs in [(103,[3,4]), (101,[1,2]), (102,[2,3]), (104,[8,9])]:
                way = ET.SubElement(root,'way',id=str(wid))
                for ref in refs:
                    ET.SubElement(way,'nd',ref=str(ref))
                reference.set_tags(way,{'garmin:feature':'extended_polyline','garmin:type':'0x10f0a'})
            # Relation and way IDs have independent namespaces. Existing relation
            # 1 must be preserved when generated relations receive positive IDs.
            ET.SubElement(root,'relation',id='1')
            ET.ElementTree(root).write(src,encoding='utf-8',xml_declaration=True)
            pois = {code: dict(row) for code,row in POIS.items()}
            pois['0x6414']['hash_skip'] = '2'
            lengths = reference.configured_lengths(pois)
            lengths['0x1ffff'] = 3  # Per-type hash override for an unmapped source type.
            python_out, native_out = out/'python.osm', out/'native.osm'
            expected = reference.transform(src,python_out,pois,LINES,out/'python-report.json',lengths=lengths)
            actual = backend.transform(TOOL,src,native_out,pois,LINES,out/'native-report.json',lengths)
            self.assertEqual(entities(python_out),entities(native_out))
            self.assertEqual(expected,actual)
            audit = reference.inspect_counts(native_out,pois,LINES,out/'python-audit.json')
            self.assertEqual(audit,json.loads((out/'semantic-audit.json').read_text()))

    def test_attachment_unmatched_members_roles_and_relation_id_remapping(self):
        from attach_native_routes import match
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            src = out/'input.osm'
            src.write_text('''<?xml version="1.0"?><osm version="0.6">
<node id="1" lat="42" lon="24"/>
<way id="10"><nd ref="1"/><nd ref="2"/><tag k="highway" v="path"/></way>
<way id="20"><nd ref="1"/><nd ref="2"/><tag k="garmin:type" v="0x10f0a"/><tag k="garmin:feature" v="extended_polyline"/></way>
<way id="21"><nd ref="8"/><nd ref="9"/><tag k="garmin:type" v="0x10f0a"/><tag k="garmin:feature" v="extended_polyline"/></way>
<relation id="-1"><member type="way" ref="20" role="forward"/><member type="way" ref="10" role="backward"/><member type="way" ref="21" role=""/><member type="node" ref="20" role="guidepost"/><member type="relation" ref="-2" role=""/><tag k="route" v="hiking"/><tag k="bgm:source_feature" v="extended_polyline"/></relation>
<relation id="-2"><member type="relation" ref="-1" role=""/></relation>
<relation id="1"/>
</osm>''')
            native = out/'native.osm'
            backend.postprocess(TOOL,src,native,POIS,LINES,out/'native-report.json',out/'semantic-audit.json',True)
            python = out/'python.osm'
            match(src,python,ROOT/'config'/'line-mapping.csv',out/'python-report.json')
            self.assertEqual(entities(python),entities(native))
            report = json.loads((out/'native-report.json').read_text())
            self.assertEqual(report['attached_to_existing_highway'],1)
            self.assertEqual(report['unmatched_overlay_members'],1)
            self.assertEqual(report['relation_ids_remapped'],2)
            self.assertTrue(all(0 < int(r.get('id')) < 1 << 27 for r in ET.parse(native).getroot().findall('relation')))
            # Shared node/way numeric IDs and distinct directional roles survive.
            members = ET.parse(native).getroot().find('relation').findall('member')
            self.assertEqual([m.get('ref') for m in members[:4]],['10','10','21','20'])

    def test_copy_and_id_repair_without_attachment_preserves_objects(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            src = out/'input.osm'
            src.write_text('<osm><node id="1" lat="42" lon="24"/><relation id="-1"/></osm>')
            backend.postprocess(TOOL,src,out/'output.osm',POIS,LINES,out/'routes.json',out/'audit.json')
            self.assertEqual(ET.parse(out/'output.osm').getroot().find('relation').get('id'),'1')
            self.assertEqual(src.read_text(),'<osm><node id="1" lat="42" lon="24"/><relation id="-1"/></osm>')

    def test_malformed_xml_preserves_previous_output_and_source(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            src, dst = out/'input.osm', out/'output.osm'
            src.write_text('<osm><node id="1"/>')
            dst.write_text('prior successful result')
            with self.assertRaises(subprocess.CalledProcessError):
                backend.postprocess(TOOL,src,dst,POIS,LINES,out/'routes.json',out/'audit.json')
            self.assertEqual(dst.read_text(),'prior successful result')
            self.assertEqual(src.read_text(),'<osm><node id="1"/>')
            self.assertEqual(list(out.glob('*.pending.osm')),[])


class MapCreatorDiagnostics(unittest.TestCase):
    def test_severe_error_with_completion_marker_fails_build(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)/'mapcreator.log'
            p.write_text('SEVERE: calcEntityIdFromRelationId() relation -1/0/10 overflow (27/9/6 bits)\nINFO: Finish writing binary file\n')
            result = analyze(p)
            self.assertEqual(result['status'],'FAIL')
            self.assertEqual(result['relation_id_overflow']['negative_relation_count'],1)
            p.write_text('WARNING: OVERPASS_URL environment variable is not set.\nINFO: Finish writing binary file\n')
            self.assertEqual(analyze(p)['status'],'PASS')

    def test_zero_exit_with_severe_log_marks_timed_stage_failed(self):
        from build_bgmountains import timed_run
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            with self.assertRaisesRegex(RuntimeError, 'MapCreator logged errors'):
                timed_run(out/'timings.jsonl', 'build-obf', sys.executable, '-c',
                          "print('SEVERE: invalid route ID'); print('INFO: Finish writing binary file')",
                          log=out/'mapcreator.log', check_mapcreator=True)
            timing = json.loads((out/'timings.jsonl').read_text())
            self.assertEqual(timing['status'], 'failed')
            self.assertEqual(json.loads((out/'mapcreator-diagnostics.json').read_text())['status'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
