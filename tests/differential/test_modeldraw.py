"""The ported Direct3D model draw (0x53f2e0) against the original x86 on real
worlds (docs/drafts/modeldraw.md). Each world is read twice: by the original
0x4ff520 in Unicorn and by the port's loaders. Then, for many nodes, cameras,
plane masks and render states, the original draws the node's model with the
draw table [0x72f3b0].. and the calls not ported (0x541df0 lens flares,
0x544ed0 light bake, the device colour calls) stubbed by recorders; the port
draws the same node through tests/probe_modeldraw.cpp with recorders of its
own. Lighting is real on both sides: the world's sun updated for each view
(0x4f2f60 / 0x4f2f30 against gmod_light), 0x53daa0 and 0x545720 inside the
draw. Every recorded call (function, arguments, the TL vertices and work
records it reads, the primitive) must match, the FPU at 24 bits as in play.

Edits per draw (both sides) reach what the shipped data never does: strips
(0x400), point models, type 3, morphs, facades turned about their centre,
model flag 0x20, priorities, model-light modes, node bake fields, material
cycles (null ones included), shadows on without casters, a second pass.

    cmake --build build && .venv/bin/python tests/differential/test_modeldraw.py [draws per world] [worlds...]
      --coverage   list the original's instructions the run never executed
      --real-mv    the original builds its own model-view (0x51fb30) instead of
                   getting the port's (its translation z sums in another order)
    MW3_PROBE=<lib>  another libmw3probe (an -ffp-contract=off build matches
                     all but the fog factors and trig bit for bit)
    MW3_DUMP=1   details of the first difference
"""
import ctypes
import math
import os
import random
import struct
import sys
from pathlib import Path

os.environ.setdefault('MW3_X87', '24')
sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT  # noqa: E402
from modeldraw_common import CIASIN, COPYSIGN, Stats, f32, fbits, mat_mul, rotation, same_float, scenario  # noqa: E402,F401
from test_gamez import ZBD, Emulated, probe  # noqa: E402

if os.environ.get('MW3_PROBE'):  # another build of libmw3probe (e.g. one with -ffp-contract=off)
    probe = ctypes.CDLL(os.environ['MW3_PROBE'])

DRAW, SET_VIEW, SET_PROJECTION, ZERR, MODEL_VIEW = 0x53f2e0, 0x520b00, 0x521c60, 0x49bfd0, 0x51fb30
# By default the original gets the port's model-view matrix (0x51fb30, checked by
# test_matrix.py) so both sides transform with the same one; --real-mv runs the
# original's own (its translation z sums in another order: see the draft).
REAL_MODEL_VIEW = '--real-mv' in sys.argv
if REAL_MODEL_VIEW:
    sys.argv.remove('--real-mv')
# --coverage lists the instructions of the ported functions the run never executed.
COVERAGE = '--coverage' in sys.argv
if COVERAGE:
    sys.argv.remove('--coverage')
PORTED = {'draw 0x53f2e0': (0x53f2e0, 0x54174e), 'cycle 0x5376c0': (0x5376c0, 0x53779a),
          'reject 0x535d20': (0x535d20, 0x535e1f), 'centre 0x53dcf0': (0x53dcf0, 0x53de44),
          'billboard 0x51ea90': (0x51ea90, 0x51efac), 'billboard 0x51efb0': (0x51efb0, 0x51f385),
          'quat 0x525a50': (0x525a50, 0x525bd7), 'quat matrix 0x525740': (0x525740, 0x525830),
          'perp 0x51e740': (0x51e740, 0x51e7ab), 'normalize 0x4020d0': (0x4020d0, 0x402130)}
EXECUTED = set()
# Relative, per float. The port runs zmth (fused multiply-adds on arm64) and the
# fog factors (gmod_fogcalc, in double), so last bits differ, and projection
# near z = 0 magnifies them; with an -ffp-contract=off build everything but the
# fog factors is bit-identical.
TOLERANCE = float(os.environ.get("MW3_TOL", "1e-4"))
# Facades also turn by zmth's sinf/cosf and atan2, last-bit apart from the x87's
# fsin/fcos/fpatan; projection near the camera magnifies that.
FACADE_TOLERANCE = 1e-3
# The fog factors (work record +8) come from gmod_fogcalc, which sums in double
# (exact for the x87 at 64 bits, not at the 24 the game runs): ~1e-6 apart, more
# once a screen clip interpolates them.
FOG_TOLERANCE = 1e-3
TABLE = {0x72f3b0: 0, 0x72f3b8: 1, 0x72f3c0: 2, 0x72f3c4: 3, 0x72f3c8: 4, 0x72f3d0: 5, 0x72f3d4: 6,
         0x72f3d8: 7, 0x72f3e0: 8, 0x72f3cc: 11, 0x72f3dc: 12}
