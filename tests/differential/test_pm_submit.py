"""PM's draw 0x543b10 whole, against its exe: near and side clipping, projection, colours,
fog, and every call to the device (the exe's table 0x8c4bdc..0x8c4c04) with its TL vertices
and sets. Lights and casters are made here. Run at 24 bits (c1) and at 53 (c3), the
shipped default; projected triangles have their own test (test_pm_projected.py).

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_submit.py [draws per world]
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
import test_pm_lighting as lit  # noqa: E402
import test_pm_modeldraw as md  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EBP  # noqa: E402

import test_pm_records as rec  # noqa: E402
import test_pm_sets as sets  # noqa: E402
import test_pm_zar  # noqa: E402

SLOTS_ = {0x8c4bdc: 0, 0x8c4be4: 1, 0x8c4bec: 2, 0x8c4bf0: 3, 0x8c4bf4: 4, 0x8c4bfc: 5, 0x8c4c00: 6, 0x8c4c04: 7, 0x8c4bf8: 8}
TL_P, RECT_P, BASE_TL, INDICES, OVERLAY, CULL = 0x8c4c54, 0x8d4b0c, 0x8c4c5c, 0x8c4c60, 0xa4e074, 0x59b870
PRIMITIVE = 0x8c4c40
TOL, ROUNDING = 2e-4, 0.05
EDGE_ON = 0.05  # square pixels: the rounding of a screen line's points
SIDE_CUT = 1.0  # pixels
VERTS_N, VERTS_P = 0x14, 0x38
FACE = 0x5442c7  # the sum and the normal stored, before the compare
NAMES = ['flat', 'lit', 'textured', 'textured lit', 'textured vertex', 'sets', 'indexed textured', 'indexed flat', 'projected']


class Emulated(sets.Emulated):
    def __init__(self, world):
        super().__init__(world)
        self.calls = []
        self.cull = 1
        self.cpu.stubs[CULL] = (self.set_cull, 0)  # already hooked by test_pm_records: only the function
        for slot, kind in SLOTS_.items():
            s = self.cpu.code(b'\xc3')
            self.native_stub(s, lambda c, k=kind: self.device(c, k))
            self.w32(slot, s)
        self.kinds = []
        self.faces = []
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.at_face, begin=FACE, end=FACE)

    def at_face(self, uc, address, size, user):  # the exe's own inputs, for the port's face
        ebp = uc.reg_read(UC_X86_REG_EBP)
        pos, normal = struct.unpack('<II', bytes(uc.mem_read(ebp - 0x44, 4)) + bytes(uc.mem_read(ebp - 0x18, 4)))
        self.faces.append((bytes(uc.mem_read(pos, 36)), bytes(uc.mem_read(normal, 12)), bytes(uc.mem_read(ebp - 0xb0, 4))))

    def set_cull(self, c):
        self.cull = c.arg(0)
        return 0

    def at_sets(self, uc, address, size, user):  # the whole draw runs
        pass

    def after_lighting(self, uc, address, size, user):
        pass

    def device(self, c, kind):
        if kind in (6, 7):
            n = c.arg(0)
            base = self.i32(BASE_TL) & 0xffffffff
            idx = struct.unpack(f'<{n}H', self.read(INDICES, n * 2))
            tl = b''.join(self.read(base + i * 0x20, 0x20) for i in idx)
            shade = c.arg(3 if kind == 6 else 2)
            self.calls.append((kind, n, self.cull, 4, tl, b'', -1, shade, None))  # 0x5a0ea0, 0x5a0f30: always triangle lists
            return 0
        # flat (colour, alpha, count, overlay), lit (alpha, count, overlay), projected (flags, count, texture, fade, overlay)
        count = {0: c.arg(2), 1: c.arg(1), 8: c.arg(1)}.get(kind, c.arg(0))
        tl = self.read(self.i32(TL_P) & 0xffffffff, count * 0x20)
        data, n = b'', -1
        if kind == 5:
            at = self.i32(0x8ccc60) & 0xffffffff
            n = self.i32(at)
            for k in range(n):
                q = self.i32(at + 0x20 + k * 4)
                data += struct.pack('<i', q) + self.read(at + 0x1558 + k * 0x200, count * 8)
                if q == 2:
                    data += self.read(at + 0x3c + k * 0x300, count * 12)
        overlay = {0: 3, 1: 2, 2: 3, 3: 3, 4: 3, 8: 4}.get(kind)  # the argument that carries it
        self.calls.append((kind, count, self.cull, self.i32(PRIMITIVE), tl, data, n, c.arg(0) if kind == 8 else None, None if overlay is None else c.arg(overlay)))
        return 0


def parse(raw):
    out, at = [], 0
    while at < len(raw):
        kind, count, cull, prim, shade, overlay, n = struct.unpack_from('<7i', raw, at)
        at += 32
        tl = raw[at:at + count * 32]
        at += count * 32
        start = at
        for _ in range(max(n, 0)):
            q = struct.unpack_from('<i', raw, at)[0]
            at += 4 + count * 8 + (count * 12 if q == 2 else 0)
        out.append((kind, count, cull, prim, tl, raw[start:at], n, shade, overlay))
    return out


def covers_nothing(calls):
    """Every triangle of these calls has no area on screen: a flat mesh seen edge-on, the camera
    in its plane. There a polygon's facing is the rounding of a sum near 0, and which ones pass
    differs at 53 bits (the exe's sums are double, ours float) with nothing to see either way."""
    for c in calls:
        v = [struct.unpack_from('<2f', c[4], k * 32) for k in range(c[1])]
        tris = [v[k:k + 3] for k in range(0, len(v) - 2, 3)] if c[0] in (6, 7) else [(v[0], v[k - 1], v[k]) for k in range(2, len(v))]
        for p, q, r in tris:
            if abs((q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])) / 2 > EDGE_ON:
                return False
    return True


