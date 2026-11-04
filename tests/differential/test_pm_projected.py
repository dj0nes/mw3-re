"""PM's projected lights against its exe: the test 0x562620, the polygon's fan with its
splits 0x562840 and the light per vertex 0x561fb0, on records and lights made here. Each
triangle the exe hands to the device (0x8c4bf8, 0x8c4bfc) is compared: TL vertices, fog,
sets' UVs and colours. At 24 bits and, for the colours' rounding, at 53.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_projected.py [polygons] [fixed]

Ties between edges fall differently too rarely for the default run. To see them, about 5
minutes: test_pm_projected.py 30000.

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
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ESP  # noqa: E402

from modeldraw_common import Stats, fbits, same_float, scenario  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_lighting as lit  # noqa: E402
import test_pm_modeldraw as md  # noqa: E402
import test_pm_records as rec  # noqa: E402
import test_pm_zar  # noqa: E402

REACH, DRAW, TL_P, SETS_P, PLAIN, WITH_SETS, PRIORITY = 0x562620, 0x562840, 0x8c4c54, 0x8ccc60, 0x8c4bf8, 0x8c4bfc, 0xa4e090
TOL, ROUNDING = 2e-4, 0.01
SPLIT, TRIS, VERTS = 0x562120, 0x68aac8, 0x6a2d48
TIE = 1e-5     # a triangle's two longest edges this close: rounding can choose either
# When a tie fell differently: a few pixels may differ a lot where a light's pool is sharp
# (38.9 of 255 seen, on 6.3% of a size-40 equilateral triangle under a light of range 9),
# the polygon as a whole hardly (mean 0.95 at most).
PICTURE, TIE_MEAN, TIE_SHARE = 3.0, 1.5, 0.10


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
        self.tie = False
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.at_split, begin=SPLIT, end=SPLIT)

    def at_split(self, uc, address, size, user):  # a near tie between the longest edges?
        tri, _ = struct.unpack('<II', uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 8, 8))
        p = [struct.unpack('<3f', uc.mem_read(VERTS + struct.unpack('<i', uc.mem_read(TRIS + tri * 32 + 4 * k, 4))[0] * 0xec, 12)) for k in range(3)]
        e = sorted(sum((a - b) ** 2 for a, b in zip(p[k], p[(k + 1) % 3])) for k in range(3))
        if e[2] - e[1] <= TIE * e[2]:
            self.tie = True

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


def colour_at(tris, x, y):
    """The Gouraud colour at a screen point, perspective-correct, from (x, y, rhw, diffuse) vertices."""
    for a, b, c in tris:
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-9:
            continue
        l = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d
        m = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d
        n = 1 - l - m
        if l >= -1e-4 and m >= -1e-4 and n >= -1e-4:
            w = l * a[2] + m * b[2] + n * c[2]
            return [(l * a[2] * (a[3] >> s & 255) + m * b[2] * (b[3] >> s & 255) + n * c[2] * (c[3] >> s & 255)) / w for s in (16, 8, 0)]
    return None


def picture_apart(exe, ours, rect):
    """The largest colour difference between two triangulations over the screen rect."""
    xs = [v[0] for t in exe for v in t]
    ys = [v[1] for t in exe for v in t]
    x0, x1 = max(min(xs), rect[0]), min(max(xs), rect[2])
    y0, y1 = max(min(ys), rect[1]), min(max(ys), rect[3])
    worst, total, over, sampled = 0.0, 0.0, 0, 0
    for j in range(48):
        for i in range(48):
            x, y = x0 + (x1 - x0) * (i + 0.5) / 48, y0 + (y1 - y0) * (j + 0.5) / 48
            a, b = colour_at(exe, x, y), colour_at(ours, x, y)
            if a and b:
                e = max(abs(p - q) for p, q in zip(a, b))
                worst, total, over, sampled = max(worst, e), total + e, over + (e > PICTURE), sampled + 1
    return worst, total / max(sampled, 1), over / max(sampled, 1)


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
    stats, seen, ties = Stats(), {}, []
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
        regular = rng.random() < 0.5
        for k in range(count):
            t = k / count * 2 * math.pi
            rr = size if regular else size * rng.uniform(0.5, 1.5)  # regular: edges tie (test below)
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
            # Short, on all but the largest polygons: the split's floor of 30. On those it would
            # pass the exe's 2,048 triangles, unchecked, and overwrite its own vertices (fixes.md).
            far = rng.uniform(40, 300) if size > 40 else rng.choice((rng.uniform(3, 12), rng.uniform(10, 300)))
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
        emu.tris, emu.tie = [], False
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
        # Two equal edges can differ by rounding, differently here and in the exe (at 53 bits
        # the more often), so a split falls on the other one: another triangulation of the same
        # polygon (traced, docs/pm-map.md). Only with such a tie, and only if the picture agrees.
        vertex = lambda o, v: struct.unpack_from('<4fIIf2ff', o, v * 40)  # noqa: E731
        ours_tris = [buf.raw[4 + q * (ours_each + extra):4 + (q + 1) * (ours_each + extra)] for q in range(tris)]
        apart = tris != len(emu.tris) or any(
            abs(struct.unpack_from('<f', w_, v * 0x20)[0] - vertex(o, v)[0]) > 1.0 or abs(struct.unpack_from('<f', w_, v * 0x20 + 4)[0] - vertex(o, v)[1]) > 1.0
            for o, w_ in zip(ours_tris, emu.tris) for v in range(3))
        if apart and emu.tie:
            exe_t = [[(lambda q: (q[0], q[1], q[3], q[4]))(struct.unpack_from('<4fI', w_, v * 0x20)) for v in range(3)] for w_ in emu.tris]
            our_t = [[(lambda q: (q[0], q[1], q[3], q[4]))(vertex(o, v)) for v in range(3)] for o in ours_tris]
            worst, mean, share = picture_apart(exe_t, our_t, rect)
            seen['ties'] = seen.get('ties', 0) + 1
            ties.append((round(worst, 1), round(mean, 2), round(share, 3), size, tris, bits53, count, flags))
            if mean > TIE_MEAN or share > TIE_SHARE:
                raise SystemExit(f'polygon {d} size {size}: a tie, but the picture is {mean:.2f} apart on average, {share:.3f} of it over {PICTURE}; flags {flags:#x}')
            continue
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
    ties.sort(reverse=True)
    print(f'ties that fell differently: {len(ties)}, {sum(1 for t in ties if not t[5])} at 24 bits; the largest mean '
          f'{max((t[1] for t in ties), default=0)}, share {max((t[2] for t in ties), default=0)}; the worst {ties[:3]} '
          '(worst, mean, share over 3, size, triangles, 53 bits, corners, flags)')
    print(f'ok: {polygons} polygons; {stats.floats} floats, {stats.exact} exact, off by place {stats.where}; {seen}')
    if not seen.get('drawn') or not seen.get('triangles'):
        raise SystemExit('nothing drawn')


if __name__ == '__main__':
    main()