POPS = {0: 12, 1: 20, 2: 8, 3: 16, 4: 12, 5: 0, 6: 4, 7: 4, 8: 4, 11: 20, 12: 8}
TL, WORK, PRIM, CAM3D = 0x72f41c, 0x7481a8, 0x72f418, 0x7476a4
STACK_SLOT, STACK_FLAG, STACK_TOP_P, STACK_FLAG_P = 0x63d760, 0x63d7e8, 0x5eda90, 0x5eda8c
TEX_TABLE, TEX_COUNT = 0x610484, 0x610480
NODE_BLOCKS, NODE_BS, POOL = 0x60bf38, 0x5ea830, 0x7402e4

probe.probe_md_load.argtypes = [ctypes.c_char_p]
probe.probe_md_node.argtypes = [ctypes.c_int, ctypes.c_void_p]
probe.probe_md_textures.argtypes = [ctypes.c_char_p, ctypes.c_int]
probe.probe_md_camera.argtypes = [ctypes.c_char_p] * 5
probe.probe_md_state.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
probe.probe_md_model_view.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
probe.probe_md_set_model.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint]
probe.probe_md_set_poly.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint]
probe.probe_md_set_morphs.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
for fn in ('probe_md_set_poly_word', 'probe_md_set_light', 'probe_md_set_cycle'):
    getattr(probe, fn).argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
probe.probe_md_set_node.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint]
probe.probe_md_draw.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_int]