def model_span(emu, node):
    """The model's largest extent on an axis."""
    model = emu.i32(emu.node(node) + 0x3c) & 0xffffffff
    n = emu.i32(model + VERTS_N)
    if n <= 0:
        return 0.0
    v = struct.unpack(f'<{n * 3}f', emu.read(emu.i32(model + VERTS_P) & 0xffffffff, n * 12))
    return max(max(v[a::3]) - min(v[a::3]) for a in range(3))


def on_side(tl, rect):
    x, y = struct.unpack_from('<2f', tl)
    return min(abs(x - rect[0]), abs(x - rect[2]), abs(y - rect[1]), abs(y - rect[3])) < 1e-3


def side_cut(a, b):
    """A vertex a screen side made, from an edge reaching far off screen: at 53 bits the exe
    interpolates in double and we in float, a step of the edge's length. Under a pixel, and a
    relative step in everything else, is that and no more."""
    x, y = struct.unpack('<8f', a), struct.unpack('<8f', b)
    if max(abs(x[0] - y[0]), abs(x[1] - y[1])) >= SIDE_CUT:
        return False
    return all(abs(x[k] - y[k]) <= 0.01 * max(abs(x[k]), 1e-3) for k in (2, 3))


def compare_floats(a, b, stats, where, msg):
    for j in range(len(a) // 4):
        x, y = struct.unpack_from('<f', a, j * 4)[0], struct.unpack_from('<f', b, j * 4)[0]
        if not same_float(x, y, stats, TOL, where, ROUNDING):
            raise SystemExit(f'{msg} float {j}: {y}, the exe {x}')


def main():
    global TOL, ROUNDING
    draws = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    p = test_pm_zar.load_pm_probe()
    F, V, I, U = ctypes.c_float, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_face.argtypes = [ctypes.c_char_p, V, I]
    p.probe_pm_face.restype = F
    p.probe_pm_draw_view.argtypes = [V, V, F, V, F, V]
    p.probe_pm_draw_view.restype = None
    p.probe_pm_model_view.argtypes = [V, V]
    p.probe_pm_model_view.restype = None
    p.probe_pm_records_state.argtypes = [I, F, F, F, F, I, I, F, I, F, F, F]
    p.probe_pm_records_state.restype = None
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
    p.probe_pm_set_bugs.argtypes = [I]
    p.probe_pm_set_bugs.restype = None
    p.probe_pm_set_bugs(0)
    p.probe_pm_submit.argtypes = [I, V, U, I, V, I, V, I]
    buf = ctypes.create_string_buffer(1 << 24)
    md.Emulated.probe = p
    rng = random.Random(0x546000)
    stats, seen, edge_on, cuts, worst, large_draws, projected_draws, unfogged, faces = Stats(), {}, 0, 0, 0.0, 0, 0, 0, 0
    for w in ('c1', 'c2', 'c3', 'c4'):
        path = g.ZBD / w / 'gamez.zbd'
        emu = Emulated(path)
        fpu53 = 1 if w in ('c3', 'c4') and not os.environ.get('PM_24') else 0
        TOL, ROUNDING = (2e-4, 0.05) if fpu53 else (0.0, 0.0)  # 24 bits: bit for bit (the audit, pm-map.md)
        emu.cpu.fpu_control(0x027f if fpu53 else 0x007f)
        assert p.probe_pm_draw_load(str(path).encode()) == 0
        nodes, large, corners = [], [], {}
        for i in range(emu.i32(g.N_CAPACITY)):
            r = emu.read(emu.node(i), 0xd4)
            model = struct.unpack_from('<I', r, 0x3c)[0]
            if struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 and model and emu.i32(model + 0x10) > 0 and emu.i32(model) == 0:
                polys = emu.i32(model + 0x34) & 0xffffffff
                flags = [emu.i32(polys + q * 0x28) for q in range(emu.i32(model + 0x10))]
                # Over 64 corners are the `inout` areas alone, deleted at mission load and never
                # drawn (0x4cddb0, pm-map.md); the exe's clippers would overrun. The largest of
                # the rest are strips, which become triangles before any clipper sees them.
                if any(f & 0x3ff > 64 and not f & 0x2000 for f in flags):
                    continue
                corners[i] = max(f & 0x3ff for f in flags)
                nodes.append(i)
                if corners[i] > 48:
                    large.append(i)
        p.probe_pm_set_frames(6, 5)
        emu.w32(0x8c4b60, 6), emu.w32(0x60d254, 5)
        p.probe_pm_light_map_surface(sets.MAP_SURFACE)
        emu.w32(sets.LIGHT_MAP, emu.map_entry), emu.w32(0x8ccc64, 0), emu.w32(0x8ccc68, 2), emu.w32(0x8ccc6c, 2)
        for d in range(draws):
            i = rng.choice(large) if large and rng.random() < 0.1 else rng.choice(nodes)
            world, cam, proj, *_ = scenario(rng, (0, 0, 0), 50.0)
            near = proj[6]
            rect = (proj[0], proj[1], proj[0] + proj[2] * 2 + 0.001, proj[1] + proj[3] * 2 + 0.001)  # 0x548a4b
            emu.view(cam, proj, near, rect, 0.0)
            emu.cpu._uc.mem_write(RECT_P, fbits(rect[0], rect[1], near))
            emu.cpu._uc.mem_write(0x8d4b24, fbits(rect[2], rect[3]))
            views = emu.read(md.VIEW, 48) + emu.read(md.CAMERA_M, 48)
            p.probe_pm_draw_view((F * 12)(*cam), (F * 8)(*proj), near, (F * 4)(*rect), 0.0, views)
            fn = rng.uniform(1, 200)
            fade = rng.choice((1.0, 1.0, 1.0, 1.0, 0.5))
            state = (rng.choice((0, 1)), fn, fn + rng.uniform(1, 600), rng.uniform(50, 400), rng.uniform(-50, 40), 1, 1,
                     fade, rng.choice((0, 0, 1)), near + rng.uniform(1, 800), rng.uniform(0.2, 2), rng.uniform(0.2, 2))
            emu.state(*state)
            p.probe_pm_records_state(*state)
            overlay = rng.choice((0, 0, 0, 1))
            emu.w32(OVERLAY, overlay)
            emu.w32(emu.node(i) + 0xc0, 0xffffff), p.probe_pm_set_node(i, 0xc0, 0xffffff)
            emu.w32(emu.node(i) + 0xbc, 0), p.probe_pm_set_node(i, 0xbc, 0)
            cp = [sum(a * b for a, b in zip(world[9:], cam[3 * k:3 * k + 3])) for k in range(3)]
            # Whole draws under projected lights, what draw_model hands them: at 24 bits (at 53 a
            # tie falls either way, test_pm_projected.py), on models small enough that the split
            # stays inside the exe's 2,048 triangles.
            projected = not fpu53 and corners[i] <= 16 and rng.random() < 0.25 and model_span(emu, i) < 120
            blobs = []
            for _ in range(rng.randint(1, 4) if projected else rng.randint(0, 4)):
                b = bytearray(lit.make_light(rng, cp))
                fl = struct.unpack_from('<I', b, 0xe0)[0] & ~0x40
                if projected:
                    # No map lights: a first set's split colours are leftovers on both sides
                    # (0x562120, fixes.md; test_pm_projected.py has that case).
                    fl = (fl | 0x10 | 0x40) & ~0x80
                    far = rng.uniform(40, 300)
                    near_l = far * rng.choice((0.0, 0.2, 0.7))
                    struct.pack_into('<5f', b, 0xe4, near_l, far, near_l * near_l, far * far, 1.0 / max(far - near_l, 0.01))
                struct.pack_into('<I', b, 0xe0, fl)
                blobs.append(bytes(b))
            emu.lights(blobs, 0, 0.1, 0, 0, 0, 1, 0.0)
            p.probe_pm_lights(len(blobs), b''.join(blobs) or None, 0, 0.1, 0, 0, 0, 1, 0.0)
            casters = [sets.make_caster(rng, world[9:]) for _ in range(rng.choice((0, 0, 2, 5)))]
            for k, c in enumerate(casters):
                a = emu.alloc(0x8c)
                emu.cpu._uc.mem_write(a, c)
                emu.w32(a + 0x64, sets.TEX_BASE + k)
                emu.w32(emu.caster_list + 4 * k, a)
            emu.w32(sets.CASTER_N, len(casters))
            p.probe_pm_casters(len(casters), b''.join(casters) or None, sets.TEX_BASE)
            emu.calls, emu.dump, emu.head = [], b'', None
            projected_draws += projected
            # A texture with an alpha plane takes its polygons apart, as a fade does.
            model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
            refs = emu.i32(model + 0x5c)
            alpha_ref = rng.randrange(refs) if refs > 0 and rng.random() < 0.3 else None
            if alpha_ref is not None:
                mat = emu.i32(emu.i32((emu.i32(model + 0x60) & 0xffffffff) + alpha_ref * 0xc + 8) + 0x14) & 0xffffffff
                mat = emu.i32(mat) & 0xffffffff
                if not emu.i32(mat) >> 8 & 1:  # only a textured material has a texture
                    alpha_ref = None
            if alpha_ref is not None:
                held = emu.i32(mat + 0x10)
                record, image = emu.alloc(4), emu.alloc(0x10)
                emu.cpu._uc.mem_write(image + 9, b'\x02')
                emu.w32(record, image), emu.w32(mat + 0x10, record)
                p.probe_pm_material_alpha(i, alpha_ref, 1)
            mask = rng.choice((0, 0x3f, 0x10, 0x0f, 0x1f))
            emu.draw_node(i, world, mask)
            n = p.probe_pm_submit(i, (F * 12)(*world), mask, fpu53, (F * 4)(*rect), overlay, buf, len(buf))
            if alpha_ref is not None:
                emu.w32(mat + 0x10, held)
                p.probe_pm_material_alpha(i, alpha_ref, 0)
            if n < 0:
                raise SystemExit('dump too long')
            ours = parse(buf.raw[:n])
            for pos, normal, sum_ in emu.faces:
                out = (F * 3)()
                got = struct.pack('<f', p.probe_pm_face(pos, out, fpu53))
                if bytes(out) != normal or got != sum_:
                    raise SystemExit(f'{w} draw {d} node {i}: face {struct.unpack("<f", got)[0]}, {list(out)}; the exe {struct.unpack("<f", sum_)[0]}, {struct.unpack("<3f", normal)}')
            faces += len(emu.faces)
            emu.faces = []
            what = f'{w} draw {d} node {i} mask {mask:#x} fade {fade} overlay {overlay}'
            large_draws += corners[i] > 48
            if fpu53 and [c[:2] for c in ours] != [c[:2] for c in emu.calls] and covers_nothing(ours) and covers_nothing(emu.calls):
                edge_on += 1
                continue
            if [c[:2] for c in ours] != [c[:2] for c in emu.calls]:
                x, y = [(NAMES[c[0]], c[1]) for c in ours], [(NAMES[c[0]], c[1]) for c in emu.calls]
                at = next((k for k, (m, n) in enumerate(zip(x, y)) if m != n), min(len(x), len(y)))
                raise SystemExit(f'{what}: {len(x)} calls, the exe {len(y)}; from call {at}: {x[at:at + 6]}, the exe {y[at:at + 6]}')
            for k, (a, b) in enumerate(zip(emu.calls, ours)):
                seen[NAMES[a[0]]] = seen.get(NAMES[a[0]], 0) + 1
                if a[2:4] != b[2:4]:
                    raise SystemExit(f'{what} call {k} {NAMES[a[0]]}: cull, primitive {b[2:4]}, the exe {a[2:4]}')
                if a[7] is not None and a[7] != b[7]:
                    raise SystemExit(f'{what} call {k} {NAMES[a[0]]}: shade {b[7]}, the exe {a[7]}')
                if a[8] is not None and a[8] != b[8]:
                    raise SystemExit(f'{what} call {k} {NAMES[a[0]]}: overlay {b[8]}, the exe {a[8]}')
                for v in range(a[1]):
                    at, bt = a[4][v * 32:v * 32 + 32], b[4][v * 32:v * 32 + 32]
                    uv = a[0] not in (0, 1, 7)  # a flat polygon's u, v are what its arena held; no texture reads them
                    try:
                        compare_floats(at[:16] + (at[24:] if uv else b''), bt[:16] + (bt[24:] if uv else b''), stats, 'tl', f'{what} call {k} {NAMES[a[0]]} vertex {v}')
                    except SystemExit:  # only what the plain compare refuses is counted
                        if not (fpu53 and on_side(at, rect) and side_cut(at, bt)):
                            raise
                        cuts, worst = cuts + 1, max(worst, abs(struct.unpack_from('<f', at)[0] - struct.unpack_from('<f', bt)[0]), abs(struct.unpack_from('<f', at, 4)[0] - struct.unpack_from('<f', bt, 4)[0]))
                        continue
                    da, db = struct.unpack_from('<I', at, 16)[0], struct.unpack_from('<I', bt, 16)[0]
                    sa, sb = struct.unpack_from('<I', at, 20)[0], struct.unpack_from('<I', bt, 20)[0]
                    slack = 1 if fpu53 else 0
                    if a[0] == 5 and mask & 0x10 and abs((sa >> 24) - (sb >> 24)) > 1 and any(
                            struct.unpack_from('<f', a[4], q * 32 + 8)[0] == 1.0 for q in range(a[1])):
                        unfogged += 1  # a polygon near cut under sets: 0x54a6f0 leaves its fog to the arena, fixes.md
                        sa = sb
                    if any(abs((da >> s & 255) - (db >> s & 255)) > slack for s in (24, 16, 8, 0)) or abs((sa >> 24) - (sb >> 24)) > 1:  # the fog: shared fog_vertices keeps double
                        raise SystemExit(f'{what} call {k} {NAMES[a[0]]} vertex {v}: diffuse, specular {db:#x}, {sb:#x}; the exe {da:#x}, {sa:#x}')
                if a[6] != b[6]:
                    raise SystemExit(f'{what} call {k}: {b[6]} sets, the exe {a[6]}')
                if a[5]:
                    if len(a[5]) != len(b[5]):
                        raise SystemExit(f'{what} call {k}: sets {len(b[5])} bytes, the exe {len(a[5])}')
                    compare_floats(a[5], b[5], stats, 'sets', f'{what} call {k} sets')
    print(f'ok: {stats.floats} floats, {stats.exact} exact, off by place {stats.where}; {dict(sorted(seen.items()))}; edge-on draws allowed: {edge_on}; side cuts allowed: {cuts}, largest {worst:.3f} px; draws of over 48 corners: {large_draws}; under projected lights: {projected_draws}; near cuts with stale fog: {unfogged}; {faces} facing tests bit for bit')
    if len(seen) < 6:
        raise SystemExit('too few kinds of call')


if __name__ == '__main__':
    main()
