"""The scene walk of a frame in PM against its exe: the world render 0x50a9e0,
the camera's matrices, the cell pick, the per-node walk and the cull on PM's
worlds, read by each side's own reader. The model draw [0xa4e100] records on
both sides: node, plane mask, the stack's top, the fade, the morph. Lights' flags
are compared after each frame. docs/pm-map.md, The debug start.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_walk.py [cameras per world]
"""
import ctypes
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modeldraw_common import COPYSIGN, CIASIN, Stats, fbits, same_float  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402

RENDER_WORLD, SET_ROT, SET_POS = 0x50a9e0, 0x506ae0, 0x506cd0
TOP_P, FLAG_P, SLOTS, FLAGS = 0x611220, 0x61121c, 0x66f4d0, 0x66f558
HSE = 0x60d978
DRAW_P, FADE, MORPH, FACTOR, FRAME, DT = 0xa4e100, 0xa4e078, 0x6390b0, 0x6301e0, 0x8c4b60, 0x8c1b4c
FLOOR = bytes.fromhex('83ec04 d93c24 668b0424 6625fff3 660d0004 6689442402 d96c2402 dd442408 d9fc d92c24 83c404 c3')
TOL, ROUNDING = 1e-4, 0.01
RECORD = 4 + 4 + 48 + 4 + 4 + 4 + 4


