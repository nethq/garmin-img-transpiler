"""Identify MapCreator failures even when its launcher returns exit status zero."""
import collections
import datetime
import json
import pathlib
import re


def analyze(log, destination=None):
    levels = collections.Counter()
    samples = []
    relation_ids = set()
    max_counter = max_hash = 0
    overpass_warning = False
    stages = []
    stamp = None
    date_pattern = re.compile(r'^[A-Z][a-z]{2} \d{1,2}, \d{4} \d{1,2}:\d{2}:\d{2} [AP]M ')
    overflow = re.compile(r'relation (-?\d+)/(\d+)/(\d+) overflow')
    previous = None
    finished = False
    with pathlib.Path(log).open(encoding='utf-8', errors='replace') as stream:
        for line in stream:
            if date_pattern.match(line):
                prefix = line.split(' net.osmand.', 1)[0]
                try:
                    stamp = datetime.datetime.strptime(prefix, '%b %d, %Y %I:%M:%S %p')
                except ValueError:
                    pass
            exception = re.match(r'^(?:Exception in thread|(?:[\w$]+\.)*[\w$]*(?:Exception|Error)(?::|$))', line)
            if line.startswith(('SEVERE:', 'ERROR:', 'FATAL:')) or exception:
                levels[line.split(':', 1)[0]] += 1
                if len(samples) < 10:
                    samples.append(line.strip())
            matched = overflow.search(line)
            if matched:
                relation, counter, hash_value = map(int, matched.groups())
                relation_ids.add(relation)
                max_counter = max(counter, max_counter)
                max_hash = max(hash_value, max_hash)
            if 'OVERPASS_URL environment variable is not set' in line:
                overpass_warning = True
            if line.startswith('INFO: ') and ' started' in line and stamp:
                name = line[6:].split(' started', 1)[0]
                if previous:
                    stages.append({'stage': previous[0], 'seconds': (stamp - previous[1]).total_seconds()})
                previous = (name, stamp)
            if 'Finish writing binary file' in line:
                finished = True
                if previous and stamp:
                    stages.append({'stage': previous[0], 'seconds': (stamp - previous[1]).total_seconds()})
                    previous = None
    result = {
        'status': 'FAIL' if levels or not finished else 'PASS',
        'error_levels': dict(levels),
        'error_samples': samples,
        'finish_writing_binary_file': finished,
        'relation_id_overflow': {
            'affected_relation_count': len(relation_ids),
            'negative_relation_count': sum(i < 0 for i in relation_ids),
            'relation_id_min': min(relation_ids) if relation_ids else None,
            'relation_id_max': max(relation_ids) if relation_ids else None,
            'max_counter': max_counter,
            'max_hash': max_hash,
            'required_relation_range': '0 <= id < 134217728',
            'required_counter_range': '0 <= counter < 512',
            'required_hash_range': '0 <= hash < 64',
        },
        'overpass_url_unset': overpass_warning,
        'stage_intervals_seconds': stages,
    }
    if destination:
        pathlib.Path(destination).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return result
