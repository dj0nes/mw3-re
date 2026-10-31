"""MW3 1.2's addresses in Pirate's Moon 1.0 (docs/pm-map.md, step 0).

  pmaddr.py           run from the repo root after pmdiff.py; writes
                      analysis-output/pm-diff/addrmap.json

So the differential tests can run on PM's exe (original.py, MW3_EXE=pm):
- functions: pmdiff.py's pairs;
- instructions: inside each pair, its instructions aligned (difflib on the
  instructions with addresses named, as pmdiff compares them), so a hook in
  the middle of a function lands on its twin;
- imports: the import table's slots by DLL and name;
- globals: the absolute data operands of aligned instructions, paired in
  order; an address seen paired with more than one partner keeps the most
  common; then "blocks": runs of pairs with the same delta, so an address
  between two pairs of a block (a struct's field, an array's element) maps
  by the block's delta.
"""
import difflib
import json
import os
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(__file__))
import pmdiff  # noqa: E402

AO = pmdiff.AO
ROOT = pmdiff.ROOT


def data_operands(e, ops):
    out = []
    for v in pmdiff.HEX.findall(ops):
        v = int(v, 16)
        s = e.pe.sec(v)
        if s and not (s[5] & 0x20000000) and v not in e.pe.imports:
            out.append(v)
    return out


def code_runs(e, starts):
    """Runs of function pointers in the data sections (vtables, callback
    tables): start -> the entries."""
    runs = {}
    for name, va, size, raw, rsize, flags in e.pe.secs:
        if flags & 0x20000000 or name not in ('.rdata', '.data'):
            continue
        run = []
        for o in range(0, min(size, rsize) - 3, 4):
            v = struct.unpack_from('<I', e.pe.b, raw + o)[0]
            if v in starts:
                run.append(v)
                continue
            if len(run) >= 2:
                runs[va + o - 4 * len(run)] = run
            run = []
    return runs


