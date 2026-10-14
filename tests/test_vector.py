#!/usr/bin/env python3
"""Independent, streaming validation of the standalone C++ IMG->OSM pipeline."""
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
fixture = Path(__file__).with_name("demo.img")
with tempfile.TemporaryDirectory() as tmp:
    destination = Path(tmp) / "smoke.osm"
    result = subprocess.run([str(binary), '--input', str(fixture), '--output', str(destination)], capture_output=True, text=True)
    assert result.returncode == 0, (result.stdout, result.stderr)
    ids, ways, referenced = set(), 0, []
    for _event, e in ET.iterparse(destination, events=('end',)):
        if e.tag == 'node':
            assert e.attrib['id'] not in ids
            ids.add(e.attrib['id'])
        elif e.tag == 'way':
            ways += 1
            refs = [n.attrib['ref'] for n in e.findall('nd')]
            assert len(refs) >= 2
            referenced.extend(refs)
            assert any(t.attrib.get('k') == 'highway' for t in e.findall('tag'))
        if e.tag in ('node', 'way'):
            e.clear()
    assert ways == 1, ways
    assert all(n in ids for n in referenced)
    assert len(ids) >= 2
print('Standalone Garmin IMG -> OSM XML fixture test passed')
