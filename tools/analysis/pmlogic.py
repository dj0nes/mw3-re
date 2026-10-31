"""The pairs pmdiff.py calls "changed only by the compiler" whose
instructions differ most, compared by Ghidra's pseudocode instead
(docs/pm-map.md): names, addresses and literal spellings normalised, so what
is left is the logic. Prints them by similarity, and a random sample.

  pmlogic.py [--sample N] [--seed S] [--show ADDR]
"""
import difflib
import json
import os
import random
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
AO = os.path.join(ROOT, 'analysis-output')


def norm(text, fmap):
    lines = []
    body = text[text.find('{'):] if '{' in text else text
    for l in body.splitlines():
        l = l.strip()
        if not l or l.startswith('/*') or l.startswith('//'):
            continue
        # variable declarations carry only types and names
        if re.match(r'^[A-Za-z_][\w \*]*\s+\*?[a-z]+Var\d+;$', l) or re.match(r'^\w[\w \*]* (local|param|in|stack|unaff|extraout)_\w+;', l):
            continue
        l = re.sub(r'FUN_([0-9a-f]{8})', lambda m: 'F' + fmap.get(int(m.group(1), 16), 'x'), l)
        l = re.sub(r'(DAT|PTR|_DAT|LAB|s|u|ds|PTR_LOOP|switchD|caseD)_[0-9a-f_]{6,}\w*', 'G', l)
        l = re.sub(r'\b(local|param|in|stack|unaff|extraout|auStack|uStack|iStack|pcStack|puStack|fStack|bStack|cStack|sStack)_?\w*', 'v', l)
        l = re.sub(r'\b[a-z]{1,4}Var\d+\b', 'v', l)
        l = re.sub(r'\(\w[\w ]*\s?\*?\)', '', l)  # casts
        l = re.sub(r'0x[0-9a-f]+', lambda m: str(int(m.group(0), 16)), l)
        lines.append(l)
    return lines


def main():
    pairs = json.load(open(os.path.join(AO, 'pm-diff/pairs.json')))
    fmap = {int(p['pm'], 16): p['mw3'] for p in pairs if 'mw3' in p and 'pm' in p}
    fmap_m = {int(p['mw3'], 16): p['mw3'] for p in pairs if 'mw3' in p}
    big = [p for p in pairs if p['kind'] == 'changed' and not p['feature_diff'] and p['diff'] > 50]
    rows = []
    for p in big:
        fa = os.path.join(AO, 'decomp', f"{p['mw3']}.c")
        fb = os.path.join(AO, 'pm-decomp', f"{p['pm']}.c")
        if not (os.path.exists(fa) and os.path.exists(fb)):
            rows.append((None, p))
            continue
        a = norm(open(fa, errors='replace').read(), fmap_m)
        b = norm(open(fb, errors='replace').read(), fmap)
        r = difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()
        rows.append((r, p, a, b))
    args = sys.argv[1:]
    if '--show' in args:
        want = args[args.index('--show') + 1]
        for row in rows:
            if row[0] is not None and row[1]['mw3'] == want:
                for l in difflib.unified_diff(row[2], row[3], 'mw3', 'pm', lineterm='', n=1):
                    print(l)
        return
    have = [r for r in rows if r[0] is not None]
    print(f'{len(big)} pairs, {len(have)} with both pseudocodes')
    buckets = [(1.0, 1.01), (0.95, 1.0), (0.85, 0.95), (0.7, 0.85), (0, 0.7)]
    for lo, hi in buckets:
        print(f'  similarity {lo:.2f}-{min(hi, 1):.2f}: {sum(lo <= r[0] < hi for r in have)}')
    print('lowest 25:')
    for r in sorted(have, key=lambda r: r[0])[:25]:
        print(f"  {r[0]:.2f} {r[1]['mw3']} -> {r[1]['pm']} diff {r[1]['diff']} {r[1]['mw3_file'][-50:]}")
    n = int(args[args.index('--sample') + 1]) if '--sample' in args else 30
    seed = int(args[args.index('--seed') + 1]) if '--seed' in args else 1
    random.seed(seed)
    print(f'random {n}:')
    for r in random.sample(have, n):
        print(f"  {r[0]:.2f} {r[1]['mw3']} -> {r[1]['pm']} diff {r[1]['diff']}")


if __name__ == '__main__':
    main()
