"""Build offline IEEE MA-L/MA-M/MA-S/IAB tables from four pinned CSV files.

Usage: generate_oui.py SOURCE_DIR OUTPUT --date YYYY-MM-DD --mirror COMMIT
No network access. Source hashes, row counts and retrieval locations are emitted
next to OUTPUT as .sources.json. Names are ASCII-transliterated for the TFT font.
"""
import argparse
import csv
import hashlib
import io
import json
import re
import unicodedata
from pathlib import Path

SOURCES = [('oui.csv', 'MA-L', 24, 'oui/oui.csv'),
           ('mam.csv', 'MA-M', 28, 'oui28/mam.csv'),
           ('oui36.csv', 'MA-S', 36, 'oui36/oui36.csv'),
           ('iab.csv', 'IAB', 36, 'iab/iab.csv')]


def generate(source, target, date, mirror):
    groups = {24: {}, 28: {}, 36: {}}
    provenance = {'retrieved': date, 'mirror_commit': mirror, 'sources': []}
    for filename, registry, bits, urlpath in SOURCES:
        raw = (source / filename).read_bytes()
        rows = list(csv.DictReader(io.StringIO(raw.decode('utf-8-sig'))))
        if not rows:
            raise ValueError(f'{filename}: empty registry')
        for row in rows:
            assignment = row['Assignment'].strip()
            if row['Registry'] != registry or not re.fullmatch('[0-9A-Fa-f]{%d}' % (bits // 4), assignment):
                raise ValueError(f'{filename}: malformed registry/assignment')
            name = unicodedata.normalize('NFKD', row['Organization Name']).encode('ascii', 'ignore').decode().strip()
            name = ' '.join(name.split()) or 'REGISTRANT NAME NOT ASCII'
            key = int(assignment, 16)
            # Retain historical assignments; runtime rejects group/local addresses.
            item = groups[bits].setdefault(key, {'names': set(), 'registries': set()})
            item['names'].add(name)
            item['registries'].add(registry)
        provenance['sources'].append({'file': filename, 'registry': registry, 'rows': len(rows),
            'sha256': hashlib.sha256(raw).hexdigest(), 'official': 'https://standards-oui.ieee.org/' + urlpath,
            'retrieved_from': f'https://raw.githubusercontent.com/runZeroInc/mac-tracker/{mirror}/data/ieee/{filename}' if mirror else 'https://standards-oui.ieee.org/' + urlpath})
    names = {}
    size = 0
    arrays = []
    for bits, group in groups.items():
        lines = []
        for key, item in sorted(group.items()):
            name = next(iter(item['names'])) if len(item['names']) == 1 else 'MULTIPLE IEEE REGISTRANTS'
            if name not in names:
                names[name] = size
                size += len(name) + 1
            offset = names[name]
            if bits == 36:
                # 8 bytes/entry: first 32 prefix bits, then name offset and last
                # nibble + IAB flag. No 64-bit alignment padding in flash.
                lines.append('{0x%08x,0x%x},\n' % (key >> 4, (offset << 5) | (16 if 'IAB' in item['registries'] else 0) | (key & 15)))
            else:
                lines.append('{0x%x,%d},\n' % (key, offset))
        arrays.append('static constexpr Entry entries%d[] = {\n%s};\n' % (bits, ''.join(lines)))
    if size >= 1 << 27:
        raise ValueError('name pool exceeds packed-offset capacity')
    text = '// Generated offline by tools/generate_oui.py; see oui_data.inc.sources.json.\n'
    text += '// IEEE registry snapshot retrieved ' + date + '; registrants are not product identifications.\n'
    text += 'static constexpr char datasetDate[] = ' + json.dumps(date) + ';\n'
    text += 'static const char names[] =\n' + ''.join(json.dumps(n + '\0') + '\n' for n in names).replace('\\u0000', '\\000') + ';\n'
    text += 'struct Entry {uint32_t prefix,offset;};\n' + ''.join(arrays)
    target.write_text(text)
    provenance['unique_prefixes'] = {str(k): len(v) for k, v in groups.items()}
    provenance['name_bytes'] = size
    target.with_suffix(target.suffix + '.sources.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print(provenance['unique_prefixes'], 'name bytes:', size)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('source', type=Path)
    p.add_argument('target', type=Path)
    p.add_argument('--date', required=True)
    p.add_argument('--mirror', default='')
    a = p.parse_args()
    if not re.fullmatch(r'\d{4}-\d{2}-\d{2}', a.date) or (a.mirror and not re.fullmatch('[0-9a-f]{40}', a.mirror)):
        p.error('invalid retrieval date or immutable mirror commit')
    generate(a.source, a.target, a.date, a.mirror)
