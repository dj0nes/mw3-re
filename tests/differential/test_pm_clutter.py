"""PM's clutter settings (pm/gamez/zclass/cls_clutter, 0x513030 and 0x513310) against PM's exe, on
the four worlds' templates.zrd and crafted entries, with every named node in the list and with every
third one missing. The anim lookup and the hit callbacks are stubbed and logged in both.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_clutter.py

Compared: the log's order, every field of every record, every node's flags. Rotations bit for bit;
slopes go through x87 fcos, within 1e-6.
"""
import ctypes
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT  # noqa: E402
import test_pm_zar  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_walk  # noqa: E402
import test_pm_zreader as zr  # noqa: E402

sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import zrd  # noqa: E402

READ, ANIM, WATCH, WATCH2, ZERR, STRDUP_IAT = 0x513030, 0x524b50, 0x5a4d80, 0x5a4dc0, 0x406cb0, '_strdup'
LISTS, LIST_DIRTY, BEGIN, END, CAP = 0x60db00, 0x60db84, 0x639bf4, 0x639bf8, 0x639bfc
LOAD_TEMPLATES, TEMPLATE_NAMES, MAP_HEAD, MAP_FIND, MAP_INSERT = 0x5139d0, 0x639c14, 0x639be4, 0x514120, 0x513ed0
CIFMOD = bytes.fromhex('d9c9 d9f8 dfe0 9e 7afa ddd9 c3')  # _CIfmod: st(1) mod st(0)
PM = ROOT / 'game' / 'pm'


def fnv(b):
    v = 2166136261
    for c in b:
        v = ((v ^ c) * 16777619) & 0xffffffff
    return v


