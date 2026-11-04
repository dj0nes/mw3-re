"""PM's projected lights against its exe: the test 0x562620, the polygon's fan with its
splits 0x562840 and the light per vertex 0x561fb0, on records and lights made here. Each
triangle the exe hands to the device (0x8c4bf8, 0x8c4bfc) is compared: TL vertices, fog,
sets' UVs and colours. At 24 bits and, for the colours' rounding, at 53.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_projected.py [polygons] [fixed]

With `fixed` the bug fix is on, and a first light map set's colours at a polygon's centre
and splits must differ from the exe's: the test then ends on that difference.
"""
import ctypes
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modeldraw_common import Stats, fbits, same_float, scenario  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_lighting as lit  # noqa: E402
import test_pm_modeldraw as md  # noqa: E402
import test_pm_records as rec  # noqa: E402
import test_pm_zar  # noqa: E402

REACH, DRAW, TL_P, SETS_P, PLAIN, WITH_SETS, PRIORITY = 0x562620, 0x562840, 0x8c4c54, 0x8ccc60, 0x8c4bf8, 0x8c4bfc, 0xa4e090
TOL, ROUNDING = 2e-4, 0.01


class Emulated(rec.Emulated):
    def __init__(self, world):
        super().__init__(world)
        self.mask_cell = self.i32(lit.MASK_P) & 0xffffffff
        self.tris = []
        for slot, fn in ((PLAIN, self.plain), (WITH_SETS, self.with_sets)):
            s = self.cpu.code(b'\xc3')
            self.native_stub(s, fn)
            self.w32(slot, s)
        self.kinds = []

    def at_end(self, uc, address, size, user):
        pass

    def triangle(self):
        tl = self.i32(TL_P) & 0xffffffff
        out = self.read(tl, 0x60)
        sets = self.i32(SETS_P) & 0xffffffff
        if sets:
            for k, kind in enumerate(self.kinds):
                out += self.read(sets + 0x1558 + k * 0x200, 24)
                if kind == 2:
                    out += self.read(sets + 0x3c + k * 0x300, 36)
        self.tris.append(out)

    def plain(self, c):
        self.triangle()
        return 0

    def with_sets(self, c):
        self.triangle()
        return 0


