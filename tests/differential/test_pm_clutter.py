"""PM's clutter settings (pm/gamez/zclass/cls_clutter, 0x513030 and 0x513310) against PM's exe, on
the four worlds' templates.zrd and crafted entries, with every named node in the list and with every
third one missing. The anim lookup and the hit callbacks are stubbed and logged in both.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_clutter.py

Compared: the log's order, every field of every record, every node's flags. Rotations bit for bit;
slopes go through x87 fcos, within 1e-6.
"""
import ctypes
import math
import random
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
import test_pm_world_box  # noqa: E402
import test_pm_zreader as zr  # noqa: E402

sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import zrd  # noqa: E402

READ, ANIM, WATCH, WATCH2, ZERR, STRDUP_IAT = 0x513030, 0x524b50, 0x5a4d80, 0x5a4dc0, 0x406cb0, '_strdup'
LISTS, LIST_DIRTY, BEGIN, END, CAP = 0x60db00, 0x60db84, 0x639bf4, 0x639bf8, 0x639bfc
ON_HIT, ON_COLLIDE, ANIM_START = 0x513c60, 0x513cf0, 0x5216e0
LAY, GROUND, BOX, CRATER, SKIP_GROUND = 0x513a30, 0x4ff8d0, 0x5043d0, 0x513d90, 0x639c1c
TEXTURE_NAME = 0xc  # in PM's 0x2c-byte entry
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


class Differences:
    """How far u and v are from the exe's: the worst and how many are bit-identical."""

    def __init__(self):
        self.worst, self.same, self.total, self.at = 0.0, 0, 0, None

    def note(self, ra, rb, fa, fb):
        for i in range(2):
            self.total += 1
            self.same += ra[i] == rb[i]
            if abs(fa[i] - fb[i]) > self.worst:
                self.worst, self.at = abs(fa[i] - fb[i]), (fa[i], fb[i], f'{ra[i]:08x}', f'{rb[i]:08x}')


DIFF = Differences()


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
        for key, value in self.groups.items():
            head = self.r32(value + 4)
            items, e = [self.cstr(key + TEXTURE_NAME).decode('latin1')], self.r32(head)
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
            DIFF.note(a[:2], b[:2], fa, fb)
            if a != b:
                raise SystemExit(f'scene {label}: piece exe {fa} {a[2:]}, port {fb} {b[2:]}')
    print(f'{len(SCENES)} crafted scenes identical')


