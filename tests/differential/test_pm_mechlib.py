"""Pirate's Moon's mechlib.zbd 41 (pm/gamez/zclass/cls_mechlib: the open
0x00510910, the load 0x00510b90, the tree, node and model readers 0x005110c0,
0x00511210, 0x00511310, the resolve 0x00510fd0 and PM's sort by material
0x0055c470) against PM's exe, through the game table (docs/pm-map.md,
mechlib 41).

    cmake --build build && .venv/bin/python -P tests/differential/test_pm_mechlib.py

The library opened over a world (c1, c3) or none, then trees loaded: every
one of the 76 in the file's order, a few again (found, not read), and
crafted files for what no shipped tree has (a cycled material, a polygon
with two materials, no colours, a LOD with a model below it, a model the
renderer would convert, an unknown box). Compared: the open's result, each
load's root, the library's materials and trees, the texture and material
tables, every model slot and every node slot with its links and data.
"""
import ctypes
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_gamez  # noqa: E402
import test_pm_zar  # noqa: E402
from original import ROOT  # noqa: E402
from twod_world import IAT  # noqa: E402

ZBD = test_pm_gamez.ZBD
MECHLIB = ZBD / 'mechlib.zbd'
CREATE, OPEN, LOAD, TRACE, POOL_READY = 0x510830, 0x510910, 0x510b90, 0x511640, 0x8d4ae4
TREE_COPY, TREE_COPY_MATERIALS = 0x50d7c0, 0x50d830
OP_NEW, OP_DELETE = 0x5ce8c6, 0x5ce8c0  # msvcrt's, through PM's thunks
NODE_POOL = (test_pm_gamez.N_BLOCKS, test_pm_gamez.N_NBLOCKS, 2500, 0xd4)


def entries(data):
    """(name, start, length) of a version-2 archive's table."""
    count = struct.unpack_from('<I', data, len(data) - 8)[0]
    table = len(data) - 12 - count * 0x94
    for i in range(count):
        start, length = struct.unpack_from('<II', data, table + i * 0x94)
        name = data[table + i * 0x94 + 8:table + i * 0x94 + 72].split(b'\0')[0].decode()
        yield name, start, length


def archive(blobs):
    """A version-2 archive of (name, bytes), its checksum 0 (mechlib's)."""
    out, table = bytearray(), bytearray()
    for name, blob in blobs:
        e = bytearray(0x94)
        struct.pack_into('<II', e, 0, len(out), len(blob))
        e[8:8 + len(name)] = name.encode()
        table += e
        out += blob
    return bytes(out + table + struct.pack('<III', 2, len(blobs), 0))


class Emulated(test_pm_gamez.Emulated):
    item_set = True

    def __init__(self):
        super().__init__()
        self.native_stub(TRACE, lambda c: 0)
        self.native_stub(OP_NEW, lambda c: self.sized(c.arg(0)))
        self.native_stub(OP_DELETE, lambda c: 0)
        self.cpu.stub_import(IAT['_stricmp'], self.stricmp)  # the archive's find
        self.cpu.stub_import(IAT['sprintf'], self.sprintf)  # a new node's name

    def stricmp(self, c):
        a, b = self.cstr(c.arg(0)).lower(), self.cstr(c.arg(1)).lower()
        return (a > b) - (a < b)

    def sprintf(self, c):
        assert self.cstr(c.arg(1)) == b'%s'
        s = self.cstr(c.arg(2))
        self.cpu._uc.mem_write(c.arg(0), s + b'\0')
        return len(s)

    def run(self, world, path, names, copies=()):
        if world:
            self.stack([world], 5, 6, 5, models=True, nodes=True)
        else:
            self.reset(5, 6, 5)
        self.w32(POOL_READY, 1)
        self.files[test_pm_zar.key(str(path))] = bytearray(Path(path).read_bytes())
        lib = self.alloc(0x44)
        self.call(CREATE, ecx=lib)
        opened = self.call(OPEN, ecx=lib, args=[self.string(str(path))]) & 0xff
        out = struct.pack('<i', 1 if opened else 0)
        self.converted = []
        if opened:
            roots = []
            for name in names:
                root = self.call(LOAD, ecx=lib, args=[self.string(name)])
                roots.append(root)
                out += struct.pack('<i', self.pool_index(root, *NODE_POOL))
            self.loaded = list(roots)  # test_pm_shadows.py
            if copies:  # in play: mechs and clones are made with the x87 at 24 bits (Direct3D's)
                self.cpu.fpu_control(0x7f)
            for i, kind, a, b in copies:  # Pirate's Moon 0x50d7c0 / 0x50d830, cdecl
                c = self.call(TREE_COPY_MATERIALS, args=[roots[i], int(kind == 1), a, b]) if kind else \
                    self.call(TREE_COPY, args=[roots[i], a, b])
                out += struct.pack('<i', self.pool_index(c, *NODE_POOL))
            if copies:
                self.cpu.fpu_control(0x27f)
        begin, end = self.i32(lib + 0x24) & 0xffffffff, self.i32(lib + 0x28) & 0xffffffff
        n = (end - begin) // 4 if begin else 0
        out += struct.pack('<i', n)
        for k in range(n):
            m = self.i32(begin + 4 * k) & 0xffffffff
            resolved = self.read(m + 4, 1)[0]
            world_index = self.mat_index(self.i32(m)) if resolved else -1
            out += struct.pack('<iii', 1 if resolved else 0, world_index, 1 if self.i32(m + 8) else 0)
        head, roots = self.i32(lib + 0x34) & 0xffffffff, []
        item = self.i32(head) & 0xffffffff
        while item != head:
            roots.append(self.pool_index(self.i32(item + 8), *NODE_POOL))
            item = self.i32(item) & 0xffffffff
        out += struct.pack(f'<i{len(roots)}i', len(roots), *roots)
        self.unresolved = self.m_unread = self.n_unread = 1 << 30
        t, md, nd = self.tables(), self.model_dump(), self.node_dump()
        self.sections = [('head', len(out)), ('tables', len(t)), ('models', len(md)), ('nodes', len(nd))]
        return out + t + md + nd