def main():
    mw3 = pmdiff.Exe('MW3 1.2', os.path.join(ROOT, 'game/mw3-1.2/Mech3.exe'),
                     os.path.join(AO, 'pm-diff/mw3/mech3-bytes.asm'), os.path.join(AO, 'pm-diff/mw3/functions.jsonl'))
    pm = pmdiff.Exe('PM 1.0', os.path.join(ROOT, 'game/pm/Mech3.exe'), os.path.join(AO, 'pm.asm'),
                    os.path.join(AO, 'pm-decomp/functions.jsonl'))
    pairs = json.load(open(os.path.join(AO, 'pm-diff/pairs.json')))
    fmap = {int(p['mw3'], 16): int(p['pm'], 16) for p in pairs if 'mw3' in p and 'pm' in p}
    back = {b: a for a, b in fmap.items()}
    # imports by name
    by_name = {n: slot for slot, n in pm.pe.imports.items()}
    by_fn = {}  # msvcp50 -> msvcp60 and the like: the function's name alone
    for slot, n in pm.pe.imports.items():
        by_fn.setdefault(n.split('!')[1], slot)
    imports, missing = {}, []
    for a, n in mw3.pe.imports.items():
        b = by_name.get(n, by_fn.get(n.split('!')[1]))
        if b is None:
            missing.append(a)  # PM does not import it (no movies, no realloc): a scratch slot
        else:
            imports[a] = b
    ins, votes = {}, defaultdict(Counter)
    for a, b in fmap.items():
        da, db = mw3.f.get(a), pm.f.get(b)
        if not da or not db:
            continue
        na = mw3.norm(da, lambda v: f'F{fmap.get(v, v):x}')
        nb = pm.norm(db, lambda v: f'F{v:x}')
        sm = difflib.SequenceMatcher(None, na, nb, autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != 'equal':
                continue
            for i, j in zip(range(i1, i2), range(j1, j2)):
                xa, xb = da['body'][i], db['body'][j]
                ins[xa] = xb
                oa, ob = data_operands(mw3, mw3.ins[xa][2]), data_operands(pm, pm.ins[xb][2])
                if len(oa) == len(ob):
                    for x, y in zip(oa, ob):
                        votes[x][y] += 1
    # vtables and callback tables: a run of function pointers whose entries
    # map to a run of PM's, the most of its entries agreeing (at least half)
    ra, rb = code_runs(mw3, set(mw3.f)), code_runs(pm, set(pm.f))
    slots = defaultdict(list)  # a PM function -> the table slots holding it
    at_b = {}                  # a PM table slot -> its function
    for w, ents in rb.items():
        for k, f in enumerate(ents):
            slots[f].append(w + 4 * k)
            at_b[w + 4 * k] = f
    # each MW3 slot: the PM slot holding its function's twin whose table
    # agrees most around it
    vt = 0
    for v, ents in ra.items():
        for k, f in enumerate(ents):
            best, best_n = None, 1
            for slot in slots.get(fmap.get(f), ()):
                # the whole table's agreement: sibling classes share their
                # base's first slots and differ in the ones they override
                n = sum(1 for j in range(-k, len(ents) - k) if j and
                        at_b.get(slot + 4 * j) is not None and fmap.get(ents[k + j]) == at_b.get(slot + 4 * j))
                if n > best_n:
                    best, best_n = slot, n
            if best is not None:
                votes[v + 4 * k][best] += 100  # a table's slots outvote stray operands
                vt += 1
        # the slots left (a table's first, a lone entry): the delta most of
        # the run's matched slots share, from two up
        deltas = Counter()
        for k in range(len(ents)):
            c = votes.get(v + 4 * k)
            if c:
                deltas[c.most_common(1)[0][0] - (v + 4 * k)] += 1
        if deltas:
            d, n = deltas.most_common(1)[0]
            if n >= 2:
                for k in range(len(ents)):
                    if not votes.get(v + 4 * k):
                        votes[v + 4 * k][v + 4 * k + d] += 50
                        vt += 1
    data = {}
    for x, c in votes.items():
        y, n = c.most_common(1)[0]
        data[x] = y
    taken = Counter(data.values())
    data = {x: y for x, y in data.items() if taken[y] == 1}
    # blocks: sorted pairs with the same delta, consecutive
    blocks = []
    for x in sorted(data):
        d = data[x] - x
        if blocks and blocks[-1][2] == d and x - blocks[-1][1] <= 0x400:
            blocks[-1][1] = x
        else:
            blocks.append([x, x, d])
    # small code the pairs lack: import thunks (jmp [slot]) and short
    # unpaired functions, by their instructions with each address translated
    def tr_ops(ops):
        def one(m):
            v = int(m.group(1), 16)
            w = imports.get(v) or data.get(v) or fmap.get(v)
            return hex(w) if w is not None else m.group(0)
        return pmdiff.HEX.sub(one, ops)
    pm_seq = defaultdict(list)
    pm_starts = sorted(pm.f)
    for a in pm.addrs:
        raw, mn, ops = pm.ins[a]
        if mn == 'jmp' and 'dword ptr [0x' in ops:
            pm_seq[('thunk', ops)].append(a)
    for b, d in pm.f.items():
        if len(d['body']) <= 6:
            pm_seq[tuple((pm.ins[x][1], pm.ins[x][2]) for x in d['body'])].append(b)
    small = 0
    for a in mw3.addrs:
        raw, mn, ops = mw3.ins[a]
        if a in fmap or a in ins:
            continue
        if mn == 'jmp' and 'dword ptr [0x' in ops:
            hit = pm_seq.get(('thunk', tr_ops(ops)), [])
            if len(hit) == 1:
                ins[a] = hit[0]
                small += 1
    for a, d in mw3.f.items():
        if a in fmap or len(d['body']) > 6:
            continue
        key = tuple((mw3.ins[x][1], tr_ops(mw3.ins[x][2])) for x in d['body'])
        hit = pm_seq.get(key, [])
        if len(hit) == 1:
            fmap[a] = hit[0]
            small += 1
    print(f'small code by its instructions: {small}')
    out = {'functions': {hex(a): hex(b) for a, b in fmap.items()},
           'instructions': {hex(a): hex(b) for a, b in ins.items()},
           'imports': {hex(a): hex(b) for a, b in imports.items()},
           'imports_missing': [hex(a) for a in sorted(missing)],
           'data': {hex(a): hex(b) for a, b in data.items()},
           'blocks': [[hex(lo), hex(hi), d] for lo, hi, d in blocks]}
    json.dump(out, open(os.path.join(AO, 'pm-diff/addrmap.json'), 'w'))
    print(f'functions {len(fmap)}, instructions {len(ins)}, imports {len(imports)} of {len(mw3.pe.imports)}, '
          f'data addresses {len(data)} in {len(blocks)} blocks; table slots {vt} of {sum(len(e) for e in ra.values())}')


if __name__ == '__main__':
    main()