def bits(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


def ground_hits(x, y, z):
    """The same made-up ground on both sides: 0..3 hits, some above the point, by the point's bits."""
    h = ((x * 31 + z * 17) & 0xffffffff) >> 3
    n = h % 4
    fy = struct.unpack('<f', struct.pack('<I', y))[0]
    return [fy - 1.5 if h >> (8 + i) & 1 else fy - 4.0 for i in range(n)]


class Ulps:
    """The rotated records' nine words; the aligned ones (no trig) must match exactly."""

    def __init__(self):
        self.same = self.total = self.exact = 0
        self.worst = self.last = 0.0

    def note(self, a, b):
        self.total += 1
        self.same += a == b
        self.last = abs(a - b)
        self.worst = max(self.worst, self.last)


ULPS = Ulps()


class Laying(World):
    """The whole load in the exe: 0x513030, 0x5139d0, 0x513a30."""

    def __init__(self, world):
        super().__init__(str(g.ZBD / world / 'gamez.zbd'))
        rel = f'zbd/{world}/reader.zbd'
        self.files[test_pm_zar.key(rel)] = bytearray((PM / rel).read_bytes())
        cpu = self.cpu
        cpu.stub_import(test_pm_zar.IAT['_stricmp'], self.stricmp)
        cpu.stub_import(test_pm_zar.IAT['_splitpath'], lambda c: self.splitpath(c))
        cpu.stub_import(test_pm_zar.IAT['_strdup'], self.strdup)
        cpu.stub_import(test_pm_zar.IAT['rand'], self.rand)
        self.native_stub(zr.OP_DELETE, lambda c: 0)
        self.native_stub(zr.FIND_PATH, lambda c: 0)
        self.call(zr.LIST_CTOR)
        if not self.call(zr.OPEN_ARCHIVE, args=[self.string(rel.replace('/', '\\')), 1]):
            raise SystemExit(f'{rel}: PM does not open it')
        self.seed = 1
        self.rands = 0
        self.ground = []
        self.native_stub(ANIM, lambda c: 0)
        self.native_stub(WATCH, lambda c: 1)
        self.native_stub(WATCH2, lambda c: 0)
        self.native_stub(GROUND, self.ground_stub)

    stricmp = zr.Emulated.stricmp
    splitpath = zr.Emulated.splitpath

    def rand(self, c):
        self.seed = (self.seed * 214013 + 2531011) & 0xffffffff
        self.rands += 1
        return (self.seed >> 16) & 0x7fff

    def ground_stub(self, c):
        hits = ground_hits(c.arg(1), c.arg(2), c.arg(3))
        out = c.arg(4)
        self.cpu._uc.mem_write(out, bytes([len(hits)]))
        for i, h in enumerate(hits):
            self.cpu._uc.mem_write(out + 0x14 + 0x2c * i, struct.pack('<f', h))
        return 0

    def crater(self, world, x, y, z, radius):
        p = self.alloc(12)
        self.cpu._uc.mem_write(p, struct.pack('<3f', x, y, z))
        self.call(CRATER, args=[world, p, bits(radius)])
        data = self.r32(world + 0x38)
        cols, rows, table = self.i32(data + 0x98), self.i32(data + 0x9c), self.r32(data + 0xa0)
        out = []
        for zi in range(rows):
            for xi in range(cols):
                cell = self.r32(table + 4 * zi) + 0x58 * xi
                items = self.r32(cell + 0x44)
                out += [self.r32(items + 0x5c * i + 0x48) for i in range(self.i32(cell + 0x40))]
        return out

    def drawn_nodes(self):
        """Every record's node."""
        data = self.r32(self.world_node + 0x38)
        cols, rows, table = self.i32(data + 0x98), self.i32(data + 0x9c), self.r32(data + 0xa0)
        seen = set()
        for z in range(rows):
            for x in range(cols):
                cell = self.r32(table + 4 * z) + 0x58 * x
                items = self.r32(cell + 0x44)
                for i in range(self.i32(cell + 0x40)):
                    seen.add(self.index(self.r32(items + 0x5c * i)))
        return sorted(seen)

    def lay(self, names, skip_ground=0):
        uc = self.cpu._uc
        uc.mem_write(SKIP_GROUND, bytes([skip_ground]))
        self.w32(BEGIN, 0), self.w32(END, 0), self.w32(CAP, 0)
        self.w32(LIST_DIRTY, 0)
        self.call(READ, args=[self.string('templates.zrd')])
        self.w32(TEMPLATE_NAMES, self.link(names))
        self.call(LOAD_TEMPLATES)
        world = self.r32(self.r32(self.camera + 0x38))  # the camera's first node
        self.call(LAY, args=[world])
        self.world_node = world
        begin, end = self.r32(BEGIN), self.r32(END)
        records = [self.r32(p) for p in range(begin, end, 4)]
        self.aligned = {i + 1 for i, r in enumerate(records) if self.r32(r + 0x3c) & 0xff}
        out = []
        data = self.r32(world + 0x38)
        cols, rows, table = self.i32(data + 0x98), self.i32(data + 0x9c), self.r32(data + 0xa0)
        for z in range(rows):
            for x in range(cols):
                cell = self.r32(table + 4 * z) + 0x58 * x
                count = self.i32(cell + 0x40)
                if not count:
                    continue
                items = self.r32(cell + 0x44)
                for i in range(count):
                    r = items + 0x5c * i
                    w = [self.r32(r + o) for o in range(0, 0x5c, 4)]
                    node = self.index(w[0])
                    which = records.index(w[0x58 // 4]) + 1 if w[0x58 // 4] else 0
                    out.append((x, z, node) + tuple(w[1:0x34 // 4 + 3]) + (w[0x40 // 4] & 0xff, w[0x48 // 4], w[0x50 // 4], w[0x54 // 4], which))
        return out, self.rands, 0, self.rand(None)


# Class, flags, +0x2c, +0x30, data flags, data +4, x, y, z, 0, each polygon's zone_set.
A, C = 4, 0x80004
LAY_ROWS = [
    (5, A, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0),                # moved by nothing
    (5, A, 0, 255, 0, 0, 20, 3, 4, 0, 0x0701, 0),          # into the next cell, a zone on the polygon
    (5, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0),                # inactive
    (5, A, 0x4000000, 255, 0, 0, 0, 0, 0, 0, 0, 0),        # +0x2c's bit
    (6, A, 0, 255, 0, 1.0, 0, 0, 0, 0, 0x0a01, 0),         # a LoD out of range
    (6, A, 0, 255, 0, -1.0, 0, 0, 0, 0, 0x0901, 0),        # a LoD in range
    (5, A, 0, 3, 0, 0, 1, 0, 1, 0, 0, 0),                  # the node's zone
    (5, A, 0, 255, 0, 0, 1000, 0, 1000, 0, 0xff01, 0x0501),  # past the grid; the second polygon's zone
    (5, A, 0, 255, 0, 0, -100, 0, -100, 0, 0xff01, 0xff01),  # before the grid; no zone set
    (5, C, 0, 255, 0, 0, 0, 5, 8, 0, 0, 0),                # the cached matrix
    (5, C, 0, 255, 0x20, 0, 3, 0, 3, 0, 0, 0),             # to be remade
    (5, A, 0, 255, 8, 0, 9, 9, 9, 0, 0, 0),                # the parent's matrix
]


def scene_lay(emu):
    uc, w = emu.cpu._uc, emu.w32

    def f(*v):
        return struct.pack(f'<{len(v)}f', *v)
    verts, uvs = emu.alloc(48), emu.alloc(32)
    uc.mem_write(verts, f(0, 0, 0, 8, 0, 0, 8, 0, 8, 0, 0, 8))
    uc.mem_write(uvs, f(0, 0, 2, 0, 2, 2, 0, 2))
    ulist = emu.alloc(8)
    w(ulist, uvs), w(ulist + 4, uvs)
    tri = [emu.alloc(12), emu.alloc(12)]
    uc.mem_write(tri[0], struct.pack('<3I', 0, 1, 2))
    uc.mem_write(tri[1], struct.pack('<3I', 0, 2, 3))
    tex, other = emu.alloc(0x2c), emu.alloc(0x2c)
    mat, oth = emu.alloc(0x2c), emu.alloc(0x2c)
    w(mat + 0x10, tex), w(oth + 0x10, other)
    mlists = [emu.alloc(8), emu.alloc(8)]
    w(mlists[0], mat), w(mlists[0] + 4, oth), w(mlists[1], oth), w(mlists[1] + 4, mat)
    children = emu.alloc(4 * len(LAY_ROWS))
    for i, r in enumerate(LAY_ROWS):
        n, data, model, polys = emu.alloc(0xd4), emu.alloc(0x100), emu.alloc(0x68), emu.alloc(0x50 + 0x28)
        w(n + 0x34, int(r[0])), w(n + 0x24, r[1]), w(n + 0x2c, r[2]), uc.mem_write(n + 0x30, bytes([r[3]]))
        if r[0] == 5:
            w(data, r[4])
        else:
            uc.mem_write(data + 4, f(r[5]))
        m = f(1, 0, 0, 0, 1, 0, 0, 0, 1, r[6], r[7], r[8])
        uc.mem_write(data + 0x30, m), uc.mem_write(data + 0x60, m)
        w(n + 0x38, data), w(n + 0x3c, model), w(model + 0x10, 2), w(model + 0x34, polys), w(model + 0x38, verts)
        for p in range(2):
            q = polys + 0x28 * p
            w(q, 3), w(q + 8, tri[p]), w(q + 0x10, 2), w(q + 0x14, mlists[p]), w(q + 0x18, ulist), w(q + 0x24, r[10 + p])
        w(children + 4 * i, n)
    world, wd = emu.alloc(0xd4), emu.alloc(0x100)
    w(world + 0x38, wd), w(world + 0x5c, children)
    uc.mem_write(world + 0x56, struct.pack('<h', len(LAY_ROWS)))
    uc.mem_write(wd + 0x84, f(0.0625, 0.0625))
    w(wd + 0x98, 2), w(wd + 0x9c, 2)
    rows = emu.alloc(8)
    cells = [emu.alloc(0x58 * 2), emu.alloc(0x58 * 2)]
    w(rows, cells[0]), w(rows + 4, cells[1]), w(wd + 0xa0, rows)
    piece_node = emu.alloc(0xd4)
    piece = emu.alloc(0x10)
    uc.mem_write(piece, f(0.5, 0.25))
    w(piece + 8, piece_node)
    head, entry, group, found = emu.alloc(12), emu.alloc(12), emu.alloc(12), emu.alloc(0x20)
    w(head, entry), w(head + 4, entry), w(entry, head), w(entry + 4, head), w(entry + 8, piece)
    w(group + 4, head), w(found + 0x10, group)
    emu.values = {tex: found}
    emu.call(LAY, args=[world])
    out, records = [], 0
    for z in range(2):
        for x in range(2):
            cell = cells[z] + 0x58 * x
            count = emu.i32(cell + 0x40)
            out += [emu.r32(cell), count]
            items = emu.r32(cell + 0x44)
            for i in range(count):
                r = items + 0x5c * i
                records += 1
                out += [x, z, emu.r32(r + 0x40) & 0xff] + [emu.r32(r + 4 + 4 * k) for k in range(9, 12)]
    return [records] + out


def compare_scene_lay(probe, emu):
    want = scene_lay(emu)
    buf = (ctypes.c_uint32 * 8192)()
    rows = (ctypes.c_double * (12 * len(LAY_ROWS)))(*[float(x) for r in LAY_ROWS for x in r])
    n = probe.probe_pm_clutter_scene_lay(rows, len(LAY_ROWS), buf, len(buf))
    got = list(buf[:n])
    if want != got:
        i = next((k for k, (a, b) in enumerate(zip(want, got)) if a != b), min(len(want), len(got)))
        raise SystemExit(f'crafted laying: word {i} of {len(want)} and {len(got)}\n exe {want[:i + 7]}\n port {got[:i + 7]}')
    print(f'crafted laying identical: {want[0]} records')


def compare_trees(probe, emu, world):
    """Every tree the frames built, and each drawn node's zone, as the exe left them."""
    data = emu.r32(emu.world_node + 0x38)
    cols, rows, table = emu.i32(data + 0x98), emu.i32(data + 0x9c), emu.r32(data + 0xa0)
    want = []
    for z in range(rows):
        for x in range(cols):
            cell = emu.r32(table + 4 * z) + 0x58 * x
            tree, count = emu.r32(cell + 0x50), emu.i32(cell + 0x54)
            if not tree:
                continue
            want += [x, z, count]
            for i in range(count):
                n = tree + 0x40 * i
                nrec, child = emu.i32(n + 0x34), emu.i32(n + 0x3c)
                want += [nrec, child & 0xffffffff]
                if nrec or child >= 0:
                    want += [emu.r32(n + o) for o in (0, 4, 8, 0xc, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30)]
                items = emu.r32(n + 0x38)
                want += [emu.r32(items + 4 * k) for k in range(nrec)]
    buf = (ctypes.c_uint32 * (1 << 22))()
    n = probe.probe_pm_clutter_trees(buf, len(buf))
    got = list(buf[:n])
    if len(want) != len(got):
        raise SystemExit(f'{world}: trees {len(want)} words, the port {len(got)}')
    worst = 0.0
    for i, (a, b) in enumerate(zip(want, got)):
        if a != b:
            fa, fb = struct.unpack('<2f', struct.pack('<2I', a, b))
            worst = max(worst, abs(fa - fb) / max(1.0, abs(fb)))
            if abs(fa - fb) > 1e-6 * max(1.0, abs(fb)):
                raise SystemExit(f'{world}: trees word {i}: exe {a:08x} {fa}, port {b:08x} {fb}')
    TREES[0] += len(want)
    TREES[1] = max(TREES[1], worst)
    zones = {}
    want_z = []
    for r in emu.drawn_nodes():
        want_z += [r, emu.cpu._uc.mem_read(emu.node(r) + 0x30, 1)[0]]
    zbuf = (ctypes.c_uint32 * (len(want_z) + 2))(*want_z)
    if probe.probe_pm_node_zones(zbuf, len(want_z) // 2) != 0:
        raise SystemExit(f'{world}: a drawn node\'s zone differs: {list(zbuf)[:12]}')


TREES = [0, 0.0]
NEAR_FAR = 8


def compare_laying(probe, world, names, skip_ground=0):
    emu = Laying(world)
    want, rands, changed, next_rand = emu.lay(names, skip_ground)
    if world == 'c1':
        compare_scene_lay(probe, emu)
        compare_hits(probe, emu)
    buf = (ctypes.c_uint32 * (1 << 25))()
    rel = f'zbd/{world}/reader.zbd'
    n = probe.probe_pm_clutter_lay(str(g.ZBD / world / 'gamez.zbd').encode(), str(PM).encode(), rel.encode(), '\n'.join(names).encode(), skip_ground, buf, len(buf))
    if n < 0:
        raise SystemExit(f'{world}: the port cannot load it ({n})')
    w = list(buf[:n])
    got_next, got_changed, count = w[0], w[1], w[2]
    size = len(want[0]) if want else 22
    got = [tuple(w[3 + size * i:3 + size * (i + 1)]) for i in range(count)]
    if len(want) != len(got):
        raise SystemExit(f'{world}: {len(want)} records against {len(got)}')
    for i, (a, b) in enumerate(zip(want, got)):
        for k, (x, y) in enumerate(zip(a, b)):
            turned = 3 <= k < 12 and a[-1] not in emu.aligned  # rotate_y, x, z: x87 fsin, fcos against the port's
            if turned:
                ULPS.note(*struct.unpack('<2f', struct.pack('<2I', x, y)))
            elif 3 <= k < 12:
                ULPS.exact += 1
            if x != y and not (turned and ULPS.last <= 2e-7):
                raise SystemExit(f'{world}: record {i} field {k}: exe {x:08x}, port {y:08x}\n exe {a}\n port {b}')
    if next_rand != got_next or changed != got_changed:
        raise SystemExit(f'{world}: next rand {next_rand} and {got_next}, cell changes {changed} and {got_changed}')
    # Frames over the laid records, before the craters clear them: the walk's whole log, as test_pm_walk's.
    frame_buf = ctypes.create_string_buffer(1 << 24)
    rng = random.Random(0x50a1e0)
    # The world's update first, as the game runs before it draws: every cell the laying queued.
    if want:
        test_pm_world_box.compare_cells(probe, emu, world, 1e-6)
    # A crater first, so some records are gone from the trees and the draws.
    if want:
        x, _, z = struct.unpack('<3f', struct.pack('<3I', *want[len(want) // 4][12:15]))
        emu.crater(emu.world_node, x, 0.0, z, 60.0)
        probe.probe_pm_clutter_crater(x, 0.0, z, 60.0, (ctypes.c_uint32 * (len(want) + 16))(), len(want) + 16)
    faded = [r for r in want if struct.unpack('<f', struct.pack('<I', r[16]))[0] < 1e30]
    for k in range(FRAMES + NEAR_FAR if want else 0):
        chosen = rng.choice(faded or want)
        place = struct.unpack('<3f', struct.pack('<3I', *chosen[12:15]))
        if k < 3:  # at the crater, looking down: its gone records in the draw and the tree
            pos, rot = (x + rng.uniform(-40, 40), rng.uniform(40, 80), z + rng.uniform(-40, 40)), (-1.2, rng.uniform(-3.1, 3.1), 0.0)
        elif k < FRAMES:
            pos = (place[0] + rng.uniform(-30, 30), place[1] + rng.uniform(10, 40), place[2] + rng.uniform(-30, 30))
            rot = (rng.uniform(-0.5, 0.2), rng.uniform(-3.1, 3.1), 0.0)
        else:  # where this record's fade is all but out: under 0.004 and just over it
            near_sq, far_sq, scale = struct.unpack('<3f', struct.pack('<3I', *chosen[15:18]))
            d2 = far_sq - rng.choice((0.002, 0.003, 0.0045, 0.006)) / (scale or 1.0)
            yaw = rng.uniform(-3.1, 3.1)
            d = max(d2, 0.0) ** 0.5
            pos = (place[0] + d * math.sin(yaw), place[1], place[2] + d * math.cos(yaw))
            rot = (0.0, yaw, 0.0)
        log, lights = emu.frame(pos, rot, k + 2, 0.03, 0)
        n = probe.probe_pm_walk_frame(test_pm_walk.fbits(*pos), test_pm_walk.fbits(*rot), k + 2, 0.03, 0, frame_buf, len(frame_buf))
        got_log = frame_buf.raw[:n]
        rec = test_pm_walk.RECORD + 4 * len(lights)
        ours = (n - 4 * len(lights)) // rec
        if ours != len(log):
            a = [struct.unpack_from('<i', got_log, i * rec)[0] for i in range(ours)]
            b = [struct.unpack_from('<i', r)[0] for r in log]
            first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
            raise SystemExit(f'{world} frame {k}: {ours} draws, the exe {len(log)}; first apart {first}: ours {a[first:first + 3]}, exe {b[first:first + 3]}')
        for i, r in enumerate(log):
            o = got_log[i * rec:(i + 1) * rec]
            if o[:8] != r[:8]:
                raise SystemExit(f'{world} frame {k} draw {i}: node, mask {struct.unpack("<iI", o[:8])}, exe {struct.unpack("<iI", r[:8])}')
            for j in list(range(12)) + [12]:
                a2, b2 = struct.unpack_from('<f', o, 8 + 4 * j)[0], struct.unpack_from('<f', r, 8 + 4 * j)[0]
                FRAME_DIFF[0] = max(FRAME_DIFF[0], abs(a2 - b2) / max(1.0, abs(b2)))
                if abs(a2 - b2) > 1e-6 * max(1.0, abs(b2)):
                    raise SystemExit(f'{world} frame {k} draw {i} word {j}: {a2}, exe {b2}')
        FRAME_DIFF[1] += len(log)
    compare_trees(probe, emu, world)
    # A crater where a tree stands, the world's update (which drops the tree and takes the flags),
    # then frames over it again: the tree built anew, without the gone records.
    if want:
        place = struct.unpack('<3f', struct.pack('<3I', *want[len(want) // 2][12:15]))
        emu.crater(emu.world_node, place[0], 0.0, place[2], 30.0)
        probe.probe_pm_clutter_crater(place[0], 0.0, place[2], 30.0, (ctypes.c_uint32 * (len(want) + 16))(), len(want) + 16)
        # The crater's caller queues the cell; its own cell is enough here.
        cx, cz = want[len(want) // 2][:2]
        d = emu.r32(emu.world_node + 0x38)
        for q in range(2):
            pos, rot = (place[0], 60.0, place[2]), (-1.2, q * 1.5, 0.0)
            log, _ = emu.frame(pos, rot, FRAMES + NEAR_FAR + 2 + q, 0.03, 0)
            n = probe.probe_pm_walk_frame(test_pm_walk.fbits(*pos), test_pm_walk.fbits(*rot), FRAMES + NEAR_FAR + 2 + q, 0.03, 0, frame_buf, len(frame_buf))
        cell = emu.r32(emu.r32(d + 0xa0) + 4 * cz) + 0x58 * cx
        items, pokes = emu.r32(cell + 0x44), random.Random(cx * 100 + cz)
        for i in range(min(12, emu.i32(cell + 0x40))):
            m = [pokes.choice((-0.8, -0.3, 0.4, 0.9)) for _ in range(3)]
            emu.cpu._uc.mem_write(items + 0x5c * i + 4 + 4, struct.pack('<f', m[0]))
            emu.cpu._uc.mem_write(items + 0x5c * i + 4 + 16, struct.pack('<f', m[1]))
            emu.cpu._uc.mem_write(items + 0x5c * i + 4 + 28, struct.pack('<f', m[2]))
            gone = i == 2
            if gone:
                emu.w32(items + 0x5c * i + 0x48, emu.r32(items + 0x5c * i + 0x48) | 1)
                emu.cpu._uc.mem_write(items + 0x5c * i + 4 + 40, struct.pack('<f', 5000.0))
            probe.probe_pm_clutter_poke(cx, cz, i, *m, int(gone))
        emu.call(BOX, args=[emu.world_node, d, cell])
        probe.probe_pm_cell_touch(cx, cz)
        test_pm_world_box.compare_cells(probe, emu, world, 1e-6)
        if emu.i32(cell + 0x50) != 0:
            raise SystemExit(f'{world}: the update left the tree')
        pos, rot = (place[0], 60.0, place[2]), (-1.2, 0.5, 0.0)
        log, lights = emu.frame(pos, rot, FRAMES + NEAR_FAR + 5, 0.03, 0)
        n = probe.probe_pm_walk_frame(test_pm_walk.fbits(*pos), test_pm_walk.fbits(*rot), FRAMES + NEAR_FAR + 5, 0.03, 0, frame_buf, len(frame_buf))
        rec = test_pm_walk.RECORD + 4 * len(lights)
        ours = [struct.unpack_from('<i', frame_buf.raw, i * rec)[0] for i in range((n - 4 * len(lights)) // rec)]
        theirs = [struct.unpack_from('<i', r)[0] for r in log]
        if ours != theirs:
            raise SystemExit(f'{world}: after the update, draws differ: ours {ours[:12]}, exe {theirs[:12]}')
        compare_trees(probe, emu, world)
    # Craters: a few records' own places, in the grid, small and large.
    if want and not skip_ground:
        cols = emu.i32(emu.r32(emu.world_node + 0x38) + 0x98)
        for i, radius in ((0, 5.0), (len(want) // 2, 40.0), (len(want) - 1, 100.0), (len(want) // 3, 0.5),
                          (-1, 60.0), (-2, 60.0), (-3, 9000.0)):
            # Past the world's corners too: the cell lookup keeps a point inside the area.
            x, _, z = struct.unpack('<3f', struct.pack('<3I', *want[i][12:15])) if i >= 0 else \
                {-1: (-5000.0, 0.0, 5000.0), -2: (9000.0, 0.0, -9000.0), -3: (0.0, 0.0, 0.0)}[i]
            flags = emu.crater(emu.world_node, x, 0.0, z, radius)
            out = (ctypes.c_uint32 * (len(want) + 16))()
            m = probe.probe_pm_clutter_crater(x, 0.0, z, radius, out, len(out))
            if m != len(flags) or list(out[:m]) != flags:
                bad = next((k for k in range(min(m, len(flags))) if out[k] != flags[k]), -1)
                raise SystemExit(f'{world}: crater at {x} {z} radius {radius}: record {bad} of {m}')
            CRATERS.append(sum(1 for f in flags if f & 4))
    return len(want), rands


CRATERS = []
FRAMES = 12
FRAME_DIFF = [0.0, 0]


def compare_hits(probe, emu):
    """0x513c60 and 0x513cf0 on made-up records, with and without settings and a model."""
    uc, w = emu.cpu._uc, emu.w32
    calls = []
    emu.native_stub(ANIM_START, lambda c: calls.append([c.arg(0), c.arg(2), c.arg(3), c.arg(4)]) or 0)
    rng = random.Random(0x513c60)
    rec, hit, tmpl = emu.alloc(0x5c), emu.alloc(0x2c), emu.alloc(0x88)
    node_a, node_b = emu.alloc(0xd4), emu.alloc(0xd4)
    w(tmpl + 0x68, 0x1111), w(tmpl + 0x80, 0x2222)
    gone = 0
    for n in range(4000):
        collide = rng.randrange(2)
        flags = rng.choice((0, 1, 2, 3, 4, 6, 7, 0x10, 0x12))
        weapon, touch = rng.choice((0.0, 0.01, 1.0, rng.uniform(0, 3))), rng.choice((0.0, 0.5, rng.uniform(0, 3)))
        damage = rng.choice((0.0, 0.01, 1.0, rng.uniform(0, 3), weapon, touch))
        settings, model = rng.randrange(2), rng.randrange(2)
        uc.mem_write(rec, bytes(0x5c))
        w(rec, node_a), w(rec + 0x48, flags), w(rec + 0x58, tmpl if settings else 0)
        uc.mem_write(rec + 0x50, struct.pack('<2f', weapon, touch))
        uc.mem_write(rec + 0x28, struct.pack('<3f', 1.5, -2.5, 3.25))
        w(tmpl + 0x6c, node_b if model else 0)
        w(hit + 0x28, 0 if n % 50 == 0 else rec)
        del calls[:]
        if collide:
            emu.call(ON_COLLIDE, args=[0, hit, bits(damage)])
        else:
            emu.call(ON_HIT, args=[0, 0, hit, bits(damage)])
        want = [emu.r32(rec + 0x48), emu.r32(rec + 0x50), emu.r32(rec + 0x54), int(emu.r32(rec) == node_b), len(calls)]
        want += calls[0] if calls else [0, 0, 0, 0]
        out = (ctypes.c_uint32 * 9)()
        if n % 50 == 0:  # no record: nothing happens, in the exe as here
            if want[0] != flags or calls:
                raise SystemExit(f'hit {n}: the exe touched a record it had no pointer to')
            continue
        probe.probe_pm_clutter_hit(collide, flags, weapon, touch, damage, settings, model, out)
        if list(out) != want:
            raise SystemExit(f'hit {n} collide {collide} flags {flags:x} {weapon} {touch} {damage} settings {settings} model {model}\n exe {want}\n port {list(out)}')
        gone += want[0] != flags
    print(f'hits: 4000 identical, {gone} changed a piece')


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
        size, chars = w[at], w[at + 1]
        at += 2
        name = bytes(w[at:at + chars]).decode('latin1')
        at += chars
        got.append([name] + [tuple(w[at + 4 * i:at + 4 * i + 4]) for i in range(size)])
        at += 4 * size
    if got_e != want_e:
        raise SystemExit(f'{world}: errors exe {want_e}, port {got_e}')
    key = lambda items: items[1][2] if len(items) > 1 else -1  # noqa: E731
    want, got = sorted(want, key=key), sorted(got, key=key)
    if len(want) != len(got):
        raise SystemExit(f'{world}: {len(want)} textures against {len(got)}')
    pieces = 0
    for a, b in zip(want, got):
        if len(a) != len(b):
            raise SystemExit(f'{world}: {len(a)} pieces against {len(b)}')
        if a[0] != b[0]:
            raise SystemExit(f'{world}: a group on texture {a[0]} in the exe, {b[0]} in the port')
        for x, y in zip(a[1:], b[1:]):
            pieces += 1
            fx = struct.unpack('<2f', struct.pack('<2I', *x[:2]))
            fy = struct.unpack('<2f', struct.pack('<2I', *y[:2]))
            DIFF.note(x[:2], y[:2], fx, fy)
            if x != y:
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
    probe.probe_pm_clutter_lay.argtypes = [ctypes.c_char_p] * 4 + [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    probe.probe_pm_clutter_hit.argtypes = [ctypes.c_int, ctypes.c_uint32, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                           ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
    probe.probe_pm_walk_frame.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_float, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    probe.probe_pm_clutter_trees.argtypes = [ctypes.c_void_p, ctypes.c_int]
    probe.probe_pm_node_zones.argtypes = [ctypes.c_void_p, ctypes.c_int]
    probe.probe_pm_world_update.restype = None
    probe.probe_pm_cell_touch.restype = None
    probe.probe_pm_clutter_poke.restype = None
    probe.probe_pm_clutter_poke.argtypes = [ctypes.c_int] * 3 + [ctypes.c_float] * 3 + [ctypes.c_int]
    probe.probe_pm_cell_touch.argtypes = [ctypes.c_int, ctypes.c_int]
    probe.probe_pm_world_mark.restype = None
    probe.probe_pm_cell_state.argtypes = [ctypes.c_void_p, ctypes.c_int]
    probe.probe_pm_clutter_crater.argtypes = [ctypes.c_float] * 4 + [ctypes.c_void_p, ctypes.c_int]
    probe.probe_pm_clutter_scene_lay.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    for world in ('c1', 'c2', 'c3', 'c4'):
        records, rands = compare_laying(probe, world, names)
        print(f'{world}: laid {records} records, {rands} rand calls; all bit for bit but the turn')
    records, _ = compare_laying(probe, 'c2', names, skip_ground=1)
    print(f'c2, as a mission loads it (no ground test): {records} records; records flagged by craters as they went: {CRATERS}')
    print(f'frames over the records: {FRAME_DIFF[1]} draws in order, worst relative float difference {FRAME_DIFF[0]:g}')
    print(f'cell state: worst relative float difference {test_pm_world_box.WORST[0]:g}')
    print(f'trees: {TREES[0]} words, worst relative float difference {TREES[1]:g}')
    print(f'rotated records: {ULPS.same} of {ULPS.total} words bit-identical, worst {ULPS.worst:g}; '
          f'{ULPS.exact} words of aligned records, all exact')
    print(f'u, v: {DIFF.same} of {DIFF.total} bit-identical, worst difference {DIFF.worst:g} {DIFF.at}')


if __name__ == '__main__':
    main()
