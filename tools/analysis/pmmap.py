"""The Pirate's Moon function map against the port (docs/pm-map.md).

  pmmap.py            run from the repo root after pmdiff.py

Reads analysis-output/pm-diff/pairs.json and the port's `// Mech3.exe 1.2 @`
comments, and prints, by the port's modules: how many of the port's
functions PM has identical, changed only in its instructions (the compiler:
nothing to port), changed in calls, strings, constants or imports (to
check, perhaps port a PM version), or not at all (gone or inlined); then the
same for MW3 functions the port does not have yet, and PM's new functions by
source range. Categories as pmdiff.py names them; "moved" is a pair whose
source file (link-order range) differs.
"""
import json
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PAIRS = os.path.join(ROOT, 'analysis-output', 'pm-diff', 'pairs.json')
ADDR = re.compile(r'Mech3\.exe 1\.2 @ (0x[0-9a-fA-F]+)')


FLAGS = os.path.join(ROOT, 'analysis-output', 'pm-diff', 'compiler-flags.json')
flags = json.load(open(FLAGS)) if os.path.exists(FLAGS) else {}


def status(p):
    if p['kind'] == 'identical':
        return 'identical'
    if p['kind'] == 'changed':
        if p['feature_diff']:
            return 'features'
        f = flags.get(p['mw3'], {})
        return 'layout' if f.get('layout') or f.get('imm') else 'compiler'
    return p['kind']


def file_of(name):
    name = (name or '').replace('\\', '/').split('/')[-1].rstrip(')')
    return name.split(' and ')[0]


def main():
    pairs = json.load(open(PAIRS))
    by_mw3 = {int(p['mw3'], 16): p for p in pairs if 'mw3' in p}
    ported = defaultdict(set)  # address -> port files
    for top in ('src',):
        for dp, _, fs in os.walk(os.path.join(ROOT, top)):
            for f in fs:
                if not f.endswith(('.cpp', '.h')):
                    continue
                path = os.path.join(dp, f)
                rel = os.path.relpath(path, ROOT)
                for m in ADDR.finditer(open(path, encoding='utf-8', errors='replace').read()):
                    ported[int(m.group(1), 16)].add(rel)
    module = lambda rel: '/'.join(rel.split('/')[1:3]) if rel.startswith('src/gamez') else \
        rel.split('/')[1] + '/' + re.sub(r'(_[a-z0-9]+)?\.(cpp|h)$', '', rel.split('/')[2])
    rows = defaultdict(Counter)
    detail = defaultdict(list)
    unknown = 0
    for a, files in ported.items():
        p = by_mw3.get(a)
        mod = module(sorted(files)[0])
        if not p:
            rows[mod]['not in the diff'] += 1
            unknown += 1
            continue
        s = status(p)
        rows[mod][s] += 1
        if s in ('features', 'MW3 only'):
            detail[mod].append((a, s, p))
    order = ['identical', 'compiler', 'layout', 'features', 'MW3 only', 'not in the diff']
    tot = Counter()
    print('## The port\'s functions in PM (by the port\'s module)\n')
    label = {'compiler': 'compiler only', 'layout': 'compiler, other offsets or constants', 'features': 'changed', 'MW3 only': 'gone'}
    print('| module | ' + ' | '.join(label.get(k, k) for k in order) + ' |')
    print('|---|' + '---|' * len(order))
    for mod in sorted(rows, key=lambda m: -sum(rows[m].values())):
        tot.update(rows[mod])
        print(f'| {mod} | ' + ' | '.join(str(rows[mod][k]) for k in order) + ' |')
    print('| **all** | ' + ' | '.join(f'**{tot[k]}**' for k in order) + ' |')
    # the unported MW3 functions by MW3 source range
    un = defaultdict(Counter)
    for a, p in by_mw3.items():
        if a in ported:
            continue
        un[file_of(p.get('mw3_file'))][status(p)] += 1
    print('\n## MW3 functions not ported yet, by MW3 source file (range start)\n')
    print('| file | ' + ' | '.join(label.get(k, k) for k in order[:5]) + ' |')
    print('|---|' + '---|' * 5)
    for f in sorted(un, key=lambda f: -sum(un[f].values()))[:40]:
        print(f'| {f} | ' + ' | '.join(str(un[f][k]) for k in order[:5]) + ' |')
    new = Counter(file_of(p.get('pm_file')) for p in pairs if p['kind'] == 'new in PM')
    print('\n## New in PM, by PM source range (start)\n')
    for f, n in new.most_common(30):
        print(f'- {f}: {n}')
    moved = [p for p in pairs if p['kind'] in ('identical', 'changed') and
             file_of(p['mw3_file']) != file_of(p['pm_file'])]
    print(f'\nmoved (paired, source range differs by name): {len(moved)}', file=sys.stderr)
    with open(os.path.join(ROOT, 'analysis-output', 'pm-diff', 'port-impact.txt'), 'w') as out:
        for mod in sorted(detail):
            out.write(f'== {mod}\n')
            for a, s, p in sorted(detail[mod]):
                f = p.get('features', {})
                fs = ' '.join(f'{k} {v[0]}->{v[1]}' for k, v in f.items() if v[0] != v[1])
                out.write(f'  0x{a:08x} -> {p.get("pm", "-")}  {s}  {fs}\n')
    print(f'ported addresses {len(ported)}, not found in the diff {unknown}', file=sys.stderr)


if __name__ == '__main__':
    main()
