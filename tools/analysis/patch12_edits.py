"""Writes the 1.2 data layer's tables (src/mech3/patch12_edits.inc,
patch12_copies.inc and patch12_added.inc) from the 1.0 and 1.2 installs' reader archives
(docs/patch12.md).

  patch12_edits.py [game dir]

Each edit is the smallest change on the parsed tree: a value replaced, a key
and its value removed or inserted, found by keys (KEY#n: the n-th KEY in that
list). Edits are listed in the order they apply; each checks the old value.
Entries the patch adds whole are not written (they are not ours to copy).
"""
import copy
import difflib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import zrd  # noqa: E402

GAME = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / 'game'

# (archive, entry): the changes the layer redoes, by sitting (docs/patch12.md).
ENTRIES = [
    ('zbd/reader.zbd', 'weapons.zrd'),
] + [('zbd/reader.zbd', f'dfn_{m}.zrd') for m in (
    'annihilator', 'avatar', 'blackhawk', 'bushwacker', 'cauldron', 'champion', 'daishi', 'madcat', 'orion',
    'owens', 'puma', 'shadowcat', 'strider', 'sunder', 'supernova', 'thor', 'vulture')] + [
    ('zbd/reader.zbd', 'briefing.zrd'), ('zbd/reader.zbd', 'dialog.zrd'), ('zbd/reader.zbd', 'sounds.zrd'),
    ('zbd/reader.zbd', 'mechsoilfx.zrd'),
    ('zbd/c1/reader.zbd', 'objectives.zrd'),
    ('zbd/c2/reader.zbd', 'start_multi_c2.zrd'), ('zbd/c2/reader.zbd', 'world.zrd'),
    ('zbd/c2/readerm3.zbd', 'aiv.zrd'), ('zbd/c2/readerm3.zbd', 'targets.zrd'),
    ('zbd/c3/readerm2.zbd', 'targets.zrd'),
    ('zbd/c3/readerm3.zbd', 'reg_inside_c3m3.zrd'), ('zbd/c3/readerm3.zbd', 'reg_outside_c3m3.zrd'),
    ('zbd/c3/readerm4.zbd', 'targets.zrd'), ('zbd/c3/readerm5.zbd', 'targets.zrd'),
    ('zbd/c3/readerm6.zbd', 'targets.zrd'),
    ('zbd/c4b/readerm4.zbd', 'aiv.zrd'), ('zbd/c4b/readerm4.zbd', 'targets.zrd'),
    ('zbd/c4b/readerm4.zbd', 'net_09.zrd'),
    ('zbd/c4b/readerm5.zbd', 'targets.zrd'),
]

# Entries 1.2 adds to an archive that 1.0 has elsewhere: (archive, entry,
# the 1.0 archive it comes from). Edits make the copy 1.2's.
COPIES = [('zbd/c4b/readerm5.zbd', e + '.zrd', 'zbd/c4b/readerm6.zbd') for e in (
    'net_07', 'net_08', 'net_09', 'net_10', 'net_11', 'net_12', 'net_13', 'net_14', 'net_15', 'net_16', 'net_17',
    'net_18', 'net_19', 'reg_inside_c4bm6', 'reg_lava', 'reg_outside_c4bm6', 'reg_p2')] + [
    ('zbd/c4b/readerm5.zbd', e + '.zrd', 'zbd/c4b/readerm4.zbd') for e in (
        'net_20', 'reg_base_interior', 'reg_inside_c4bm5', 'reg_outside_c4bm5', 'reg_warehouse')]

# Entries 1.2 adds that 1.0 has nowhere: written whole (Owen, 2026-10-31:
# small amounts are fine to copy). c3's multiplayer maps stay out until
# multiplayer is ported.
ADDED = [('zbd/c1/reader.zbd', 'envlights.zrd'), ('zbd/c1/reader.zbd', 'intro.zrd'),
         ('zbd/c2/reader.zbd', 'mobilerepair.zrd'), ('zbd/c2/readerm3.zbd', 'net_14.zrd')] + [
    ('zbd/c4b/readerm4.zbd', f'net_{n:02d}.zrd') for n in (2, 5, 6, 7, 8)]


def f32(v):
    """The shortest fixed-point text that reads back as the same float."""
    for n in range(1, 12):
        s = f'{v:.{n}f}'
        if struct.unpack('<f', struct.pack('<f', float(s)))[0] == v:
            break
    s = s.rstrip('0')
    return s + '0' if s.endswith('.') else s


def txt(v):
    if isinstance(v, list):
        return '(' + ' '.join(txt(x) for x in v) + ')'
    if isinstance(v, float):
        return f32(v)
    if isinstance(v, str):
        assert '"' not in v and '\\' not in v, v
        return '"' + v + '"'
    return str(v)


def is_kv(v):
    """A record: keys and values, some value a list (a pair of names is not)."""
    return isinstance(v, list) and len(v) % 2 == 0 and len(v) > 0 and all(isinstance(k, str) for k in v[::2]) \
        and any(isinstance(x, list) for x in v[1::2])


def keyname(lst, i):
    """How the value after lst[i] is found: by the string (KEY#n, the n-th
    item equal to it, as patch12_edits.cpp counts)."""
    k = lst[i]
    n = sum(1 for x in lst[:i] if x == k) + 1
    return k if n == 1 else f'{k}#{n}'


