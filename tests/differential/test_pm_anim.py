"""Pirate's Moon's anim.zbd reader (pm/gamez/zeffect/zeff_anim_init
0x00523140, docs/pm-map.md, anim.zbd 50) against PM's exe, each world's
anim.zbd read against that world.

    cmake --build build && .venv/bin/python -P tests/differential/test_pm_anim.py

Both read the world first (test_pm_gamez.py's stack: PM's section readers),
then the animations, whose lookups search that world's nodes and make new
light (PM's 0x0050e470) and sound nodes. The calls into parts not ported
(puffers made and undone, zSound's lookup, the effect table, the tree copy)
are recorded in both and answer fake ids (0x70000000 + 0x10 * call number).
A full run compares that trace, the reports, every def, list, sequence and
script in PM's layout (nodes as pool slots) and the whole node pool after.
Runs where one recorded call answers "not found", an anim file against
another world, crafted files (a def's node by a path, objects and nodes found
under a parent, a second sequence, a missing def node and root, defs sharing a
file pointer over a path or after a missing node, an anim ref's pointer, the
prerequisite FIX) and cut copies compare the result, reports and trace.
"""
import ctypes
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_gamez as g  # noqa: E402
from test_pm_zar import load_pm_probe  # noqa: E402
from twod_world import IAT  # noqa: E402

READ, PATH, LOADING, COPY_TREES = 0x523140, 0x63a2d8, 0x60f390, 0x63a358
BLOCK, DEFS = 0x8d3f00, 0x8d3f0c
REPORT = 0x406cb0
HOOKS = {0x50d830: 3, 0x565ad0: 7, 0x565cb0: 8, 0x590a70: 10, 0x5271c0: 11}
ANIM_FILE = 0x48

probe = load_pm_probe()
probe.probe_pm_anim.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p,
                                ctypes.c_int]
probe.probe_pm_anim_fix.argtypes = [ctypes.c_int]
probe.probe_pm_anim_prereqs.argtypes = [ctypes.c_int]


