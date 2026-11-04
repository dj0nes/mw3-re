"""PM's draw 0x543b10 after the lighting, to the end of its sets (0x54600e), against the
exe: strips into triangles, polygons with codes into fans, the projected shadows' sets,
the light map's, every record and what it holds. Casters and lights are made here, and
a polygon can take all seven sets. The fade runs at both ends of its range.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_sets.py [draws per world]
"""
import ctypes
import math
import random
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EDI

sys.path.insert(0, str(Path(__file__).parent))
from modeldraw_common import Stats, fbits, same_float, scenario  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_lighting as lit  # noqa: E402
import test_pm_modeldraw as md  # noqa: E402
import test_pm_records as rec  # noqa: E402
import test_pm_zar  # noqa: E402

END, CASTERS, CASTER_N, LIGHT_MAP = 0x54600e, 0x8d444c, 0x8d4ac4, 0x683cb0
TEX_BASE, MAP_SURFACE = 0x7000, 0x7777
TOL, ROUNDING = 2e-4, 0.01


class Emulated(lit.Emulated):
    def __init__(self, world):
        super().__init__(world)
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.at_sets, begin=END, end=END)
        self.caster_list = self.alloc(4 * 64)
        self.w32(CASTERS, self.caster_list)
        self.map_entry = self.alloc(16)
        self.w32(self.map_entry + 8, MAP_SURFACE)
        self.head_sets = None

    def after_lighting(self, uc, address, size, user):  # the sets come after
        pass

    def at_sets(self, uc, address, size, user):
        start = self.i32(rec.RECORDS) & 0xffffffff
        end = self.i32(0x8dcbb8) & 0xffffffff
        ebp = uc.reg_read(UC_X86_REG_EBP)
        word = lambda local: self.i32(ebp - local + 4) & 0xffffffff  # noqa: E731
        self.head = (word(0x58), int(word(0x7c) != 0) | int(word(0x94) != 0) << 1 | int(word(0xa4) != 0) << 2
                     | int(word(0x8c) != 0) << 3 | int(word(0xa8) != 0) << 4)
        refs = self.i32(self.model + 0x60) & 0xffffffff
        ref = (word(0x54) - refs) // 0xc
        polys = self.i32(self.model + 0x34) & 0xffffffff
        at = start
        while at < end:
            r = self.read(at, 0x38)
            poly, pos, normal, _, record, tl, sets, flags, count = struct.unpack_from('<9I', r)
            codes = struct.unpack_from('<I', r, 0x34)[0]
            self.dump += struct.pack('<iiIiI', (poly - polys) // 0x28, ref, flags, count, codes)
            at += 0x38
            if count == 0:
                continue
            self.dump += self.read(pos, count * 12)
            for v in range(count):
                self.dump += self.read(record + v * 0x20, 0x20) + self.read(tl + v * 0x20 + 0x18, 8)
            n = self.i32(sets) if sets else 0
            self.dump += (self.read(normal, 12) if normal else bytes(12)) + struct.pack('<i', n)
            for k in range(n):
                self.dump += self.read(sets + 4 + k * 4, 4) + self.read(sets + 0x20 + k * 4, 4)
                self.dump += self.read(sets + 0x1558 + k * 0x200, count * 8)
                if self.i32(sets + 0x20 + k * 4) == 2:
                    self.dump += self.read(sets + 0x3c + k * 0x300, count * 12) + self.read(sets + 0x153c + k * 4, 4)
        self.w32(0x8dcbb8, start)
        uc.reg_write(UC_X86_REG_EDI, start)


def exact_zero_case(exe, ours):
    return ours[0] == 0.0 and exe[0] != 0.0 and abs(exe[0]) < 1e-7


def axis_factor(exe):
    """What 0x5381c0 gives along x for a normal with x tiny: y / approx_sqrt(x^2 + y^2).
    The approximate root by its float bits, in 32-bit float arithmetic, as the exe's."""
    sq = struct.unpack('<f', struct.pack('<f', exe[0] * exe[0] + exe[1] * exe[1]))[0]
    bits = struct.unpack('<i', struct.pack('<f', sq))[0]
    root = struct.unpack('<f', struct.pack('<i', (bits >> 1) + 0x1fc00000))[0]
    return exe[1] / root


def make_caster(rng, around):
    b = bytearray(0x8c)
    pos = [c + rng.uniform(-60, 60) for c in around]
    s = (rng.uniform(-1, 1), rng.uniform(-1, 1))
    struct.pack_into('<3f', b, 4, *pos)
    struct.pack_into('<f', b, 0x1c, pos[1] + rng.uniform(-20, 20))
    struct.pack_into('<3f', b, 0x20, s[0], s[1], (s[0] ** 2 + s[1] ** 2) ** 0.5)
    w = rng.uniform(5, 200)
    lo = [pos[0] - w, pos[1] - w, pos[2] - w]
    hi = [pos[0] + w, pos[1] + w, pos[2] + w]
    struct.pack_into('<3f', b, 0x2c, *lo)
    struct.pack_into('<3f', b, 0x38, *hi)
    struct.pack_into('<2f', b, 0x44, 1 / (2 * w), 1 / (2 * w))
    v = [rng.uniform(-1, 1) for _ in range(3)]
    struct.pack_into('<3f', b, 0x4c, *v)
    return bytes(b)


def main():
    draws = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    p = test_pm_zar.load_pm_probe()
    F, V, I, U = ctypes.c_float, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_draw_view.argtypes = [V, V, F, V, F, V]
    p.probe_pm_draw_view.restype = None
    p.probe_pm_model_view.argtypes = [V, V]
    p.probe_pm_model_view.restype = None
    p.probe_pm_records_state.argtypes = [I, F, F, F, F, I, I, F, I, F, F, F]
    p.probe_pm_records_state.restype = None
    p.probe_pm_records_light_map.argtypes = [I, I]
    p.probe_pm_records_light_map.restype = None
    p.probe_pm_lights.argtypes = [I, V, I, F, F, F, F, F, F]
    p.probe_pm_lights.restype = None
    p.probe_pm_set_node.argtypes = [I, I, U]
    p.probe_pm_set_node.restype = None
    p.probe_pm_set_frames.argtypes = [U, U]
    p.probe_pm_set_frames.restype = None
    p.probe_pm_casters.argtypes = [I, V, U]
    p.probe_pm_casters.restype = None
    p.probe_pm_light_map_surface.argtypes = [U]
    p.probe_pm_light_map_surface.restype = None
    p.probe_pm_sets.argtypes = [I, V, U, I, V, I]
    p.probe_pm_set_sets.argtypes = [I, I, I, V]
    p.probe_pm_set_sets.restype = None
    buf = ctypes.create_string_buffer(1 << 24)
    md.Emulated.probe = p
    rng = random.Random(0x54600e)
    stats, seen, records = Stats(), {}, 0
    for w in ('c1', 'c3'):
        path = g.ZBD / w / 'gamez.zbd'
        emu = Emulated(path)
        # c3 at 53 bits: the fade is the value rounded there, at 24 only 0 (CLAUDE.md).
        fpu53 = 1 if w == 'c3' else 0
        emu.cpu.fpu_control(0x027f if fpu53 else 0x007f)
        assert p.probe_pm_draw_load(str(path).encode()) == 0
        nodes = []
        for i in range(emu.i32(g.N_CAPACITY)):
            r = emu.read(emu.node(i), 0xd4)
            model = struct.unpack_from('<I', r, 0x3c)[0]
            if struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 and model and emu.i32(model + 0x10) > 0 and emu.i32(model) == 0:
                nodes.append(i)
        p.probe_pm_set_frames(6, 5)
        emu.w32(0x8c4b60, 6), emu.w32(0x60d254, 5)
        p.probe_pm_light_map_surface(MAP_SURFACE)
        emu.w32(LIGHT_MAP, emu.map_entry), emu.w32(0x8ccc64, 0), emu.w32(0x8ccc68, 2), emu.w32(0x8ccc6c, 2)
        for d in range(draws):
            i = rng.choice(nodes)
            world, cam, proj, *_ = scenario(rng, (0, 0, 0), 50.0)
            near = proj[6]
            if rng.random() < 0.3:  # an unturned model, a camera turned about x alone: normals' x near 0
                t = rng.uniform(-1.2, 1.2)
                cs, sn = math.cos(t), math.sin(t)
                world = [1, 0, 0, 0, 1, 0, 0, 0, 1, rng.uniform(-30, 30), rng.uniform(-30, 30), rng.uniform(-30, 30)]
                cam = [1, 0, 0, 0, cs, sn, 0, -sn, cs, rng.uniform(-30, 30), rng.uniform(-30, 30), rng.uniform(20, 80)]
            rect = (proj[0], proj[1], proj[0] + proj[2] * 2 - 0.001, proj[1] + proj[3] * 2 - 0.001)
            emu.view(cam, proj, near, rect, 0.0)
            views = emu.read(md.VIEW, 48) + emu.read(md.CAMERA_M, 48)
            p.probe_pm_draw_view((F * 12)(*cam), (F * 8)(*proj), near, (F * 4)(*rect), 0.0, views)
            fn = rng.uniform(1, 200)
            state = (rng.choice((0, 1)), fn, fn + rng.uniform(1, 600), rng.uniform(50, 400), rng.uniform(-50, 40), 1, 1,
                     rng.choice((1.0, 1.0, 1.0, 1.0, 0.5)), 0, near + rng.uniform(1, 800), rng.uniform(0.2, 2), rng.uniform(0.2, 2))
            emu.state(*state)
            p.probe_pm_records_state(*state)
            emu.w32(emu.node(i) + 0xc0, 0xffffff), p.probe_pm_set_node(i, 0xc0, 0xffffff)
            emu.w32(emu.node(i) + 0xbc, 0), p.probe_pm_set_node(i, 0xbc, 0)
            cp = [sum(a * b for a, b in zip(world[9:], cam[3 * k:3 * k + 3])) for k in range(3)]
            blobs = []
            for _ in range(rng.randint(0, 9)):
                b = bytearray(lit.make_light(rng, cp))
                fl = struct.unpack_from('<I', b, 0xe0)[0]
                if rng.random() < 0.7:
                    fl = (fl & ~8) | 0x10 | 0x80 | 2 | 0x800
                struct.pack_into('<I', b, 0xe0, fl)
                far = rng.uniform(40, 400)
                near_l = far * rng.choice((0.0, 0.1, 0.5, 0.99))
                # Now and then out of range: a colour past 255 once multiplied, a fade past 255.
                rate = 1.0 / max(far - near_l, 0.01) * rng.choice((1.0, 1.0, 3.0, -1.0))
                struct.pack_into('<5f', b, 0xe4, near_l, far, near_l * near_l, far * far, rate)
                struct.pack_into('<3f', b, 0xd4, *(rng.uniform(-1, 4) for _ in range(3)))
                blobs.append(bytes(b))
            emu.lights(blobs, 0, 0.1, 0, 0, 0, 1, 0.0)
            p.probe_pm_lights(len(blobs), b''.join(blobs) or None, 0, 0.1, 0, 0, 0, 1, 0.0)
            world_at = [world[9 + k] for k in range(3)]
            casters = [make_caster(rng, world_at) for _ in range(rng.choice((0, 2, 5, 9)))]
            for k, c in enumerate(casters):
                a = emu.alloc(0x8c)
                emu.cpu._uc.mem_write(a, c)
                emu.w32(a + 0x64, TEX_BASE + k)  # PM keeps it at +0x64, MW3 at +0x60
                emu.w32(emu.caster_list + 4 * k, a)
            emu.w32(CASTER_N, len(casters))
            p.probe_pm_casters(len(casters), b''.join(casters) or None, TEX_BASE)
            # Extra materials on a ref's first polygon, once per model: no shipped polygon has
            # any, and a strip or fan must carry them into its triangles.
            model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
            if rng.random() < 0.3:
                poly = emu.i32((emu.i32(model + 0x60) & 0xffffffff) + 8) & 0xffffffff
                if emu.i32(poly + 0x18) and emu.i32(poly + 0x10) == 1:
                    pi = (poly - (emu.i32(model + 0x34) & 0xffffffff)) // 0x28
                    k, pn = rng.randint(1, 3), emu.i32(poly) & 0x3ff
                    uvs = [rng.uniform(-2, 2) for _ in range(k * pn * 2)]
                    mats, arr = emu.alloc(4 * (k + 1)), emu.alloc(4 * (k + 1))
                    m0, a0 = emu.i32(emu.i32(poly + 0x14) & 0xffffffff), emu.i32(emu.i32(poly + 0x18) & 0xffffffff)
                    emu.w32(mats, m0), emu.w32(arr, a0)
                    for j in range(k):
                        q = emu.alloc(pn * 8)
                        emu.cpu._uc.mem_write(q, fbits(*uvs[j * pn * 2:(j + 1) * pn * 2]))
                        emu.w32(mats + 4 * (j + 1), m0), emu.w32(arr + 4 * (j + 1), q)
                    emu.w32(poly + 0x14, mats), emu.w32(poly + 0x18, arr), emu.w32(poly + 0x10, k + 1)
                    p.probe_pm_set_sets(i, pi, k, (F * len(uvs))(*uvs))
                    seen['extra materials'] = seen.get('extra materials', 0) + 1
            emu.dump, emu.head, emu.mixes = b'', None, []
            mask = rng.choice((0, 0x3f, 0x10, 0x0f, 0x1f))
            head, want = emu.draw_node(i, world, mask)
            n = p.probe_pm_sets(i, (F * 12)(*world), mask, fpu53, buf, len(buf))
            if n < 0:
                raise SystemExit('dump too long')
            ok, needs, bits = struct.unpack_from('<3I', buf.raw)
            got = buf.raw[12:n]
            if head is None:
                if got:
                    raise SystemExit(f'{w} draw {d} node {i}: records here, none in the exe')
                continue
            if (head[0], head[1]) != (needs, bits):
                raise SystemExit(f'{w} draw {d} node {i}: needs, gates {needs:#x}, {bits:#x}; the exe {head[0]:#x}, {head[1]:#x}')
            model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
            at, ag = 0, 0
            while at < len(want) or ag < len(got):
                if at >= len(want) or ag >= len(got):
                    raise SystemExit(f'{w} draw {d} node {i}: {len(got)} bytes, the exe {len(want)}; at {ag}, {at}')
                a = struct.unpack_from('<iiIiI', want, at)
                b = struct.unpack_from('<iiIiI', got, ag)
                if a != b:
                    raise SystemExit(f'{w} draw {d} node {i} mask {mask:#x}: poly, ref, flags, count, codes {b}, the exe {a}')
                at += 20
                ag += 20
                records += 1
                if a[3] == 0:
                    continue
                seen['triangle' if a[3] == 3 else 'polygon'] = seen.get('triangle' if a[3] == 3 else 'polygon', 0) + 1
                mat = emu.i32(emu.i32((emu.i32((emu.i32(model + 0x60) & 0xffffffff) + a[1] * 0xc + 8) & 0xffffffff) + 0x14) & 0xffffffff) & 0xffffffff
                textured = emu.i32(mat) >> 8 & 1
                for j in range(a[3] * 3):
                    x, y = struct.unpack_from('<f', want, at)[0], struct.unpack_from('<f', got, ag)[0]
                    if not same_float(x, y, stats, TOL, 'pos', ROUNDING):
                        raise SystemExit(f'{w} draw {d} node {i} poly {a[0]}: position {j}: {y}, the exe {x}')
                    at += 4
                    ag += 4
                for v in range(a[3]):
                    for k in range(10):
                        relevant = (k in (6, 7) and needs & 6 and False) or k in (0, 1, 2, 4, 5, 6, 7) and (k > 4 and textured or k == 4 and (needs & 1) or k < 3 and needs & 6) or (k in (8, 9) and textured)
                        x, y = struct.unpack_from('<f', want, at)[0], struct.unpack_from('<f', got, ag)[0]
                        if relevant and not same_float(x, y, stats, TOL, 'rec', ROUNDING):
                            raise SystemExit(f'{w} draw {d} node {i} poly {a[0]} vertex {v} word {k}: {y}, the exe {x}; flags {a[2]:#x}')
                        at += 4
                        ag += 4
                na, ng3 = struct.unpack_from('<3f', want, at), struct.unpack_from('<3f', got, ag)
                at += 12
                ag += 12
                ns, ng = struct.unpack_from('<i', want, at)[0], struct.unpack_from('<i', got, ag)[0]
                if ns != ng:
                    raise SystemExit(f'{w} draw {d} node {i} poly {a[0]} mask {mask:#x}: {ng} sets, the exe {ns}; flags {a[2]:#x}')
                at += 4
                ag += 4
                seen[f'{ns} sets'] = seen.get(f'{ns} sets', 0) + 1
                for k in range(ns):
                    ta, ka = struct.unpack_from('<Ii', want, at)
                    tb, kb = struct.unpack_from('<Ii', got, ag)
                    if (ta, ka) != (tb, kb):
                        raise SystemExit(f'{w} draw {d} node {i} poly {a[0]} set {k}: texture, kind {tb:#x}, {kb}; the exe {ta:#x}, {ka}')
                    seen[f'kind {ka}'] = seen.get(f'kind {ka}', 0) + 1
                    at += 8
                    ag += 8
                    extra = a[3] * 2 + (a[3] * 3 if ka == 2 else 0)
                    for j in range(extra):
                        x, y = struct.unpack_from('<f', want, at)[0], struct.unpack_from('<f', got, ag)[0]
                        # A normal's x exactly 0 here and tiny in the exe (53 bits): 0x5381c0's other
                        # branch, these axes turned and scaled by the exe's own factor, and only that
                        # (traced, docs/pm-map.md).
                        if ka == 2 and j < a[3] * 2 and exact_zero_case(na, ng3):
                            if not same_float(x - 0.5, (y - 0.5) * axis_factor(na), stats, TOL, 'turned', 0.002):
                                raise SystemExit(f'{w} draw {d} node {i} poly {a[0]} set {k} float {j}: {y} turned and '
                                                 f'scaled is not the exe {x}; normals {ng3}, {na}')
                            seen['turned axes'] = seen.get('turned axes', 0) + 1
                            at += 4
                            ag += 4
                            continue
                        if not same_float(x, y, stats, TOL, 'set', ROUNDING):
                            raise SystemExit(f'{w} draw {d} node {i} poly {a[0]} set {k} kind {ka} float {j}: {y}, the exe {x}; normals {ng3}, {na}')
                        at += 4
                        ag += 4
                    if ka == 2:
                        fa, fb = struct.unpack_from('<I', want, at)[0], struct.unpack_from('<I', got, ag)[0]
                        if abs((fa >> 24) - (fb >> 24)) > (1 if fpu53 else 0):  # a rounding either way at 53
                            raise SystemExit(f'{w} draw {d} node {i} poly {a[0]} set {k}: fade {fb:#x}, the exe {fa:#x}')
                        name = 'fade full' if fa == 0xff000000 else 'fade zero' if fa == 0 else 'fade between'
                        seen[name] = seen.get(name, 0) + 1
                        at += 4
                        ag += 4
    print(f'ok: {records} records; {stats.floats} floats, {stats.exact} exact, off by place {stats.where}; {dict(sorted(seen.items()))}')
    print(f"turned axes allowed on {seen.get('turned axes', 0) // 2} UV pairs (53 bits)")
    for need in ('triangle', '7 sets', 'kind 1', 'kind 2', 'fade full', 'fade zero', 'fade between', 'turned axes'):
        if not seen.get(need):
            raise SystemExit(f'never reached: {need}')


if __name__ == '__main__':
    main()
