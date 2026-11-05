"""Pirate's Moon's puffers (gmod_puff and zeff_anim_puffer on `pm`) against PM's exe, the FPU at 24 bits:

1. The emission tick 0x565320 on random puffers (PM's 0xec-byte layout: the priority at +0x70, the node at
   +0x74, a node's world matrix through 0x4ff360 from its cached matrix), with random frame steps and
   moving emitters: every puffer field and every particle made (PM's 0x78-byte particle: all fields,
   the iterators, its textures and the shared colour and growth tracks) after every call.
2. The particle draw 0x563f10 on those particles under random view matrices, priorities and the
   inverse-Z tolerance [0xa4e090]: the cull's radius ([0x611354], answered 0 on both sides) and the
   sprite 0x588c50's screen point, size, texture, alpha, coloured flag and the colour it sets.
3. The PUFFER_STATE event 0x51bbf0 (PM's layout: the priority at +0xc0, the number at +0xc4, the
   textures, colours and growth four bytes on) on random events against a def with puffer and node
   refs, its own node and offset (node ref -200), and a def with no puffers: every puffer after.

rand is MSVC's from the same seed on both sides; operator new, the texture lookups (0x5319f0, 0x5319e0)
and the lists' heads are the bench's.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_puffers.py [runs]
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_zar  # noqa: E402
import test_pm_zreader as zr  # noqa: E402

TICK, DRAW, EVENT = 0x565320, 0x563f10, 0x51bbf0
FIND_TEX, DEFAULT_TEX, SPRITE = 0x5319f0, 0x5319e0, 0x588c50
PUF_HEAD, PUF_COUNT, PART_HEAD, PART_COUNT = 0x8c14a4, 0x8c14a8, 0x8c14b4, 0x8c14b8
ON, FRAME_DT, CULL_PTR, SCALE, CX, CY, VERTS, WORK = (0x612eb0, 0x8c1b4c, 0x611354, 0xa4e090, 0x8d4388,
                                                       0x8d438c, 0x8c4c5c, 0x9ccbf4)
PUFFER_SIZE, PARTICLE_SIZE = 0xec, 0x78
INT_WORDS = {0, 1, 2, 0x3c // 4, 0x74 // 4, 0x84 // 4}  # the puffer's non-float words
NTEX, NNODES = 6, 3
DEFAULT_ID, NONE_ID = 0xfffffffe, 0xffffffff
TOL = 2e-6


def f2u(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def u2f(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


class MsvcRand:
    def __init__(self, seed):
        self.state = seed

    def __call__(self, c=None):
        self.state = (self.state * 214013 + 2531011) & 0xffffffff
        return (self.state >> 16) & 0x7fff


class Emu(zr.Emulated):
    def __init__(self):
        super().__init__()
        cpu = self.cpu
        cpu.fpu_control(0x7f)  # 24 bits, as in play
        self.rand = MsvcRand(0)
        cpu.stub_import(test_pm_zar.IAT['rand'], lambda c: self.rand())
        self.tex = self.alloc(0x2c * NTEX)
        self.default = self.alloc(0x2c)
        self.native_stub(FIND_TEX, self.find_tex)
        self.native_stub(DEFAULT_TEX, lambda c: self.default)
        self.log = []
        cull = self.cpu.code(b'\xc3' * 16)
        self.native_stub(cull, lambda c: self.log.extend([1, c.arg(1)]) or 0)
        self.w32(CULL_PTR, cull)
        self.native_stub(SPRITE, self.sprite)
        self.w32(VERTS, self.alloc(0x100))
        self.nodes = []
        for _ in range(NNODES):
            n, d = self.alloc(0xd4), self.alloc(0x100)
            self.w32(n + 0x34, 5), self.w32(n + 0x24, 0x80000), self.w32(n + 0x38, d)
            self.nodes.append((n, d))

    def w32(self, a, v):
        self.cpu._uc.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def wf(self, a, x):
        self.w32(a, f2u(x))

    def find_tex(self, c):
        name = self.cstr(c.arg(0)).decode('latin-1')
        if name.startswith('tex') and name[3:].isdigit() and int(name[3:]) < NTEX:
            return self.tex + 0x2c * int(name[3:])
        return 0

    def tex_id(self, a):
        if a == self.default:
            return DEFAULT_ID
        if self.tex <= a < self.tex + 0x2c * NTEX:
            return (a - self.tex) // 0x2c
        return NONE_ID

    def node_id(self, a):
        for i, (n, _) in enumerate(self.nodes):
            if a == n:
                return i + 1
        return 0 if a == 0 else NONE_ID

    def sprite(self, c):
        s = c.arg(0)
        rec = [2] + [self.r32(s + 4 * i) for i in range(3)] + [c.arg(1), self.tex_id(c.arg(2)), c.arg(3),
                                                                c.arg(4) & 0xff]
        if c.arg(4) & 0xff:
            rec += [self.r32(WORK + 4 * i) for i in range(3)]
        self.log.extend(rec)
        return 0

    def reset(self, seed, matrices):
        self.rand.state = seed
        self.cpu._uc.mem_write(ON, b'\x01')
        for head, count in ((PUF_HEAD, PUF_COUNT), (PART_HEAD, PART_COUNT)):
            h = self.alloc(0xc)
            self.w32(h, h), self.w32(h + 4, h)
            self.w32(head, h), self.w32(count, 0)
        for (n, d), m in zip(self.nodes, matrices):
            for i, x in enumerate(m):
                self.wf(d + 0x60 + 4 * i, x)
        self.puffers = []

    def vector(self, at, items, size):
        if not items:
            for o in (4, 8, 0xc):
                self.w32(at + o, 0)
            return
        b = self.alloc(size * len(items))
        for i, words in enumerate(items):
            for k, w in enumerate(words):
                self.w32(b + size * i + 4 * k, w)
        self.w32(at + 4, b), self.w32(at + 8, b + size * len(items)), self.w32(at + 0xc, b + size * len(items))

    def puffer(self, words, textures, colors, growth):
        p = self.alloc(PUFFER_SIZE)
        for i, w in enumerate(words):
            self.w32(p + 4 * i, w)
        self.w32(p + 0x74, self.nodes[words[0x74 // 4] - 1][0] if words[0x74 // 4] else 0)
        self.vector(p + 0xc, [(t, self.default if i == DEFAULT_ID else self.tex + 0x2c * i) for t, i in textures], 8)
        self.vector(p + 0x1c, [c for c in colors], 0x14)
        self.vector(p + 0x2c, [g for g in growth], 8)
        self.w32(p + 0xe8, 0)
        self.puffers.append(p)

    def track(self, begin, end, size, conv):
        out = []
        if begin:
            for a in range(begin, end, size):
                out.append(conv(a))
        return out

    def dump_puffer(self, i):
        p = self.puffers[i]
        w = [self.r32(p + 4 * k) for k in range(59)]
        for k in (0, 2, 0x3c // 4, 0x84 // 4):
            w[k] &= 0xff
        w[0x74 // 4] = self.node_id(w[0x74 // 4])
        for o in (0xc, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0xe8):
            w[o // 4] = 0
        t = self.track(self.r32(p + 0x10), self.r32(p + 0x14), 8,
                       lambda a: (self.r32(a), self.tex_id(self.r32(a + 4))))
        c = self.track(self.r32(p + 0x20), self.r32(p + 0x24), 0x14, lambda a: tuple(self.r32(a + 4 * k) for k in range(5)))
        g = self.track(self.r32(p + 0x30), self.r32(p + 0x34), 8, lambda a: (self.r32(a), self.r32(a + 4)))
        return w, t, c, g

    def particles(self):
        out = []
        head = self.r32(PART_HEAD)
        node = self.r32(head)
        while node != head:
            q = self.r32(node + 8)
            w = [self.r32(q + 4 * k) for k in range(22)]
            tb, te = self.r32(q + 0x60), self.r32(q + 0x64)
            share = self.r32(q + 0x6c)
            cb, ce, gb, ge = (self.r32(share + 4), self.r32(share + 8), self.r32(share + 0x14),
                              self.r32(share + 0x18))
            its = [(self.r32(q + 0x58) - tb) // 8, (self.r32(q + 0x70) - cb) // 0x14 if cb else 0,
                   (self.r32(q + 0x74) - gb) // 8 if gb else 0]
            t = self.track(tb, te, 8, lambda a: (self.r32(a), self.tex_id(self.r32(a + 4))))
            c = self.track(cb, ce, 0x14, lambda a: tuple(self.r32(a + 4 * k) for k in range(5)))
            g = self.track(gb, ge, 8, lambda a: (self.r32(a), self.r32(a + 4)))
            out.append((w, its, t, c, g))
            node = self.r32(node)
        return out

    def tick(self, i, dt, frame_dt):
        self.wf(FRAME_DT, frame_dt)
        self.call(TICK, ecx=self.puffers[i], args=[f2u(dt)])

    def draw(self, m, near_z, fx_near, cx, cy, scale):
        self.log = []
        self.wf(CX, cx), self.wf(CY, cy), self.wf(SCALE, scale)
        mp = self.alloc(48)
        for i, x in enumerate(m):
            self.wf(mp + 4 * i, x)
        head = self.r32(PART_HEAD)
        node = self.r32(head)
        while node != head:
            self.call(DRAW, ecx=self.r32(node + 8), args=[mp, f2u(near_z), f2u(fx_near)])
            node = self.r32(node)
        return self.log

    def event(self, refs, nodes, def_node, offset, ev):
        d = self.alloc(0x140)
        if refs:
            lst = self.alloc(0x2c * (refs + 1))
            for i in range(1, refs + 1):
                self.cpu._uc.mem_write(lst + 0x2c * i, f'puf{i}'.encode() + b'\0')
                self.w32(lst + 0x2c * i + 0x24, self.puffers[i - 1])
            self.w32(d + 0xf0, lst)
        self.cpu._uc.mem_write(d + 0xdc, bytes([refs + 1 if refs else 0]))
        nl = self.alloc(0x2c * (nodes + 1))
        for i in range(1, nodes + 1):
            self.w32(nl + 0x2c * i + 0x28, self.nodes[i - 1][0])
        self.w32(d + 0xe8, nl)
        self.w32(d + 0x7c, self.nodes[def_node - 1][0] if def_node else 0)
        for i in range(3):
            self.wf(d + 0x80 + 4 * i, offset[i])
        e = self.alloc(len(ev))
        self.cpu._uc.mem_write(e, bytes(ev))
        self.call(EVENT, args=[d, 0, e])


class Port:
    def __init__(self):
        self.p = test_pm_zar.load_pm_probe()
        self.p.pmpf_reset.argtypes = [ctypes.c_uint32, ctypes.c_int32, ctypes.c_int32, ctypes.c_void_p]
        self.p.pmpf_puffer.argtypes = [ctypes.c_void_p]
        self.p.pmpf_tick.argtypes = [ctypes.c_int32, ctypes.c_float, ctypes.c_float]
        self.p.pmpf_dump_puffer.argtypes = [ctypes.c_int32, ctypes.c_void_p]
        self.p.pmpf_dump_particles.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        self.p.pmpf_draw.argtypes = [ctypes.c_void_p] + [ctypes.c_float] * 5 + [ctypes.c_void_p, ctypes.c_int32]
        self.p.pmpf_event.argtypes = [ctypes.c_int32] * 3 + [ctypes.c_void_p, ctypes.c_void_p]
        self.buf = (ctypes.c_uint32 * (1 << 20))()

    def reset(self, seed, matrices):
        flat = [x for m in matrices for x in m]
        self.p.pmpf_reset(seed, NTEX, NNODES, (ctypes.c_float * len(flat))(*flat))

    def puffer(self, words, textures, colors, growth):
        v = list(words) + [len(textures)] + [x for t, i in textures for x in (t, i)]
        v += [len(colors)] + [x for c in colors for x in c] + [len(growth)] + [x for g in growth for x in g]
        if self.p.pmpf_puffer((ctypes.c_uint32 * len(v))(*[x & 0xffffffff for x in v])) < 0:
            raise SystemExit('the port made no puffer')

    def dump_puffer(self, i):
        n = self.p.pmpf_dump_puffer(i, self.buf)
        w = list(self.buf[:n])
        k = 59
        t, c, g = [], [], []
        cnt = w[k]; k += 1
        for _ in range(cnt):
            t.append((w[k], w[k + 1])); k += 2
        cnt = w[k]; k += 1
        for _ in range(cnt):
            c.append(tuple(w[k:k + 5])); k += 5
        cnt = w[k]; k += 1
        for _ in range(cnt):
            g.append((w[k], w[k + 1])); k += 2
        return w[:59], t, c, g

    def particles(self):
        n = self.p.pmpf_dump_particles(self.buf, len(self.buf))
        w = list(self.buf[:n])
        out, k = [], 0
        while k < n:
            words = w[k:k + 22]; k += 22
            its = w[k:k + 3]; k += 3
            t, c, g = [], [], []
            cnt = w[k]; k += 1
            for _ in range(cnt):
                t.append((w[k], w[k + 1])); k += 2
            cnt = w[k]; k += 1
            for _ in range(cnt):
                c.append(tuple(w[k:k + 5])); k += 5
            cnt = w[k]; k += 1
            for _ in range(cnt):
                g.append((w[k], w[k + 1])); k += 2
            out.append((words, its, t, c, g))
        return out

    def tick(self, i, dt, frame_dt):
        self.p.pmpf_tick(i, dt, frame_dt)

    def draw(self, m, near_z, fx_near, cx, cy, scale):
        n = self.p.pmpf_draw((ctypes.c_float * 12)(*m), near_z, fx_near, cx, cy, scale, self.buf, len(self.buf))
        return list(self.buf[:n])

    def event(self, refs, nodes, def_node, offset, ev):
        self.p.pmpf_event(refs, nodes, def_node, (ctypes.c_float * 3)(*offset), (ctypes.c_uint8 * len(ev))(*ev))


def same_float(a, b):
    if a == b:
        return True
    x, y = u2f(a), u2f(b)
    return abs(x - y) <= TOL * max(1.0, abs(x), abs(y))


def check(label, want, got, floats):
    """want/got: flat lists; floats: which indices are floats."""
    if len(want) != len(got):
        raise SystemExit(f'{label}: {len(want)} words (exe) against {len(got)} (port)')
    for i, (a, b) in enumerate(zip(want, got)):
        if a == b or (i in floats and same_float(a, b)):
            continue
        raise SystemExit(f'{label}: word {i}: exe {a:#010x} ({u2f(a):.7g}), port {b:#010x} ({u2f(b):.7g})')


def flat_tracks(t, c, g):
    words, floats = [len(t)], set()
    for time, tid in t:
        floats.add(len(words)); words += [time, tid]
    words.append(len(c))
    for k in c:
        floats.update(range(len(words), len(words) + 5)); words += list(k)
    words.append(len(g))
    for k in g:
        floats.update(range(len(words), len(words) + 2)); words += list(k)
    return words, floats


def compare_puffer(label, a, b):
    wa, ta, ca, ga = a
    wb, tb, cb, gb = b
    check(label + ' fields', wa, wb, set(range(59)) - INT_WORDS)
    x, fx = flat_tracks(ta, ca, ga)
    y, _ = flat_tracks(tb, cb, gb)
    check(label + ' tracks', x, y, fx)


def compare_particles(label, a, b):
    if len(a) != len(b):
        raise SystemExit(f'{label}: {len(a)} particles (exe) against {len(b)} (port)')
    for n, (pa, pb) in enumerate(zip(a, b)):
        check(f'{label} particle {n}', pa[0] + pa[1], pb[0] + pb[1], set(range(22)))
        x, fx = flat_tracks(*pa[2:])
        y, _ = flat_tracks(*pb[2:])
        check(f'{label} particle {n} tracks', x, y, fx)


def rnd_vec(r, s):
    return [r.uniform(-s, s) for _ in range(3)]


def rotation(r):
    a, b, c = (r.uniform(-3.2, 3.2) for _ in range(3))
    import math
    ca, sa, cb, sb, cc, sc = math.cos(a), math.sin(a), math.cos(b), math.sin(b), math.cos(c), math.sin(c)
    return [cb * cc, cb * sc, -sb, sa * sb * cc - ca * sc, sa * sb * sc + ca * cc, sa * cb,
            ca * sb * cc + sa * sc, ca * sb * sc - sa * cc, ca * cb]


def random_puffer(r):
    w = [0] * 59
    w[0] = 1 if r.random() < 0.9 else 0
    w[1] = r.choice([0, 1, 1, 2, 3, 5])
    w[2] = r.choice([0, 1])
    w[0x3c // 4] = 1 if r.random() < 0.3 else 0
    interval = r.choice([0.05, 0.1, 0.25, 1.0, 2.0]) if not w[0x3c // 4] else r.choice([0.5, 1.0, 3.0])

    def f(off, x):
        w[off // 4] = f2u(x)
    f(0x40, interval), f(0x44, 1.0 / interval), f(0x48, r.uniform(0, interval))
    lo = r.uniform(0.5, 3.0)
    f(0x4c, lo), f(0x50, lo + r.uniform(0, 4)), f(0x54, r.uniform(0.3, 1.0)), f(0x58, r.uniform(1.0, 3.0))
    f(0x5c, r.choice([0.0, 0.0, r.uniform(0, 0.5)])), f(0x60, r.uniform(0.0, 0.8))
    f(0x64, r.choice([0.0, r.uniform(0, 3)])), f(0x68, r.choice([0.0, 0.5, 2.0])), f(0x6c, r.uniform(0.5, 1.5))
    f(0x70, r.choice([0.0, 0.0, 1.0, 3.0, -2.0, r.uniform(-5, 5)]))
    w[0x74 // 4] = r.choice([0, 0, 1, 2, 3])
    for off, s in ((0x78, 50), (0x88, 5), (0x94, 10), (0xa0, 4), (0xac, 3), (0xc4, 2)):
        for i, x in enumerate(rnd_vec(r, s)):
            f(off + 4 * i, x)
    for i in range(3):
        f(0xb8 + 4 * i, u2f(w[(0xac + 4 * i) // 4]) + r.uniform(0, 4))
    w[0x84 // 4] = 1 if r.random() < 0.8 else 0
    ns, ne = r.uniform(0, 5), r.uniform(5, 20)
    fs, fe = r.uniform(100, 300), r.uniform(300, 900)
    if r.random() < 0.5:
        fs = fe = 3.4028235e38
    f(0xd0, ns), f(0xd4, ne), f(0xd8, 1.0 / (ne - ns)), f(0xdc, fs), f(0xe0, fe)
    f(0xe4, 1.0 / (fe - fs) if fe != fs else 0.0)
    textures = []
    # Timed with no textures, a particle's draw reads through a null iterator in the exe (and an empty
    # vector in the port); the runs do not make one (shipped data not searched for it).
    for k in range(r.choice([1, 2, 3] if w[2] else [0, 1, 2, 3])):
        textures.append((f2u(k / 3.0 if w[2] else 0.0), r.choice([0, 1, 2, 3, 4, 5, DEFAULT_ID])))
    colors = []
    for k in range(r.choice([0, 0, 1, 2, 3])):
        colors.append(tuple(f2u(x) for x in (r.uniform(0, 255), r.uniform(0, 255), r.uniform(0, 255), r.uniform(0, 1),
                                             k / 3.0)))
    growth = []
    for k in range(r.choice([0, 0, 1, 2, 3])):
        growth.append((f2u(k / 3.0), f2u(r.uniform(0.5, 3))))
    return w, textures, colors, growth


def random_event(r, refs, nodes):
    ev = bytearray(0x260)
    struct.pack_into('<II', ev, 0, 42, len(ev))
    name_ref = r.randint(1, refs) if refs else 1
    ev[0x0c:0x0c + 5] = f'puf{name_ref}'.encode()
    struct.pack_into('<i', ev, 0x2c, r.choice([name_ref, name_ref, 0]))  # 0: looked up by name
    flags = 0
    for bit in range(23):
        if r.random() < 0.45:
            flags |= 1 << bit
    struct.pack_into('<I', ev, 0x30, flags)
    struct.pack_into('<i', ev, 0x34, r.choice([0, 1, 2, 3, -200, -200, 9]))
    struct.pack_into('<i', ev, 0x38, r.choice([0, 1]))
    for off in range(0x3c, 0x84, 4):
        struct.pack_into('<f', ev, off, r.uniform(-20, 20))
    struct.pack_into('<i', ev, 0x84, r.choice([0, 1]))
    for off in range(0x88, 0xc0, 4):
        struct.pack_into('<f', ev, off, r.uniform(0.1, 5))
    struct.pack_into('<f', ev, 0xc0, r.uniform(-4, 4))
    struct.pack_into('<i', ev, 0xc4, r.choice([1, 2, 4]))
    struct.pack_into('<i', ev, 0xc8, r.choice([0, 1]))
    for i in range(r.choice([0, 1, 2, 6])):
        off = 0xd0 + 0x24 * i
        struct.pack_into('<f', ev, off - 4, i / 6.0)
        name = r.choice(['tex0', 'tex3', 'tex5', 'nosuch'])
        ev[off:off + len(name)] = name.encode()
    struct.pack_into('<i', ev, 0x1a4, r.choice([0, 1, 3, 6]))
    for off in range(0x1a8, 0x1a8 + 0x14 * 6, 4):
        struct.pack_into('<f', ev, off, r.uniform(0, 255))
    struct.pack_into('<i', ev, 0x220, r.choice([0, 1, 2, 6]))
    for off in range(0x224, 0x224 + 8 * 6, 4):
        struct.pack_into('<f', ev, off, r.uniform(0, 3))
    return ev


def view_matrix(r):
    m = rotation(r)
    return m + [r.uniform(-20, 20), r.uniform(-20, 20), r.uniform(20, 300)]


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    emu, port = Emu(), Port()
    # Before the first reset no puffer is made (0x565ad0 reads [0x612eb0]).
    emu.cpu._uc.mem_write(ON, b'\x00')
    made = int(emu.call(0x565ad0) != 0)
    if made != port.p.pmpf_create_before_reset():
        raise SystemExit(f'before the reset: the exe made {made} puffer(s), the port another count')
    stats = {'particles': 0, 'sprites': 0, 'events': 0, 'ticks': 0}
    for run in range(runs):
        r = random.Random(run)
        matrices = [rotation(r) + rnd_vec(r, 100) for _ in range(NNODES)]
        seed = r.randrange(1 << 31)
        emu.reset(seed, matrices)
        port.reset(seed, matrices)
        n = r.choice([1, 2, 3])
        for _ in range(n):
            pf = random_puffer(r)
            emu.puffer(*pf)
            port.puffer(*pf)
        for frame in range(r.choice([3, 8, 15])):
            i = r.randrange(n)
            dt, frame_dt = r.choice([1 / 30, 1 / 60, 0.1, 0.0]), r.choice([1 / 30, 1 / 60, 0.05])
            emu.tick(i, dt, frame_dt)
            port.tick(i, dt, frame_dt)
            stats['ticks'] += 1
            label = f'run {run} frame {frame} puffer {i}'
            compare_puffer(label, emu.dump_puffer(i), port.dump_puffer(i))
            compare_particles(label, emu.particles(), port.particles())
        parts = emu.particles()
        stats['particles'] += len(parts)
        for d in range(2):
            m = view_matrix(r)
            args = (m, r.uniform(0.5, 2), r.uniform(200, 900), r.uniform(0, 640), r.uniform(0, 480),
                    r.choice([0.02, 0.01, r.uniform(0, 0.1)]))
            a, b = emu.draw(*args), port.draw(*args)
            floats = set(range(len(a)))
            check(f'run {run} draw {d}', a, b, floats)
            stats['sprites'] += a.count(2)
        # events
        refs = n if r.random() < 0.85 else 0
        for e in range(4):
            ev = random_event(r, refs, NNODES)
            def_node, offset = r.choice([0, 1, 2]), rnd_vec(r, 5)
            emu.event(refs, NNODES, def_node, offset, ev)
            port.event(refs, NNODES, def_node, offset, ev)
            stats['events'] += 1
            for i in range(n):
                compare_puffer(f'run {run} event {e} puffer {i}', emu.dump_puffer(i), port.dump_puffer(i))
    print(f'ok: {runs} runs, {stats["ticks"]} ticks, {stats["particles"]} particles, {stats["sprites"]} sprites, '
          f'{stats["events"]} events identical')


if __name__ == '__main__':
    main()
