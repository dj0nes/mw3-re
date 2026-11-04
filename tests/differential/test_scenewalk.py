"""The scene walk of a frame against the original x86 (docs/drafts/scenewalk.md):
the world render 0x4f7030 with its camera matrices (0x4f4370, 0x4ed280),
horizon follow (0x4f6d40), viewport (0x526e10), partition picking (0x4f61b0,
0x4f5c80), cells (0x4f6720), the per-node walk (0x4f5910 and the class
renderers) and the cull (0x541760), on every mission world read by the
original's 0x4ff520 and by the port's loaders.

The model draw [0x74a2e0] is a recorder on both sides: each call logs the node,
the plane mask, the top of the matrix stack and the render state. The draw-time
calls that are not ported are stand-ins that log themselves: the
hidden-surface test 0x53d750 (hidden on a coarse grid, the same rule both
sides), the frame update 0x4f7f70, the zone probe 0x4eda10; BeginScene,
EndScene, the flushes and the puffers do nothing. Lights (0x4f2f30, 0x4f2f60)
and the shadow caster lists (0x543720 / 0x543760 / 0x543810, on caster
records over drawn nodes, docs/render-shadows.md) run for real; each list
call logs its result (the casters taken, their camera-space slopes).

    cmake --build build && .venv/bin/python tests/differential/test_scenewalk.py [cameras per world]
"""
import ctypes
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402
from test_gamez import (BIG, BLOCKS, CAPACITY, COUNT, COUNT_M, GROUP, HANDLE, IAT_FCLOSE, IAT_FOPEN,  # noqa: E402
                        IAT_FSEEK, POOL, POOL_COUNT, POOL_SIZE, READ_ZBD, SET_FORMAT, WORLDS, ZBD, Emulated)

LIB = next((ROOT / 'build').glob('libmw3probe.*'))
probe = ctypes.CDLL(str(LIB))
probe.sw_load.argtypes = [ctypes.c_char_p]
probe.sw_find.argtypes = [ctypes.c_char_p]
probe.sw_set_camera.argtypes = [ctypes.c_float] * 6
probe.sw_set_fov.argtypes = [ctypes.c_float] * 2
probe.sw_set_clip.argtypes = [ctypes.c_float] * 2
probe.sw_camera_put.argtypes = [ctypes.c_int, ctypes.c_uint32]
probe.sw_globals.argtypes = [ctypes.c_uint32, ctypes.c_float] + [ctypes.c_int] * 6
probe.sw_casters.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                             ctypes.POINTER(ctypes.c_float), ctypes.c_int]
probe.sw_light_edit.argtypes = [ctypes.c_uint32] + [ctypes.c_float] * 4
probe.sw_render.argtypes = [ctypes.c_void_p, ctypes.c_int]
probe.sw_render_subtree.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
probe.sw_hull.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int]
probe.sw_set_lod_scale.argtypes = [ctypes.c_float]
probe.sw_clear_flags.argtypes = [ctypes.c_int, ctypes.c_uint32]
probe.sw_box_sphere.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                ctypes.POINTER(ctypes.c_float)]

# MSVCRT helpers as x87 code (test_mechdef.py, test_matrix.py).
FLOOR = bytes.fromhex('83ec04 d93c24 668b0424 6625fff3 660d0004 6689442402 d96c2402 dd442408 d9fc d92c24 83c404 c3')
ASIN = bytes.fromhex('d9c0 d8c8 d9e8 dee1 d9fa d9f3 c3')
COPYSIGN = bytes.fromhex('8b442410 2500000080 8b4c2408 81e1ffffff7f 0bc8 894c2408 dd442404 c3')
IAT_FLOOR, IAT_ASIN, IAT_COPYSIGN = 0x5bb66c, 0x5bb5f4, 0x5bb5ac

RENDER_WORLD, SET_ROT, SET_POS, SET_FOV, SET_CLIP, SET_HORIZON = 0x4f7030, 0x4f36b0, 0x4f3870, 0x4f3b90, 0x4f3a70, 0x4f4090
RENDER_SUBTREE = 0x4f73f0
BOX_CORNERS, SPHERE, HULL, SET_LOD_SCALE = 0x4ebdb0, 0x4f9c40, 0x4f5a90, 0x4f3ff0
NODE_BLOCKS, NODE_BLOCK_SIZE = 0x60bf38, 0x5ea830
CAMERA_LIST = 0x5ea6d0 + 4 * 8   # list 8's head pointer slot
STACK_TOP_PTR, STACK_FLAG_PTR = 0x5eda90, 0x5eda8c
RINGS = 0x736640
END = 0xffffffff


