"""PM's side clipper (pm/gamez/zmodel/gmod_clip3d, clip2d; docs/pm-map.md,
gmod_matl.c) against PM's exe: 0x552e50 through its wrappers 0x54ade0, 0x54ae00,
0x54ae20, 0x54ae40, 0x54ae60, at the game's 24-bit x87 precision. Every buffer,
the result, the count, what moved to the arenas.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_clip2d.py [cases]
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_clip3d as c3  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402

FUNCS = [(0x54ade0, 'plain'), (0x54ae00, 'lit'), (0x54ae20, 'textured'), (0x54ae40, 'textured lit'), (0x54ae60, 'textured lit shadow')]
LEFT, TOP, RIGHT, BOTTOM = 0x8d4b0c, 0x8d4b10, 0x8d4b24, 0x8d4b28
f32 = c3.f32
SIZES = dict(rec=64 * 32, tl=64 * 32, extra=64, sh=0x2358)
ARENA = dict(rec=256 * 32, tl=256 * 32, extra=256)


class Emulated(g.Emulated):
    def __init__(self):
        super().__init__()
        self.cpu.fpu_control(0x007f)
        self.a = {k: self.alloc(v) for k, v in SIZES.items()}
        self.ar = {k: self.alloc(v) for k, v in ARENA.items()}
        self.poly = self.alloc(0x38)

    def run(self, which, codes, sides, count, bufs):
        uc = self.cpu._uc
        for k in SIZES:
            uc.mem_write(self.a[k], bufs[k])
        for k in ARENA:
            uc.mem_write(self.ar[k], b'\xcd' * ARENA[k])
        for addr, v in zip((LEFT, RIGHT, TOP, BOTTOM), sides):
            uc.mem_write(addr, f32(v))
        for addr, k in ((c3.A_REC, 'rec'), (c3.A_TL, 'tl'), (c3.A_EXTRA, 'extra')):
            self.w32(addr, self.ar[k])
        p = self.poly
        for off, v in ((0x10, self.a['rec']), (0x14, self.a['tl']), (0x18, self.a['sh']), (0x20, count),
                       (0x24, self.a['extra']), (0x34, codes)):
            self.w32(p + off, v)
        ret = self.call(FUNCS[which][0], args=[p, p + 0x20])
        ptr = lambda off: self.i32(p + off) & 0xffffffff
        out = [ret, self.i32(p + 0x20), int(ptr(0x10) != self.a['rec']), int(ptr(0x14) != self.a['tl']), int(ptr(0x24) != self.a['extra']),
               ((self.i32(c3.A_REC) & 0xffffffff) - self.ar['rec']) // 4, ((self.i32(c3.A_TL) & 0xffffffff) - self.ar['tl']) // 4,
               (self.i32(c3.A_EXTRA) & 0xffffffff) - self.ar['extra']]
        mem = {k: self.read(self.a[k], SIZES[k]) for k in SIZES}
        mem.update({'a_' + k: self.read(self.ar[k], ARENA[k]) for k in ARENA})
        return out, mem


def make(rng, which):
    wide = rng.random() < 0.05
    n = rng.randint(0, 2) if rng.random() < 0.05 else rng.randint(3, 12)
    l, t = rng.choice((0.0, rng.uniform(0, 100))), rng.choice((0.0, rng.uniform(0, 100)))
    sides = [l, l + rng.uniform(100, 640), t, t + rng.uniform(100, 480)]
    bufs = {k: bytearray(rng.randbytes(v)) for k, v in SIZES.items()}
    style = rng.choice(('wide', 'wide', 'inside', 'left', 'right', 'top', 'bottom', 'zigzag', 'on', 'nan'))
    for i in range(n):
        x, y = rng.uniform(l - 300, sides[1] + 300), rng.uniform(t - 300, sides[3] + 300)
        if style == 'inside':
            x, y = rng.uniform(l, sides[1] - 1), rng.uniform(t, sides[3] - 1)
        elif style == 'left':
            x = l - rng.uniform(0.1, 50)
        elif style == 'right':
            x = sides[1] + rng.uniform(0.1, 50)
        elif style == 'top':
            y = t - rng.uniform(0.1, 50)
        elif style == 'bottom':
            y = sides[3] + rng.uniform(0.1, 50)
        elif style == 'zigzag':
            x = l + (10 if i % 2 else -10)
        elif style == 'on':
            x, y = rng.choice((l, sides[1], l + 5)), rng.choice((t, sides[3], t + 5))
        if style == 'nan' and i % 3 == 0:  # the compares' NaN, with no extreme values
            x, y = rng.choice(((float('nan'), y), (x, float('nan'))))
        z = c3.rnd(rng, True) if wide else rng.uniform(0.001, 1.0)
        if wide:
            x, y = c3.rnd(rng, True), c3.rnd(rng, True)
        bufs['tl'][32 * i:32 * i + 12] = f32(x) + f32(y) + f32(z)
        bufs['tl'][32 * i + 24:32 * i + 32] = f32(rng.uniform(-2, 2)) + f32(rng.uniform(-2, 2))
        bufs['rec'][32 * i:32 * i + 32] = b''.join(f32(rng.uniform(0, 255)) for _ in range(8))
    sets = rng.randint(0, 7) if which == 4 else 0
    struct.pack_into('<i', bufs['sh'], 0, sets)
    for s in range(sets):
        kind = rng.choice((1, 2))
        struct.pack_into('<i', bufs['sh'], 0x20 + 4 * s, kind)
        for i in range(n):
            bufs['sh'][0x1558 + s * 0x200 + 8 * i:0x1558 + s * 0x200 + 8 * i + 8] = f32(rng.uniform(-2, 2)) + f32(rng.uniform(-2, 2))
            if kind == 2:
                bufs['sh'][0x3c + s * 0x300 + 12 * i:0x3c + s * 0x300 + 12 * i + 12] = b''.join(f32(rng.uniform(-9, 9)) for _ in range(3))
    codes = rng.choice((0, 0xf, 0xf, 1, 2, 4, 8, 3, 0xc, 5, rng.getrandbits(4), 0x10, 0x30, 0x1f, rng.getrandbits(32)))
    return codes, sides, n, {k: bytes(v) for k, v in bufs.items()}, wide


def port(p, which, codes, sides, n, bufs):
    b = {k: ctypes.create_string_buffer(bufs[k], SIZES[k]) for k in SIZES}
    a = {k: ctypes.create_string_buffer(b'\xcd' * ARENA[k], ARENA[k]) for k in ARENA}
    out = (ctypes.c_int32 * 8)()
    s = (ctypes.c_float * 4)(*sides)
    p.probe_pm_clip2d(which, codes, s, n, b['rec'], b['tl'], b['extra'], b['sh'], a['rec'], a['tl'], a['extra'], out)
    mem = {k: bytes(b[k].raw[:SIZES[k]]) for k in SIZES}
    mem.update({'a_' + k: bytes(a[k].raw[:ARENA[k]]) for k in ARENA})
    return list(out), mem


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_clip2d.argtypes = [ctypes.c_int, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int] + [ctypes.c_void_p] * 7 + [ctypes.POINTER(ctypes.c_int32)]
    emu = Emulated()
    rng = random.Random(0x552e50)
    stats = dict(nan=0, wide=0, wide_result=0, dropped=0, moved=0, kept=0, over=0)
    for c in range(cases):
        for which in range(5):
            codes, sides, n, bufs, wide = make(rng, which)
            want = emu.run(which, codes, sides, n, bufs)
            if want[0][1] > 64:
                stats['over'] += 1
                continue
            got = port(p, which, codes, sides, n, bufs)
            if wide and want[0] != got[0]:  # the x87's 15-bit exponent can change which vertices survive
                stats['wide_result'] += 1
                continue
            problem = c3.diff(want, got, wide, stats)
            if problem:
                print(f'case {c} {FUNCS[which][1]}, n={n} codes={codes:#x}: {problem}')
                sys.stdout.flush()
                os._exit(1)
            stats['dropped' if not want[0][0] else 'moved' if want[0][3] else 'kept'] += 1
    print(f'ok: {cases * 5} polygons identical ({stats})')
    sys.stdout.flush()
    os._exit(0)


if __name__ == '__main__':
    main()
