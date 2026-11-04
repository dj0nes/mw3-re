"""PM's Direct3D model draw (pm/gamez/zmodel/gmod_draw; docs/pm-map.md,
gmod_matl.c) against PM's exe on real worlds. Grows with the port; so far the
type dispatch 0x542a70 and what it calls: the stack's top, the facades' turn,
the points. Edits reach what shipped data never does (points, flag 0x40, morphs,
type 3). The FPU at 24 bits, as in play.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_modeldraw.py [draws per world]
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modeldraw_common import Stats, fbits, same_float, scenario  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402

PREPARE, PUSH, POP, SET_VIEW, SET_PROJECTION, MODEL_VIEW, CENTRE = 0x542a70, 0x538470, 0x5384a0, 0x53a5d0, 0x53b730, 0x5395f0, 0x5416e0
TOP_P, FLAG_P, SLOTS, FLAGS = 0x611220, 0x61121c, 0x66f4d0, 0x66f558
NEAR, CAMERA, D3D_ON, POINT, DEFAULT, CAM_P = 0x8d4b14, 0x8d4b04, 0x8c4b70, 0x8c4c08, 0xa4e22c, 0x8dabac
RECT, VIEW, CAMERA_M = 0x8d4b2c, 0x8d43c0, 0x8d4400
TOL, FACADE_TOL = 1e-4, 1e-3  # as test_modeldraw.py: zmth's trig against the x87's


class Emulated(g.Emulated):
    def __init__(self, world):
        super().__init__()
        self.cpu.fpu_control(0x007f)
        self.stack([world], 5, 6, 5, models=True, nodes=True)
        self.world, self.cam, self.local = self.alloc(48), self.alloc(48), self.alloc(48)
        self.camera = self.alloc(0x1e8)
        self.centre = self.alloc(12)
        self.points = b''
        stub = self.cpu.code(b'\xc3')
        self.native_stub(stub, self.point)
        self.w32(POINT, stub)
        self.w32(D3D_ON, 1)
        self.w32(CAM_P, 0x8d4bac)  # 0x53fe30's, which the bench does not run
        self.w32(CAMERA, self.camera)
        self.native_stub(MODEL_VIEW, self.model_view)

    def point(self, c):
        self.points += self.read(c.arg(0), 12) + struct.pack('<II', c.arg(1) & 0xffff, c.arg(2))
        return 0

    def model_view(self, c):
        top = self.i32(TOP_P) & 0xffffffff
        below = self.read(self.i32(top - 4) & 0xffffffff, 48)
        out = (ctypes.c_float * 12)()
        self.probe.probe_pm_model_view(below, out)
        self.cpu._uc.mem_write(self.i32(top) & 0xffffffff, bytes(out))
        self.w32(self.i32(FLAG_P) & 0xffffffff, 0)
        return 0

    def node(self, i):
        blocks = self.i32(g.N_BLOCKS) & 0xffffffff
        return (self.i32(blocks + 4 * (i // 2500)) & 0xffffffff) + 0xd4 * (i % 2500)

    def view(self, cam, proj, near, rect, yaw):
        uc = self.cpu._uc
        uc.mem_write(self.cam, fbits(*cam))
        self.call(SET_VIEW, args=[self.cam])
        self.call(SET_PROJECTION, args=[struct.unpack("<I", fbits(p))[0] for p in proj])
        uc.mem_write(NEAR, fbits(near))
        uc.mem_write(RECT, fbits(*rect))
        uc.mem_write(self.camera + 0x3c, fbits(yaw))

    def prepare(self, i, world):
        uc = self.cpu._uc
        uc.mem_write(self.world, fbits(*world))
        self.w32(SLOTS, self.world)
        self.w32(FLAGS, 0)
        self.w32(TOP_P, SLOTS)
        self.w32(FLAG_P, FLAGS)
        self.call(PUSH, args=[self.local])
        self.points = b''
        node = self.node(i)
        model = self.i32(node + 0x3c) & 0xffffffff
        ret = self.call(PREPARE, args=[node, model])
        out = struct.pack('<ii', ret, self.i32(DEFAULT)) + self.read(self.local, 48) + self.read(self.world, 48)
        # The centre stays a local of 0x542a70: asked for again, with flag 0x40.
        centre = bytes(12)
        if self.i32(model) == 1 and self.i32(model + 8) & 0x40:
            self.call(CENTRE, args=[model, self.centre])
            centre = self.read(self.centre, 12)
        out += centre
        out += struct.pack('<i', len(self.points) // 20) + self.points
        if self.i32(model) == 2 and self.i32(model + 0x14) > 0:
            out += self.read(self.i32(CAM_P) & 0xffffffff, self.i32(model + 0x14) * 12)
        self.call(POP)
        return out


def compare(want, got, stats, tol):
    if len(want) != len(got):
        return f'{len(got)} bytes, the exe {len(want)}'
    if want[:8] != got[:8]:
        return f'result, default {struct.unpack_from("<ii", got)}, the exe {struct.unpack_from("<ii", want)}'
    np_ = struct.unpack_from('<i', want, 116)[0]
    if np_ != struct.unpack_from('<i', got, 116)[0]:
        return f'{struct.unpack_from("<i", got, 116)[0]} points, the exe {np_}'
    if want[104:116] != got[104:116]:  # the centre, bit for bit
        return f'centre {struct.unpack_from("<3f", got, 104)}, the exe {struct.unpack_from("<3f", want, 104)}'
    for off in range(8, len(want), 4):
        if 104 <= off <= 116:
            continue
        if 120 <= off < 120 + 20 * np_ and (off - 120) % 20 >= 12:  # a point's colour, alpha
            if want[off:off + 4] != got[off:off + 4]:
                return f'point field at {off}'
            continue
        a, b = struct.unpack_from('<f', want, off)[0], struct.unpack_from('<f', got, off)[0]
        if not same_float(a, b, stats, tol, off):
            return f'float at {off}: {b}, the exe {a}'
    return None


def main():
    draws = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    p = test_pm_zar.load_pm_probe()
    F, V = ctypes.c_float, ctypes.c_void_p
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_draw_view.argtypes = [V, V, F, V, F, V]
    p.probe_pm_draw_view.restype = None
    p.probe_pm_model_view.argtypes = [V, V]
    p.probe_pm_model_view.restype = None
    p.probe_pm_set_model.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint32]
    p.probe_pm_set_model.restype = None
    p.probe_pm_set_morphs.argtypes = [ctypes.c_int, V, ctypes.c_int]
    p.probe_pm_set_morphs.restype = None
    p.probe_pm_prepare.argtypes = [ctypes.c_int, V, V, ctypes.c_int]
    buf = ctypes.create_string_buffer(1 << 22)
    Emulated.probe = p
    rng = random.Random(0x542a70)
    stats, kinds = Stats(), {}
    for w in ('c1', 'c3'):
        path = g.ZBD / w / 'gamez.zbd'
        emu = Emulated(path)
        assert p.probe_pm_draw_load(str(path).encode()) == 0
        cap = emu.i32(g.N_CAPACITY)
        nodes = []
        for i in range(cap):
            n = emu.node(i)
            r = emu.read(n, 0xd4)
            if struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 and struct.unpack_from('<I', r, 0x3c)[0]:
                nodes.append(i)
        for d in range(draws):
            i = rng.choice(nodes)
            model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
            saved = {off: emu.i32(model + off) & 0xffffffff for off in (0, 4, 8, 0x1c, 0x24)}
            saved_m = emu.i32(model + 0x44) & 0xffffffff

            def set_model(off, v):
                emu.w32(model + off, v)
                p.probe_pm_set_model(i, off, v)
            kind = rng.choice(('as is', 'as is', 'points', 'points', 'facade 0', 'facade 1', 'centre', 'type 3', 'morph'))
            nv = saved and emu.i32(model + 0x14)
            if kind in ('points', 'morph') and emu.i32(model + 0x10) <= 0:
                kind = 'type 3'  # the exe reads through a null polygon list: pm_points_without_polygons
            if kind == 'points':
                set_model(0, 2)
            elif kind in ('facade 0', 'facade 1'):
                set_model(0, 1), set_model(4, int(kind[-1]))
            elif kind == 'centre':
                set_model(0, 1), set_model(4, rng.choice((0, 1))), set_model(8, saved[8] | 0x40)
            elif kind == 'type 3':
                set_model(0, 3)
            elif kind == 'morph' and nv > 0:
                set_model(0, rng.choice((2, 2, 1)))
                if emu.i32(model) == 1:
                    set_model(4, 0), set_model(8, saved[8] | 0x48)
                else:
                    set_model(8, saved[8] | 8)
                # Points past a shorter morph list are the last draw's, on both sides.
                k = nv if emu.i32(model) == 2 else rng.randint(1, nv)
                deltas = struct.pack(f'<{k * 3}f', *(rng.uniform(-3, 3) for _ in range(k * 3)))
                a = emu.alloc(len(deltas))
                emu.cpu._uc.mem_write(a, deltas)
                emu.w32(model + 0x44, a)
                p.probe_pm_set_morphs(i, deltas, k)
                set_model(0x1c, k)
                emu.cpu._uc.mem_write(model + 0x24, fbits(rng.choice((0.0, 0.5, 1.0, -0.25))))
                p.probe_pm_set_model(i, 0x24, emu.i32(model + 0x24) & 0xffffffff)
            kinds[kind] = kinds.get(kind, 0) + 1
            world, cam, proj, clip, *_ = scenario(rng, (0, 0, 0), 50.0)
            near = proj[6]
            rect = (proj[0], proj[1], proj[0] + proj[2] * 2 - 0.001, proj[1] + proj[3] * 2 - 0.001)
            yaw = rng.uniform(-3.2, 3.2)
            emu.view(cam, proj, near, rect, yaw)
            cams = (ctypes.c_float * 12)(*cam)
            prs = (ctypes.c_float * 8)(*proj)
            rc = (ctypes.c_float * 4)(*rect)
            views = emu.read(VIEW, 48) + emu.read(CAMERA_M, 48)
            p.probe_pm_draw_view(cams, prs, near, rc, yaw, views)
            want = emu.prepare(i, world)
            wm = (ctypes.c_float * 12)(*world)
            n = p.probe_pm_prepare(i, wm, buf, len(buf))
            assert n >= 0
            tol = FACADE_TOL if emu.i32(model) == 1 else TOL
            if any(want[104:116]):
                # A centre far from the origin with the camera near it: the top's
                # translation is a small difference of large sums, so a last bit
                # (2^-23 of the centre) grows by their ratio. The centre itself is
                # compared bit for bit. Measured: 93% of these floats are exact.
                big = max(abs(v) for v in struct.unpack_from('<3f', want, 104))
                t = struct.unpack_from('<3f', want, 44)
                near_t = max(max(abs(v) for v in t), 1e-3)
                tol = max(tol, 16 * 2 ** -23 * big / near_t)
            problem = compare(want, buf.raw[:n], stats, tol)
            if problem:
                print(f'{w} draw {d} node {i} {kind}: {problem}')
                if os.environ.get('MW3_DUMP'):
                    print('exe ', struct.unpack_from('<12f', want, 8))
                    print('port', struct.unpack_from('<12f', buf.raw, 8))
                    print('type, mode', emu.i32(model), emu.i32(model + 4), 'world', world[:3], world[9:], 'cam', cam[9:])
                sys.stdout.flush()
                os._exit(1)
            for off, v in saved.items():
                set_model(off, v)
            emu.w32(model + 0x44, saved_m)
            if kind == 'morph':
                p.probe_pm_set_morphs(i, b'', 0)
    print(f'ok: {draws * 2} draws identical; {stats.floats} floats, {stats.exact} bit-identical, worst {stats.worst:.2g}; {kinds}')
    sys.stdout.flush()
    os._exit(0)


if __name__ == '__main__':
    main()
