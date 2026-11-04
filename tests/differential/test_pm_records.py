"""PM's Direct3D model draw 0x543b10, its first part: the model's needs, camera
space, the plane codes, the normals, then each material ref's polygon records
(pm/gamez/zmodel/gmod_draw; docs/pm-map.md, gmod_matl.c). The exe stops at the end of
each ref's build (0x54471c) and what it built is compared, the rest skipped. Lights
wait for phase 4. The FPU at 24 bits, as in play.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_records.py [draws per world]
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EDI

sys.path.insert(0, str(Path(__file__).parent))
from modeldraw_common import Stats, fbits, same_float, scenario  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_modeldraw as md  # noqa: E402
import test_pm_zar  # noqa: E402

DRAW, RECORDS, BUILD_END = 0x543b10, 0x8dcbb4, 0x54471c
FOG_ON, FOG_NEAR, FOG_FAR, ALT_HIGH, ALT_LOW = 0xa4e038, 0x540520, 0x540570, 0x5405b0, 0x5405f0
SHADING, SHADOWS, FADE, TINT, FAR, SX, SY = 0xa4d008, 0xa4e070, 0xa4e078, 0xa4e07c, 0x8d4b20, 0x8d43a4, 0x8d43a8
TOL = 1e-4
FACADE_TOL = 1e-3  # as test_pm_modeldraw.py: zmth's trig against the x87's turns the whole polygon
ROUNDING = 0.02  # a position that nearly cancels against a translation of hundreds at 24 bits


class Emulated(md.Emulated):
    STUB_BAKE = True

    def __init__(self, world):
        flags_holder = {}
        self._before = flags_holder
        super().__init__(world)
        # 0x5400e0's arenas (a device's, which the bench has not).
        self.w32(RECORDS, self.alloc(0x38 * 0x2000)), self.w32(0x8dcbbc, self.alloc(0xc * 0x2000))
        self.w32(0xa4cfa8, self.alloc(0x2358 * 0x400)), self.w32(0x8c4c5c, self.alloc(0x803e0))
        # A texture's image is null here (the bench loads none): its flags, at +9, read as 0.
        self.cpu._uc.mem_map(0, 0x1000)
        self.dump, self.model = b'', 0
        self.head = None
        self.native_stub(0x59b870, lambda c: 0)
        if self.STUB_BAKE:  # this test stops short of the light; test_pm_bake has it
            self.native_stub(0x55fb30, lambda c: 0)
        s = self.cpu.code(b'\xc3')
        self.native_stub(s, lambda c: 0)
        self.w32(0x8c4c18, s)
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.at_end, begin=BUILD_END, end=BUILD_END)

    def stack(self, *a, **k):  # zModel's start first: the arenas, the mask stack, the fade
        flags = self.alloc(4)
        self.native_stub(0x5b9070, lambda c: flags)
        self.w32(0x8c4b70, 1)
        self.call(0x53fe30)
        return super().stack(*a, **k)

    def at_end(self, uc, address, size, user):
        start = self.i32(RECORDS) & 0xffffffff
        end = uc.reg_read(UC_X86_REG_EDI)
        ebp = uc.reg_read(UC_X86_REG_EBP)
        word = lambda local: self.i32(ebp - local + 4) & 0xffffffff  # noqa: E731
        self.head = (word(0x58), int(word(0x7c) != 0) | int(word(0x94) != 0) << 1 | int(word(0xa4) != 0) << 2
                     | int(word(0x8c) != 0) << 3 | int(word(0xa8) != 0) << 4)
        needs, cull = self.head[0], self.head[1] & 0x10
        ref_at = word(0x54)  # the ref
        refs = self.i32(self.model + 0x60) & 0xffffffff
        ref = (ref_at - refs) // 0xc
        polys = self.i32(self.model + 0x34) & 0xffffffff
        mat = self.i32(self.i32((self.i32(ref_at + 8) & 0xffffffff) + 0x14) & 0xffffffff) & 0xffffffff
        textured = self.i32(mat) >> 8 & 1
        at = start
        while at < end:
            r = self.read(at, 0x38)
            poly, pos, normal, normals, rec, tl, sets, flags, count = struct.unpack_from('<9I', r)
            codes = struct.unpack_from('<I', r, 0x34)[0]
            verts = self.i32(poly) & 0x3ff
            self.dump += struct.pack('<iiIiI', (poly - polys) // 0x28, ref, flags, count, codes)
            self.dump += self.read(pos, verts * 12)
            n_sets = self.i32(sets) if sets else 0
            self.dump += struct.pack('<iii', int(normal != 0), int(normals != 0), n_sets)
            if normal and cull:
                self.dump += self.read(normal, 12)
            if normals:
                self.dump += self.read(normals, verts * 12)
            for k in range(n_sets):
                self.dump += self.read(sets + 0x1558 + k * 0x200, verts * 8)
            for v in range(verts):
                if needs & 1:
                    self.dump += self.read(rec + v * 0x20 + 0x10, 4)
                if textured:
                    self.dump += self.read(rec + v * 0x20 + 0x14, 12) + self.read(tl + v * 0x20 + 0x18, 8)
            at += 0x38
        uc.reg_write(UC_X86_REG_EDI, start)  # nothing built: on to the next ref

    def state(self, fog, near, far, high, low, shading, shadows, fade, tint, far_z, sx, sy):
        for a, v in ((FOG_NEAR, near), (FOG_FAR, far), (ALT_HIGH, high), (ALT_LOW, low)):
            self.call(a, args=[struct.unpack('<I', fbits(v))[0]])
        self.w32(FOG_ON, fog), self.w32(SHADING, shading), self.w32(SHADOWS, shadows), self.w32(TINT, tint)
        self.cpu._uc.mem_write(FADE, fbits(fade))
        self.cpu._uc.mem_write(FAR, fbits(far_z))
        self.cpu._uc.mem_write(SX, fbits(sx)), self.cpu._uc.mem_write(SY, fbits(sy))

    def draw_node(self, i, world, mask):
        self.cpu._uc.mem_write(self.world, fbits(*world))
        self.w32(md.SLOTS, self.world)
        self.w32(md.FLAGS, 0)
        self.w32(md.TOP_P, md.SLOTS)
        self.w32(md.FLAG_P, md.FLAGS)
        self.dump, self.head = b'', None
        node = self.node(i)
        self.model = self.i32(node + 0x3c) & 0xffffffff
        self.call(DRAW, args=[node, mask])
        return self.head, self.dump


def main():
    draws = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    p = test_pm_zar.load_pm_probe()
    F, V, I, U = ctypes.c_float, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_draw_view.argtypes = [V, V, F, V, F, V]
    p.probe_pm_draw_view.restype = None
    p.probe_pm_model_view.argtypes = [V, V]
    p.probe_pm_model_view.restype = None
    p.probe_pm_set_model.argtypes = [I, I, U]
    p.probe_pm_set_model.restype = None
    p.probe_pm_set_morphs.argtypes = [I, V, I]
    p.probe_pm_set_morphs.restype = None
    p.probe_pm_records_state.argtypes = [I, F, F, F, F, I, I, F, I, F, F, F]
    p.probe_pm_records_state.restype = None
    p.probe_pm_records.argtypes = [I, V, U, V, I]
    p.probe_pm_set_node.argtypes = [I, I, U]
    p.probe_pm_set_node.restype = None
    p.probe_pm_records_light_map.argtypes = [I, I]
    p.probe_pm_records_light_map.restype = None
    p.probe_pm_set_sets.argtypes = [I, I, I, V]
    p.probe_pm_set_sets.restype = None
    buf = ctypes.create_string_buffer(1 << 24)
    md.Emulated.probe = p
    rng = random.Random(0x543b10)
    stats, kinds, records, drawn, seen = Stats(), {}, 0, 0, {}
    for w in ('c1', 'c3'):
        path = g.ZBD / w / 'gamez.zbd'
        emu = Emulated(path)
        assert p.probe_pm_draw_load(str(path).encode()) == 0
        nodes = []
        for i in range(emu.i32(g.N_CAPACITY)):
            n = emu.node(i)
            r = emu.read(n, 0xd4)
            model = struct.unpack_from('<I', r, 0x3c)[0]
            if struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 and model and emu.i32(model + 0x10) > 0 and emu.i32(model) in (0, 1):
                nodes.append(i)
        for d in range(draws):
            i = rng.choice(nodes)
            model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
            undo = []

            def set_model(off, v):
                undo.append((off, emu.i32(model + off) & 0xffffffff))
                emu.w32(model + off, v)
                p.probe_pm_set_model(i, off, v)
            nv = emu.i32(model + 0x14)
            kind = rng.choice(('as is', 'as is', 'as is', 'facade 0', 'facade 1', 'facade 2', 'morph', 'morph unflagged', 'fog flag', 'sets'))
            if kind.startswith('facade'):
                set_model(0, 1), set_model(4, int(kind[-1]))
            elif kind == 'sets':
                # The ref's first polygon, as its UVs are read: two or more materials.
                ref_at = emu.i32(model + 0x60) & 0xffffffff
                poly = emu.i32(ref_at + 8) & 0xffffffff
                pi = (poly - (emu.i32(model + 0x34) & 0xffffffff)) // 0x28
                if not emu.i32(poly + 0x18) or emu.i32(poly + 0x10) != 1:
                    kind = 'as is'
                else:
                    k, pn = rng.randint(1, 3), emu.i32(poly) & 0x3ff
                    uvs = [rng.uniform(-2, 2) for _ in range(k * pn * 2)]
                    mats, arr = emu.alloc(4 * (k + 1)), emu.alloc(4 * (k + 1))
                    m0, a0 = emu.i32(emu.i32(poly + 0x14) & 0xffffffff), emu.i32(emu.i32(poly + 0x18) & 0xffffffff)
                    emu.w32(mats, m0), emu.w32(arr, a0)
                    for j in range(k):
                        a = emu.alloc(pn * 8)
                        emu.cpu._uc.mem_write(a, fbits(*uvs[j * pn * 2:(j + 1) * pn * 2]))
                        emu.w32(mats + 4 * (j + 1), m0), emu.w32(arr + 4 * (j + 1), a)
                    emu.w32(poly + 0x14, mats), emu.w32(poly + 0x18, arr), emu.w32(poly + 0x10, k + 1)
                    p.probe_pm_set_sets(i, pi, k, (F * len(uvs))(*uvs))
            elif kind.startswith('morph') and nv > 0:
                k = nv  # past a shorter list the vertices are the last draw's on each side
                deltas = struct.pack(f'<{k * 3}f', *(rng.uniform(-3, 3) for _ in range(k * 3)))
                a = emu.alloc(len(deltas))
                emu.cpu._uc.mem_write(a, deltas)
                emu.w32(model + 0x44, a)
                p.probe_pm_set_morphs(i, deltas, k)
                if kind == 'morph':
                    set_model(8, emu.i32(model + 8) | 8)
                else:
                    set_model(8, emu.i32(model + 8) & ~8)
                set_model(0x1c, k)
                f = rng.choice((0.0, 0.5, 1.0))
                set_model(0x24, struct.unpack('<I', fbits(f))[0])
            elif kind == 'fog flag':
                set_model(8, emu.i32(model + 8) | 2)
            kinds[kind] = kinds.get(kind, 0) + 1
            world, cam, proj, *_ = scenario(rng, (0, 0, 0), 50.0)
            # Facades turn to the camera: exactly on the node that is atan2 of noise
            # (test_pm_modeldraw.py).
            while kind.startswith('facade') and all(abs(a - b) < 1 for a, b in zip(world[9:], cam[9:])):
                world, cam, proj, *_ = scenario(rng, (0, 0, 0), 50.0)
            near = proj[6]
            rect = (proj[0], proj[1], proj[0] + proj[2] * 2 - 0.001, proj[1] + proj[3] * 2 - 0.001)
            yaw = rng.uniform(-3.2, 3.2)
            emu.view(cam, proj, near, rect, yaw)
            views = emu.read(md.VIEW, 48) + emu.read(md.CAMERA_M, 48)
            p.probe_pm_draw_view((F * 12)(*cam), (F * 8)(*proj), near, (F * 4)(*rect), yaw, views)
            fog = rng.choice((0, 0, 1, 2))
            fn = rng.uniform(1, 200)
            state = (fog, fn, fn + rng.uniform(1, 600), rng.uniform(50, 400), rng.uniform(-50, 40), rng.choice((0, 1)),
                     rng.choice((0, 1)), rng.choice((1.0, 1.0, 1.0, 0.5, 0.0)), rng.choice((0, 0, 1)),
                     near + rng.uniform(1, 800), rng.uniform(0.2, 2), rng.uniform(0.2, 2))
            emu.state(*state)
            p.probe_pm_records_state(*state)
            shadowers = rng.choice((0, 0, 1, 2))  # node +0xbc: 2 or more, no light map or projected lights
            emu.w32(emu.node(i) + 0xbc, shadowers), p.probe_pm_set_node(i, 0xbc, shadowers)
            lm, option = rng.choice((0, 1, 1)), rng.choice((0, 1, 2, -1))
            emu.w32(0x683cb0, 0x1000 if lm else 0), emu.w32(0x8ccc64, option)
            emu.w32(0x8ccc68, 2), emu.w32(0x8ccc6c, 2)  # FIX (A): as ours always is
            p.probe_pm_records_light_map(lm, option)
            mask = rng.choice((0, 0, 0x3f, 0x30, 0x10, 0x0f, 0x20))
            head, want = emu.draw_node(i, world, mask)
            n = p.probe_pm_records(i, (F * 12)(*world), mask, buf, len(buf))
            if n < 0:
                raise SystemExit('dump too long')
            got = buf.raw[12:n]
            ok, needs, bits = struct.unpack_from('<III', buf.raw)
            seen['light map gate'] = seen.get('light map gate', 0) + (bits >> 1 & 1)
            for off, v in reversed(undo):
                emu.w32(model + off, v)
                p.probe_pm_set_model(i, off, v)
            if head is None:  # the exe never reached a ref's end
                if got:
                    raise SystemExit(f'{w} draw {d} node {i} {kind}: records here, none in the exe')
                continue
            drawn += 1
            if ok and (head[0], head[1]) != (needs, bits):
                raise SystemExit(f'{w} draw {d} node {i} {kind}: needs, gates {needs:#x}, {bits:#x}; the exe {head[0]:#x}, {head[1]:#x}')
            if len(want) != len(got):
                raise SystemExit(f'{w} draw {d} node {i} {kind} fog {fog} mask {mask:#x}: {len(got)} bytes, the exe {len(want)};'
                                 f'heads {struct.unpack_from("<iiIiI", got) if got else None} {struct.unpack_from("<iiIiI", want) if want else None}')
            tol = FACADE_TOL if kind.startswith('facade') else TOL
            at = 0
            while at < len(want):
                poly_w = struct.unpack_from('<iiIiI', want, at)
                poly_g = struct.unpack_from('<iiIiI', got, at)
                if poly_w != poly_g:
                    raise SystemExit(f'{w} draw {d} node {i} {kind} at {at}: poly, ref, flags, count, codes {poly_g}, the exe {poly_w}')
                records += 1
                # The rest of a record is floats and three counts; floats compared one by one.
                verts = emu.i32((emu.i32(model + 0x34) & 0xffffffff) + poly_w[0] * 0x28) & 0x3ff
                at += 20
                for j in range(verts * 3):
                    a, b = struct.unpack_from('<f', want, at)[0], struct.unpack_from('<f', got, at)[0]
                    if not same_float(a, b, stats, tol, 'pos', ROUNDING):
                        raise SystemExit(f'{w} draw {d} node {i} {kind}: position {j}: {b}, the exe {a}')
                    at += 4
                if want[at:at + 12] != got[at:at + 12]:
                    raise SystemExit(f'{w} draw {d} node {i}: normal, normals, sets {struct.unpack_from("<iii", got, at)}, the exe {struct.unpack_from("<iii", want, at)}')
                has_n, has_vn, sets = struct.unpack_from('<iii', want, at)
                pf = emu.i32((emu.i32(model + 0x34) & 0xffffffff) + poly_w[0] * 0x28)
                for name, on in (('strip', pf & 0x2000), ('two-sided', pf & 0x400), ('sets', sets), ('vertex normals', has_vn),
                                 ('fogged', poly_w[2] & 8), ('fog varies', poly_w[2] & 0x20), ('all fog', poly_w[3] == 0),
                                 ('codes', poly_w[4])):
                    if on:
                        seen[name] = seen.get(name, 0) + 1
                at += 12
                ref_at = (emu.i32(model + 0x60) & 0xffffffff) + poly_w[1] * 0xc
                mat = emu.i32(emu.i32((emu.i32(ref_at + 8) & 0xffffffff) + 0x14) & 0xffffffff) & 0xffffffff
                textured = emu.i32(mat) >> 8 & 1
                count = 0
                if has_n and head[1] & 0x10:
                    count += 3
                if has_vn:
                    count += verts * 3
                count += sets * verts * 2
                count += verts * ((1 if head[0] & 1 else 0) + (5 if textured else 0))
                for j in range(count):
                    a, b = struct.unpack_from('<f', want, at)[0], struct.unpack_from('<f', got, at)[0]
                    if not same_float(a, b, stats, tol, 'data', ROUNDING):
                        raise SystemExit(f'{w} draw {d} node {i} {kind} poly {poly_w[0]}: float {j} of {count}: {b}, the exe {a}')
                    at += 4
    print(f'ok: {drawn} draws reached a ref, {records} records; {stats.floats} floats, {stats.exact} exact, off by place {stats.where}; {kinds}; {seen}')
    if records < 1000:
        raise SystemExit('too few records')


if __name__ == '__main__':
    main()
