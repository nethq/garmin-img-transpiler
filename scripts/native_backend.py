"""Compile the existing CSV configuration into native stage inputs."""
from pathlib import Path
import json
import subprocess
import tempfile
import sys
import xml.etree.ElementTree as ET


def run_native(command, log=None):
    if log is None:
        subprocess.run(command, check=True)
        return
    with Path(log).open('w', encoding='utf-8') as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(''.join(Path(log).read_text(errors='replace').splitlines(True)[-40:]), file=sys.stderr)
        result.check_returncode()


def transform(tool, source, destination, pois, lines, report, lengths):
    from native_translate import tags_from_string
    destination = Path(destination)
    profile = destination.with_suffix('.profile.xml')
    root = ET.Element('profile')
    for code, row in pois.items():
        ET.SubElement(root, 'poi', code=code, action=(row.get('action') or 'replace').strip().lower(),
                      label=row.get('label_bg') or '',
                      legend=row.get('legend_bg') or row.get('label_bg') or '',
                      priority=row.get('priority', 'normal'),
                      width=str(lengths.get(code, lengths['*'])),
                      skip=str(int(row.get('hash_skip') or 0)))
    for code, width in lengths.items():
        if code != '*':
            ET.SubElement(root,'hash',code=code,width=str(width))
    for code, row in lines.items():
        rule = ET.SubElement(root, 'line', code=code, mode=row.get('route_mode', '').lower(),
                             legend=row.get('legend_bg', ''),
                             confidence=row.get('route_confidence', 'unknown'),
                             symbol=row.get('osmc_symbol', ''), network=row.get('network', ''),
                             color=row.get('legacy_color', ''))
        for key, value in tags_from_string(row.get('osm_way_tags', '')).items():
            ET.SubElement(rule, 'tag', k=key, v=value)
    ET.ElementTree(root).write(profile, encoding='utf-8', xml_declaration=True)
    # Native output is promoted only after both XML parsing and audit finish.
    with tempfile.NamedTemporaryFile(prefix=destination.stem+'-', suffix='.pending.osm',
                                     dir=destination.parent, delete=False) as temporary:
        pending = Path(temporary.name)
    try:
        subprocess.run([str(tool), '--osm', str(source), '--out', str(pending),
                        '--transform-profile', str(profile), '--default-width', str(lengths['*']),
                        '--report', str(report), '--audit', str(destination.parent / 'semantic-audit.json')], check=True)
        pending.replace(destination)
    finally:
        pending.unlink(missing_ok=True)
    return json.loads(Path(report).read_text(encoding='utf-8'))


def postprocess(tool, source, destination, pois, lines, report, audit, attach=False, log=None):
    destination = Path(destination)
    codes = destination.parent / 'native-poi-codes.txt'
    routes = destination.parent / 'native-route-types.txt'
    codes.write_text('\n'.join(pois) + '\n', encoding='utf-8')
    routes.write_text('\n'.join(code for code, row in lines.items()
                                if row.get('route_mode') == 'relation') + '\n', encoding='utf-8')
    with tempfile.NamedTemporaryFile(prefix=destination.stem+'-', suffix='.pending.osm',
                                     dir=destination.parent, delete=False) as temporary:
        pending = Path(temporary.name)
    cmd = [str(tool), '--osm', str(source), '--out', str(pending),
           '--poi-codes', str(codes), '--audit', str(audit), '--report', str(report)]
    if attach:
        cmd += ['--attach', '--route-types', str(routes)]
    try:
        run_native(cmd,log)
        pending.replace(destination)
    finally:
        pending.unlink(missing_ok=True)


def audit(tool, source, pois, destination, strict=False):
    destination = Path(destination)
    codes = destination.with_suffix('.poi-codes.txt')
    codes.write_text('\n'.join(pois) + '\n', encoding='utf-8')
    cmd = [str(tool), '--osm', str(source), '--audit-only',
           '--poi-codes', str(codes), '--audit', str(destination)]
    if strict:
        cmd.append('--strict')
    run_native(cmd)
    return json.loads(destination.read_text(encoding='utf-8'))
