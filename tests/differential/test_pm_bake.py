"""PM's static light bake 0x55fb30 against its exe: the same camera-space vertices,
normals and lights on real models, every polygon's scratch compared, and the
light-to-vertex vector the exe keeps on its stack (seeded alike, compared after: a
spot's first vertex reads it unset). The FPU at 24 bits, as in play.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_bake.py [bakes]
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBP

sys.path.insert(0, str(Path(__file__).parent))
from modeldraw_common import Stats, fbits, same_float  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_lighting as lit  # noqa: E402
import test_pm_modeldraw as md  # noqa: E402
import test_pm_records as rec  # noqa: E402
import test_pm_zar  # noqa: E402

BAKE, CAM, NORMALS = 0x55fb30, 0x8dabac, 0x8dabb0
TOL, ROUNDING = 2e-4, 0.002
FIRST = 0x55fb39  # past the prologue
STALE = 0x6c      # local_70: [ebp-0x6c]


class Emulated(rec.Emulated):
    STUB_BAKE = False

    def __init__(self, world):
        super().__init__(world)
        self.seed, self.left = bytes(12), bytes(12)
        self.mask_cell = self.i32(lit.MASK_P) & 0xffffffff
        self.ebp = 0
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.entry, begin=FIRST, end=FIRST)
        self.cam_at, self.normals_at = self.alloc(12 * 4096), self.alloc(12 * 4096)
        self.w32(CAM, self.cam_at), self.w32(NORMALS, self.normals_at)

    def entry(self, uc, address, size, user):
        self.ebp = uc.reg_read(UC_X86_REG_EBP)
        uc.mem_write(self.ebp - STALE, self.seed)


def main():
    bakes = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    p = test_pm_zar.load_pm_probe()
    F, V, I = ctypes.c_float, ctypes.c_void_p, ctypes.c_int
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_lights.argtypes = [I, V, I, F, F, F, F, F, F]
    p.probe_pm_lights.restype = None
    p.probe_pm_light_reach.argtypes = [I, I, I]
    p.probe_pm_light_reach.restype = None
    p.probe_pm_bake.argtypes = [I, V, V, V, V, I]
    p.probe_pm_set_model.argtypes = [I, I, ctypes.c_uint32]
    p.probe_pm_set_model.restype = None
    buf = ctypes.create_string_buffer(1 << 22)
    rng = random.Random(0x55fb30)
    stats, seen, floats = Stats(), {}, 0
    path = g.ZBD / 'c1' / 'gamez.zbd'
    emu = Emulated(path)
    assert p.probe_pm_draw_load(str(path).encode()) == 0
    nodes = []
    for i in range(emu.i32(g.N_CAPACITY)):
        r = emu.read(emu.node(i), 0xd4)
        model = struct.unpack_from('<I', r, 0x3c)[0]
        if struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 and model and emu.i32(model + 0x10) > 0 and emu.i32(model + 0x14) > 0:
            nodes.append(i)
    for d in range(bakes):
        i = rng.choice(nodes)
        model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
        nv, nn = emu.i32(model + 0x14), emu.i32(model + 0x18)
        cam = [rng.uniform(-60, 60) for _ in range(nv * 3)]
        normals = []
        for _ in range(max(nn, 1)):
            v = [rng.uniform(-1, 1) for _ in range(3)]
            s = sum(x * x for x in v) ** 0.5 or 1.0
            normals += [x / s for x in v]
        emu.cpu._uc.mem_write(emu.cam_at, fbits(*cam))
        emu.cpu._uc.mem_write(emu.normals_at, fbits(*normals))
        blobs = []
        for _ in range(rng.randint(1, 5)):
            b = bytearray(lit.make_light(rng, (0, 0, 0)))
            struct.pack_into('<I', b, 0xe0, struct.unpack_from('<I', b, 0xe0)[0] | rng.choice((0x200, 0x200, 0x200, 0)))
            if rng.random() < 0.3:
                struct.pack_into('<f', b, 0xa0, 0.0)
            blobs.append(bytes(b))
        lit.Emulated.lights(emu, blobs, 0, 0.1, 0, 0, 0, 1, 0)
        p.probe_pm_lights(len(blobs), b''.join(blobs), 0, 0.1, 0, 0, 0, 1, 0)
        for q in range(len(blobs)):
            full, reach = rng.choice((0, 0, 1)), rng.choice((1, 1, 0))
            emu.w32(lit.LIST + q * 20 + 8, full), emu.w32(lit.LIST + q * 20 + 12, reach)
            p.probe_pm_light_reach(q, full, reach)
        flags = emu.i32(model + 8)
        if rng.random() < 0.3:  # model flag 0x20: no sun on it
            flags ^= 0x20
            emu.w32(model + 8, flags), p.probe_pm_set_model(i, 8, flags & 0xffffffff)
        seen['flag 0x20'] = seen.get('flag 0x20', 0) + (1 if flags & 0x20 else 0)
        emu.seed = fbits(*(rng.uniform(-1, 1) for _ in range(3)))
        stale = (F * 3)(*struct.unpack('<3f', emu.seed))
        emu.call(BAKE, args=[model])
        want = b''
        polys = emu.i32(model + 0x34) & 0xffffffff
        for q in range(emu.i32(model + 0x10)):
            a = polys + q * 0x28
            f = emu.i32(a)
            if f & 0x400:
                continue
            want += emu.read(emu.i32(a + 0x20) & 0xffffffff, (f & 0x3ff) * 4)
            seen['vertex normals'] = seen.get('vertex normals', 0) + (1 if f & 0x1000 and not flags & 0x20 else 0)
        n = p.probe_pm_bake(i, (F * len(cam))(*cam), (F * len(normals))(*normals), stale, buf, len(buf))
        got = buf.raw[:n]
        if len(want) != len(got):
            raise SystemExit(f'bake {d} node {i}: {len(got)} bytes, the exe {len(want)}')
        for k in range(len(want) // 4):
            a, b = struct.unpack_from('<f', want, 4 * k)[0], struct.unpack_from('<f', got, 4 * k)[0]
            if not same_float(a, b, stats, TOL, 'scratch', ROUNDING):
                raise SystemExit(f'bake {d} node {i} float {k}: {b}, the exe {a}; lights '
                                 f'{[(hex(struct.unpack_from("<I", x, 0xe0)[0])) for x in blobs]}')
        floats += len(want) // 4
        left = struct.unpack('<3f', emu.read(emu.ebp - STALE, 12))
        for a, b in zip(left, stale):
            if not same_float(a, b, stats, TOL, 'vector', ROUNDING):
                raise SystemExit(f'bake {d} node {i}: vector {list(stale)}, the exe {left}')
        for fl in (0x200, 0x204, 0x208):
            seen[hex(fl)] = seen.get(hex(fl), 0) + sum(1 for x in blobs if struct.unpack_from('<I', x, 0xe0)[0] & 0x20c == fl)
    print(f'ok: {bakes} bakes, {floats} values; {stats.floats} floats, {stats.exact} exact, off by place {stats.where}; {seen}')


if __name__ == '__main__':
    main()
