#!/usr/bin/env python3
"""Streaming, disk-backed source IMG manifest vs OSM XML POI audit.

Requires Python 3 standard library only. Do not mistake internal roundtrip
consistency for correctness of Garmin TYP semantics or the resulting OBF.
"""
import argparse
import csv
from decimal import Decimal, ROUND_HALF_UP
import gzip
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from collections import Counter
from xml.etree import ElementTree as ET

SCALE = Decimal('10000000')

def scaled(s):
    return int((Decimal(str(s)) * SCALE).to_integral_value(rounding=ROUND_HALF_UP))

def opencsv(p):
    return gzip.open(p, "rt", newline="", encoding="utf-8-sig") if str(p).endswith(".gz") else open(p, "r", newline="", encoding="utf-8-sig")

def openxml(p):
    return gzip.open(p, 'rb') if str(p).endswith('.gz') else open(p, 'rb')

def parse_rules(path):
    if not path:
        return []
    with open(path, encoding='utf-8-sig', newline='') as f:
        rows = []
        for r in csv.DictReader(x for x in f if not x.startswith('#')):
            if not r.get('code'):
                continue
            tags = dict(t.split('=', 1) for t in r['tags'].split(';') if t)
            rows.append((int(r['code'], 0), r['name_contains'], r['mode'], tags))
        return rows

def matching_rules(rules, code, name):
    return [r for r in rules if r[0] == int(code, 0) and (not r[1] or r[1] in name)]

def source_audit(ns):
    # Aggregate duplicates in SQLite instead of holding 400k+ records in RAM.
    tmp = tempfile.TemporaryDirectory(prefix='garmin-poi-qa-')
    db = sqlite3.connect(str(Path(tmp.name) / 'audit.sqlite'))
    db.executescript('''PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;
       CREATE TABLE src(code TEXT, lat INTEGER, lon INTEGER, name TEXT, n INTEGER DEFAULT 0,
         seen INTEGER DEFAULT 0, PRIMARY KEY(code,lat,lon,name)) WITHOUT ROWID;
       CREATE TABLE extra(code TEXT,lat INTEGER,lon INTEGER,name TEXT,n INTEGER DEFAULT 0,
         PRIMARY KEY(code,lat,lon,name)) WITHOUT ROWID;''')
    rules = parse_rules(ns.rules)
    accepted_total, dropped_total, input_total, output_total = 0, 0, 0, 0
    with opencsv(ns.source) as f, db:
        for r in csv.DictReader(f):
            input_total += 1
            matching = matching_rules(rules, r['code'], r['name'])
            if any(x[2] == 'drop' for x in matching):
                dropped_total += 1
                continue
            accepted_total += 1
            db.execute('''INSERT INTO src(code,lat,lon,name,n) VALUES(?,?,?,?,1)
               ON CONFLICT(code,lat,lon,name) DO UPDATE SET n=n+1''',
               (r['code'].lower(), scaled(r['lat']), scaled(r['lon']), r['name']))
    # Updating primary-key rows in place avoids immense Python Counter memory.
    violations = Counter()
    tag_examples = []
    with openxml(ns.osm) as f, db:
        for _, el in ET.iterparse(f, events=('end',)):
            if el.tag != 'node':
                if el.tag in ('way', 'relation'):
                    el.clear()
                continue
            tags = {t.get('k', ''): t.get('v', '') for t in el.findall('tag')}
            if tags.get('garmin:feature') != 'poi':
                el.clear()
                continue
            output_total += 1
            name = tags.get('name', '')
            code = tags.get('garmin:type', '').lower()
            key = (code, scaled(el.attrib['lat']), scaled(el.attrib['lon']), name)
            row = db.execute('SELECT n,seen FROM src WHERE code=? AND lat=? AND lon=? AND name=?', key).fetchone()
            if row and row[1] < row[0]:
                db.execute('UPDATE src SET seen=seen+1 WHERE code=? AND lat=? AND lon=? AND name=?', key)
            else:
                db.execute('''INSERT INTO extra(code,lat,lon,name,n) VALUES(?,?,?,?,1)
                   ON CONFLICT(code,lat,lon,name) DO UPDATE SET n=n+1''', key)
            for _, contains, mode, mapped in matching_rules(rules, code, name):
                if mode == 'drop':
                    violations['forbidden dropped POI emitted'] += 1
                    continue
                for k, expected in mapped.items():
                    if tags.get(k) != expected:
                        violations[f'{code} {k}: expected {expected!r}, got {tags.get(k)!r}'] += 1
                        if len(tag_examples) < 50:
                            tag_examples.append({'code': code, 'name': name, 'lat': el.attrib['lat'],
                                                 'lon': el.attrib['lon'], 'tag': k, 'expected': expected,
                                                 'actual': tags.get(k)})
            el.clear()
    missing = db.execute('SELECT COALESCE(SUM(n-seen),0) FROM src').fetchone()[0]
    extra = db.execute('SELECT COALESCE(SUM(n),0) FROM extra').fetchone()[0]
    if ns.mismatches:
        with open(ns.mismatches, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['status', 'code', 'lat', 'lon', 'name', 'count', 'osm_url'])
            for status, query in [('missing','SELECT code,lat,lon,name,n-seen FROM src WHERE n>seen'),
                                  ('extra','SELECT code,lat,lon,name,n FROM extra')]:
                for code, lat, lon, name, n in db.execute(query):
                    la, lo = lat / 1e7, lon / 1e7
                    w.writerow([status, code, f'{la:.7f}', f'{lo:.7f}', name, n,
                                f'https://www.openstreetmap.org/?mlat={la:.7f}&mlon={lo:.7f}#map=18/{la:.7f}/{lo:.7f}'])
    summary = dict(source_pois=input_total, expected_after_drops=accepted_total,
                   declared_drops=dropped_total, osm_pois=output_total,
                   missing=missing, extra=extra, rule_tag_violations=dict(violations), examples=tag_examples,
                   status='PASS' if missing == extra == 0 and not violations else 'FAIL',
                   limitation='Same IMG decoder is used for manifest and OSM: this validates export fidelity, not ground truth or OBF persistence.')
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if ns.report:
        Path(ns.report).write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    db.close()
    tmp.cleanup()
    return 0 if summary['status'] == 'PASS' else 1