class Emulated(g.Emulated):
    def __init__(self):
        super().__init__()
        cpu = self.cpu
        cpu.stub_import(IAT['fopen'], self.fopen)
        cpu.stub_import(IAT['fclose'], lambda c: 0)
        cpu.stub_import(IAT['sprintf'], self.sprintf)
        # test_pm_zar's bench already stubs it: its handler replaced, not hooked twice.
        cpu.stubs[REPORT] = (lambda c: self.reports.append((c.arg(0), c.arg(2))) or 0, 0)
        for addr, kind in HOOKS.items():
            self.native_stub(addr, lambda c, a=addr, k=kind: self.hook(a, k, c))
        self.reports, self.trace, self.calls, self.fail_at = [], b'', 0, -1

    def fopen(self, c):
        self.data, self.pos = self.anim, 0
        return g.HANDLE

    def sprintf(self, c):
        assert self.cstr(c.arg(1)) == b'%s'
        s = self.cstr(c.arg(2))
        self.cpu._uc.mem_write(c.arg(0), s + b'\0')
        return len(s)

    def hook(self, addr, kind, c):
        if kind == 3:
            self.trace += struct.pack('<Ii', 3, self.node(c.arg(0)))
        elif kind == 7:
            self.trace += struct.pack('<I', 7)
        elif kind == 8:
            self.trace += struct.pack('<II', 8, c.arg(0))
            return 0
        else:
            self.trace += struct.pack('<I', kind) + self.cstr(c.arg(0)) + b'\0'
        k = self.calls
        self.calls += 1
        if k == self.fail_at:
            return 0xffffffff if kind == 11 else 0
        return 0x70000000 + 0x10 * k

    def node(self, p):
        return self.pool_index(p, g.N_BLOCKS, g.N_NBLOCKS, 2500, 0xd4)

    def u(self, a):
        return self.i32(a) & 0xffffffff

    def run(self, world, anim, fail_at=-1, full=False):
        self.stack([world], 5, 6, 5, models=True, nodes=True)
        self.anim = Path(anim).read_bytes()
        self.reports, self.trace, self.calls, self.fail_at = [], b'', 0, fail_at
        self.cpu._uc.mem_write(BLOCK, bytes(0x6c))
        self.cpu._uc.mem_write(PATH, str(anim).encode()[:0x7f] + b'\0')
        self.w32(LOADING, 1)
        self.w32(COPY_TREES, 0)
        res = self.signed(self.call(READ))
        out = [struct.pack('<iii', res, self.calls, len(self.reports))]
        out += [struct.pack('<ii', *x) for x in self.reports]
        out.append(self.trace)
        if full and res == 0:
            out.append(self.anim_dump())
            out.append(self.node_dump())
        return b''.join(out)

    def anim_dump(self):
        r, u, ix = self.read, self.u, self.node
        out = []

        def raw(addr, n, ptrs=()):
            b = bytearray(r(addr, n))
            for o in ptrs:
                b[o:o + 4] = bytes(4)
            out.append(bytes(b))

        def i32(v):
            out.append(struct.pack('<i', v))

        def seq(p):
            raw(p, 0x40, (0x38,))
            n = self.i32(p + 0x3c)
            if n > 0:
                out.append(r(u(p + 0x38), n))

        raw(BLOCK, 0x6c, (0x0c, 0x14, 0x1c, 0x20))
        count = struct.unpack('<h', r(BLOCK + 0x0a, 2))[0]
        defs = u(DEFS)
        for i in range(count):
            d = defs + 0x10c * i
            rec = r(d, 0x10c)
            fixed = i != 0 and rec[0xa0] != 5
            b = bytearray(rec)
            for o in (0x20, 0x48, 0x6c, 0xcc, 0xd0, 0xd4) + tuple(range(0xe4, 0x108, 4)):
                b[o:o + 4] = bytes(4)
            b[0xd0], b[0xd4] = int(u(d + 0xd0) != 0), int(u(d + 0xd4) != 0)
            out.append(bytes(b))
            i32(ix(u(d + 0x48)) if fixed else 0), i32(ix(u(d + 0x6c)) if fixed else 0)
            npath = struct.unpack_from('<i', rec, 0x24)[0]
            for k in range(max(npath, 0)):
                e = u(d + 0x20) + 0x24 * k
                raw(e, 0x24, (0x20,))
                i32(ix(u(e + 0x20)) if fixed else 0)
            for cnt, ptr, size, at, kind in ((0xd9, 0xe4, 0x5c, 0x24, 'node'), (0xda, 0xe8, 0x2c, 0x28, 'node'),
                                             (0xdb, 0xec, 0x2c, 0x24, 'node'), (0xdc, 0xf0, 0x2c, 0x24, 'id'),
                                             (0xdd, 0xf4, 0x2c, 0x24, 'node'), (0xde, 0xf8, 0x24, 0x20, 'id')):
                lst = u(d + ptr)
                for k in range(rec[cnt]):
                    raw(lst + size * k, size, (at,))
                    v = u(lst + size * k + at)
                    i32((ix(v) if kind == 'node' else self.signed(v)) if fixed and k else 0)
            lst = u(d + 0xfc)
            for k in range(rec[0xdf]):
                raw(lst + 0x24 * k, 0x24)
            lst = u(d + 0x100)
            for k in range(rec[0xe0]):
                e = lst + 0x30 * k
                raw(e, 0x30, (0x28, 0x2c))
                if not fixed:
                    continue
                t = r(e + 4, 1)[0]
                if t == 1:
                    p = u(e + 0x28)
                    i32(-1 if p == 0 else (p - defs) // 0x10c)
                if t in (2, 3):
                    i32(ix(u(e + 0x2c)))
            lst = u(d + 0x104)
            for k in range(rec[0xe2]):
                raw(lst + 0x48 * k, 0x48)
            for at in (0xd0, 0xd4):
                if u(d + at):
                    seq(u(d + at))
            for k in range(rec[0xd8]):
                seq(u(d + 0xcc) + 0x40 * k)
        scripts = u(BLOCK + 0x14)
        for k in range(self.i32(BLOCK + 0x10)):
            e = scripts + 0x1c * k
            raw(e, 0x1c, (0, 4, 0x18))
            rec = r(e, 0x1c)
            if rec[8]:
                out.append(r(u(e), rec[8]))
            if rec[9]:
                out.append(r(u(e + 4), rec[9]))
            n = struct.unpack_from('<i', rec, 0x14)[0]
            if n > 0:
                out.append(r(u(e + 0x18), n))
        msgs = u(BLOCK + 0x1c)
        for k in range(self.i32(BLOCK + 0x18)):
            raw(msgs + 0x24 * k, 0x24)
        return b''.join(out)


LISTS = ((0xd9, 0x5c), (0xda, 0x2c), (0xdb, 0x2c), (0xdc, 0x2c), (0xdd, 0x2c), (0xde, 0x24), (0xdf, 0x24),
         (0xe0, 0x30), (0xe2, 0x48))


def layout(data):
    """Each def's record offset and its lists' offsets by count byte, the
    reset, second and other sequences' offsets; the scripts' offset."""
    nsrc = struct.unpack_from('<i', data, 0xc)[0]
    p = 0x10 + 0x54 * nsrc
    ndef = struct.unpack_from('<h', data, p + 0xa)[0]
    p += 0x6c
    defs = []

    def seq(q):
        return q + 0x40 + max(struct.unpack_from('<i', data, q + 0x3c)[0], 0)
    for _ in range(ndef):
        r = data[p:p + 0x10c]
        e = {'at': p, 'lists': {}}
        p += 0x10c
        e['path'] = p
        p += 0x24 * max(struct.unpack_from('<i', r, 0x24)[0], 0)
        for off, size in LISTS:
            e['lists'][off] = p
            p += r[off] * size
        e['reset'] = p if struct.unpack_from('<I', r, 0xd0)[0] else None
        if e['reset'] is not None:
            p = seq(p)
        e['seq2'] = p
        if struct.unpack_from('<I', r, 0xd4)[0]:
            p = seq(p)
        e['seqs'] = []
        for _ in range(r[0xd8]):
            e['seqs'].append(p)
            p = seq(p)
        e['end'] = p
        defs.append(e)
    return defs, p


def name(data, at):
    return data[at:at + 0x20].split(b'\0')[0]


def fixed_defs(data, defs):
    return [i for i in range(1, len(defs)) if data[defs[i]['at'] + 0xa0] != 5]


def craft(data, edits):
    """Edits (offset, new bytes, replaced length) applied from the end."""
    b = bytearray(data)
    for at, new, n in sorted(edits, key=lambda e: -e[0]):
        b[at:at + n] = new
    return bytes(b)


def crafted(data):
    """(label, bytes) for the paths the shipped files never take."""
    defs, _ = layout(data)
    out = []
    live = fixed_defs(data, defs)
    with_objs = [i for i in live if data[defs[i]['at'] + 0xd9] >= 3 and data[defs[i]['at'] + 0xda] >= 3]
    i = with_objs[0]
    d = defs[i]
    node = name(data, d['at'] + 0x28)
    obj1 = name(data, d['lists'][0xd9] + 0x5c)
    for label, path in (('a path of one', [node]), ('a path to itself', [node, node]),
                        ('a path through an object', [node, obj1]), ('a path that ends nowhere', [node, b'no_such'])):
        recs = b''.join(n.ljust(0x20, b'\0') + struct.pack('<I', 0xffffffff) for n in path)
        out.append((label, craft(data, [(d['at'] + 0x24, struct.pack('<i', len(path)), 4), (d['path'], recs, 0)])))
    # The same file pointer and node name as the def before: its node, not the next of that name.
    runs = [k for k in live[:-1] if k + 1 in live and name(data, defs[k]['at'] + 0x28) == name(data, defs[k + 1]['at'] + 0x28)]
    k = runs[0]
    same = [(defs[k]['at'] + 0x48, struct.pack('<I', 0x1234), 4), (defs[k + 1]['at'] + 0x48, struct.pack('<I', 0x1234), 4)]
    out.append(('one file pointer, one name', craft(data, same)))
    # The def before found by a path whose first name is this one's (its own node named otherwise): the
    # path's node, through the path's file pointer.
    n0 = name(data, defs[k]['at'] + 0x28)
    rec = n0.ljust(0x20, b'\0') + struct.pack('<I', 0x1234)
    out.append(('one file pointer, the path before', craft(data, [
        (defs[k]['at'] + 0x28, b'no_such_node'.ljust(0x20, b'\0'), 0x20), (defs[k]['at'] + 0x24, struct.pack('<i', 1), 4),
        (defs[k]['path'], rec, 0), same[1]])))
    # A def whose node is missing just before the run, all three on one file pointer: the missing one's
    # clear (0x524230) zeroes the later pointers, so the run's second takes the next node of that name.
    before = [j for j in runs if j - 1 in live]
    if before:
        j = before[0]
        out.append(('a missing node before one file pointer', craft(data, [
            (defs[j - 1]['at'] + 0x28, b'no_such_node'.ljust(0x20, b'\0'), 0x20),
            (defs[j - 1]['at'] + 0x48, struct.pack('<I', 0x1234), 4), (defs[j]['at'] + 0x48, struct.pack('<I', 0x1234), 4),
            (defs[j + 1]['at'] + 0x48, struct.pack('<I', 0x1234), 4)])))
    # An anim ref with a pointer in the file: the reader zeroes it.
    refs = [j for j in live if data[defs[j]['at'] + 0xe2]]
    if refs:
        at = defs[refs[0]]['lists'][0xe2] + 0x44
        out.append(('an anim ref with a pointer', craft(data, [(at, struct.pack('<I', 0x5678), 4)])))
    other = next(j for j in live if j > k + 1 and name(data, defs[j]['at'] + 0x28) != name(data, defs[k]['at'] + 0x28))
    out.append(('one file pointer, two names', craft(data, [same[0], (defs[other]['at'] + 0x48, struct.pack('<I', 0x1234), 4)])))
    # Objects and nodes found under an earlier entry; every object kept to the def's own nodes.
    out.append(('objects under a parent', craft(data, [(d['lists'][0xd9] + 0x5c * k2 + 0x2a, struct.pack('<H', 1), 2)
                                                      for k2 in range(2, data[d['at'] + 0xd9])])))
    out.append(('nodes under a parent', craft(data, [(d['lists'][0xda] + 0x2c * k2 + 2, struct.pack('<H', 1), 2)
                                                    for k2 in range(2, data[d['at'] + 0xda])])))
    local = [(defs[j]['lists'][0xd9] + 0x5c * k2 + 0x28, struct.pack('<H', 2), 2)
             for j in live for k2 in range(1, data[defs[j]['at'] + 0xd9])]
    out.append(('every object local', craft(data, local)))
    # A second sequence, a copy of the def's first.
    s0 = d['seqs'][0]
    seq = data[s0:s0 + 0x40 + max(struct.unpack_from('<i', data, s0 + 0x3c)[0], 0)]
    out.append(('a second sequence', craft(data, [(d['at'] + 0xd4, struct.pack('<I', 1), 4), (d['seq2'], seq, 0)])))
    # A def whose root, and one whose node, no world has; the reset left out of one.
    out.append(('a missing root', craft(data, [(d['at'] + 0x4c, b'no_such_root'.ljust(0x20, b'\0'), 0x20)])))
    out.append(('a missing node', craft(data, [(d['at'] + 0x28, b'no_such_node'.ljust(0x20, b'\0'), 0x20)])))
    r = next(j for j in live if defs[j]['reset'] is not None)
    rs = defs[r]['reset']
    n = 0x40 + max(struct.unpack_from('<i', data, rs + 0x3c)[0], 0)
    out.append(('a reset left out', craft(data, [(defs[r]['at'] + 0xd0, bytes(4), 4), (rs, b'', n)])))
    return out


def compare(what, want, n, buf):
    got = buf.raw[:max(n, 0)]
    if got != want:
        at = next((i for i in range(min(n, len(want))) if got[i] != want[i]), None)
        raise SystemExit(f'{what}: original {len(want)} bytes, port {n}, first difference at {at}')


def main():
    emu = Emulated()
    buf = ctypes.create_string_buffer(96 << 20)
    tmp = tempfile.TemporaryDirectory(prefix='pm-anim-')
    tmpdir = Path(tmp.name)

    def case(label, world, anim, fail_at=-1, full=True):
        want = emu.run(world, anim, fail_at=fail_at, full=full)
        n = probe.probe_pm_anim(str(world).encode(), str(anim).encode(), fail_at, int(full), buf, len(buf))
        compare(label, want, n, buf)
        return struct.unpack_from('<i', want)[0], struct.unpack_from('<i', want, 4)[0], struct.unpack_from('<i', want, 8)[0]

    def written(label, data):
        path = tmpdir / f'{len(list(tmpdir.iterdir()))}.zbd'
        path.write_bytes(data)
        return path

    calls = runs = 0
    for w in g.WORLDS:
        world, anim = g.ZBD / w / 'gamez.zbd', g.ZBD / w / 'anim.zbd'
        res, n, reports = case(f'{w} full', world, anim)
        assert res == 0, f'{w}: {res}'
        calls += n
        print(f'{w}: identical ({n} calls, {reports} reports)', flush=True)
    for w, other in (('c1', 'c2'), ('c3', 'c4')):
        res, _, reports = case(f'{w} anim on {other} world', g.ZBD / other / 'gamez.zbd', g.ZBD / w / 'anim.zbd')
        print(f'{w} on {other}: result {res}, {reports} reports', flush=True)
        runs += 1
    world, anim = g.ZBD / 'c1' / 'gamez.zbd', g.ZBD / 'c1' / 'anim.zbd'
    total = struct.unpack_from('<i', emu.run(world, anim), 4)[0]
    for k in sorted({0, 1, 2, 5, total // 3, total // 2, total - 2, total - 1}):
        case(f'c1 fail at {k}', world, anim, fail_at=k)
        runs += 1
    data = anim.read_bytes()
    for label, crafted_data in crafted(data):
        res, _, reports = case(f'crafted: {label}', world, written(label, crafted_data))
        print(f'crafted, {label}: result {res}, {reports} reports', flush=True)
        runs += 1
    defs, scripts = layout(data)
    cuts = [0x8, 0x10 + 0x54 * 3, defs[1]['at'] + 0x30, defs[len(defs) // 2]['lists'][0xd9] + 5, defs[-1]['end'] - 3]
    for at in cuts:
        res, _, _ = case(f'cut at {at:#x}', world, written('cut', data[:at]))
        assert res == -1, f'cut at {at:#x}: {res}'
        runs += 1
    res, _, _ = case('version 49', world, written('v49', data[:4] + struct.pack('<I', 49) + data[8:]))
    assert res == -1
    runs += 1
    # Cut in the scripts' names: the exe's reads there are unchecked (it returns 0 with
    # what it read); the port refuses the file, as it does any broken copy.
    nscripts = struct.unpack_from('<i', data, 0x10 + 0x54 * struct.unpack_from('<i', data, 0xc)[0] + 0x10)[0]
    want = emu.run(world, written('cut', data[:scripts + 0x1c * nscripts + 3]))
    n = probe.probe_pm_anim(str(world).encode(), str(tmpdir / f'{len(list(tmpdir.iterdir())) - 1}.zbd').encode(),
                            -1, 0, buf, len(buf))
    got = struct.unpack_from('<i', buf.raw)[0]
    assert struct.unpack_from('<i', want)[0] == 0 and got == -1, (struct.unpack_from('<i', want)[0], got)
    print(check_prerequisite_fix(emu, buf, written))
    print(f'ok: {len(g.WORLDS)} anim.zbd read against their worlds identical ({calls} unported calls); '
          f'{runs} mismatched, failing, crafted and cut runs agree; a cut in the scripts refused (the exe reads on)')


def check_prerequisite_fix(emu, buf, written):
    """A node prerequisite renamed to a node no world has: PM's exe drops all
    of the def's prerequisites (0x376f); the port's exe path matches; its FIX
    (bug) drops only that one."""
    for w in g.WORLDS:
        world, anim = g.ZBD / w / 'gamez.zbd', g.ZBD / w / 'anim.zbd'
        data = anim.read_bytes()
        defs, _ = layout(data)
        for i in fixed_defs(data, defs):
            d = defs[i]
            n = data[d['at'] + 0xe0]
            recs = [data[d['lists'][0xe0] + 0x30 * k:d['lists'][0xe0] + 0x30 * k + 0x30] for k in range(n)]
            two = [k for k, r in enumerate(recs) if r[4] == 2]
            if n >= 2 and two:
                break
        else:
            continue
        at = d['lists'][0xe0] + 0x30 * two[0] + 0xc
        path = written('prereq', data[:at] + b'no_such_node'.ljust(0x20, b'\0') + data[at + 0x20:])
        want = emu.run(world, path, full=True)
        compare(f'prerequisite {w}, exe path', want,
                probe.probe_pm_anim(str(world).encode(), str(path).encode(), -1, 1, buf, len(buf)), buf)
        assert 0x376f in [x[1] for x in emu.reports]
        assert emu.read(emu.u(DEFS) + 0x10c * i + 0xe0, 1)[0] == 0, 'the original kept them'
        probe.probe_pm_anim_fix(1)
        try:
            probe.probe_pm_anim(str(world).encode(), str(path).encode(), -1, 0, buf, len(buf))
            got = probe.probe_pm_anim_prereqs(i)
        finally:
            probe.probe_pm_anim_fix(0)
        assert got & 0xff == n - 1, f'the fix kept {got & 0xff} of {n}'
        return (f'prerequisite FIX (bug): {w} def {i} with a missing node: the original drops all {n}, the exe path '
                f'matches, the fix keeps {n - 1}')
    raise SystemExit('prerequisite fix: no def with two prerequisites to break')

if __name__ == '__main__':
    main()