def main():
    polygons = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
    p = test_pm_zar.load_pm_probe()
    F, V, I, U = ctypes.c_float, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_draw_view.argtypes = [V, V, F, V, F, V]
    p.probe_pm_draw_view.restype = None
    p.probe_pm_model_view.argtypes = [V, V]
    p.probe_pm_model_view.restype = None
    p.probe_pm_lights.argtypes = [I, V, I, F, F, F, F, F, F]
    p.probe_pm_lights.restype = None
    p.probe_pm_light_reach.argtypes = [I, I, I]
    p.probe_pm_light_reach.restype = None
    p.probe_pm_set_fpu53.argtypes = [I]
    p.probe_pm_set_fpu53.restype = None
    p.probe_pm_set_bugs.argtypes = [I]
    p.probe_pm_set_bugs.restype = None
    # The original's path: the skipped first set's colours are compared as it leaves them.
    p.probe_pm_set_bugs(int(len(sys.argv) > 2 and sys.argv[2] == 'fixed'))
    p.probe_pm_projected.argtypes = [I, I, U, I, V, V, V, I, V, V, V, ctypes.POINTER(I), V, I]
    buf = ctypes.create_string_buffer(1 << 24)
    md.Emulated.probe = p
    rng = random.Random(0x562840)
    path = g.ZBD / 'c1' / 'gamez.zbd'
    emu = Emulated(path)
    assert p.probe_pm_draw_load(str(path).encode()) == 0
    nodes = []
    for i in range(emu.i32(g.N_CAPACITY)):
        r = emu.read(emu.node(i), 0xd4)
        model = struct.unpack_from('<I', r, 0x3c)[0]
        if struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 and model and emu.i32(model + 0x10) > 0:
            nodes.append(i)
    stats, seen = Stats(), {}
    for d in range(polygons):
        bits53 = int(d >= polygons // 2)  # the second half at 53 bits
        if d == 0 or d == polygons // 2:
            emu.cpu.fpu_control(0x027f if bits53 else 0x007f)
        p.probe_pm_set_fpu53(bits53)
        i = rng.choice(nodes)
        model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
        pi = rng.randrange(emu.i32(model + 0x10))
        poly = (emu.i32(model + 0x34) & 0xffffffff) + pi * 0x28
        _, cam, proj, *_ = scenario(rng, (0, 0, 0), 50.0)
        near = proj[6]
        rect = (proj[0], proj[1], proj[0] + proj[2] * 2 - 0.001, proj[1] + proj[3] * 2 - 0.001)
        emu.view(cam, proj, near, rect, 0.0)
        views = emu.read(md.VIEW, 48) + emu.read(md.CAMERA_M, 48)
        p.probe_pm_draw_view((F * 12)(*cam), (F * 8)(*proj), near, (F * 4)(*rect), 0.0, views)
        count = rng.randint(3, 8)
        # A polygon ahead of the camera: positions and their screen points.
        cx, cy, cz = rng.uniform(-60, 60), rng.uniform(-60, 60), rng.uniform(near + 5, 200)
        size = rng.choice((2.0, 10.0, 40.0, 120.0))
        pos = []
        for k in range(count):
            t = k / count * 2 * math.pi
            rr = size * rng.uniform(0.5, 1.5)  # uneven: a tie between edges could fall either way at 53 bits
            pos += [cx + rr * math.cos(t), cy + rr * math.sin(t), max(near + 1, cz + rng.uniform(-size, size) * 0.3)]
        # Screen points the exe itself projects, so unprojecting comes back close.
        tl, record = [], []
        cx0, cy0 = proj[0] + proj[2], proj[1] + proj[3]
        for k in range(count):
            x, y, z = pos[k * 3:k * 3 + 3]
            w = near / z
            fx = proj[4] * proj[2] / near
            fy = -proj[5] * proj[3] / near
            tl += [fx * x * w + cx0, fy * y * w + cy0, w, w, 0, 0, rng.uniform(0, 1), rng.uniform(0, 1)]
            record += [rng.uniform(0, 1.5), rng.uniform(0, 1.5), rng.uniform(0, 1.5), 0, rng.uniform(0, 1), rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255)]
        flags = rng.choice((0, 8, 0x10, 0x18, 0x400, 0x408, 0x410))
        nsets = rng.choice((-1, -1, 0, 1, 2, 4, 7))
        kinds = [rng.choice((0, 1, 2)) for _ in range(max(nsets, 0))]
        uvs = [rng.uniform(-1, 2) for _ in range(max(nsets, 0) * count * 2)]
        xyz = [rng.uniform(0, 255) for _ in range(max(nsets, 0) * count * 3)]
        at_pos, at_rec, at_tl = emu.alloc(12 * count), emu.alloc(0x20 * count), emu.alloc(0x20 * count)
        emu.cpu._uc.mem_write(at_pos, fbits(*pos)), emu.cpu._uc.mem_write(at_rec, fbits(*record)), emu.cpu._uc.mem_write(at_tl, fbits(*tl))
        sets_at = 0
        if nsets >= 0:
            sets_at = emu.alloc(0x2358)
            emu.w32(sets_at, nsets)
            for k in range(nsets):
                emu.w32(sets_at + 0x20 + k * 4, kinds[k])
                emu.cpu._uc.mem_write(sets_at + 0x1558 + k * 0x200, fbits(*uvs[k * count * 2:(k + 1) * count * 2]))
                emu.cpu._uc.mem_write(sets_at + 0x3c + k * 0x300, fbits(*xyz[k * count * 3:(k + 1) * count * 3]))
        rec_at = emu.alloc(0x38)
        emu.cpu._uc.mem_write(rec_at, struct.pack('<9I', poly, at_pos, 0, 0, at_rec, at_tl, sets_at, flags, count))
        emu.kinds = kinds
        emu.w32(SETS_P, 0)
        emu.cpu._uc.mem_write(PRIORITY, fbits(0.02))
        tl_arena = emu.alloc(0x20 * 3)
        emu.w32(0xa4cfb4, tl_arena)
        blobs = []
        for _ in range(rng.randint(1, 5)):
            b = bytearray(lit.make_light(rng, (cx, cy, cz)))
            fl = struct.unpack_from('<I', b, 0xe0)[0] | 0x10 | 0x40
            if rng.random() < 0.2:
                fl &= ~0x40
            if rng.random() < 0.2:
                fl |= 0x80
            far = rng.choice((rng.uniform(3, 12), rng.uniform(10, 300)))  # short: the split's floor of 30
            near_l = far * rng.choice((0.0, 0.2, 0.7))
            struct.pack_into('<I', b, 0xe0, fl)
            if far < 12:  # over the polygon, or a short light reaches nothing
                struct.pack_into('<3f', b, 0x78, cx + rng.uniform(-size, size), cy + rng.uniform(-size, size), cz + rng.uniform(-3, 3))
            struct.pack_into('<5f', b, 0xe4, near_l, far, near_l * near_l, far * far, 1.0 / max(far - near_l, 0.01))
            blobs.append(bytes(b))
        lit.Emulated.lights(emu, blobs, 0, 0.1, 0, 0, 0, 1, 0)
        p.probe_pm_lights(len(blobs), b''.join(blobs), 0, 0.1, 0, 0, 0, 1, 0)
        p.probe_pm_set_fpu53(bits53)  # after: that call sets it off, for the clouds
        for q in range(len(blobs)):
            reach = rng.choice((1, 1, 1, 0))
            emu.w32(lit.LIST + q * 20 + 12, reach)
            p.probe_pm_light_reach(q, 0, reach)
        want_reach = emu.call(REACH, args=[rec_at]) & 0xffffffff
        emu.tris = []
        want_ret = emu.call(DRAW, args=[rec_at]) & 0xffffffff
        reach = I()
        n = p.probe_pm_projected(i, pi, flags, count, (F * len(pos))(*pos), (F * len(record))(*record), (F * len(tl))(*tl), nsets,
                                 (I * max(1, len(kinds)))(*(kinds or [0])), (F * max(1, len(uvs)))(*(uvs or [0.0])), (F * max(1, len(xyz)))(*(xyz or [0.0])),
                                 ctypes.byref(reach), buf, len(buf))
        if n < 0:
            raise SystemExit('dump too long')
        ret = struct.unpack_from('<i', buf.raw)[0]
        if reach.value != want_reach:
            raise SystemExit(f'polygon {d}: reach {reach.value}, the exe {want_reach}')
        if ret != want_ret:
            raise SystemExit(f'polygon {d}: result {ret}, the exe {want_ret}')
        seen['reach'] = seen.get('reach', 0) + want_reach
        seen['drawn'] = seen.get('drawn', 0) + want_ret
        ours_each = 3 * 40  # ours: 3 x 10 words, the exe's TL 3 x 8
        extra = sum(24 + (36 if q == 2 else 0) for q in kinds)
        tris = (n - 4) // (ours_each + extra)
        if tris != len(emu.tris):
            raise SystemExit(f'polygon {d} size {size}: {tris} triangles, the exe {len(emu.tris)}; flags {flags:#x}')
        seen['triangles'] = seen.get('triangles', 0) + tris
        seen[f'split {size}'] = seen.get(f'split {size}', 0) + max(0, tris - count)
        for t in range(tris):
            o = buf.raw[4 + t * (ours_each + extra):4 + (t + 1) * (ours_each + extra)]
            w_ = emu.tris[t]
            for v in range(3):
                sx, sy, sz, rhw, dif, spe, _, tu, tv, _ = struct.unpack_from('<4fIIf2ff', o, v * 40)
                ex, ey, ez, er, edif, espe, eu, ev = struct.unpack_from('<4fII2f', w_, v * 0x20)
                for name, a, b in (('x', ex, sx), ('y', ey, sy), ('z', ez, sz), ('rhw', er, rhw), ('u', eu, tu), ('v', ev, tv)):
                    if not same_float(a, b, stats, TOL, name, ROUNDING):
                        raise SystemExit(f'polygon {d} triangle {t} vertex {v} {name}: {b}, the exe {a}; flags {flags:#x}, {bits53=}')
                if abs((edif >> 24) - (dif >> 24)) > 1:
                    raise SystemExit(f'polygon {d} triangle {t} vertex {v}: alpha {dif:#x}, the exe {edif:#x}')
                for sh in (16, 8, 0):
                    if abs((edif >> sh & 255) - (dif >> sh & 255)) > (1 if bits53 else 0):
                        raise SystemExit(f'polygon {d} triangle {t} vertex {v}: diffuse {dif:#x}, the exe {edif:#x}; {bits53=}, flags {flags:#x}')
                if abs((espe >> 24) - (spe >> 24)) > 1:
                    raise SystemExit(f'polygon {d} triangle {t} vertex {v}: specular {spe:#x}, the exe {espe:#x}')
            ao, ae = 3 * 40, 0x60
            for q in kinds:
                k = 6 + (9 if q == 2 else 0)
                for j in range(k):
                    a, b = struct.unpack_from('<f', w_, ae + j * 4)[0], struct.unpack_from('<f', o, ao + j * 4)[0]
                    if not same_float(a, b, stats, TOL, 'set', ROUNDING):
                        raise SystemExit(f'polygon {d} triangle {t} kind {q} float {j}: {b}, the exe {a}')
                ao += k * 4
                ae += k * 4
    print(f'ok: {polygons} polygons; {stats.floats} floats, {stats.exact} exact, off by place {stats.where}; {seen}')
    if not seen.get('drawn') or not seen.get('triangles'):
        raise SystemExit('nothing drawn')


if __name__ == '__main__':
    main()
