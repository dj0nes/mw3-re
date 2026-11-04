"""PM's near clippers (pm/gamez/zmodel/gmod_clip3d; docs/pm-map.md, gmod_matl.c)
against PM's exe: 0x549440, 0x549630, 0x549ab0, 0x54a450, 0x54a6f0, 0x54aa50
with their carriers 0x54fea0 and 0x550100, at the game's 24-bit x87 precision.
Random polygons: every buffer, the result, the count, what moved to the arenas.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_clip3d.py [cases]
"""
import ctypes
import math
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402

FUNCS = [(0x549440, 'plain'), (0x549630, 'lit'), (0x549ab0, 'textured'), (0x54a450, 'textured lit'),
         (0x54a6f0, 'textured shadow'), (0x54aa50, 'textured lit shadow')]
NEAR, A_POS, A_REC, A_TL, A_EXTRA = 0x8d4b14, 0xa4cfb0, 0xa4cfac, 0xa4cfb4, 0xa4cfbc
SIZES = dict(pos=64 * 12, rec=64 * 32, tl=64 * 32, extra=64, sh=0x2358)
ARENA = dict(pos=128 * 12, rec=128 * 32, tl=128 * 32, extra=128)


def f32(x):
    if math.isfinite(x) and abs(x) > 3.4028234e38:
        x = math.copysign(math.inf, x)
    return struct.pack('<f', x)


def is_nan(b):
    v = struct.unpack('<I', b)[0]
    return v & 0x7f800000 == 0x7f800000 and v & 0x7fffff