class Emu(zr.Emulated):
    def __init__(self, path, rel):
        super().__init__()
        self.files[test_pm_zar.key(rel)] = bytearray(path.read_bytes())
        if not self.call(zr.OPEN_ARCHIVE, args=[self.string(rel.replace('/', '\\')), 1]):
            raise SystemExit(f'{rel}: PM does not open it')
        self.cpu.fpu_control(0x7f)
        self.log = []
        self.nodes = []
        self.cpu.stub_import(test_pm_zar.IAT[STRDUP_IAT], self.strdup)
        self.native_stub(ANIM, self.anim)
        self.native_stub(WATCH, self.watch)
        self.native_stub(WATCH2, lambda c: 0)
        self.cpu.stubs[ZERR] = (self.zerr, 0)  # the bench's own hook stays on it

    def w32(self, a, v):
        self.cpu._uc.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def anim(self, c):
        if not c.arg(0):
            return 0
        v = fnv(self.cstr(c.arg(0)))
        self.log += [0xa0000000, v]
        return v

    def watch(self, c):
        self.log += [0xb0000000, self.nodes.index(c.arg(1)) + 1]
        return 1

    def zerr(self, c):
        self.log += [0xc0000000, c.arg(0) << 16 | c.arg(2)]
        return 0

    def make(self, name):
        n = self.alloc(0xd4)
        self.cpu._uc.mem_write(n, name.encode()[:35] + b'\0')
        self.w32(n + 0x34, 5)
        self.w32(n + 0x24, 0x10)
        self.nodes.append(n)
        return n

    def run(self, file, names):
        uc, w = self.cpu._uc, self.w32
        self.log, self.nodes = [], []
        items = []
        for name in names:
            n = self.make(name)
            a, b = self.make(name + '_a'), self.make(name + '_b')
            kids = self.alloc(8)
            uc.mem_write(kids, struct.pack('<II', a, b))
            uc.mem_write(n + 0x56, struct.pack('<h', 2))
            w(n + 0x5c, kids)
            items.append((n, self.alloc(0x10)))
        for k, (n, item) in enumerate(items):
            uc.mem_write(item, struct.pack('<IIII', n, items[k - 1][1] if k else 0,
                                           items[k + 1][1] if k + 1 < len(items) else 0, 1))
        head = self.alloc(12)
        uc.mem_write(head, struct.pack('<III', items[0][1] if items else 0, items[-1][1] if items else 0, 0))
        w(LISTS + 24, head)
        w(LIST_DIRTY, 0)
        for a in (BEGIN, END, CAP):
            w(a, 0)
        self.call(READ, args=[self.string(file)])
        r = self.r32
        out = [len(self.log)] + self.log
        begin, end = r(BEGIN), r(END)
        out.append((end - begin) // 4)
        text = ''
        self.slopes = set()

        def nid(n):
            return self.nodes.index(n) + 1 if n else 0
        for p in range(begin, end, 4):
            t = r(p)
            text += (self.cstr(r(t)).decode('latin1') if r(t) else '') + '\n'
            out += [r(t + o) for o in range(0x14, 0x3c, 4)]
            out.append(r(t + 0x3c) & 0xff)
            out += [r(t + o) for o in range(0x40, 0x60, 4)]
            self.slopes |= {len(out) - 2, len(out) - 1}
            out.append(r(t + 0x60) & 0xff)
            for o in (0x64, 0x70, 0x7c):
                out += [r(t + o), r(t + o + 4), nid(r(t + o + 8))]
            a, b = r(t + 8), r(t + 0xc)
            out.append((b - a) // 8 if a else 0)
            for s in range(a, b, 8):
                out += [r(s), nid(r(s + 4))]
        out.append(len(self.nodes))
        out += [r(n + 0x24) for n in self.nodes]
        return out, text


def compare(label, emu, probe, base, rel, file, names):
    want, wtext = emu.run(file, names)
    buf = (ctypes.c_uint32 * (1 << 18))()
    text = ctypes.create_string_buffer(1 << 18)
    n = probe.probe_pm_clutter(str(base).encode(), rel.encode(), file.encode(), '\n'.join(names).encode(), buf,
                               len(buf), text, len(text))
    if n < 0:
        raise SystemExit(f'{label}: the port cannot open it')
    got = list(buf[:n])
    if text.value.decode('latin1') != wtext:
        raise SystemExit(f'{label}: names differ\n exe {wtext!r}\n port {text.value!r}')
    if len(got) != len(want):
        raise SystemExit(f'{label}: {len(want)} words against {len(got)}\n exe {want[:30]}\n port {got[:30]}')
    for i, (a, b) in enumerate(zip(want, got)):
        if a != b:
            fa, fb = [struct.unpack('<f', struct.pack('<I', v))[0] for v in (a, b)]
            if i in emu.slopes and abs(fa - fb) < 1e-6:  # x87 fcos
                continue
            raise SystemExit(f'{label}: word {i}: exe {a:08x}, port {b:08x}\n exe {want[max(0, i - 6):i + 3]}')
    return wtext.count('\n')


def names_in(text):
    return sorted(set(re.findall(r"[A-Za-z0-9_]+\.flt", text)))


def tree(v):
    if isinstance(v, int):
        return struct.pack('<ii', 1, v)
    if isinstance(v, float):
        return struct.pack('<if', 2, v)
    if isinstance(v, str):
        b = v.encode('latin-1')
        return struct.pack('<iI', 3, len(b)) + b
    return struct.pack('<iI', 4, len(v) + 1) + b''.join(tree(x) for x in v)


CRAFTED = [
    ['node', ['a.flt'], 'substitute', [[1.0, 'a.flt'], [3, 'b.flt'], [2.0, 'nosuch.flt'], [2.0]],
     'rotation_range', [[10.0, 20, 30.0], [40, 50.0, 60]], 'min_slope', [5], 'max_slope', [45.0], 'align_normal', [],
     'scale_range', [2, 3.5], 'translate_uv_range', [[-1, 1.5], [2, -3]], 'far_fade_range', [[1, 2], [3, 4]],
     'OnWeaponHit', ['health', [0.5], 'anim', ['boom'], 'model', ['b.flt']],
     'OnCrater', ['health', [2], 'model', ['nosuch.flt']], 'OnCollide', ['health', [0.0], 'anim', ['bang']]],
    ['node', ['bare.flt']],
    ['nothing', [1]],
    ['node', ['zero.flt'], 'substitute', [[0.0, 'a.flt'], [0, 'b.flt']]],
    ['node', ['again.flt'], 'substitute', [[1.0, 'a.flt']], 'OnWeaponHit', ['anim', ['no_health']]],
    ['node', ['ints.flt'], 'scale_range', [1, 2], 'substitute', [[1, 'c.flt']]],
]


class World(test_pm_walk.Emulated):
    """0x5139d0 on a world loaded by the exe's own reader. The map at 0x639be0 is the exe's std::map,
    stubbed by a dict: only what goes in it is compared."""

    def __init__(self, world):
        super().__init__(world)
        self.native_stub(zr.OP_NEW, lambda c: self.alloc(c.arg(0)))
        self.cpu.import_code(g.IAT['_CIfmod'] if '_CIfmod' in g.IAT else test_pm_zar.IAT['_CIfmod'], CIFMOD)
        self.groups, self.values = {}, {}
        self.head = self.alloc(0x20)
        self.w32(MAP_HEAD, self.head)
        self.native_stub(MAP_FIND, self.find, 8)
        self.native_stub(MAP_INSERT, self.insert, 8)
        self.errors = []
        self.cpu.stubs[test_pm_zar.LOG_ERROR] = (self.error, 0)

    def error(self, c):
        self.errors.append(c.arg(0) << 16 | c.arg(2))
        return 0

    def find(self, c):
        key = self.r32(c.arg(1))
        self.w32(c.arg(0), self.values.get(key, self.head))
        return c.arg(0)

    def insert(self, c):
        key, value = self.r32(c.arg(1)), self.r32(c.arg(1) + 4)
        node = self.alloc(0x20)
        self.w32(node + 0x10, value)
        self.values[key] = node
        self.groups[key] = value
        return c.arg(0)

    def link(self, names):
        head = self.alloc(12)
        self.w32(head, head), self.w32(head + 4, head)
        prev = head
        for name in names:
            e = self.alloc(12)
            self.w32(e + 8, self.string(name))
            self.w32(prev, e), self.w32(e + 4, prev), self.w32(e, head), self.w32(head + 4, e)
            prev = e
        return head

    def run(self, settings, names):
        records = [self.alloc(0x88) for _ in settings]
        for r, name in zip(records, settings):
            self.w32(r, self.string(name))
        table = self.alloc(4 * max(len(records), 1))
        for i, r in enumerate(records):
            self.w32(table + 4 * i, r)
        self.w32(BEGIN, table), self.w32(END, table + 4 * len(records))
        self.w32(LIST_DIRTY, 0)
        self.w32(TEMPLATE_NAMES, self.link(names))
        self.call(LOAD_TEMPLATES)
        groups = []
        for value in self.groups.values():
            head = self.r32(value + 4)
            items, e = [], self.r32(head)
            while e != head:
                p = self.r32(e + 8)
                node = self.r32(p + 8)
                which = self.r32(p + 12)
                items.append((self.r32(p), self.r32(p + 4), self.index(node), records.index(which) + 1 if which else 0))
                e = self.r32(e)
            groups.append(items)
        return self.errors, groups


    def scene(self, verts, uvs, kids, polys, mats, mat_flags, wrap_node):
        uc, w = self.cpu._uc, self.w32

        def node(name, flags=0x10):
            n = self.alloc(0xd4)
            uc.mem_write(n, name.encode() + b'\0')
            w(n + 0x34, 5), w(n + 0x24, flags)
            return n
        children = []
        for i, k in enumerate(kids):
            c = node(f'c{i}', 0x80010)
            data = self.alloc(0x100)
            uc.mem_write(data + 0x84, struct.pack('<3f', *k))
            w(c + 0x38, data)
            children.append(c)
        model, poly, material = self.alloc(0x68), self.alloc(0x28), self.alloc(0x2c)
        vert, uv, index = self.alloc(12 * len(verts)), self.alloc(8 * len(verts)), self.alloc(4 * len(verts))
        for i, v in enumerate(verts):
            uc.mem_write(vert + 12 * i, struct.pack('<3f', *v))
            uc.mem_write(uv + 8 * i, struct.pack('<2f', *uvs[i]))
            w(index + 4 * i, i)
        w(model + 0x10, polys), w(model + 0x34, poly), w(model + 0x38, vert)
        mlist, ulist = self.alloc(8), self.alloc(8)
        w(mlist, material), w(mlist + 4, material), w(ulist, uv), w(ulist + 4, uv)
        w(poly, len(verts)), w(poly + 8, index), w(poly + 0x10, mats), w(poly + 0x14, mlist), w(poly + 0x18, ulist)
        uc.mem_write(material + 1, bytes([mat_flags]))
        tmpl = node('tmpl')
        owner = tmpl
        if wrap_node:
            owner = node('holder')
            kid = self.alloc(4)
            w(kid, owner), w(tmpl + 0x5c, kid)
            uc.mem_write(tmpl + 0x56, struct.pack('<h', 1))
        w(owner + 0x3c, model)
        table = self.alloc(4 * max(len(children), 1))
        for i, c in enumerate(children):
            w(table + 4 * i, c)
        w(owner + 0x5c, table)
        uc.mem_write(owner + 0x56, struct.pack('<h', len(children)))
        item, head = self.alloc(0x10), self.alloc(12)
        uc.mem_write(item, struct.pack('<IIII', tmpl, 0, 0, 1))
        uc.mem_write(head, struct.pack('<III', item, item, 0))
        w(LISTS + 24, head)
        self.groups, self.values, self.errors = {}, {}, []
        record = self.alloc(0x88)
        w(record, self.string('c1'))
        table2 = self.alloc(4)
        w(table2, record), w(BEGIN, table2), w(END, table2 + 4)
        w(LIST_DIRTY, 0), w(TEMPLATE_NAMES, self.link(['tmpl']))
        self.call(LOAD_TEMPLATES)
        out = [len(self.errors)] + self.errors
        for value in self.groups.values():
            head, e = self.r32(value + 4), None
            e = self.r32(head)
            while e != head:
                p = self.r32(e + 8)
                out += [self.r32(p), self.r32(p + 4), children.index(self.r32(p + 8)), int(self.r32(p + 12) != 0)]
                e = self.r32(e)
        return out


QUAD = [(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)]
QUAD_UV = [(-0.5, -0.5), (1.5, -0.5), (1.5, 1.5), (-0.5, 1.5)]
QUAD_KIDS = [(0, 0, 0), (0.5, 0, 0.25), (0, 3, 0), (0, 9, 0.5), (5, 0, 0), (-0.75, 0, -0.75), (0.99, 0, 0.99),
             (-0.5, 0, 0.5), (0.5, 0, -0.5), (1, 0, 1), (-0.25, 0, 0.1), (0.125, 0, -0.875)]
# Tilted and away from the origin, u and v over several wraps.
TILT = [(10, 10, 10), (12, 11, 10), (12, 13, 12), (10, 12, 12)]
TILT_UV = [(-3.25, 2.5), (4.5, 2.5), (4.5, -2.75), (-3.25, -2.75)]
TILT_KIDS = [(11, 11, 11), (10.5, 10.75, 10.5), (11.5, 12, 11.25), (0, 0, 0), (11, 15, 11), (50, 50, 50)]
SCENES = [
    ('quad', QUAD, QUAD_UV, QUAD_KIDS, 1, 1, 1, 0),
    ('tilted', TILT, TILT_UV, TILT_KIDS, 1, 1, 1, 0),
    ('whole numbers', QUAD, [(-2, -2), (2, -2), (2, 2), (-2, 2)], [(-0.5, 0, -0.5), (0.5, 0, 0.5), (0, 0, 0)], 1, 1, 1, 0),
    ('in a node without a model', QUAD, QUAD_UV, QUAD_KIDS, 1, 1, 1, 1),
    ('two polygons', QUAD, QUAD_UV, QUAD_KIDS, 2, 1, 1, 0),
    ('two materials', QUAD, QUAD_UV, QUAD_KIDS, 1, 2, 1, 0),
    ('untextured', QUAD, QUAD_UV, QUAD_KIDS, 1, 1, 0, 0),
    ('no children', QUAD, QUAD_UV, [], 1, 1, 1, 0),
]


def compare_scenes(probe):
    emu = World(str(g.ZBD / 'c1' / 'gamez.zbd'))
    for label, verts, uvs, kids, polys, mats, flags, wrap_node in SCENES:
        want = emu.scene(verts, uvs, kids, polys, mats, flags, wrap_node)
        buf = (ctypes.c_uint32 * 4096)()
        vf = (ctypes.c_float * (3 * len(verts)))(*[x for v in verts for x in v])
        uf = (ctypes.c_float * (2 * len(uvs)))(*[x for v in uvs for x in v])
        kf = (ctypes.c_float * max(3 * len(kids), 1))(*[x for v in kids for x in v])
        n = probe.probe_pm_clutter_scene(vf, uf, len(verts), kf, len(kids), polys, mats, flags, wrap_node, buf, len(buf))
        got = list(buf[:n])
        if len(want) != len(got):
            raise SystemExit(f'scene {label}: {len(want)} words against {len(got)}\n exe {want}\n port {got}')
        at = 1 + want[0]
        if want[:at] != got[:at]:
            raise SystemExit(f'scene {label}: errors exe {want[:at]}, port {got[:at]}')
        for i in range(at, len(want), 4):
            a, b = want[i:i + 4], got[i:i + 4]
            fa, fb = struct.unpack('<2f', struct.pack('<2I', *a[:2])), struct.unpack('<2f', struct.pack('<2I', *b[:2]))
            if a[2:] != b[2:] or max(abs(fa[0] - fb[0]), abs(fa[1] - fb[1])) > 2e-6:
                raise SystemExit(f'scene {label}: piece exe {fa} {a[2:]}, port {fb} {b[2:]}')
    print(f'{len(SCENES)} crafted scenes identical')


def compare_world(probe, world, settings, names):
    emu = World(str(g.ZBD / world / 'gamez.zbd'))
    want_e, want = emu.run(settings, names)
    buf = (ctypes.c_uint32 * (1 << 20))()
    n = probe.probe_pm_clutter_nodes(str(g.ZBD / world / 'gamez.zbd').encode(), '\n'.join(settings).encode(), '\n'.join(names).encode(), buf, len(buf))
    if n < 0:
        raise SystemExit(f'{world}: the port cannot load it')
    w = list(buf[:n])
    got_e = w[1:1 + w[0]]
    at = 1 + w[0]
    count = w[at]
    at += 1
    got = []
    for _ in range(count):
        size = w[at]
        at += 1
        got.append([tuple(w[at + 4 * i:at + 4 * i + 4]) for i in range(size)])
        at += 4 * size
    if got_e != want_e:
        raise SystemExit(f'{world}: errors exe {want_e}, port {got_e}')
    key = lambda items: items[0][2] if items else -1  # noqa: E731
    want, got = sorted(want, key=key), sorted(got, key=key)
    if len(want) != len(got):
        raise SystemExit(f'{world}: {len(want)} textures against {len(got)}')
    pieces = 0
    for a, b in zip(want, got):
        if len(a) != len(b):
            raise SystemExit(f'{world}: {len(a)} pieces against {len(b)}')
        for x, y in zip(a, b):
            pieces += 1
            fx = struct.unpack('<2f', struct.pack('<2I', *x[:2]))
            fy = struct.unpack('<2f', struct.pack('<2I', *y[:2]))
            if x[2:] != y[2:] or max(abs(fx[0] - fy[0]), abs(fx[1] - fy[1])) > 2e-6:
                raise SystemExit(f'{world}: piece of node {x[2]}: exe {fx} {x[2:]}, port {fy} {y[2:]}')
    return len(want), pieces, len(want_e)


def script_names():
    data = (PM / 'zbd' / 'interp.zbd').read_bytes()
    return sorted({m.group(1).decode() for m in re.finditer(rb'AddClutterTemplates\0([A-Za-z0-9_]+)', data)}) + ['nosuch_clutter']


def main():
    probe = test_pm_zar.load_pm_probe()
    probe.probe_pm_clutter_nodes.argtypes = [ctypes.c_char_p] * 3 + [ctypes.c_void_p, ctypes.c_int]
    probe.probe_pm_clutter_scene.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p] + [ctypes.c_int] * 5 + [ctypes.c_void_p, ctypes.c_int]
    probe.probe_pm_clutter.argtypes = [ctypes.c_char_p] * 4 + [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    runs = templates = 0
    for world in ('c1', 'c2', 'c3', 'c4'):
        rel = f'zbd/{world}/reader.zbd'
        data = (PM / rel).read_bytes()
        top = next(zrd.node(data[s:s + n], 0)[0] for name, s, n in zrd.entries(data) if name == 'templates.zrd')
        names = names_in(repr(top))
        emu = Emu(PM / rel, rel)
        for scene in (names, [n for i, n in enumerate(names) if i % 3]):
            templates += compare(f'{world} {len(scene)} nodes', emu, probe, PM, rel, 'templates.zrd', scene)
            runs += 1
    with tempfile.TemporaryDirectory() as tmp:
        body = tree(CRAFTED)
        entry = struct.pack('<II', 0, len(body)) + b'templates.zrd'.ljust(64, b'\0')
        path = Path(tmp) / 'crafted.zbd'
        path.write_bytes(body + entry.ljust(test_pm_zar.ENTRY, b'\0') + struct.pack('<III', 2, 1, test_pm_zar.crc32(body)))
        emu = Emu(path, 'crafted.zbd')
        for scene in (['a.flt', 'b.flt', 'c.flt'], ['b.flt'], []):
            compare(f'crafted {scene}', emu, probe, tmp, 'crafted.zbd', 'templates.zrd', scene)
            runs += 1
    print(f'ok: {runs} settings runs identical, {templates} shipped templates')
    names = script_names()
    for world in ('c1', 'c2', 'c3', 'c4'):
        data = (PM / 'zbd' / world / 'reader.zbd').read_bytes()
        top = next(zrd.node(data[s:s + n], 0)[0] for name, s, n in zrd.entries(data) if name == 'templates.zrd')
        settings = [e[1][0] for e in top]
        textures, pieces, errors = compare_world(probe, world, settings, names)
        print(f'{world}: {textures} textures, {pieces} pieces, {errors} errors, from {names}')
    compare_scenes(probe)


if __name__ == '__main__':
    main()