def u(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


class Emulated(g.Emulated):
    def __init__(self, world):
        super().__init__()
        self.cpu.fpu_control(0x007f)
        flags = self.alloc(4)
        self.native_stub(0x5b9070, lambda c: flags)  # the settings' GfxFlags entry
        self.w32(0x8c4b70, 1)
        self.call(0x53fe30)  # zModel's start: the view polygon, the mask stack, the fade
        self.stack([world], 5, 6, 5, models=True, nodes=True)
        self.w32(0x5de5bc, self.cpu.code(COPYSIGN))
        self.cpu._uc.mem_write(0x5cefd6, CIASIN)
        if 'floor' in g.IAT:
            self.cpu.import_code(g.IAT['floor'], FLOOR)
        self.log = []
        for a in (0x598bb0, 0x598bd0, 0x564ce0, 0x50b880, 0x4ff820, 0x515140, 0x5a1220):
            self.native_stub(a, lambda c: 0)
        for slot in (0x8c4c0c, 0x8c4c10):
            s = self.cpu.code(b'\xc3')
            self.native_stub(s, lambda c: 0)
            self.w32(slot, s)
        self.native_stub(0x541020, self.hidden)
        s = self.cpu.code(b'\xc3')
        self.native_stub(s, self.draw)
        self.w32(DRAW_P, s)
        self.base = self.alloc(48)
        self.cpu._uc.mem_write(self.base, fbits(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0))
        self.w32(SLOTS, self.base)
        self.w32(FLAGS, 1)
        self.w32(TOP_P, SLOTS)
        self.w32(FLAG_P, FLAGS)
        self.camera = None
        for i in range(self.i32(g.N_CAPACITY)):
            n = self.node(i)
            if self.i32(n + 0xd0) & 0x1000000 and self.i32(n + 0x34) == 1:
                self.camera, self.camera_index = n, i
                break

    def node(self, i):
        blocks = self.i32(g.N_BLOCKS) & 0xffffffff
        return (self.i32(blocks + 4 * (i // 2500)) & 0xffffffff) + 0xd4 * (i % 2500)

    def index(self, p):
        blocks = self.i32(g.N_BLOCKS) & 0xffffffff
        for b in range(self.i32(g.N_NBLOCKS)):
            base = self.i32(blocks + 4 * b) & 0xffffffff
            if base <= p < base + 0xd4 * 2500:
                return b * 2500 + (p - base) // 0xd4
        return -1

    def draw(self, c):
        node, mask = c.arg(0), c.arg(1)
        top = self.i32(self.i32(TOP_P) & 0xffffffff) & 0xffffffff
        model = self.i32(node + 0x3c) & 0xffffffff
        self.log.append(struct.pack('<iI', self.index(node), mask & 0xffffffff) + self.read(top, 48) + self.read(FADE, 4)
                        + self.read(MORPH, 4) + self.read(model + 8, 4) + self.read(model + 0x24, 4)
                        + b''.join(self.read(a + 0xe0, 4) for a in self.lights()))
        return 0

    def hidden(self, c):  # the same stand-in on both sides: a coarse grid of the centre
        x, _, z = struct.unpack('<3f', self.read(c.arg(0), 12))
        gx, gz = math.floor(struct.unpack('<f', struct.pack('<f', x / 37.0))[0]), math.floor(struct.unpack('<f', struct.pack('<f', z / 37.0))[0])
        return int((gx * 7 + gz * 13) % 5 != 0)

    def lights(self):
        data = self.i32(self.i32(self.i32(self.camera + 0x38)) + 0x38) & 0xffffffff
        return [self.i32((self.i32(data + 0xb8) & 0xffffffff) + 4 * i) & 0xffffffff for i in range(self.i32(data + 0xb0))]

    def frame(self, pos, rot, frame, dt, hse):
        self.w32(HSE, hse)
        self.call(SET_ROT, args=[self.camera] + [u(v) for v in rot])
        self.call(SET_POS, args=[self.camera] + [u(v) for v in pos])
        self.w32(FRAME, frame)
        self.cpu._uc.mem_write(DT, fbits(dt))
        self.log = []
        self.call(RENDER_WORLD, args=[self.camera, 0])
        return self.log, [self.i32(a + 0xe0) & 0xffffffff for a in self.lights()]


def main():
    cameras = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    p = test_pm_zar.load_pm_probe()
    V = ctypes.c_void_p
    p.probe_pm_walk_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_lod_set.argtypes = [ctypes.c_int, V]
    p.probe_pm_lod_set.restype = None
    p.probe_pm_light_edit.argtypes = [ctypes.c_uint32] + [ctypes.c_float] * 4
    p.probe_pm_light_edit.restype = None
    p.probe_pm_walk_frame.argtypes = [V, V, ctypes.c_uint32, ctypes.c_float, ctypes.c_int, V, ctypes.c_int]
    buf = ctypes.create_string_buffer(1 << 24)
    rng = random.Random(0x50a9e0)
    stats = Stats()
    draws, morphs, faded, lod_count = 0, 0, 0, 0
    for w in ('c1', 'c2', 'c3', 'c4'):
        path = g.ZBD / w / 'gamez.zbd'
        emu = Emulated(path)
        cam = p.probe_pm_walk_load(str(path).encode())
        if cam != emu.camera_index:
            raise SystemExit(f'{w}: camera node {cam}, the exe {emu.camera_index}')
        world = emu.i32(emu.i32(emu.camera + 0x38)) & 0xffffffff
        data = emu.i32(world + 0x38) & 0xffffffff
        lods = [i for i in range(emu.i32(g.N_CAPACITY)) if emu.i32(emu.node(i) + 0xd0) & 0x1000000 and emu.i32(emu.node(i) + 0x34) == 6]
        originals = {i: emu.read(emu.i32(emu.node(i) + 0x38) & 0xffffffff, 0x5c) for i in lods}
        lod_count += len(lods)
        x0, z0, x1, z1 = (struct.unpack('<f', emu.read(data + o, 4))[0] for o in (0x34, 0x38, 0x54, 0x58))
        for k in range(cameras):
            margin = 400.0 if k % 4 == 3 else 0.0  # past the edge: the wrapped cells
            pos = (rng.uniform(x0 - margin, x1 + margin), rng.uniform(5, 300), rng.uniform(z1 - margin, z0 + margin))
            rot = (rng.uniform(-0.6, 0.6), rng.uniform(-3.1, 3.1), rng.choice((0.0, 0.0, rng.uniform(-0.3, 0.3))))
            for i in rng.sample(lods, min(len(lods), 40)):  # every LoD field the walk reads
                d = emu.i32(emu.node(i) + 0x38) & 0xffffffff
                b = bytearray(originals[i])
                far = struct.unpack_from('<f', b, 8)[0] or 500.0
                for off in (0x10, 0x14, 0x18, 0x34, 0x38, 0x3c, 0x40, 0x48, 0x58):
                    struct.pack_into('<f', b, off, rng.uniform(1, max(2.0, far * 0.9)))
                for off in (0x1c, 0x20, 0x24, 0x44, 0x4c):
                    struct.pack_into('<f', b, off, rng.choice((0.0, 1.0)))
                for off in (0x28, 0x2c, 0x30):
                    struct.pack_into('<f', b, off, rng.uniform(0.5, 2.0))
                struct.pack_into('<I', b, 0x50, rng.choice((0, 1)))
                struct.pack_into('<I', b, 0x54, rng.choice((0, 1)))
                emu.cpu._uc.mem_write(d, bytes(b))
                p.probe_pm_lod_set(i, bytes(b))
            if emu.lights() and k % 3 == 0:  # a positioned, ranged light over part of the view
                a = emu.lights()[0]
                lf = emu.i32(a + 0xe0) & 0xffffffff | 0x12
                lp = (pos[0] + rng.uniform(-60, 60), pos[1], pos[2] + rng.uniform(-60, 60))
                lr = rng.uniform(10, 200)
                emu.w32(a + 0xe0, lf)
                emu.cpu._uc.mem_write(a + 0x60, fbits(*lp))
                emu.cpu._uc.mem_write(a + 0xe8, fbits(lr))
                p.probe_pm_light_edit(lf, *lp, lr)
            frame, dt = k + 2, 0.03
            hse = k % 2
            want, lights = emu.frame(pos, rot, frame, dt, hse)
            n = p.probe_pm_walk_frame(fbits(*pos), fbits(*rot), frame, dt, hse, buf, len(buf))
            if n < 0:
                raise SystemExit('log too long')
            got = buf.raw[:n]
            count = len(want)
            rec = RECORD + 4 * len(lights)
            if len(got) != count * rec + 4 * len(lights):
                ours = (len(got) - 4 * len(lights)) // rec
                a = [struct.unpack_from('<i', got, i * rec)[0] for i in range(ours)]
                b = [struct.unpack_from('<i', r)[0] for r in want]
                first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
                raise SystemExit(f'{w} camera {k} {pos} {rot}: {ours} draws, the exe {count}; first apart at {first}: '
                                 f'ours {a[first:first + 3]}, exe {b[first:first + 3]}')
            for i, r in enumerate(want):
                o = got[i * rec:(i + 1) * rec]
                if o[:8] != r[:8]:
                    raise SystemExit(f'{w} camera {k} draw {i}: node, mask {struct.unpack("<iI", o[:8])}, the exe {struct.unpack("<iI", r[:8])}')
                for j in range(12):
                    a, b = struct.unpack_from('<f', o, 8 + 4 * j)[0], struct.unpack_from('<f', r, 8 + 4 * j)[0]
                    if not same_float(a, b, stats, TOL, 'matrix', ROUNDING):
                        raise SystemExit(f'{w} camera {k} draw {i} matrix {j}: {a}, the exe {b}')
                a, b = struct.unpack_from('<f', o, 56)[0], struct.unpack_from('<f', r, 56)[0]
                if not same_float(a, b, stats, TOL, 'fade'):
                    raise SystemExit(f'{w} camera {k} draw {i}: fade {a}, the exe {b}')
                faded += a != 1.0
                ma, mb = struct.unpack_from('<I', o, 60)[0], struct.unpack_from('<I', r, 60)[0]
                if ma != mb or o[64:68] != r[64:68]:
                    raise SystemExit(f'{w} camera {k} draw {i}: morph {ma}, flags {o[64:68].hex()}; the exe {mb}, {r[64:68].hex()}')
                a, b = struct.unpack_from('<f', o, 68)[0], struct.unpack_from('<f', r, 68)[0]
                if not same_float(a, b, stats, TOL, 'factor'):
                    raise SystemExit(f'{w} camera {k} draw {i}: factor {a}, the exe {b}')
                morphs += ma != 0
                if o[72:] != r[72:]:
                    raise SystemExit(f'{w} camera {k} draw {i}: lights\' flags {o[72:].hex()}, the exe {r[72:].hex()}')
            for i, want_flags in enumerate(lights):
                ours = struct.unpack_from('<I', got, count * rec + 4 * i)[0]
                if ours != want_flags:
                    raise SystemExit(f'{w} camera {k} light {i}: flags {ours:#x}, the exe {want_flags:#x}')
            draws += count
    print(f'ok: {cameras} cameras on 4 worlds, {draws} draws in order, {morphs} morphing, {faded} faded, {lod_count} LoDs edited; '
          f'{stats.floats} floats, {stats.exact} exact, off by place {stats.where}')
    if not draws or not morphs or not faded:
        raise SystemExit('nothing drawn, or no morph reached')


if __name__ == '__main__':
    main()