def crafted(scratch, emu, world, cycled_flag=True):
    """mechlib.zbd's materials and two of its trees, plus a crafted one.
    Without `cycled_flag` the cycled material's flag 4 is cleared: with no
    world to find it in, the exe adds it and copies its cycle from null
    (0x005568f5)."""
    data = MECHLIB.read_bytes()
    ents = {name: (start, length) for name, start, length in entries(data)}
    blob = lambda name: data[ents[name][0]:ents[name][0] + ents[name][1]]
    # A library material equal to one of the world's cycled ones, as the
    # exe holds it once loaded (the exe copies a new material's cycle from
    # the library's, which has none: only an equal one can be cycled), after
    # the file's materials.
    mats = bytearray(blob('materials'))
    count = struct.unpack_from('<I', mats, 0)[0]
    emu.stack([world], 5, 6, 5)
    blocks = emu.i32(test_pm_gamez.BLOCKS) & 0xffffffff
    cycled = None
    for i in range(emu.i32(test_pm_gamez.CAPACITY)):
        w = emu.read((emu.i32(blocks + 4 * (i // 1000)) & 0xffffffff) + 0x2c * (i % 1000), 0x2c)
        if w[1] & 0x20 == 0 and w[1] & 5 == 5:
            cycled = w
            break
    assert cycled, 'the world has no cycled material'
    entry = struct.unpack_from('<I', cycled, 0x10)[0]
    name = emu.read(entry + 0xc, 20).split(b'\0')[0]
    raw = bytearray(cycled[:0x28])
    raw[1] &= 0x0f if cycled_flag else 0x0b
    struct.pack_into('<I', raw, 0x10, 1)
    struct.pack_into('<I', raw, 0x24, 0)
    mats[0:4] = struct.pack('<I', count + 1)
    mats += raw + struct.pack('<I', len(name) + 1) + name + b'\0'
    cyc = count  # its library index
    # The crafted tree: an object3d with a model of seven polygons (two on the
    # cycled material, one with two materials and their UVs, one without
    # colours, one with normals, one with the normals' flag 0x1000 but none in
    # the file: +0xc is 0) and a box index of 3, then a LOD child
    # (box 1) under it, and below that an object3d with a one-polygon model
    # without flag 0x80 (the exe's strip conversion; box 2).
    first = blob(next(n for n in ents if n not in ('version', 'format', 'materials')))
    node5, obj = bytearray(first[:0xd0]), first[0xd0:0xd0 + 0x90]

    def node(name, cls, model, children, box):
        n = bytearray(node5)
        n[0:0x24] = name.encode().ljust(0x24, b'\0')
        struct.pack_into('<i', n, 0x34, cls)
        struct.pack_into('<I', n, 0x3c, 1 if model else 0)
        struct.pack_into('<h', n, 0x56, children)
        struct.pack_into('<i', n, 0x70, box)
        return bytes(n)

    def model(flags, verts, polys):
        h = bytearray(0x64)
        struct.pack_into('<I', h, 8, flags)
        struct.pack_into('<iiiii', h, 0x10, len(polys), len(verts), 1, 0, 0)
        struct.pack_into('<i', h, 0x5c, 2)  # refs in the file: skipped
        out = bytes(h) + b''.join(struct.pack('<3f', *v) for v in verts) + struct.pack('<3f', 0, 1, 0)
        table, arrays = b'', b''
        for k, mats_, uvs, colours, normals in polys:
            q = bytearray(0x28)
            struct.pack_into('<Ii', q, 0, k | (0x1000 if normals else 0), 3)
            struct.pack_into('<I', q, 0xc, 1 if normals == 1 else 0)
            struct.pack_into('<i', q, 0x10, len(mats_))
            struct.pack_into('<III', q, 0x18, 1 if uvs else 0, 1 if colours else 0, 0)
            struct.pack_into('<I', q, 0x24, 5)
            table += bytes(q)
            arrays += struct.pack(f'<{k}I', *range(k))
            if normals == 1:
                arrays += struct.pack(f'<{k}I', *([0] * k))
            arrays += struct.pack(f'<{len(mats_)}i', *mats_)
            if uvs:
                for j in range(len(mats_)):
                    arrays += struct.pack(f'<{2 * k}f', *[0.25 * j + 0.125 * i for i in range(2 * k)])
            if colours:
                arrays += struct.pack(f'<{3 * k}f', *([0.5] * 3 * k))
        return out + table + arrays + bytes(12 * 2)

    verts = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)]
    lod = bytes(range(0x5c))
    tree = (node('crafted', 5, True, 1, 3) + obj
            + model(0x80, verts, [(3, [0], 1, 1, 0), (3, [cyc], 1, 1, 0), (4, [1], 0, 1, 1),
                                  (3, [0, 1], 1, 1, 0), (3, [cyc], 0, 0, 0), (3, [2], 1, 0, 0), (3, [2], 1, 1, 2)])
            + node('crafted_lod', 6, False, 1, 1) + lod
            + node('crafted_leaf', 5, True, 0, 2) + obj + model(0, verts, [(3, [1], 1, 1, 0)]))
    trees = [n for n in ents if n not in ('version', 'format', 'materials')][:2]
    path = scratch / ('crafted.zbd' if cycled_flag else 'crafted_plain.zbd')
    path.write_bytes(archive([('version', blob('version')), ('format', blob('format')), ('materials', bytes(mats))]
                             + [(t, blob(t)) for t in trees] + [('crafted', tree)]))
    return path, trees + ['crafted', trees[0]]


def describe_model(a, b, at):
    """The slot and field of byte `at` in a models dump (put_models' layout)."""
    i32 = lambda d, o: struct.unpack_from('<i', d, o)[0]
    o, slot = 12, 0
    cap = i32(a, 0)
    while slot < cap and o <= at:
        start = o
        h = a[o:o + 0x34]
        polys, verts, norms, morphs, lights = (struct.unpack_from('<i', h, k)[0] for k in (0x10, 0x14, 0x18, 0x1c, 0x20))
        refs = i32(a, o + 0x34 + 0x14)
        o += 0x50
        if at < o:
            print(f'  model slot {slot}, header byte {at - start}'); return
        o += 12 * (verts + norms + morphs)
        for _ in range(lights):
            o += 0x28 + 2 + 0x1c
            o += 12 * i32(a, o - 0x1c - 2 - 0x28 + 0xc)
        if at < o:
            print(f'  model slot {slot}, arrays'); return
        for k in range(polys):
            ps = o
            flags, pri, mc, uvs = (i32(a, o + 4 * j) for j in range(4))
            n = flags & 0x3ff
            o += 20 + 4 * n
            if n and (flags & 0x1000):
                o += 4 * n
            o += mc * (4 + (8 * n if uvs else 0))
            o += 12 * n  # colours, when present (assumed)
            if at < o:
                print(f'  model slot {slot} polygon {k} of {polys}, byte {at - ps}: exe {a[ps:ps+20].hex()} port {b[ps:ps+20].hex()}'); return
        rs = o
        o += 12 * refs
        if at < o:
            print(f'  model slot {slot} refs (of {refs}), byte {at - rs}: exe {a[rs:o].hex()} port {b[rs:o].hex()}'); return
        slot += 1
    print('  past the walk')


def main():
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_mechlib.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int]
    p.probe_pm_mechlib_copies.argtypes = [ctypes.c_void_p, ctypes.c_int]
    buf = ctypes.create_string_buffer(1 << 27)
    data = MECHLIB.read_bytes()
    trees = [n for n, _, _ in entries(data) if n not in ('version', 'format', 'materials')]
    c1, c3 = ZBD / 'c1' / 'gamez.zbd', ZBD / 'c3' / 'gamez.zbd'
    scratch = Path(tempfile.mkdtemp(prefix='pm_mechlib_'))
    emu = Emulated()
    made, made_names = crafted(scratch, emu, c1)
    plain, _ = crafted(scratch, emu, c1, cycled_flag=False)
    # MW3's mechlib (version 27) is refused; a tree twice is found, not read.
    mw3 = ROOT / 'game' / 'mw3-1.2' / 'zbd' / 'mechlib.zbd'
    # The tree copies (0x50d7c0 / 0x50d830, the node's model copy 0x50cd10 and PM's model copy 0x559340):
    # mechs as the game makes them (models shared), copied models with their own materials or not, and
    # the materials variant, over the shipped trees and the crafted ones (a cycled material changes).
    copy_all = [(i, 0, 0, 0) for i in range(0, 76, 5)] + [(i, 0, 1, 1) for i in range(1, 76, 7)] + \
               [(i, 0, 1, 0) for i in range(2, 76, 9)] + [(i, 1, 1, 1) for i in range(3, 76, 11)] + \
               [(i, 1, 0, 1) for i in range(4, 76, 13)] + [(i, 2, 1, 1) for i in range(5, 76, 17)]
    copy_made = [(i, k, a, b) for i in range(len(made_names)) for k, a, b in ((0, 0, 0), (0, 1, 1), (0, 1, 0), (1, 1, 1), (2, 1, 1))]
    cases = [(c1, MECHLIB, trees, ()), (c3, MECHLIB, trees[::-1][:20] + trees[-3:], ()), (None, MECHLIB, trees[:10], ()),
             (c1, mw3, trees[:1], ()), (c1, made, made_names, ()), (None, plain, made_names, ()),
             (c1, MECHLIB, trees, copy_all), (c1, made, made_names, copy_made)]
    runs = 0
    for world, path, names, copies in cases:
        want = emu.run(world, path, names, copies)
        flat = [x for c in copies for x in c]
        p.probe_pm_mechlib_copies((ctypes.c_int32 * max(len(flat), 1))(*flat), len(flat))
        n = p.probe_pm_mechlib(str(world).encode() if world else b'', str(path).encode(), '|'.join(names).encode(),
                               buf, len(buf))
        if n < 0:
            raise SystemExit(f'{path.name}: probe {n}')
        got = buf.raw[:n]
        label = f'{world.parent.name if world else "no world"} + {path.parent.parent.name}/{path.name}'
        if want != got:
            at = next((i for i in range(min(len(want), len(got))) if want[i] != got[i]), min(len(want), len(got)))
            print(f'{label}: differ at byte {at} of {len(want)}/{len(got)}')
            start = 0
            for name, size in emu.sections:
                if at < start + size:
                    print(f'  in the {name} section, byte {at - start} of {size}')
                    if name == 'nodes':
                        n0, i = [(o, k) for o, k in emu.node_marks if o <= at - start][-1]
                        slot = want[start + n0:start + n0 + 0xd4]
                        print(f'  node slot {i} ({slot[:32].split(bytes(1))[0]!r}), class {struct.unpack_from("<i", slot, 0x34)[0]}, '
                              f'byte {at - start - n0} of its dump')
                    if name == 'models':
                        describe_model(want[start:start + size], got[start:start + size], at - start)
                    break
                start += size
            print('exe: ', want[max(0, at - 16):at + 32].hex())
            print('port:', got[max(0, at - 16):at + 32].hex())
            raise SystemExit(1)
        runs += 1
        opened = struct.unpack_from('<i', want)[0]
        roots = struct.unpack_from(f'<{len(names) if opened else 0}i', want, 4)
        print(f'{label}: open {opened}, {len(names) if opened else 0} loads, roots {roots[:4]}..., '
              f'{len(emu.converted)} to the strip conversion')
    print(f'ok: {runs} runs identical')


if __name__ == '__main__':
    main()