class Original:
    """The emulator with one world loaded and the model draw's surroundings stubbed."""

    def __init__(self, world):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        path = ZBD / world / 'gamez.zbd'
        self.emu.open_ok = True
        res = self.emu.read_zbd(path.read_bytes(), str(path).encode())
        assert struct.unpack_from('<i', res)[0] == 0
        self.out = []
        self.calls = 0
        self.fake_lights = 0
        for addr, which in TABLE.items():
            stub = cpu.code(b'\xc3')
            self.hook(stub, lambda c, w=which: self.table(w), POPS[which])
            cpu.u32(addr, stub)
        fog_send = cpu.code(b'\xc3')
        self.hook(fog_send, lambda c: self.put(struct.pack('<I', 10) + c.read(0x72f400, 12)), 0)
        cpu.u32(0x72f3f0, fog_send)
        self.hook(0x5798a0, lambda c: self.put(struct.pack('<I', 15) + c.read(c.ecx, 12)), 0)
        self.hook(0x579980, lambda c: self.put(struct.pack('<I', 16)), 0)
        self.hook(0x5680c0, lambda c: self.put(struct.pack('<I', 17) + c.read(c.ecx, 12)), 0)
        self.hook(0x544ed0, lambda c: self.put(struct.pack('<I', 13)), 0)
        self.hook(0x4ed980, lambda c: 0, 0)
        self.hook(ZERR, lambda c: self.put(struct.pack('<II', 14, c.arg(2))), 0)
        if not REAL_MODEL_VIEW:
            self.hook(MODEL_VIEW, self.model_view, 0)
        self.hook(0x567420, lambda c: self.put(struct.pack('<I', 18)), 8)  # the lens-flare list
        cpu.u32(0x74639c, 0x74039c)
        cpu.u32(0x7463a0, 0x74339c)
        cpu.u32(0x72f348, 1)
        cpu.u32(0x5bb5ac, cpu.code(COPYSIGN))  # the facade modes' import slot
        cpu.write(0x5ac380, CIASIN)
        cpu.u32(0x74a2c8, 0xffffffff)
        cpu.u32(0x74a2cc, 0x74a2c8)
        cpu.u32(0x74981c, 0)
        cpu.u32(0x5e9e2c, 1)
        for a in (0x74a264, 0x74a270, 0x7489a8, 0x747ca4):
            cpu.u32(a, 0)
        cpu.write(0x72f3f4, bytes(24))
        self.world_buf = self.emu.alloc(48)
        self.cam_buf = self.emu.alloc(48)
        self.camera_data = self.emu.alloc(0x1e8)
        self.texture_count = struct.unpack('<i', cpu.read(TEX_COUNT, 4))[0]
        for a, v in ((0x74a2d4, 0), (0x5ef860, 0)):  # one light per vertex, no clouds
            cpu.u32(a, v)
        cpu.write(0x73fbb0, bytes(12))
        self.world_node = None
        for i in range(self.i32(0x60bf34)):
            a = self.node_addr(i)
            h = cpu.read(a, 0xd4)
            if struct.unpack_from('<I', h, 0xd0)[0] & 0x1000000:
                if struct.unpack_from('<i', h, 0x34)[0] == 2 and self.world_node is None:
                    self.world_node = a
        # The static-light bake (polygon +0x18) starts at 0 on both sides.
        pool, size = self.i32(POOL) & 0xffffffff, self.i32(0x7402e0)
        for mi in range(size):
            m = pool + 0x60 * mi
            polys, count = self.i32(m + 0x34) & 0xffffffff, self.i32(m + 0x10)
            for k in range(max(count, 0)):
                q = cpu.read(polys + 0x24 * k, 0x24)
                scratch = struct.unpack_from('<I', q, 0x18)[0]
                if scratch:
                    cpu.write(scratch, bytes(4 * (q[0])))
        self.light_slot = self.emu.alloc(48)
        if COVERAGE:
            from unicorn import UC_HOOK_CODE
            for lo, hi in PORTED.values():
                cpu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, n, u: EXECUTED.add(a), begin=lo, end=hi - 1)

    def hook(self, addr, fn, pops):
        cpu = self.cpu
        if addr not in cpu.stubs:
            cpu.stubs[addr] = (fn, pops)
            from unicorn import UC_HOOK_CODE
            cpu.uc.hook_add(UC_HOOK_CODE, cpu._stub_hit, begin=addr, end=addr)
        else:
            cpu.stubs[addr] = (fn, pops)

    def model_view(self, c):
        top = self.i32(STACK_TOP_P) & 0xffffffff
        below = c.read(self.i32(top - 4) & 0xffffffff, 48)
        out = (ctypes.c_float * 12)()
        probe.probe_md_model_view(below, out)
        c.write(self.i32(top) & 0xffffffff, bytes(out))
        c.u32(self.i32(STACK_FLAG_P) & 0xffffffff, 0)

    def fail(self, addr):
        raise SystemExit(f'the original called {addr:#x}, which the test does not stand in for')

    def put(self, b):
        self.out.append(b)

    def i32(self, a):
        return struct.unpack('<i', self.cpu.read(a, 4))[0]

    def verts(self, count):
        count = max(0, min(count, 64))
        r = self.cpu.read
        return b''.join(r(TL + 0x20 * i, 0x20) + r(WORK + 0x20 * i, 0x18) for i in range(count))

    def sets(self, count):
        """The projected-shadow sets (0x7489a8): count, textures, each set's UVs."""
        count = max(0, min(count, 64))
        n = self.i32(0x7489a8)
        r = self.cpu.read
        return (struct.pack('<i', n) + r(0x7489ac, 4 * n) +
                b''.join(r(0x7489c8 + 0x200 * k, 8 * count) for k in range(max(n, 0))))

    def table(self, which):
        c = self.cpu
        ecx, edx, a = c.ecx, c.edx, c.arg
        prim = struct.pack('<i', self.i32(PRIM))
        if which == 0:
            self.put(struct.pack('<6I', 0, ecx, edx, a(0), a(1), a(2)) + prim + self.verts(a(0)))
        elif which == 1:
            self.put(struct.pack('<2I', 1, ecx) + c.read(edx, 12) + struct.pack('<5I', *(a(k) for k in range(5)))
                     + prim + self.verts(a(2)))
        elif which == 2:
            self.put(struct.pack('<5I', 2, ecx, edx, a(0), a(1)) + prim + self.verts(ecx))
        elif which == 3:
            self.put(struct.pack('<7I', 3, ecx, edx, a(0), a(1), a(2), a(3)) + prim + self.verts(edx))
        elif which == 4:
            self.put(struct.pack('<6I', 4, ecx, edx, a(0), a(1), a(2)) + prim + self.verts(edx))
        elif which == 5:
            self.put(struct.pack('<3I', 5, ecx, edx) + prim + self.verts(ecx) + self.sets(ecx))
        elif which in (6, 7):
            self.put(struct.pack('<4I', which, ecx, edx, a(0)) + prim + self.verts(edx) + self.sets(edx))
        elif which == 11:
            n = max(0, min(a(0), 64))
            self.put(struct.pack('<7I', 11, edx, a(0), a(1), a(2), a(3), a(4)) + prim + c.read(ecx, 12 * n)
                     + self.verts(n))
        elif which == 12:
            n = max(0, min(a(0), 64))
            self.put(struct.pack('<4I', 12, edx, a(0), a(1)) + prim + c.read(ecx, 12 * n) + self.verts(n)
                     + self.sets(n))
        elif which == 8:
            self.put(struct.pack('<I', 8) + c.read(ecx, 12) + struct.pack('<II', edx, a(0)))

    def model_light(self, c):
        self.put(struct.pack('<I', 9) + c.read(c.ecx, 12) + struct.pack('<I', c.edx & 0xffff))

    def node_addr(self, i):
        bs = self.i32(NODE_BS)
        blocks = self.i32(NODE_BLOCKS) & 0xffffffff
        return (self.i32(blocks + 4 * (i // bs)) & 0xffffffff) + (i % bs) * 0xd4

    def model_addr(self, mi):
        return (self.i32(POOL) & 0xffffffff) + 0x60 * mi

    def set_model(self, mi, off, v):
        self.cpu.u32(self.model_addr(mi) + off, v)
        probe.probe_md_set_model(mi, off, v)

    def model_u32(self, mi, off):
        return struct.unpack('<I', self.cpu.read(self.model_addr(mi) + off, 4))[0]

    def poly_flags(self, mi):
        polys = self.model_u32(mi, 0x34)
        return [struct.unpack('<I', self.cpu.read(polys + 0x24 * k, 4))[0] for k in range(self.model_u32(mi, 0x10))]

    def set_poly(self, mi, k, flags):
        self.cpu.u32(self.model_u32(mi, 0x34) + 0x24 * k, flags)
        probe.probe_md_set_poly(mi, k, flags)

    def u32_at(self, a):
        return struct.unpack('<I', self.cpu.read(a, 4))[0]

    def poly_word(self, mi, k, off, v=None):
        a = self.model_u32(mi, 0x34) + 0x24 * k + off
        old = self.u32_at(a)
        if v is not None:
            self.cpu.u32(a, v)
            probe.probe_md_set_poly_word(mi, k, off, v)
        return old

    def light_word(self, mi, li, off, v=None):
        a = self.model_u32(mi, 0x40) + 0x4c * li + off
        old = self.u32_at(a) & (0xffff if off == 0x28 else 0xffffffff)
        if v is not None:
            if off == 0x28:
                self.cpu.write(a, struct.pack('<H', v & 0xffff))
            else:
                self.cpu.u32(a, v)
            probe.probe_md_set_light(mi, li, off, v)
        return old

    def node_word(self, i, off, v=None):
        a = self.node_addr(i) + off
        old = self.u32_at(a)
        if v is not None:
            self.cpu.u32(a, v)
            probe.probe_md_set_node(i, off, v)
        return old

    def material(self, mi, k):
        return self.u32_at(self.model_u32(mi, 0x34) + 0x24 * k + 0x1c)

    def cycle_word(self, mi, k, off, v=None):
        c = self.u32_at(self.material(mi, k) + 0x24)
        old = self.u32_at(c + off)
        if v is not None:
            self.cpu.u32(c + off, v)
            probe.probe_md_set_cycle(mi, k, off, v)
        return old

    def null_cycle(self, mi, k, restore=None):
        a = self.material(mi, k) + 0x24
        if restore is None:
            old = self.u32_at(a)
            self.cpu.u32(a, 0)
            probe.probe_md_set_cycle(mi, k, -1, 0)
            return old
        self.cpu.u32(a, restore)
        probe.probe_md_set_cycle(mi, k, -2, 0)

    def set_morphs(self, mi, deltas):
        a = self.emu.alloc(len(deltas))
        self.cpu.write(a, deltas)
        self.cpu.u32(self.model_addr(mi) + 0x44, a)
        probe.probe_md_set_morphs(mi, deltas, len(deltas) // 12)

    def textures(self, flags):
        cpu = self.cpu
        for i, f in enumerate(flags):
            img = self.emu.alloc(16)
            cpu.write(img, bytes(9) + bytes([f]) + bytes(6))
            cpu.u32(TEX_TABLE + 0x28 * i, img)
            cpu.u32(TEX_TABLE + 0x28 * i + 4, 0x50000000 + 0x10 * i)

    def camera(self, cam, proj, clip, second):
        cpu = self.cpu
        cpu.write(self.cam_buf, fbits(*cam))
        cpu.call(SET_VIEW, ecx=self.cam_buf)
        cpu.call(SET_PROJECTION, stack_args=[struct.unpack('<I', fbits(p))[0] for p in proj])
        cpu.write(0x7402f8, clip[:0x24])
        cpu.write(0x74031c, clip[0x24:0x34])
        cpu.write(0x740338, second[:0x24])
        cpu.write(0x74038c, second[0x24:0x34])
        r = cpu.read
        return r(0x73fbc0, 48) + r(0x73fc00, 48) + r(0x73fb80, 36) + r(0x73fc30, 4)

    def state(self, st, fog):
        cpu = self.cpu
        cpu.write(0x74a26c, fbits(st[0]))
        cpu.u32(0x74a268, int(st[1]))
        cpu.u32(0x749808, int(st[2]))
        cpu.u32(0x74a32c, int(st[3]))
        cpu.write(0x74a284, fbits(st[4]))
        cpu.u32(0x72f338, int(st[5]))
        cpu.write(0x72c52c, fbits(st[6]))
        cpu.u32(0x749814, 0)
        cpu.u32(0x74981c, 0)
        cpu.u32(0x74a264, int(st[9]))  # shadows on, with no casters ([0x7402c4] = 0)
        cpu.u32(0x7402c4, 0)
        if st[7]:  # the world's lights for this view (0x4f2f60, 0x4f2f30), as the frame does
            cpu.u32(STACK_SLOT + 4, self.light_slot), cpu.u32(STACK_FLAG + 4, 0)
            cpu.u32(STACK_TOP_P, STACK_SLOT + 4), cpu.u32(STACK_FLAG_P, STACK_FLAG + 4)
            cpu.call(0x4f2f60, ecx=self.world_node)
            cpu.call(0x4f2f30, ecx=self.world_node, edx=0)
        cpu.write(self.camera_data + 0x3c, fbits(st[8]))
        cpu.u32(0x7402f4, self.camera_data)
        cpu.write(0x74a22c, fog)

    def draw(self, i, world, mask):
        cpu = self.cpu
        cpu.write(self.world_buf, fbits(*world))
        cpu.u32(STACK_SLOT, self.world_buf)
        cpu.u32(STACK_FLAG, 0)
        cpu.u32(STACK_TOP_P, STACK_SLOT)
        cpu.u32(STACK_FLAG_P, STACK_FLAG)
        cpu.write(0x30000000 - 0x8000, bytes(0x7000))  # a clean stack: the draw reads some locals unset
        self.out, self.calls = [], 0
        cpu.call(DRAW, ecx=self.node_addr(i), edx=mask)
        assert self.i32(STACK_TOP_P) == STACK_SLOT, 'matrix stack not balanced'
        return b''.join(self.out)


# --- The event stream: parse both sides' bytes the same way. ---
# u: exact u32, f: float, c: the vertex count (exact). Kinds 0-7, 11 and 12
# carry vertices (TL + work record, 14 dwords each), 11 and 12 first three
# light floats per vertex, 5-7 and 12 then the projected-shadow sets.
FORMATS = {0: 'uuc' 'uuu', 1: 'ufffuucuuu', 2: 'cufuu', 3: 'ucuufuu', 4: 'ucufuu', 5: 'cuu', 6: 'ucuu',
           7: 'ucuu', 8: 'fffuu', 9: 'fffu', 10: 'fff', 11: 'ucuufuu', 12: 'ucuu', 13: '', 14: 'u', 15: 'fff',
           16: '', 17: 'fff', 18: ''}
VERTEX = 'ffffuuff' 'ffffff'
WITH_VERTS = (0, 1, 2, 3, 4, 5, 6, 7, 11, 12)
WITH_LIGHT = (11, 12)
WITH_SETS = (5, 6, 7, 12)


def parse(b):
    """Each event: [kind, fields...], a float as ('f', value, label)."""
    ev, p = [], 0
    while p < len(b):
        kind, = struct.unpack_from('<I', b, p)
        p += 4
        fields, count = [kind], 0

        def f(label):
            nonlocal p
            v = ('f', struct.unpack_from('<f', b, p)[0], label)
            p += 4
            return v

        def u():
            nonlocal p
            v, = struct.unpack_from('<I', b, p)
            p += 4
            return v
        for j, ch in enumerate(FORMATS[kind]):
            if ch == 'f':
                fields.append(f(j))
            else:
                v = u()
                fields.append(v)
                if ch == 'c':
                    count = v
        n = max(0, min(count, 64))
        if kind in WITH_LIGHT:
            fields += [f('light') for _ in range(3 * n)]
        if kind in WITH_VERTS:
            for _ in range(n):
                for k, ch in enumerate(VERTEX):
                    fields.append(f('v%d' % k) if ch == 'f' else u())
        if kind in WITH_SETS:
            sets = u()
            fields.append(sets)
            fields += [u() for _ in range(sets)]
            fields += [f('uv') for _ in range(2 * n * sets)]
        ev.append(fields)
    return ev


def compare(want, got, stats, tol=TOLERANCE, pixels=0.0):
    """None when equal, else a description of the first difference."""
    a, b = parse(want), parse(got)
    for k, (x, y) in enumerate(zip(a, b)):
        if len(x) != len(y) or x[0] != y[0]:
            return f'event {k}: original {x[:8]} port {y[:8]}'
        for j, (u, v) in enumerate(zip(x, y)):
            if isinstance(u, tuple):
                at = (x[0], u[2])
                t = max(tol, FOG_TOLERANCE) if u[2] == 'v10' else tol
                px = pixels if u[2] in ('v0', 'v1') else 0.0
                if not isinstance(v, tuple) or not same_float(u[1], v[1], stats, t, at, px):
                    return f'event {k} (kind {x[0]}) field {j}: original {u} port {v}'
            elif u != v:
                return f'event {k} (kind {x[0]}) field {j}: original {u:#x} port {v:#x}'
    if len(a) != len(b):
        return f'{len(a)} events in the original, {len(b)} in the port; next {(a + b)[min(len(a), len(b))][:6]}'
    return None


def edit(orig, rng, mi, mtype, node):
    """Changes to the model for this draw, both sides, to reach what the shipped
    data never does: strips (polygon flag 0x400), point models (type 2), morphs
    (flag 8), facades turned about their centre (flag 0x40). Returns the undos."""
    undo = []

    def set_model(off, v):
        old = orig.model_u32(mi, off)
        orig.set_model(mi, off, v)
        undo.append(lambda off=off, old=old: orig.set_model(mi, off, old))
    flags = orig.model_u32(mi, 8)
    if rng.random() < 0.12:
        old = orig.poly_flags(mi)
        for k, f in enumerate(old):
            if rng.random() < 0.8:
                orig.set_poly(mi, k, f | 0x400)
        undo.append(lambda old=old: [orig.set_poly(mi, k, f) for k, f in enumerate(old)])
    if mtype == 0 and rng.random() < 0.05 and orig.model_u32(mi, 0x10) > 0:
        set_model(0, 2)
    if rng.random() < 0.1:
        n = orig.model_u32(mi, 0x14)
        count = rng.choice([n, n, max(0, n - 2), 1])
        # One shift for the model with a little per-vertex noise: random deltas
        # crumple polygons into slivers whose normals hang on the last bit.
        shift = [rng.uniform(-2, 2) for _ in range(3)]
        orig.set_morphs(mi, fbits(*(shift[k % 3] * rng.uniform(0.9, 1.1) for k in range(3 * max(n, 1)))))
        set_model(0x1c, count)
        set_model(0x24, struct.unpack('<I', fbits(rng.choice([0.5, 1.0, -0.3, 0.0])))[0])
        flags |= 8
    if mtype == 1 and rng.random() < 0.3:
        flags |= 0x40
    if rng.random() < 0.3:
        flags |= 0x20  # no clouds: the lighting is told so (vary flag 4)
    if mtype == 0 and rng.random() < 0.03:
        set_model(0, 3)  # an unknown type draws as a default model
    polys = orig.model_u32(mi, 0x10)
    if rng.random() < 0.15:
        for k in range(polys):
            if rng.random() < 0.5:
                old = orig.poly_word(mi, k, 4, rng.randrange(1, 16))
                undo.append(lambda k=k, old=old: orig.poly_word(mi, k, 4, old))
    lights = orig.model_u32(mi, 0x20)
    for li in range(lights if rng.random() < 0.6 else 0):
        n = orig.light_word(mi, li, 0xc)
        if n == 0 or n > 64:
            continue
        for off, v in ((0, rng.choice([0, 1, 1, 2])), (4, rng.choice([0, 1, 1])),
                       (8, struct.unpack('<I', fbits(rng.choice([0.0, 0.01, 0.1, 1.0])))[0]),
                       (0x10, struct.unpack('<I', fbits(rng.uniform(0, 0.3)))[0]),
                       (0x14, rng.randrange(n) | rng.randrange(0x10000) << 16),
                       (0x28, rng.randrange(0x10000))):
            old = orig.light_word(mi, li, off, v)
            undo.append(lambda li=li, off=off, old=old: orig.light_word(mi, li, off, old))
        # What 0x541df0 reads: the priority (+0x18, a short), the fade
        # (near +0x30, far +0x34, rate +0x38) and the flare flag (+0x3c).
        if rng.random() < 0.7:
            near = rng.choice([0.0, 5.0, 40.0, rng.uniform(0, 200)])
            far = rng.choice([0.0, 0.0, near + rng.uniform(1, 400), near])
            prio = (orig.light_word(mi, li, 0x18) & 0xffff0000) | (rng.choice([0, 0, 1, 3, -2, 15]) & 0xffff)
            for off, v in ((0x18, prio), (0x30, struct.unpack('<I', fbits(near))[0]),
                           (0x34, struct.unpack('<I', fbits(far))[0]),
                           (0x38, struct.unpack('<I', fbits(rng.choice([0.5, 1.0, 2.55, 10.0])))[0]),
                           (0x3c, rng.choice([0, 0, 0, 1]))):
                old = orig.light_word(mi, li, off, v)
                undo.append(lambda li=li, off=off, old=old: orig.light_word(mi, li, off, old))
    if rng.random() < 0.3:
        for off, v in ((0xc0, rng.choice([0, 1, 5])), (0xc4, rng.choice([0, 0x7fffffff, 3]))):
            old = orig.node_word(node, off, v)
            undo.append(lambda off=off, old=old: orig.node_word(node, off, old))
    cycled = [k for k in range(polys) if orig.material(mi, k) and orig.cpu.read(orig.material(mi, k) + 1, 1)[0] & 5 == 5]
    if cycled and rng.random() < 0.5:
        k = rng.choice(cycled)
        if rng.random() < 0.15:
            old = orig.null_cycle(mi, k)
            undo.append(lambda k=k, old=old: orig.null_cycle(mi, k, old))
        else:
            for off, v in ((0, rng.choice([0, 1])), (8, struct.unpack('<I', fbits(rng.uniform(0, 30)))[0]),
                           (0xc, struct.unpack('<I', fbits(rng.choice([-3.5, -1.5, 2.0, 40.0])))[0])):
                old = orig.cycle_word(mi, k, off, v)
                undo.append(lambda off=off, old=old: orig.cycle_word(mi, k, off, old))
    if flags != orig.model_u32(mi, 8):
        set_model(8, flags)
    return undo


def run_world(world, draws, rng, stats, breaks=False):
    orig = Original(world)
    path = ZBD / world / 'gamez.zbd'
    if probe.probe_md_load(str(path).encode()) != 0:
        raise SystemExit(f'{world}: the port could not read {path}')
    flags = bytes(rng.choice([0, 0, 2]) for _ in range(orig.texture_count))
    orig.textures(flags)
    probe.probe_md_textures(flags, len(flags))
    info = (ctypes.c_int * 5)()
    nodes = []
    for i in range(probe.probe_md_node_count()):
        probe.probe_md_node(i, info)
        if info[0] and info[2] >= 0 and info[3] in (0, 1, 2):
            nodes.append((i, info[2], info[3]))
    # Polygons over 64 vertices overrun the original's buffers (only the inout
    # areas have them, deleted at mission load; the port skips them).
    nodes = [n for n in nodes if n[2] in PORTED_TYPES and probe.probe_md_max_poly(n[1]) <= 64]
    buf = ctypes.create_string_buffer(1 << 22)
    events = calls = 0
    kinds = {}
    for k in range(draws):
        i, mi, mtype = rng.choice(nodes)
        m = orig.model_addr(mi)
        h = orig.cpu.read(m, 0x60)
        centre = struct.unpack_from('<3f', h, 0x48)
        radius, = struct.unpack_from('<f', h, 0x54)
        world_m, cam, proj, clip, second, mask, st, fog = scenario(rng, centre, radius)
        st[5] = k + 2
        views = orig.camera(cam, proj, clip, second)
        probe.probe_md_camera(fbits(*cam), fbits(*proj), clip, second, views)
        orig.state(st, fog)
        probe.probe_md_state(fbits(*st), fog)
        undo = edit(orig, rng, mi, mtype, i)
        # The turns about one axis, which no shipped world uses; not with a camera
        # exactly on the node, where the direction to it is rounding noise.
        if mtype == 1 and rng.random() < 0.4 and any(abs(a - b) >= 1e-3 for a, b in zip(world_m[9:], cam[9:])):
            old_mode = orig.model_u32(mi, 4)
            orig.set_model(mi, 4, rng.choice((2, 3)))
            undo.append(lambda old=old_mode: orig.set_model(mi, 4, old))
        if mtype == 1:
            # Facades turn by zmth's sin/cos and atan2, a last bit apart; with vertices behind the camera and no near clip (a mask the
            # cull never leaves for a sphere crossing the near plane) that bit
            # grows into pixels.
            mask |= 0x10
        want = orig.draw(i, world_m, mask)
        n = probe.probe_md_draw(i, fbits(*world_m), mask, buf, len(buf))
        got = buf.raw[:n]
        loose = mtype == 1  # facades: zmth's trig
        for fn in reversed(undo):
            fn()
        tol = FACADE_TOLERANCE if loose else TOLERANCE
        why = compare(want, got, stats, tol, 0.01 if loose else 0.0)
        if why and os.environ.get('MW3_DUMP'):
            nv = orig.model_u32(mi, 0x14)
            pc = (ctypes.c_float * (3 * nv))()
            probe.probe_md_cam(pc, nv)
            oc = struct.unpack(f'<{3 * nv}f', orig.cpu.read(0x74039c, 12 * nv))
            print('loose', loose, 'model flags', hex(orig.model_u32(mi, 8)), 'type', orig.model_u32(mi, 0), 'morph', orig.model_u32(mi, 0x1c))
            ea, eb = parse(want), parse(got)
            for x, y in zip(ea, eb):
                if x != y:
                    n0 = len(FORMATS[x[0]]) + 1
                    for vi in range((len(x) - n0) // 14):
                        print('v', vi, [round(t[1], 5) if isinstance(t, tuple) else t for t in x[n0 + 14 * vi + 8:n0 + 14 * vi + 11]],
                              [round(t[1], 5) if isinstance(t, tuple) else t for t in y[n0 + 14 * vi + 8:n0 + 14 * vi + 11]])
                    break
            print('mode', orig.model_u32(mi, 4), 'normals', orig.model_u32(mi, 0x18))
            print('cam differ', [(j, a, b) for j, (a, b) in enumerate(zip(oc, pc)) if a != b][:10])
        if why:
            if breaks:
                return k
            raise SystemExit(f'{world} draw {k} (node {i}, model {mi}, mask {mask:#x}, state {st}): {why}')
        for e in parse(want):
            kinds[e[0]] = kinds.get(e[0], 0) + 1
        events += len(parse(want))
        calls += 1
    return calls, events, kinds


PORTED_TYPES = tuple(int(t) for t in os.environ.get("MW3_TYPES", "0,1").split(","))


def report_coverage():
    import re
    addrs = []
    for line in open(ROOT / 'analysis-output' / 'mech3.asm'):
        m = re.match(r'  ([0-9a-f]{6}):\s+\t(\S+)', line)
        if m and m.group(2) not in ('nop', '<unknown>'):
            addrs.append(int(m.group(1), 16))
    for name, (lo, hi) in PORTED.items():
        inside = [a for a in addrs if lo <= a < hi]
        missed = [a for a in inside if a not in EXECUTED]
        runs, start, prev = [], None, None
        for a in inside:
            if a in EXECUTED:
                if start is not None:
                    runs.append((start, prev))
                    start = None
            else:
                start = a if start is None else start
                prev = a
        if start is not None:
            runs.append((start, prev))
        text = ', '.join(f'{a:#x}' if a == b else f'{a:#x}..{b:#x}' for a, b in runs)
        print(f'{name}: {len(inside) - len(missed)}/{len(inside)} instructions; never run: {text or "none"}')


def main():
    draws = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    worlds = sys.argv[2:] or ['c1', 'c4b', 't1']
    rng = random.Random(1)
    stats = Stats()
    total = {}
    for w in worlds:
        calls, events, kinds = run_world(w, draws, rng, stats)
        for k, v in kinds.items():
            total[k] = total.get(k, 0) + v
        print(f'{w}: {calls} draws, {events} recorded calls identical')
    names = {0: 'flat', 1: 'lit', 2: 'textured', 3: 'textured lit', 4: 'textured per-vertex', 8: 'point',
             9: 'model light', 10: 'fog colour', 13: 'static bake', 14: 'zerr', 15: 'point colour', 16: 'send mix',
             17: 'mix fog'}
    print('calls: ' + ', '.join(f'{names.get(k, k)} {v}' for k, v in sorted(total.items())))
    if os.environ.get('MW3_WHERE'):
        print(sorted(stats.where.items(), key=lambda kv: -kv[1])[:20])
    print(f'floats: {stats.floats}, {stats.exact} bit-identical, worst relative difference {stats.worst:.2e}')
    if COVERAGE:
        report_coverage()
    print(f'ok: {sum(total.values())} recorded calls identical over {draws * len(worlds)} draws')


if __name__ == '__main__':
    # Leave without Unicorn's teardown (it can hang at exit): the status
    # SystemExit carries, 1 for any other fault.
    code = 0
    try:
        main()
    except SystemExit as ex:
        if ex.code not in (None, 0):
            print(ex.code if isinstance(ex.code, str) else '', flush=True)
        code = 0 if ex.code in (None, 0) else 1
    except BaseException:
        import traceback
        traceback.print_exc()
        code = 1
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(code)
