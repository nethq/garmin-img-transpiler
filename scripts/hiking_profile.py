"""Prepare the editable IMG inventory and compile its stock OsmAnd index settings."""
import csv
import pathlib
import xml.etree.ElementTree as ET
import zipfile

from native_translate import code, read_csv


def review_inventory(catalog, profile, destination):
    records = list(read_csv(profile))
    columns = list(records[0])
    inventory = {code(r['code']): r for r in read_csv(catalog)}
    known = {code(r['code']) for r in records}
    for c in sorted(inventory.keys() - known):
        records.append(dict(code=c, action='raw', label_bg=f'Garmin обект {c}',
                            priority='normal', hash_length='6', note='New IMG type; review source examples.'))
    records.sort(key=lambda r: (code(r['code']) not in inventory, int(r['code'], 0)))
    with destination.open('x', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=columns, extrasaction='ignore', lineterminator='\n')
        writer.writeheader()
        for row in records:
            observed = inventory.get(code(row['code']), {})
            row['source_count'] = observed.get('count', '0')
            if not row.get('name_contains'):
                for field in ('lat', 'lon', 'name'):
                    row['example_' + field] = observed.get('example1_' + field, '')
            writer.writerow(row)


def index_settings(rows):
    settings = {}
    for row in rows:
        key = row.get('index_type', '').strip()
        value = row.get('index_minzoom', '').strip()
        if not key and not value:
            continue
        if key.count('=') != 1 or not value.isdigit() or not 1 <= int(value) <= 22:
            raise ValueError(f"Invalid index_type/index_minzoom for {row['code']}: {key} {value}")
        if key in settings and settings[key] != int(value):
            raise ValueError(f'Conflicting map zooms for {key}')
        settings[key] = int(value)
    return settings


def compile_indexes(creator, profile, out):
    rows = list(read_csv(profile))
    settings = index_settings(rows)
    lookups = {row['search_fallback'] for row in rows if row.get('search_fallback')}
    if not settings and not lookups:
        return []
    resources = {}
    for jar in sorted((creator / 'lib').glob('*.jar')):
        with zipfile.ZipFile(jar) as z:
            for name in ('rendering_types', 'poi_types'):
                path = f'net/osmand/osm/{name}.xml'
                if path in z.namelist():
                    resources[name] = ET.fromstring(z.read(path))
        if len(resources) == 2:
            break
    if len(resources) != 2:
        raise ValueError('MapCreator installation lacks native map and POI XML resources.')
    rendering = resources['rendering_types']
    for key, zoom in settings.items():
        tag, value = key.split('=')
        matches = [r for r in rendering.iter('type') if r.get('tag') == tag and r.get('value') == value
                   and r.get('additional') not in ('true', 'text')]
        if len(matches) != 1:
            raise ValueError(f'Expected one native rendering rule for {key}, found {len(matches)}')
        matches[0].set('minzoom', str(zoom))
    if 'man_made/gate' in lookups:
        # The packaged POI registry also marks gate as an additional category.
        # Keep barrier=gate for map/routing and use one searchable POI entry.
        ET.SubElement(rendering, 'entity_convert', pattern='tag_transform',
                      from_tag='barrier', from_value='gate', if_tag1='bgm:lookup',
                      if_value1='man_made/gate', map='no', routing='no', poi='yes')
    poi = resources['poi_types']
    for lookup in sorted(lookups):
        category, subtype = lookup.split('/', 1)
        parent = poi.find(f".//poi_category[@name='{category}']")
        if parent is None:
            raise ValueError(f'Unknown POI search category: {category}')
        ET.SubElement(parent, 'poi_type', name='bgm_' + subtype, tag='bgm:lookup', value=lookup)
    args = []
    for name, element in resources.items():
        target = out / (name + '.xml')
        ET.ElementTree(element).write(target, encoding='utf-8', xml_declaration=True)
        args.append('--' + name.replace('_','-') + '=' + str(target))
    return args
