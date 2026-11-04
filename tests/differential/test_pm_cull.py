"""The Direct3D draw's plane tests and vertex colours (pm/gamez/zmodel/gmod_cull;
docs/pm-map.md, gmod_matl.c) against PM's exe: 0x54faa0, 0x54fc60, 0x5439e0. The
colours at both precisions: 53-bit (play, zfix fpu53) and 24-bit.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_cull.py [cases]
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

PLANE_CODES, POLY_CODES, COLOURS = 0x54faa0, 0x54fc60, 0x5439e0
PLANES, NEAR, FAR, SX, SY, CODES, FADE = 0x8d4b08, 0x8d4b14, 0x8d4b20, 0x8d43a4, 0x8d43a8, 0x8dbbb4, 0xa4e078


def f32(x):
    if math.isfinite(x) and abs(x) > 3.4028234e38:
        x = math.copysign(math.inf, x)
    return struct.pack('<f', x)


def rnd(rng):
    k = rng.random()
    if k < 0.8:
        return rng.uniform(-200, 200)
    if k < 0.9:
        return rng.choice((-1, 1)) * 10 ** rng.uniform(-20, 20)
    return rng.choice((0.0, -0.0, float('inf'), float('-inf'), float('nan')))


class Emulated(g.Emulated):
    def __init__(self, cw):
        super().__init__()
        self.cpu.fpu_control(cw)
        self.pos, self.codes, self.poly = self.alloc(64 * 12), self.alloc(64 * 4), self.alloc(0x38)
        self.rec, self.tl, self.verts, self.out = self.alloc(64 * 32), self.alloc(64 * 32), self.alloc(64 * 4), self.alloc(4)

    def plane_codes(self, planes, near, far, sx, sy, pos, n):
        uc = self.cpu._uc
        self.w32(PLANES, planes)
        for a, v in ((NEAR, near), (FAR, far), (SX, sx), (SY, sy)):
            uc.mem_write(a, f32(v))
        uc.mem_write(self.pos, pos)
        uc.mem_write(self.codes, b'\xcd' * (64 * 4))
        return self.call(PLANE_CODES, args=[self.pos, n, self.codes]), self.read(self.codes, 64 * 4)

    def polygon_codes(self, table, n, verts):
        uc = self.cpu._uc
        uc.mem_write(CODES, table)
        uc.mem_write(self.verts, verts)
        uc.mem_write(self.out, b'\xcd' * 4)  # 0x54fc60 clears it
        return self.call(POLY_CODES, args=[n, self.verts, self.out]), self.read(self.out, 4)

    def colours(self, fade, count, rec, tl):
        uc = self.cpu._uc
        uc.mem_write(FADE, f32(fade))
        uc.mem_write(self.rec, rec)
        uc.mem_write(self.tl, tl)
        self.w32(self.poly + 0x10, self.rec)
        self.w32(self.poly + 0x14, self.tl)
        self.w32(self.poly + 0x20, count)
        self.call(COLOURS, args=[self.poly])
        return self.read(self.tl, 64 * 32)


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
    p = test_pm_zar.load_pm_probe()
    F, V = ctypes.c_float, ctypes.c_void_p
    p.probe_pm_plane_codes.argtypes = [ctypes.c_uint32, F, F, F, F, V, ctypes.c_int, V]
    p.probe_pm_polygon_codes.argtypes = [V, ctypes.c_int, V, V]
    p.probe_pm_tl_colours.argtypes = [ctypes.c_int, F, ctypes.c_int, V, V]
    p.probe_pm_tl_colours.restype = None
    emu53, emu24 = Emulated(0x027f), Emulated(0x007f)
    rng = random.Random(0x54faa0)
    rejected = kept = 0
    for c in range(cases):
        # The plane codes.
        n = rng.randint(0, 2) if rng.random() < 0.05 else rng.randint(3, 64)
        near, far = rng.choice((0.1, 1.0, rng.uniform(0, 50))), rng.uniform(50, 2000)
        sx, sy = rng.uniform(0.2, 3), rng.uniform(0.2, 3)
        planes = rng.getrandbits(6) | (rng.getrandbits(26) << 6 if rng.random() < 0.1 else 0)
        style = rng.choice(('wide', 'wide', 'behind', 'beyond', 'left', 'right', 'up', 'down'))
        pts = []
        for _ in range(n):
            x, y, z = rnd(rng), rnd(rng), rnd(rng)
            if style == 'behind':
                z = near - rng.uniform(0.01, 5)
            elif style == 'beyond':
                z = far + rng.choice((0.0, 0.0, rng.uniform(0, 5)))
            elif style == 'left':
                x, z = -abs(x) - 1, 0.1
            elif style == 'right':
                x, z = abs(x) + 1, 0.1
            elif style == 'up':
                y, z = abs(y) + 1, 0.1
            elif style == 'down':
                y, z = -abs(y) - 1, 0.1
            pts.append((x, y, z))
        pos = bytes(b''.join(f32(v) for q in pts for v in q).ljust(64 * 12, b'\0'))
        want = emu24.plane_codes(planes, near, far, sx, sy, pos, n)
        codes = ctypes.create_string_buffer(b'\xcd' * (64 * 4), 64 * 4)
        got = p.probe_pm_plane_codes(planes, near, far, sx, sy, pos, n, codes) & 0xffffffff
        if bool(got) != bool(want[0]) or codes.raw[:64 * 4] != want[1]:
            sys.exit(f'case {c} plane_codes planes={planes:#x} n={n}: {got} {codes.raw[:32].hex()}, exe {want[0]} {want[1][:32].hex()}')
        rejected += not want[0]
        kept += bool(want[0])
        # A polygon's codes.
        table = struct.pack('<1024I', *(rng.choice((0, 0, 0x10, 0x20, 1, 2, 4, 8, 0x30, rng.getrandbits(6), 0x80 | rng.getrandbits(6))) for _ in range(1024)))
        m = rng.randint(0, 12)
        one = rng.choice((0x10, 0x20, 1, 2, 4, 8))
        if rng.random() < 0.3:  # all out of one plane
            table = struct.pack('<1024I', *(one | rng.getrandbits(6) for _ in range(1024)))
        verts = struct.pack(f'<{m}i', *(rng.randrange(1024) for _ in range(m))).ljust(64 * 4, b'\0')
        want = emu24.polygon_codes(table, m, verts)
        out = ctypes.create_string_buffer(b'\xcd' * 4, 4)
        got = p.probe_pm_polygon_codes(table, m, verts, out) & 0xffffffff
        if bool(got) != bool(want[0]) or out.raw[:4] != want[1]:
            sys.exit(f'case {c} polygon_codes n={m}: {got} {out.raw.hex()}, exe {want[0]} {want[1].hex()}')
        # The colours.
        count = rng.randint(0, 64)
        fade = rng.choice((1.0, 1.0, rng.uniform(0, 1), rng.uniform(-1, 3), 0.0, float('nan')))
        recs = bytearray(rng.randbytes(64 * 32))
        for i in range(count):
            recs[32 * i:32 * i + 12] = b''.join(f32(rng.choice((rng.uniform(-20, 280), float(rng.randint(0, 255)) + 0.5, rnd(rng)))) for _ in range(3))
        tl0 = rng.randbytes(64 * 32)
        for fpu53, emu in ((1, emu53), (0, emu24)):
            want = emu.colours(fade, count, bytes(recs), tl0)
            tl = ctypes.create_string_buffer(tl0, 64 * 32)
            r = ctypes.create_string_buffer(bytes(recs), 64 * 32)
            p.probe_pm_tl_colours(fpu53, fade, count, r, tl)
            if tl.raw[:64 * 32] != want:
                for off in range(0, 64 * 32, 4):
                    if tl.raw[off:off + 4] != want[off:off + 4]:
                        i = off // 32
                        sys.exit(f'case {c} colours fpu53={fpu53} fade={fade} v{i} rec {struct.unpack_from("<3f", recs, 32 * i)}: {tl.raw[off:off + 4].hex()}, exe {want[off:off + 4].hex()}')
    print(f'ok: {cases} of each identical ({rejected} rejected, {kept} kept; colours at 53 and 24 bits)')
    sys.stdout.flush()
    os._exit(0)


if __name__ == '__main__':
    main()
