"""PM's Direct3D model draw 0x543b10, its lighting: which lights reach a model
(0x55ebf0), their kinds (0x5602e0), each polygon's light (0x5603f0) and the colours
after it, against the exe, which stops at the end of each ref's lighting (0x544ae2).
The lights are made here, every kind and flag. The FPU at 24 bits, as the probes keep it.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_lighting.py [draws per world]
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
import test_pm_modeldraw as md  # noqa: E402
import test_pm_records as rec  # noqa: E402
import test_pm_zar  # noqa: E402

LIT_END = 0x544ae2
MASK_P, ANY, COUNT, LIST = 0xa4e0d8, 0xa4d014, 0xa4d01c, 0xa4d024
CLOUDS, SCALE, U, V, AMP, BASE, TINT_W = 0x612e18, 0x612e20, 0x612e34, 0x612e38, 0x612e28, 0x612e24, 0xa4e08c
TOL, ROUNDING = 2e-4, 0.002
seen_bakes = [0]


class Emulated(rec.Emulated):
    STUB_BAKE = False  # the real bake feeds the lighting when the node's stamp is old

    def __init__(self, world):
        super().__init__(world)
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.bake_entry, begin=0x55fb39, end=0x55fb39)
        self.mixes = []
        self.native_stub(0x5989b0, lambda c: self.mixes.append(self.read(c.arg(0), 12)) or 0)
        self.native_stub(0x598aa0, lambda c: 0)
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.after_lighting, begin=LIT_END, end=LIT_END)
        self.gates = None
        self.mask_cell = self.i32(MASK_P) & 0xffffffff

    def bake_entry(self, uc, address, size, user):  # its vector on the stack: zero, as the port's here
        uc.mem_write(uc.reg_read(UC_X86_REG_EBP) - 0x6c, bytes(12))
        seen_bakes[0] += 1

    def at_end(self, uc, address, size, user):  # the build stays: lighting comes next
        pass

    def after_lighting(self, uc, address, size, user):
        start = self.i32(rec.RECORDS) & 0xffffffff
        end = self.i32(0x8dcbb8) & 0xffffffff
        ebp = uc.reg_read(UC_X86_REG_EBP)
        word = lambda local: self.i32(ebp - local + 4) & 0xffffffff  # noqa: E731
        self.head = (word(0x58), int(word(0x7c) != 0) | int(word(0x94) != 0) << 1 | int(word(0xa4) != 0) << 2
                     | int(word(0x8c) != 0) << 3 | int(word(0xa8) != 0) << 4)
        refs = self.i32(self.model + 0x60) & 0xffffffff
        ref = (word(0x54) - refs) // 0xc
        polys = self.i32(self.model + 0x34) & 0xffffffff
        if end == start:  # the build left none: the exe's own branch, nothing changes
            return
        at = start
        while at < end:
            r = self.read(at, 0x38)
            poly, _, _, _, record, _, _, flags, count = struct.unpack_from('<9I', r)
            self.dump += struct.pack('<iiIi', (poly - polys) // 0x28, ref, flags, count) + r[0x28:0x34]
            for v in range(count):
                self.dump += self.read(record + v * 0x20, 12) + self.read(record + v * 0x20 + 0x10, 4)
            at += 0x38
        self.w32(0x8dcbb8, start)  # no records: the rest of the draw (later phases) finds none
        uc.reg_write(UC_X86_REG_EDI, start)  # the draw's copy of the end

    def lights(self, blobs, clouds, scale, u, v, amp, base, tint):
        self.w32(COUNT, len(blobs)), self.w32(ANY, int(bool(blobs)))
        self.w32(self.mask_cell, 1)
        for i, b in enumerate(blobs):
            a = self.alloc(0x100)
            self.cpu._uc.mem_write(a, b)
            self.w32(LIST + i * 20, a)
            for k in range(1, 5):
                self.w32(LIST + i * 20 + 4 * k, 0)
        self.w32(CLOUDS, clouds)
        for addr, val in ((SCALE, scale), (U, u), (V, v), (AMP, amp), (BASE, base), (TINT_W, tint)):
            self.cpu._uc.mem_write(addr, fbits(val))


def make_light(rng, near_cam):
    b = bytearray(0x100)
    flags = rng.choice((8, 8, 0, 0, 4, 4)) | rng.choice((2, 2, 0)) | 0x800
    for bit in (0x10, 0x20, 0x40, 0x80, 0x200, 0x400):
        if rng.random() < 0.2:
            flags |= bit
    if rng.random() < 0.1:
        flags &= ~0x800
    far = rng.uniform(20, 400)
    nr = far * rng.uniform(0.1, 0.8)
    pos = [c + rng.uniform(-far, far) for c in near_cam]
    d = [rng.uniform(-1, 1) for _ in range(3)]
    n = math.sqrt(sum(x * x for x in d)) or 1.0
    d = [x / n for x in d]
    struct.pack_into('<3f', b, 0x78, *pos)
    struct.pack_into('<3f', b, 0x90, *d)
    struct.pack_into('<2f', b, 0x9c, rng.choice((0.0, rng.uniform(0, 1), rng.uniform(0, 1))), rng.choice((0.0, rng.uniform(0, 0.5))))
    struct.pack_into('<3f', b, 0xa4, *(rng.uniform(0, 1) for _ in range(3)))
    struct.pack_into('<12f', b, 0xb0, *(rng.uniform(0, 1) for _ in range(12)))
    struct.pack_into('<I', b, 0xe0, flags)
    struct.pack_into('<5f', b, 0xe4, nr, far, nr * nr, far * far, 1.0 / (far - nr))
    return bytes(b)


def main():
    draws = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    p = test_pm_zar.load_pm_probe()
    F, V_, I, U_ = ctypes.c_float, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_draw_view.argtypes = [V_, V_, F, V_, F, V_]
    p.probe_pm_draw_view.restype = None
    p.probe_pm_model_view.argtypes = [V_, V_]
    p.probe_pm_model_view.restype = None
    p.probe_pm_records_state.argtypes = [I, F, F, F, F, I, I, F, I, F, F, F]
    p.probe_pm_records_state.restype = None
    p.probe_pm_records_light_map.argtypes = [I, I]
    p.probe_pm_records_light_map.restype = None
    p.probe_pm_lights.argtypes = [I, V_, I, F, F, F, F, F, F]
    p.probe_pm_lights.restype = None
    p.probe_pm_set_bake.argtypes = [I, V_]
    p.probe_pm_set_bake.restype = None
    p.probe_pm_set_fog_color.argtypes = [V_]
    p.probe_pm_set_fog_color.restype = None
    p.probe_pm_set_node.argtypes = [I, I, U_]
    p.probe_pm_set_node.restype = None
    p.probe_pm_set_frames.argtypes = [U_, U_]
    p.probe_pm_set_frames.restype = None
    p.probe_pm_lighting.argtypes = [I, V_, U_, V_, I]
    buf = ctypes.create_string_buffer(1 << 24)
    md.Emulated.probe = p
    rng = random.Random(0x5603f0)
    stats, records, drawn, seen = Stats(), 0, 0, {}
    for w in ('c1', 'c3'):
        path = g.ZBD / w / 'gamez.zbd'
        emu = Emulated(path)
        assert p.probe_pm_draw_load(str(path).encode()) == 0
        nodes = []
        for i in range(emu.i32(g.N_CAPACITY)):
            r = emu.read(emu.node(i), 0xd4)
            model = struct.unpack_from('<I', r, 0x3c)[0]
            if struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 and model and emu.i32(model + 0x10) > 0 and emu.i32(model) == 0:
                nodes.append(i)
        for d in range(draws):
            i = rng.choice(nodes)
            model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
            world, cam, proj, *_ = scenario(rng, (0, 0, 0), 50.0)
            near = proj[6]
            rect = (proj[0], proj[1], proj[0] + proj[2] * 2 - 0.001, proj[1] + proj[3] * 2 - 0.001)
            yaw = rng.uniform(-3.2, 3.2)
            emu.view(cam, proj, near, rect, yaw)
            views = emu.read(md.VIEW, 48) + emu.read(md.CAMERA_M, 48)
            p.probe_pm_draw_view((F * 12)(*cam), (F * 8)(*proj), near, (F * 4)(*rect), yaw, views)
            fog = rng.choice((0, 1, 2))
            fn = rng.uniform(1, 200)
            state = (fog, fn, fn + rng.uniform(1, 600), rng.uniform(50, 400), rng.uniform(-50, 40), rng.choice((1, 1, 0)),
                     0, 1.0, rng.choice((0, 0, 1)), near + rng.uniform(1, 800), rng.uniform(0.2, 2), rng.uniform(0.2, 2))
            emu.state(*state)
            p.probe_pm_records_state(*state)
            lm = rng.choice((0, 1))
            emu.w32(0x683cb0, 0x1000 if lm else 0), emu.w32(0x8ccc64, 0)
            emu.w32(0x8ccc68, 2), emu.w32(0x8ccc6c, 2)
            p.probe_pm_records_light_map(lm, 0)
            # Lights around the model, in camera space.
            cp = [sum(a * b for a, b in zip(world[9:], cam[3 * k:3 * k + 3])) for k in range(3)]
            blobs = [make_light(rng, cp) for _ in range(rng.randint(0, 5))]
            cl = (rng.choice((0, 1)), rng.uniform(0.001, 0.05), rng.uniform(0, 3), rng.uniform(0, 3), rng.uniform(0, 0.4), rng.uniform(0.3, 1), rng.uniform(0, 1))
            emu.lights(blobs, *cl)
            p.probe_pm_lights(len(blobs), b''.join(blobs) or None, *cl)
            colour = [rng.uniform(0, 1) for _ in range(3)]  # the world's fog colour
            a = emu.alloc(12)
            emu.cpu._uc.mem_write(a, fbits(*colour))
            emu.call(0x540650, args=[a])
            p.probe_pm_set_fog_color((F * 3)(*colour))
            # The bake's results, with the node's stamp saying they are good, half the time.
            node_at = emu.node(i)
            # (Old with vertex shading off: the exe would read last draw's normals.)
            stamp = rng.choice((0, 0xffffff)) if state[5] else 0xffffff
            emu.w32(node_at + 0xc0, stamp), p.probe_pm_set_node(i, 0xc0, stamp)
            emu.w32(node_at + 0xc4, 0), p.probe_pm_set_node(i, 0xc4, 0)
            emu.w32(0x8c4b60, 6), emu.w32(0x60d254, 5), p.probe_pm_set_frames(6, 5)
            polys_at = emu.i32(model + 0x34) & 0xffffffff
            bake = []
            for q in range(emu.i32(model + 0x10)):
                pa = polys_at + q * 0x28
                k = emu.i32(pa) & 0x3ff
                vals = [rng.uniform(0, 1.5) for _ in range(k)]
                emu.cpu._uc.mem_write(emu.i32(pa + 0x20) & 0xffffffff, fbits(*vals))
                bake += vals
            p.probe_pm_set_bake(i, (F * max(1, len(bake)))(*bake))
            emu.dump, emu.head, emu.mixes = b'', None, []
            mask = rng.choice((0, 0, 0x3f))
            head, want = emu.draw_node(i, world, mask)
            n = p.probe_pm_lighting(i, (F * 12)(*world), mask, buf, len(buf))
            if n < 0:
                raise SystemExit('dump too long')
            ok, needs, bits, mixes = struct.unpack_from('<4I', buf.raw)
            got = buf.raw[16 + mixes * 12:n]
            tail = 8 * len(blobs) + 12  # what the lights reached, the device's fog colour
            reached, got = got[len(got) - tail:], got[:len(got) - tail]
            exe_reached = b''.join(emu.read(LIST + q * 20 + 8, 8) for q in range(len(blobs))) + emu.read(0x8c4c28, 12)
            if head is not None and reached != exe_reached:
                raise SystemExit(f'{w} draw {d} node {i}: reached, device fog {reached.hex()}, the exe {exe_reached.hex()}')
            ours_mix = buf.raw[16:16 + mixes * 12]
            if ours_mix != b''.join(emu.mixes):
                raise SystemExit(f'{w} draw {d} node {i}: fog mixes {mixes}, the exe {len(emu.mixes)}')
            seen['fog mix'] = seen.get('fog mix', 0) + mixes
            if head is None:
                if got:
                    raise SystemExit(f'{w} draw {d} node {i}: records here, none in the exe')
                continue
            drawn += 1
            for name, on in (('baked', head[1] & 8), ('needs lights', head[0] & 2), ('projected gate', head[1] & 4), ('light map gate', head[1] & 2),
                             ('tint', head[0] & 4), ('fog', head[0] & 1)):
                if on:
                    seen[name] = seen.get(name, 0) + 1
            if (head[0], head[1]) != (needs, bits):
                raise SystemExit(f'{w} draw {d} node {i}: needs, gates {needs:#x}, {bits:#x}; the exe {head[0]:#x}, {head[1]:#x}')
            if len(want) != len(got):
                raise SystemExit(f'{w} draw {d} node {i} fog {fog}: {len(got)} bytes, the exe {len(want)}; '
                                 f'{struct.unpack_from("<iiIi", got) if got else None}, {struct.unpack_from("<iiIi", want) if want else None}')
            at = 0
            while at < len(want):
                a, b = struct.unpack_from('<iiIi', want, at), struct.unpack_from('<iiIi', got, at)
                if a != b:
                    raise SystemExit(f'{w} draw {d} node {i} fog {fog}, {len(blobs)} lights {[hex(struct.unpack_from("<I", x, 0xe0)[0]) for x in blobs]}: '
                                     f'poly, ref, flags, count {b}, the exe {a}')
                records += 1
                for name, bit in (('white off', 0x10), ('uneven', 0x40), ('fogged', 8), ('fog light', 0x18), ('light map', 0x400)):
                    if a[2] & bit == bit:
                        seen[name] = seen.get(name, 0) + 1
                at += 16
                if a[2] & 0x18:
                    for k in range(3):
                        x, y = struct.unpack_from('<f', want, at + 4 * k)[0], struct.unpack_from('<f', got, at + 4 * k)[0]
                        if not same_float(x, y, stats, TOL, 'lit', ROUNDING):
                            raise SystemExit(f'{w} draw {d} node {i} poly {a[0]}: lit {k}: {y}, the exe {x}; flags {a[2]:#x}')
                at += 12
                if head[0] == 0:  # 0x5602e0 has not run: nothing to compare yet
                    at += a[3] * 16
                    continue
                compare = a[3]
                if a[2] & 0x18 and not a[2] & 0x10 and a[2] & 0xe0:
                    seen['one colour, all vertices'] = seen.get('one colour, all vertices', 0) + 1
                if a[2] & 0x18 and not a[2] & 0x10 and not a[2] & 0xe0:  # one colour: vertex 0's
                    compare = 1
                for v in range(a[3]):
                    if v >= compare:
                        at += 16
                        continue
                    for k in range(4):
                        x, y = struct.unpack_from('<f', want, at)[0], struct.unpack_from('<f', got, at)[0]
                        if not same_float(x, y, stats, TOL, 'rgb' if k < 3 else 'fog', ROUNDING):
                            raise SystemExit(f'{w} draw {d} node {i} poly {a[0]} vertex {v}.{k}: {y}, the exe {x}; flags {a[2]:#x}, '
                                             f'lights {[hex(struct.unpack_from("<I", q, 0xe0)[0]) for q in blobs]}, needs {head[0]:#x}, clouds {cl[0]}, fog {fog}')
                        at += 4
    seen['real bakes'] = seen_bakes[0]
    print(f'ok: {drawn} draws reached a ref, {records} records; {stats.floats} floats, {stats.exact} exact, off by place {stats.where}; {seen}')
    if not seen.get('needs lights') or not seen.get('projected gate') or not seen_bakes[0]:
        raise SystemExit('the lights bit, the projected gate or the bake was never reached')


if __name__ == '__main__':
    main()