class Emulated(g.Emulated):
    def __init__(self):
        super().__init__()
        self.cpu.fpu_control(0x007f)  # Direct3D 6's single precision
        self.a = {k: self.alloc(v) for k, v in SIZES.items()}
        self.ar = {k: self.alloc(v) for k, v in ARENA.items()}
        self.poly = self.alloc(0x38)

    def run(self, which, near, count, flags, bufs):
        uc = self.cpu._uc
        for k in SIZES:
            uc.mem_write(self.a[k], bufs[k])
        for k in ARENA:
            uc.mem_write(self.ar[k], b'\xcd' * ARENA[k])
        uc.mem_write(NEAR, f32(near))
        for addr, k in ((A_POS, 'pos'), (A_REC, 'rec'), (A_TL, 'tl'), (A_EXTRA, 'extra')):
            self.w32(addr, self.ar[k])
        p = self.poly
        for off, v in ((4, self.a['pos']), (0x10, self.a['rec']), (0x14, self.a['tl']), (0x18, self.a['sh']),
                       (0x1c, flags), (0x20, count), (0x24, self.a['extra'])):
            self.w32(p + off, v)
        ret = self.call(FUNCS[which][0], args=[p, p + 0x20])
        out = [ret, self.i32(p + 0x20)]
        ptr = lambda off: self.i32(p + off) & 0xffffffff
        out += [int(ptr(4) != self.a['pos']), int(ptr(0x10) != self.a['rec']), int(ptr(0x14) != self.a['tl']),
                int(ptr(0x24) != self.a['extra'])]
        out += [((self.i32(A_POS) & 0xffffffff) - self.ar['pos']) // 4, ((self.i32(A_REC) & 0xffffffff) - self.ar['rec']) // 4,
                ((self.i32(A_TL) & 0xffffffff) - self.ar['tl']) // 4, (self.i32(A_EXTRA) & 0xffffffff) - self.ar['extra']]
        mem = {k: self.read(self.a[k], SIZES[k]) for k in SIZES}
        mem.update({'a_' + k: self.read(self.ar[k], ARENA[k]) for k in ARENA})
        return out, mem


def rnd(rng, wide):
    if not wide:
        return rng.uniform(-50, 50)
    k = rng.random()
    if k < 0.5:
        return rng.choice((-1, 1)) * 10 ** rng.uniform(-30, 30)
    return rng.choice((0.0, -0.0, 1e38, -1e38, 3.4e38, float('inf'), float('-inf'), float('nan')))


def make(rng, which):
    wide = rng.random() < 0.08
    near = rng.choice((0.1, 0.1, 1.0, rng.uniform(0.01, 50), 0.0, -1.0))
    n = rng.randint(0, 2) if rng.random() < 0.05 else rng.randint(3, 30)
    bufs = {k: bytearray(rng.randbytes(v)) for k, v in SIZES.items()}
    style = rng.choice(('front', 'behind', 'cross', 'cross', 'cross', 'on', 'zigzag'))
    for i in range(n):
        if wide:
            z = rnd(rng, True)
        elif style == 'front':
            z = rng.uniform(near, near + 500)
        elif style == 'behind':
            z = near - rng.uniform(1e-3, 100)
        elif style == 'on':
            z = rng.choice((near, near + 1, near - 1))
        elif style == 'zigzag':
            z = near + (1 if i % 2 else -1) * rng.uniform(0.01, 50)
        else:
            z = rng.uniform(near - 50, near + 50)
        bufs['pos'][12 * i:12 * i + 12] = f32(rnd(rng, wide)) + f32(rnd(rng, wide)) + f32(z)
        bufs['rec'][32 * i:32 * i + 32] = b''.join(f32(rnd(rng, wide) if wide else rng.uniform(0, 255)) for _ in range(8))
        bufs['tl'][32 * i + 24:32 * i + 32] = f32(rng.uniform(-2, 2)) + f32(rng.uniform(-2, 2))
    sets = rng.randint(0, 7) if which >= 4 else 0
    struct.pack_into('<i', bufs['sh'], 0, sets)
    for s in range(sets):
        kind = rng.choice((1, 2))
        struct.pack_into('<i', bufs['sh'], 0x20 + 4 * s, kind)
        for i in range(n):
            bufs['sh'][0x1558 + s * 0x200 + 8 * i:0x1558 + s * 0x200 + 8 * i + 8] = f32(rng.uniform(-2, 2)) + f32(rng.uniform(-2, 2))
            if kind == 2:
                bufs['sh'][0x3c + s * 0x300 + 12 * i:0x3c + s * 0x300 + 12 * i + 12] = b''.join(f32(rng.uniform(-9, 9)) for _ in range(3))
    flags = rng.getrandbits(32)
    return near, n, flags, {k: bytes(v) for k, v in bufs.items()}, wide


def port(p, which, near, n, flags, bufs):
    b = {k: ctypes.create_string_buffer(bufs[k], SIZES[k]) for k in SIZES}
    a = {k: ctypes.create_string_buffer(b'\xcd' * ARENA[k], ARENA[k]) for k in ARENA}
    out = (ctypes.c_int32 * 10)()
    p.probe_pm_clip3d(which, ctypes.c_float(near), n, flags, b['pos'], b['rec'], b['tl'], b['extra'], b['sh'],
                      a['pos'], a['rec'], a['tl'], a['extra'], out)
    mem = {k: bytes(b[k].raw[:SIZES[k]]) for k in SIZES}
    mem.update({'a_' + k: bytes(a[k].raw[:ARENA[k]]) for k in ARENA})
    return list(out), mem


def diff(want, got, wide, stats):
    if want[0] != got[0]:
        return f'{got[0]}, the exe {want[0]}'
    for k in want[1]:
        w, h = want[1][k], got[1][k]
        if w == h:
            continue
        for off in range(0, len(w), 4):
            a, b = w[off:off + 4], h[off:off + 4]
            if a == b:
                continue
            if is_nan(a) and is_nan(b):
                stats['nan'] += 1
            elif wide:  # the x87 keeps a 15-bit exponent in registers
                stats['wide'] += 1
            else:
                return f'{k}+{off:#x}: {b.hex()}, the exe {a.hex()}'
    return None


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_clip3d.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_int, ctypes.c_uint32] + [ctypes.c_void_p] * 9 + [ctypes.POINTER(ctypes.c_int32)]
    emu = Emulated()
    rng = random.Random(0x549440)
    stats = dict(nan=0, wide=0, dropped=0, moved=0, kept=0, over=0)
    for c in range(cases):
        for which in range(6):
            near, n, flags, bufs, wide = make(rng, which)
            want = emu.run(which, near, n, flags, bufs)
            if want[0][1] > 64:
                stats['over'] += 1
                continue
            got = port(p, which, near, n, flags, bufs)
            problem = diff(want, got, wide, stats)
            if problem:
                print(f'case {c} {FUNCS[which][1]}, n={n} near={near}: {problem}')
                sys.stdout.flush()
                os._exit(1)
            stats['dropped' if not want[0][0] else 'moved' if want[0][2] else 'kept'] += 1
    if not p.probe_pm_clip3d_fog_fix():  # the shipped path: fixes.md, 0x54a6f0
        print('with bugs on, 0x54a6f0 near cut does not carry the fog')
        sys.stdout.flush()
        os._exit(1)
    print(f'ok: {cases * 6} polygons identical ({stats})')
    sys.stdout.flush()
    os._exit(0)


if __name__ == '__main__':
    main()