def itemname(lst, i):
    """Item i of a list: by the string before it when there is one."""
    if i > 0 and isinstance(lst[i - 1], str) and not isinstance(lst[i], str):
        return keyname(lst, i - 1)
    return f'@{i}'


def start(root):
    while isinstance(root, list) and len(root) == 1 and isinstance(root[0], list):
        root = root[0]
    return root


def diff(a, b, path, out):
    """Edits that make list `a` (changed in place) into `b`. Key/value lists
    are matched by keys; other lists item by item (@i, from 0)."""
    if is_kv(a) and is_kv(b):
        ka, kb = a[::2], b[::2]
        ops = difflib.SequenceMatcher(None, ka, kb, autojunk=False).get_opcodes()
        for tag, i1, i2, j1, j2 in reversed(ops):  # from the end: earlier positions hold
            if tag == 'equal':
                for i, j in zip(range(i1, i2), range(j1, j2)):
                    if a[2 * i + 1] != b[2 * j + 1]:
                        step(a, 2 * i + 1, b[2 * j + 1], path + [keyname(a, 2 * i)], out)
                continue
            if tag in ('replace', 'delete'):
                for i in reversed(range(i1, i2)):
                    out.append(('Erase', '/'.join(path + [keyname(a, 2 * i)]), txt(a[2 * i + 1]), ''))
                    del a[2 * i:2 * i + 2]
            if tag in ('replace', 'insert'):
                after = keyname(a, 2 * (i1 - 1)) if i1 > 0 else ''
                for j in reversed(range(j1, j2)):
                    out.append(('Insert', '/'.join(path + [after]), txt(b[2 * j]), txt(b[2 * j + 1])))
                    a[2 * i1:2 * i1] = [b[2 * j], b[2 * j + 1]]
        return
    ops = difflib.SequenceMatcher(None, [txt(x) for x in a], [txt(x) for x in b], autojunk=False).get_opcodes()
    for tag, i1, i2, j1, j2 in reversed(ops):
        if tag == 'equal':
            continue
        if tag == 'replace' and i2 - i1 == j2 - j1:
            for i, j in zip(range(i1, i2), range(j1, j2)):
                step(a, i, b[j], path + [itemname(a, i)], out)
            continue
        for i in reversed(range(i1, i2)):
            out.append(('Erase', '/'.join(path + [f'@{i}']), txt(a[i]), ''))
            del a[i]
        for j in reversed(range(j1, j2)):
            out.append(('Insert', '/'.join(path + [f'@{i1}']), '', txt(b[j])))
            a.insert(i1, b[j])


def step(parent, k, new, path, out):
    """parent[k] into `new`: down into lists, else one Set."""
    old = parent[k]
    flat = lambda v: len(v) <= 8 and not any(isinstance(x, list) for x in v)
    if isinstance(old, list) and isinstance(new, list) and old and new and not (flat(old) and flat(new)):
        trial = []
        diff(copy.deepcopy(old), new, path, trial)
        # a record always goes down; an item list rewritten mostly is one Set
        if is_kv(old) and is_kv(new) or len(trial) <= max(4, len(new) // 2):
            diff(old, new, path, out)
            return
    out.append(('Set', '/'.join(path), txt(old), txt(new)))
    parent[k] = new


def raw(archive, entry):
    d = (GAME / archive).read_bytes()
    for n, s, length in zrd.entries(d):
        if n.lower() == entry:
            return d[s:s + length]
    return None


def tree(archive, entry):
    b = raw(archive, entry)
    return None if b is None else zrd.node(b, 0)[0]


def crc32(b, crc=0):
    """zutl_zar::crc32 from 0: polynomial 0x04c11db7, MSB first, no final xor."""
    table = []
    for i in range(256):
        v = i << 24
        for _ in range(8):
            v = ((v << 1) ^ 0x04c11db7) & 0xffffffff if v & 0x80000000 else (v << 1) & 0xffffffff
        table.append(v)
    for x in b:
        crc = table[(crc >> 24) ^ x] ^ ((crc << 8) & 0xffffffff)
    return crc


def main():
    root = Path(__file__).resolve().parents[2] / 'src' / 'mech3'
    head = ['// Generated by tools/analysis/patch12_edits.py from the 1.0 and 1.2 installs',
            '// (docs/patch12.md).']
    edits, copies = list(head), list(head)
    jobs = [(a, e, a) for a, e in ENTRIES] + COPIES
    for archive, entry, source in jobs:
        a, b = tree('mw3-1.0/' + source, entry), tree('mw3-1.2/' + archive, entry)
        out = []
        diff(start(a), start(b), [], out)
        assert start(a) == start(b), entry
        if source != archive:
            data = raw('mw3-1.0/' + source, entry)
            copies.append(f'{{"{archive}", "{entry}", "{source}", {len(data)}, 0x{crc32(data):08x}}},')
        for op, p, x, y in out:
            edits.append(f'{{"{archive}", "{entry}", {op}, R"({p})", R"({x})", R"({y})"}},')
    added = list(head)
    for archive, entry in ADDED:
        assert raw('mw3-1.0/' + archive, entry) is None, entry
        added.append(f'{{"{archive}", "{entry}", R"({txt(tree("mw3-1.2/" + archive, entry))})"}},')
    (root / 'patch12_added.inc').write_text('\n'.join(added) + '\n')
    (root / 'patch12_edits.inc').write_text('\n'.join(edits) + '\n')
    (root / 'patch12_copies.inc').write_text('\n'.join(copies) + '\n')


if __name__ == '__main__':
    main()