def f2u(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def u2f(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


class Walk(Emulated):
    def __init__(self):
        super().__init__()
        cpu = self.cpu
        cpu.fpu_control(0x007f)   # play precision (CLAUDE.md)
        cpu.import_code(IAT_FLOOR, FLOOR)
        cpu.import_code(IAT_ASIN, ASIN)
        cpu.import_code(IAT_COPYSIGN, COPYSIGN)
        cpu.stub_import(IAT_FOPEN, lambda c: HANDLE)
        cpu.stub_import(IAT_FSEEK, self.fseek)
        cpu.stub_import(IAT_FCLOSE, lambda c: 0)
        cpu.stub_import(0x74a2e0, self.draw)
        cpu.stub_import(0x72f3e4, lambda c: 0)
        cpu.stub_import(0x72f3e8, lambda c: 0)
        cpu.stub_import(0x72f3ec, lambda c: 0)  # the quad, called by the subtree pass 0x4f73f0
        for a in (0x579af0, 0x579b10, 0x5496c0):
            cpu.stub_function(a, lambda c: 0)
        cpu.stub_function(0x53d750, self.hse, 4)
        cpu.stub_function(0x4f7f70, lambda c: self.events.append((3,)))
        cpu.stub_function(0x4eda10, self.zone_probe, 4)
        from unicorn import UC_HOOK_CODE
        cpu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, n, u: self.list_event(5, 0x73fc60, 0x7402c0), begin=0x543758,
                        end=0x543758)
        cpu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, n, u: self.slopes_event(), begin=0x543801, end=0x543801)
        cpu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, n, u: self.list_event(7, 0x73ffa0, 0x73ff80), begin=0x5438cf,
                        end=0x5438cf)
        self.events = []
        self.casters = []
        self.cell_filter = 0
        self.zerr_stubbed = False

    # --- memory helpers ---
    def u32(self, a):
        return struct.unpack('<I', self.cpu.read(a, 4))[0]

    def f32(self, a):
        return struct.unpack('<f', self.cpu.read(a, 4))[0]

    def node_index(self, ptr):
        bs = self.u32(NODE_BLOCK_SIZE)
        for b, base in enumerate(self.blocks):
            if base <= ptr < base + bs * 0xd4:
                return b * bs + (ptr - base) // 0xd4
        return -1

    def node_ptr(self, i):
        bs = self.u32(NODE_BLOCK_SIZE)
        return self.blocks[i // bs] + (i % bs) * 0xd4

    # --- stand-ins ---
    def draw(self, c):
        top = self.u32(self.u32(STACK_TOP_PTR))
        m = struct.unpack('<12f', c.read(top, 48))
        self.events.append((1, self.node_index(c.ecx), c.edx, m, self.f32(0x74a26c), self.u32(0x74a268),
                            self.u32(0x74a264), self.u32(0x74a270), self.u32(0x60bc20)))
        wd = self.u32(self.camera_world() + 0x38)
        table = self.u32(wd + 0xa8)
        self.events.append((8, tuple(self.u32(self.u32(table + 4 * i) + 0xb0) for i in range(self.u32(wd + 0xa0)))))
        return 0

    def hse(self, c):
        cx, cz = self.f32(c.ecx), self.f32(c.ecx + 8)
        r = u2f(c.arg(0))
        # float32 division, as the port's
        gx = math.floor(u2f(f2u(cx / 37.0)) if abs(cx) < 3e38 else cx)
        gz = math.floor(u2f(f2u(cz / 37.0)) if abs(cz) < 3e38 else cz)
        v = int(((gx * 7 + gz * 13) % 5 + 5) % 5 != 0)
        self.events.append((2, gx, gz, r, v))
        return v

    def zone_probe(self, c):
        self.events.append((4,))
        c.write(c.arg(0), b'\0')
        return 0

    # The caster lists at the return of 0x543720 / 0x543810, the slopes at 0x543760's.
    def list_event(self, kind, base, count_at):
        n = self.u32(count_at)
        ids = [self.caster_records.index(self.u32(base + 4 * i)) for i in range(n)]
        self.events.append((kind, tuple(ids)))

    def slopes_event(self):
        n = self.u32(0x7402c0)
        out = []
        for i in range(n):
            rec = self.u32(0x73fc60 + 4 * i)
            out.append((self.caster_records.index(rec),) + struct.unpack('<3f', self.cpu.read(rec + 0x4c, 12)))
        self.events.append((6, tuple(out)))

    def fseek(self, c):
        self.pos = c.arg(1)
        return 0

    # --- the world ---
    def load(self, path):
        cpu = self.cpu
        data = path.read_bytes()
        self.next, self.data, self.pos = BIG, data, 0
        for a in (BLOCKS, 0x63fbc0, CAPACITY, COUNT_M, POOL, POOL_SIZE, POOL_COUNT, COUNT):
            cpu.u32(a, 0)
        cpu.u32(GROUP, 3)
        cpu.call(SET_FORMAT, ecx=5, edx=6, stack_args=(5, 0, 0, 0))
        self.reset_world(-1)
        p = self.alloc(len(str(path)) + 1)
        cpu.write(p, str(path).encode() + b'\0')
        cpu.call(READ_ZBD, ecx=p)
        nb = self.u32(0x60bf30)
        self.blocks = [self.u32(self.u32(NODE_BLOCKS) + 4 * b) for b in range(nb)]
        self.camera = self.u32(self.u32(self.u32(CAMERA_LIST)))   # list head item -> node
        # The model renderer's init (0x526510) and .data defaults the walk reads.
        for a, v in ((0x72f348, 1), (0x74a324, 0x74a2e4), (0x74a290, 0x74039c), (0x74a294, 0x74339c),
                     (0x74a26c, f2u(1.0)), (0x74a268, 0), (0x74a264, 0), (0x74a270, 0), (0x74a32c, 0),
                     (0x74a2cc, 0x74a2c8), (0x74a2c8, 0), (0x5ea540, 0), (0x5ea530, 0), (0x5ea534, 0),
                     (0x5ea538, 0), (0x60bc20, 0), (0x60bac8, 0), (0x5ea544, 0xffffffff), (0x5ea548, 0xffffffff),
                     (0x60be3c, 0), (0x60be28, 0), (0x60be30, 0), (0x60be34, 0), (0x60be38, 0),
                     (0x5ea524, f2u(1.0)), (0x7402c4, 0), (0x73fc4c, 0), (0x7402c0, 0), (0x73ff80, 0),
                     (0x74a298, 0), (0x5eda90, 0x63d760), (0x5eda8c, 0x63d7e8)):
            cpu.u32(a, v)
        return data

    def globals(self, frame, dt, hse, lod_dynamic, shadows, zone_mode, virtual, cell_filter):
        cpu = self.cpu
        cpu.u32(0x72f338, frame)
        cpu.u32(0x72c52c, f2u(dt))
        cpu.u32(0x5ea518, hse)
        cpu.u32(0x5ea51c, lod_dynamic)
        cpu.u32(0x74a264, shadows)
        cpu.u32(0x5ea1d0, zone_mode)
        cpu.u32(self.u32(self.camera_world() + 0x38) + 0x50, virtual)
        self.cell_filter = cell_filter

    def camera_world(self):
        return self.u32(self.u32(self.camera + 0x38))

    def set_casters(self, casters):
        """Caster records (0x8c bytes) linked from 0x64fbe0 in the given
        order: node, enabled, slope x/z, box min/max, position, ground y."""
        self.caster_records = []
        for node, on, slope, bmin, bmax, pos, ground in casters:
            rec = self.alloc(0x90)
            body = bytearray(0x8c)
            struct.pack_into('<I3f', body, 0, self.node_ptr(node), *pos)
            struct.pack_into('<f2f', body, 0x1c, ground, *slope)
            struct.pack_into('<6f', body, 0x2c, *bmin, *bmax)
            struct.pack_into('<I', body, 0x58, on)
            self.cpu.write(rec, bytes(body))
            self.caster_records.append(rec)
        for i, rec in enumerate(self.caster_records):
            prev = self.caster_records[i - 1] if i else 0
            nxt = self.caster_records[i + 1] if i + 1 < len(self.caster_records) else 0
            self.cpu.u32(rec + 0x84, prev)
            self.cpu.u32(rec + 0x88, nxt)
        self.cpu.u32(0x64fbe0, self.caster_records[0] if self.caster_records else 0)

    def render(self):
        self.events = []
        self.cpu.call(RENDER_WORLD, ecx=self.camera, edx=0, count=2_000_000_000)
        cam = self.cpu.read(self.u32(self.camera + 0x38), 0x1e8)
        view = self.cpu.read(0x73fbc0, 48)
        rings = []
        for r in range(50):
            base = RINGS + 0x2d4 * r
            n = self.u32(base + 0x2d0)
            rings.append([struct.unpack('<iiiI', self.cpu.read(base + 0x18 * k, 12) + self.cpu.read(base + 0x18 * k + 0x14, 4))
                          for k in range(n)])
        return self.events, cam, view, rings

    def render_subtree(self, node):
        """0x4f73f0 (the cockpit pass) on one node, the Direct3D path."""
        self.events = []
        self.cpu.call(RENDER_SUBTREE, ecx=self.camera, edx=node, stack_args=(1,), count=2_000_000_000)
        cam = self.cpu.read(self.u32(self.camera + 0x38), 0x1e8)
        view = self.cpu.read(0x73fbc0, 48)
        return self.events, cam, view, []


def parse_port(raw):
    ev, i = [], 0
    while True:
        kind, = struct.unpack_from('<I', raw, i)
        i += 4
        if kind == END:
            break
        if kind == 1:
            node, mask = struct.unpack_from('<iI', raw, i)
            m = struct.unpack_from('<12f', raw, i + 8)
            op, ov, sh, tint, morph = struct.unpack_from('<fIIII', raw, i + 56)
            ev.append((1, node, mask, m, op, ov, sh, tint, morph))
            i += 76
        elif kind == 2:
            gx, gz, r, v = struct.unpack_from('<iifI', raw, i)
            ev.append((2, gx, gz, r, v))
            i += 16
        elif kind == 8:
            n, = struct.unpack_from('<I', raw, i)
            ev.append((8, struct.unpack_from(f'<{n}I', raw, i + 4)))
            i += 4 + 4 * n
        elif kind in (5, 7):
            n, = struct.unpack_from('<I', raw, i)
            ev.append((kind, struct.unpack_from(f'<{n}i', raw, i + 4)))
            i += 4 + 4 * n
        elif kind == 6:
            n, = struct.unpack_from('<I', raw, i)
            ev.append((6, tuple(struct.unpack_from('<i3f', raw, i + 4 + 16 * k) for k in range(n))))
            i += 4 + 16 * n
        else:
            ev.append((kind,))
    cam = raw[i:i + 0x1e8]
    view = raw[i + 0x1e8:i + 0x1e8 + 48]
    i += 0x1e8 + 48
    rings = []
    for _ in range(50):
        if i >= len(raw):  # a subtree pass has no rings
            break
        n, = struct.unpack_from('<I', raw, i)
        i += 4
        rings.append([struct.unpack_from('<iiiI', raw, i + 16 * k) for k in range(n)])
        i += 16 * n
    return ev, cam, view, rings


TOL = 2e-4


def close(a, b, scale=1.0):
    return a == b or abs(a - b) <= TOL * (scale + abs(a) + abs(b))


CAMERA_INTS = {0x10, 0xf8, 0x138, 0x184, 0x1e0, 0x1e4}


def compare(what, want, got):
    ev_w, cam_w, view_w, rings_w = want
    ev_g, cam_g, view_g, rings_g = got
    for k in range(min(len(ev_w), len(ev_g))):
        a, b = ev_w[k], ev_g[k]
        if a[0] != b[0]:
            return f'{what}: event {k}: original {a[:3]} port {b[:3]}'
        if a[0] == 1:
            if a[1:3] != b[1:3] or a[4:] != b[4:]:
                return (f'{what}: draw {k}: original node {a[1]} mask {a[2]:#x} state {a[4:]}, '
                        f'port node {b[1]} mask {b[2]:#x} state {b[4:]}')
            scale = max(abs(x) for x in a[3]) + 1.0
            if not all(close(x, y, scale) for x, y in zip(a[3], b[3])):
                return f'{what}: draw {k} node {a[1]}: matrix {a[3]} vs {b[3]}'
        elif a[0] == 2:
            if a[1:3] != b[1:3] or a[4] != b[4] or not close(a[3], b[3]):
                return f'{what}: hidden-surface call {k}: original {a[1:]} port {b[1:]}'
        elif a[0] == 8:
            if a != b:
                return f'{what}: event {k}: lights\' flags, original {[hex(x) for x in a[1]]} port {[hex(x) for x in b[1]]}'
        elif a[0] in (5, 7):
            if a != b:
                return f'{what}: caster list {k} (kind {a[0]}): original {a[1]} port {b[1]}'
        elif a[0] == 6:
            if len(a[1]) != len(b[1]) or any(x[0] != y[0] or not all(close(p, q) for p, q in zip(x[1:], y[1:]))
                                             for x, y in zip(a[1], b[1])):
                return f'{what}: caster slopes {k}: original {a[1]} port {b[1]}'
    if len(ev_w) != len(ev_g):
        return f'{what}: {len(ev_w)} events in the original, {len(ev_g)} in the port'
    for off in range(0x10, 0x1e8, 4):
        a, b = struct.unpack_from('<I', cam_w, off)[0], struct.unpack_from('<I', cam_g, off)[0]
        if off in CAMERA_INTS:
            if a != b:
                return f'{what}: camera +{off:#x}: {a:#x} vs {b:#x}'
        elif not close(u2f(a), u2f(b), 1.0):
            return f'{what}: camera +{off:#x}: {u2f(a)} vs {u2f(b)}'
    va, vb = struct.unpack('<12f', view_w), struct.unpack('<12f', view_g)
    if not all(close(x, y, 100.0) for x, y in zip(va, vb)):
        return f'{what}: view {va} vs {vb}'
    if rings_w != rings_g:
        r = next(r for r in range(50) if rings_w[r] != rings_g[r])
        return f'{what}: ring {r}: original {rings_w[r]} port {rings_g[r]}'
    return None


def random_caster(emu, rng, node, lo_x, hi_x, lo_z, hi_z):
    """A caster record over node: a box around its sphere (+0x64, +0x70) or,
    without one yet, somewhere on the map; a light from above."""
    p = emu.node_ptr(node)
    cx, cy, cz, r = struct.unpack('<4f', emu.cpu.read(p + 0x64, 16))
    if not (abs(cx) < 1e5 and abs(cz) < 1e5 and 0 < r < 1e4):
        cx, cy, cz, r = rng.uniform(lo_x, hi_x), rng.uniform(0, 50), rng.uniform(lo_z, hi_z), rng.uniform(2, 40)
    sx, sz = rng.uniform(-1.5, 1.5), rng.uniform(-1.5, 1.5)
    grow = r * rng.uniform(0.5, 3.0)
    bmin = [cx - grow + sx * r, cy - r, cz - grow + sz * r]
    bmax = [cx + grow + sx * r, cy + r + 2.0, cz + grow + sz * r]
    f = [u2f(f2u(v)) for v in (sx, sz, *bmin, *bmax, cx, cy, cz, cy - r)]
    return node, int(rng.random() < 0.85), f[0:2], f[2:5], f[5:8], f[8:11], f[11]


def check_box_sphere(emu, rng, count=400):
    """0x4ebdb0 + 0x4f9c40 on random nodes and stack tops, bit for bit."""
    cpu = emu.cpu
    out = (ctypes.c_float * 28)()
    corners = emu.alloc(0x60)
    centre = emu.alloc(16)
    top = emu.alloc(48)
    n_ok = 0
    nodes = [i for i in range(2000) if emu.u32(emu.node_ptr(i) + 0x34) in (5, 6)]
    for k in range(count):
        i = rng.choice(nodes)
        m = [rng.uniform(-2, 2) for _ in range(9)] + [rng.uniform(-500, 500) for _ in range(3)]
        m = [u2f(f2u(x)) for x in m]
        ident = k % 5 == 0
        cpu.write(top, struct.pack('<12f', *m))
        cpu.u32(0x63d764, top)
        cpu.u32(0x63d7ec, int(ident))
        cpu.u32(STACK_TOP_PTR, 0x63d764)
        cpu.u32(STACK_FLAG_PTR, 0x63d7ec)
        cpu.call(BOX_CORNERS, ecx=emu.node_ptr(i), edx=corners)
        r_w = cpu.uc.reg_read(UC_X86_REG_EAX)
        cpu.call(SPHERE, ecx=corners, edx=centre, stack_args=(centre + 12,))
        want = cpu.read(corners, 0x60) + cpu.read(centre, 16)
        r_g = probe.sw_box_sphere(i, (ctypes.c_float * 12)(*m), int(ident), out)
        got = bytes(out)
        if r_w != r_g or want != got:
            if r_w == r_g == 1:
                n_ok += 1
                continue
            wa, ga = struct.unpack('<28f', want), struct.unpack('<28f', got)
            raise SystemExit(f'box/sphere node {i} (identity {ident}): result {r_w}/{r_g}\n {wa}\n {ga}')
        n_ok += 1
    cpu.u32(STACK_TOP_PTR, 0x63d760)
    cpu.u32(STACK_FLAG_PTR, 0x63d7e8)
    return n_ok


def check_hull(emu, rng, count=3000):
    """0x4f5a90 on 3..5 random XZ points, many on a small grid (ties and
    collinear points), compared with the port: the count and the reordered
    points."""
    cpu = emu.cpu
    pts = emu.alloc(12 * 8)
    for k in range(count):
        n = rng.choice((3, 4, 5))
        if k % 2:
            p = [(float(rng.randint(-2, 2)), rng.uniform(-5, 5), float(rng.randint(-2, 2))) for _ in range(n)]
        else:
            p = [(rng.uniform(-500, 500), rng.uniform(-5, 5), rng.uniform(-500, 500)) for _ in range(n)]
        flat = [u2f(f2u(v)) for q in p for v in q] + [0.0] * 3
        cpu.write(pts, struct.pack(f'<{len(flat)}f', *flat))
        cpu.call(HULL, ecx=pts, edx=n)
        want_n = cpu.uc.reg_read(UC_X86_REG_EAX)
        want = struct.unpack(f'<{3 * (n + 1)}f', cpu.read(pts, 12 * (n + 1)))
        arr = (ctypes.c_float * len(flat))(*flat)
        got_n = probe.sw_hull(arr, n)
        got = tuple(arr)
        if want_n != got_n or want[:3 * want_n] != got[:3 * got_n]:
            raise SystemExit(f'hull {p}: original {want_n} {want}, port {got_n} {got}')
    return count


def main():
    per_world = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    emu = Walk()
    rng = random.Random(1234)
    crng = random.Random(4321)
    srng = random.Random(7310)  # the subtree passes' own, so the frames stay as before
    buf = ctypes.create_string_buffer(64 << 20)
    total_frames = total_draws = total_hse = 0
    light_frames = 0
    cover = dict(wrapped=0, cells=0, casters=0, cell_casters=0, full_lists=0, far_kept=0, near=0, sides=0, zone_frames=0, clamped=0,
                 hidden=0, faded=0, overlay=0)
    boxes = 0
    subtree_draws = 0
    for wname in WORLDS:
        path = ZBD / wname / 'gamez.zbd'
        emu.load(path)
        if probe.sw_load(str(path).encode()) != 0:
            raise SystemExit(f'{wname}: the port could not read the world')
        if wname == 'c1':
            boxes = check_box_sphere(emu, rng)
            hulls = check_hull(emu, rng)
        # Wrapped cells draw only nodes with flag 0x8000, which nearly every
        # cell node has: clear it on every third node, both sides.
        for i in range(0, 20000, 3):
            try:
                p = emu.node_ptr(i)
            except IndexError:
                break
            if emu.u32(p + 0x34) in (5, 6) and emu.u32(p + 0x24) & 0x8000:
                emu.cpu.u32(p + 0x24, emu.u32(p + 0x24) & ~0x8000)
                probe.sw_clear_flags(i, 0x8000)
        cam_idx = emu.node_index(emu.camera)
        if probe.sw_camera() != cam_idx:
            raise SystemExit(f'{wname}: camera node {cam_idx} vs {probe.sw_camera()}')
        horizon = probe.sw_find(b'horizon')
        emu.cpu.call(SET_HORIZON, ecx=emu.camera, edx=emu.node_ptr(horizon))
        probe.sw_set_horizon(horizon)
        # The listener path needs zSound: keep camera flag bit 0 off on both.
        cdata = emu.u32(emu.camera + 0x38)
        flags = emu.u32(cdata + 0x10) & ~1
        emu.cpu.u32(cdata + 0x10, flags)
        probe.sw_camera_put(0x10, flags)
        wd = emu.u32(emu.camera_world() + 0x38)
        x0, z0 = emu.f32(wd + 0x34), emu.f32(wd + 0x38)
        x1, z1 = emu.f32(wd + 0x44), emu.f32(wd + 0x48)
        lo_x, hi_x = min(x0, x1), max(x0, x1)
        lo_z, hi_z = min(z0, z1), max(z0, z1)
        objs = [i for i in range(3000) if emu.u32(emu.node_ptr(i) + 0x34) == 5]
        frames = draws = 0
        last = []
        for k in range(per_world):
            what = f'{wname} camera {k}'
            margin = 400.0 if k % 4 == 3 else 0.0
            px = rng.uniform(lo_x - margin, hi_x + margin)
            pz = rng.uniform(lo_z - margin, hi_z + margin)
            py = rng.choice([rng.uniform(2, 40), rng.uniform(40, 250)])
            yaw = rng.uniform(-math.pi, math.pi)
            pitch = rng.uniform(-0.12, 0.12) if k % 3 else rng.uniform(-0.9, 0.5)
            roll = 0.0 if k % 5 else rng.uniform(-0.4, 0.4)
            rot = [u2f(f2u(v)) for v in (pitch, yaw, roll)]
            pos = [u2f(f2u(v)) for v in (px, py, pz)]
            hse = int(k % 2 == 0)
            lod_dyn = int(k % 7 == 6)
            dt = 0.02 if k % 2 else 0.08
            shadows = int(k % 6 == 5)
            zone_mode = 1 if k % 8 != 7 else 0
            virtual = 0 if k % 9 == 8 else 1
            cell_filter = k & 1
            frame = 100 + k
            if k % 7 == 2:    # the LoD distance scale (CameraSetLODScale-like)
                emu.cpu.call(SET_LOD_SCALE, ecx=emu.camera, stack_args=(f2u(0.6),))
                probe.sw_set_lod_scale(0.6)
            elif k % 7 == 4:
                emu.cpu.call(SET_LOD_SCALE, ecx=emu.camera, stack_args=(f2u(1.0),))
                probe.sw_set_lod_scale(1.0)
            if k % 10 == 4:   # zoomed and clipped
                emu.cpu.call(SET_FOV, ecx=emu.camera, stack_args=(f2u(1.5), f2u(1.25)))
                probe.sw_set_fov(1.5, 1.25)
                emu.cpu.call(SET_CLIP, ecx=emu.camera, stack_args=(f2u(0.5), f2u(500.0)))
                probe.sw_set_clip(0.5, 500.0)
            elif k % 10 == 5:
                emu.cpu.call(SET_FOV, ecx=emu.camera, stack_args=(f2u(1.0), f2u(1.0)))
                probe.sw_set_fov(1.0, 1.0)
                emu.cpu.call(SET_CLIP, ecx=emu.camera, stack_args=(f2u(0.1), f2u(800.0)))
                probe.sw_set_clip(0.1, 800.0)
            if k % 8 == 3:    # a camera zone list: zone 71 only, or all
                zl = 0xffff4701 if k % 16 == 3 else 0xffff0101
                for off, v in ((0x1e0, 1), (0x1e4, zl)):
                    emu.cpu.u32(cdata + off, v)
                    probe.sw_camera_put(off, v)
            elif k % 8 == 4:
                for off, v in ((0x1e0, 0), (0x1e4, 0)):
                    emu.cpu.u32(cdata + off, v)
                    probe.sw_camera_put(off, v)
            # Casters: some of the last frame's drawn nodes, some at random,
            # boxes around their spheres; now and then more than the 200 the
            # lists hold.
            # The records come from their own random stream, so the cameras
            # stay those of the runs before the caster lists were real.
            drawn = sorted({e[1] for e in last if e[0] == 1})
            nodes = (rng.sample(drawn, min(8, len(drawn))) + rng.sample(objs, 4)) if shadows else []
            if shadows and k % 12 == 11:
                nodes = [crng.choice(objs) for _ in range(crng.choice([199, 236, 240, 300]))]
            casters = [random_caster(emu, crng, i, lo_x, hi_x, lo_z, hi_z) for i in nodes]
            emu.set_casters(casters)
            flat = [x for c in casters for x in (*c[2], *c[3], *c[4], *c[5], c[6])]
            probe.sw_casters((ctypes.c_int * max(1, len(casters)))(*[c[0] for c in casters]),
                             (ctypes.c_int * max(1, len(casters)))(*[c[1] for c in casters]),
                             (ctypes.c_float * max(1, len(flat)))(*flat), len(casters))
            # A ranged light over part of the view, every third frame (0x4f6720's reach test).
            ltable = emu.u32(wd + 0xa8)
            if emu.u32(wd + 0xa0) and k % 3 == 0:
                ld = emu.u32(ltable)
                lf = emu.u32(ld + 0xb0) | 0x12
                lp = (pos[0] + crng.uniform(-60, 60), pos[1], pos[2] + crng.uniform(-60, 60))
                lr = crng.uniform(10, 200)
                emu.cpu.u32(ld + 0xb0, lf)
                emu.cpu.write(ld + 0x60, struct.pack('<3f', *lp))
                emu.cpu.write(ld + 0xb8, struct.pack('<f', lr))
                probe.sw_light_edit(lf, *lp, lr)
                light_frames += 1
            emu.globals(frame, dt, hse, lod_dyn, shadows, zone_mode, virtual, cell_filter)
            probe.sw_globals(frame, dt, hse, lod_dyn, shadows, zone_mode, virtual, cell_filter)
            emu.cpu.call(SET_ROT, ecx=emu.camera, stack_args=tuple(f2u(v) for v in rot))
            emu.cpu.call(SET_POS, ecx=emu.camera, stack_args=tuple(f2u(v) for v in pos))
            probe.sw_set_camera(*pos, *rot)
            want = emu.render()
            n = probe.sw_render(buf, len(buf))
            if n < 0:
                raise SystemExit(f'{what}: port trace too long')
            got = parse_port(buf.raw[:n])
            err = compare(f'{what} at {pos} rot {rot}', want, got)
            if err:
                raise SystemExit(err)
            # The cockpit pass, 0x4f73f0: one object (and its children) after the frame.
            seen = sorted({e[1] for e in want[0] if e[0] == 1})  # drawn this frame: in view
            sub_i = srng.choice(seen) if seen and k % 4 else srng.choice(objs)
            want_s = emu.render_subtree(emu.node_ptr(sub_i))
            n = probe.sw_render_subtree(sub_i, 1, buf, len(buf))
            if n < 0:
                raise SystemExit(f'{what}: port trace too long')
            got_s = parse_port(buf.raw[:n])
            got_s = (got_s[0], got_s[1], got_s[2], [])
            err = compare(f'{what} subtree {sub_i}', want_s, got_s)
            if err:
                raise SystemExit(err)
            subtree_draws += sum(1 for e in want_s[0] if e[0] == 1)
            last = want[0]
            frames += 1
            draws += sum(1 for e in want[0] if e[0] == 1)
            for ring in want[3]:
                cover['cells'] += len(ring)
                cover['wrapped'] += sum(1 for c in ring if c[2])
            for e in want[0]:
                if e[0] == 1:
                    cover['casters'] += int(shadows == 1 and e[6] == 0)
                    cover['far_kept'] += int(bool(e[2] & 0x20))
                    cover['near'] += int(bool(e[2] & 0x10))
                    cover['sides'] += int(bool(e[2] & 0xf))
                    cover['faded'] += int(e[4] != 1.0)
                    cover['overlay'] += int(e[5] != 0)
                elif e[0] == 7:
                    cover['cell_casters'] += len(e[1])
                elif e[0] == 5:
                    cover['full_lists'] += int(len(e[1]) == 200)
                elif e[0] == 2:
                    cover['hidden'] += int(e[4] == 0)
            cover['clamped'] += int(virtual == 0)
            cover['zone_frames'] += int(k % 16 == 3)
            total_hse += sum(1 for e in want[0] if e[0] == 2)
        total_frames += frames
        total_draws += draws
        print(f'{wname}: {frames} frames, {draws} model draws identical')
    print('covered:', ', '.join(f'{k} {v}' for k, v in cover.items()))
    print(f'subtree passes (0x4f73f0): {subtree_draws} model draws identical')
    if subtree_draws < 50:
        raise SystemExit('coverage: too few subtree draws')
    print(f'ok: {total_frames} frames on {len(WORLDS)} worlds, {total_draws} model draws (node, mask, '
          f'matrix, state) and {total_hse} hidden-surface calls in order; {boxes} box/sphere and {hulls} hull '
          f'checks bit-exact')


if __name__ == '__main__':
    main()