def audit_obf(ns):
    cmd = [ns.inspector, '-vpoi', '-vpoiobjects']
    if ns.bbox:
        w, s, e, n = map(float, ns.bbox.split(','))
        cmd.append(f'-bbox={w},{n},{e},{s}')
    cmd.append(ns.obf)
    with open(ns.report, 'w', encoding='utf-8') as f:
        run = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    print(json.dumps({'command': cmd, 'exit_code': run.returncode, 'dump_file': ns.report,
        'note': 'Inspector output permits manual indexed-POI audit; it does NOT independently prove equality of IMG/OSM/OBF entities.'}, indent=2))
    return run.returncode

def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('compare', help='Full source manifest vs .osm or .osm.gz POI comparison')
    c.add_argument('--source', required=True, help='--poi-manifest output from garmin2osm')
    c.add_argument('--osm', required=True)
    c.add_argument('--rules', help='same overrides CSV used during conversion')
    c.add_argument('--report', help='JSON output path')
    c.add_argument('--mismatches', help='CSV missing/extra records with coordinates and map links')
    c.set_defaults(fn=source_audit)
    o = sub.add_parser('obf-inspect', help='Invoke existing OsmAndMapCreator inspector.sh (requires installing it)')
    o.add_argument('--inspector', required=True, help='/path/to/OsmAndMapCreator/inspector.sh')
    o.add_argument('--obf', required=True)
    o.add_argument('--bbox', help='west,south,east,north')
    o.add_argument('--report', required=True)
    o.set_defaults(fn=audit_obf)
    ns = p.parse_args()
    return ns.fn(ns)

if __name__ == '__main__':
    sys.exit(main())
