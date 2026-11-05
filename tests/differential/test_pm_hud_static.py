"""Pirate's Moon's HUD static (src/mech3/hud.cpp on the pm branch; docs/hud.md
section 21, Pirate's Moon) against PM's exe in Unicorn, at the game's 24-bit x87:

- the noise 0x43dda0 (256 x 256: which green and the alpha of every pixel) on
  random rand seeds;
- the quad 0x43dfa0 over random rects (its four vertices, as handed to the
  device's draw 0x59df40), with a device;
- the tick 0x43e1f0 over frames with the panels hidden: the warning (the HUD's
  mech's health under 0.2, its Invincible flag, GameIntensity's flag), the add
  0x43d0b0 with its random channel, the channels' easing to their floors, and the
  radar's fade (0x43f190). The exe adds 0.1 a frame; the port's per_second
  makes it a 30 fps frame's, so the tick is compared with per_second off.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_static.py [runs]

Not compared here: which panel's rect the tick draws over (read from the code,
as MW3's 0x43cd20, test_hud.py).
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))
probe.probe_pm_static_noise.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p]
probe.probe_pm_static_quad.argtypes = [ctypes.c_uint32] + [ctypes.c_int] * 4 + [ctypes.c_void_p]
probe.probe_pm_static_frames.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                         ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
RAND, MALLOC = 0x5de668, 0x5de664
DEVICE, UPLOAD, QUAD_BUF, INTENSITY = 0x8c4b70, 0x8c4b8c, 0x8c4c5c, 0x62cc59


def fb(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


def bf(b):
    return struct.unpack('<f', struct.pack('<I', b & 0xffffffff))[0]


class PmCpu(Original):
    def __init__(self, seed):
        super().__init__(exe='pm')
        self.tr = lambda a, size=1: a
        self.fpu_control(0x007f)
        self.seed = seed
        self.stub_import(RAND, self.rand)

    def rand(self, c):  # MSVCRT's
        self.seed = (self.seed * 214013 + 2531011) & 0xffffffff
        return (self.seed >> 16) & 0x7fff

    def r32(self, a):
        return struct.unpack('<I', self.read(a, 4))[0]

    def floats(self, a, n):
        return list(struct.unpack(f'<{n}f', self.read(a, 4 * n)))


def exe_noise(seed):
    cpu = PmCpu(seed)
    cpu.write(DEVICE, struct.pack('<I', 1))
    obj, img = cpu.alloc(0x80), cpu.alloc(0x40)
    mallocs = []

    def malloc(c):
        a = cpu.alloc(max(c.arg(0), 4))
        mallocs.append(a)
        return a
    cpu.stub_import(MALLOC, malloc)
    cpu.stub_function(0x530f40, lambda c: img, 0)
    for a in (0x530fa0, 0x530fd0, 0x530fb0):
        cpu.stub_function(a, lambda c: 0, 0)
    cpu.stub_function(0x530f50, lambda c: 2, 0)  # two bytes a pixel
    codes = {(0x20, 0x40, 0x20): 0x1111, (0x40, 0x80, 0x40): 0x2222, (0x60, 0xc0, 0x60): 0x3333}
    cpu.stub_function(0x5983e0, lambda c: codes[(c.arg(0), c.arg(1), c.arg(2))], 0)
    up = cpu.code(b'\xc3')
    cpu.stub_function(up, lambda c: 0x77, 0)
    cpu.write(UPLOAD, struct.pack('<I', up))
    cpu.call(0x43dda0, ecx=obj)
    px = struct.unpack('<65536H', cpu.read(mallocs[0], 0x20000))
    back = {0x1111: 0, 0x2222: 1, 0x3333: 2}
    return bytes(back[p] for p in px), cpu.read(mallocs[1], 0x10000)


def exe_quad(seed, rect):
    cpu = PmCpu(seed)
    noise, r, buf = cpu.alloc(0x80), cpu.alloc(16), cpu.alloc(0x80)
    cpu.write(r, struct.pack('<4i', *rect))
    cpu.write(QUAD_BUF, struct.pack('<I', buf))
    cpu.stub_function(0x5c0320, lambda c: 1, 0)
    got = []
    cpu.stub_function(0x59df40, lambda c: got.append(cpu.read(buf, 0x80)) or 0, 0)
    cpu.call(0x43dfa0, ecx=noise, stack_args=(fb(0.5), r))
    return got[0]


def exe_frames(seed, channels, frames):
    cpu = PmCpu(seed)
    hud = cpu.alloc(0x23200)
    cpu.write(hud + 0x2310c, struct.pack('<12f', *channels))
    mech, health = cpu.alloc(0x700), cpu.alloc(4)
    vt = cpu.alloc(0x100)
    cpu.u32(vt + 0xa4, cpu.code(b'\xd9\x05' + struct.pack('<I', health) + b'\xc3'))  # fld dword [health]; ret
    cpu.u32(mech, vt)
    cpu.u32(hud + 0x10, mech)
    fade = []
    cpu.stub_function(0x43f190, lambda c: fade.append(c.arg(0)) or 0, 4)
    out = []
    for on_bits, h, dt in frames:
        cpu.write(mech + 0x654, bytes([on_bits & 1]))   # invincible
        cpu.write(INTENSITY, bytes([(on_bits >> 1) & 1]))
        cpu.write(health, struct.pack('<f', h))
        fade.clear()
        cpu.call(0x43e1f0, ecx=hud, stack_args=(fb(dt),))
        out.append(bf(fade[-1]) if fade else None)
    return cpu.floats(hud + 0x2310c, 12), out


def port_frames(seed, channels, frames):
    n = len(frames)
    c = (ctypes.c_float * 12)(*channels)
    on = (ctypes.c_int * n)(*[int(b == 0) for b, _, _ in frames])
    h = (ctypes.c_float * n)(*[x for _, x, _ in frames])
    dt = (ctypes.c_float * n)(*[x for _, _, x in frames])
    fade = (ctypes.c_float * n)()
    probe.probe_pm_static_frames(seed, 0, c, n, on, h, dt, fade)
    return list(c), list(fade)


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    r = random.Random(1999)
    for k in range(4):
        seed = r.getrandbits(32)
        pick, alpha = exe_noise(seed)
        pp, pa = (ctypes.c_uint8 * 65536)(), (ctypes.c_uint8 * 65536)()
        probe.probe_pm_static_noise(seed, pp, pa)
        if bytes(pp) != pick or bytes(pa) != alpha:
            i = next(i for i in range(65536) if pp[i] != pick[i] or pa[i] != alpha[i])
            print(f'noise seed {seed:#x}: pixel {i}: exe green {pick[i]} alpha {alpha[i]}, port {pp[i]} {pa[i]}')
            sys.exit(1)
    for k in range(runs * 5):
        seed = r.getrandbits(32)
        x, y = r.randint(0, 600), r.randint(0, 440)
        rect = (x, y, x + r.randint(1, 300), y + r.randint(1, 200))
        e = exe_quad(seed, rect)
        p = (ctypes.c_float * 32)()
        probe.probe_pm_static_quad(seed, *rect, p)
        if bytes(p) != e:
            print(f'quad seed {seed:#x} rect {rect}:\n  exe  {struct.unpack("<32f", e)}\n  port {list(p)}')
            sys.exit(1)
    frames_total = 0
    for k in range(runs):
        seed = r.getrandbits(32)
        ch = []
        for _ in range(4):
            ch += [r.choice([0.0, 0.0, r.uniform(0, 0.3)]), r.uniform(0, 1.2), r.uniform(0, 0.5)]
        frames = []
        for _ in range(r.randint(20, 120)):
            bits = 0 if r.random() < 0.85 else r.choice([1, 2, 3])
            frames.append((bits, r.choice([r.uniform(0, 0.25), r.uniform(0, 1)]), r.choice([1 / 30, 1 / 60, 0.0069, 0.1])))
        frames = [(b, bf(fb(h)), bf(fb(d))) for b, h, d in frames]
        ec, ef = exe_frames(seed, ch, frames)
        pc, pf = port_frames(seed, ch, frames)
        same = all(abs(a - b) <= 1e-6 for a, b in zip(ec, pc)) and all(
            (a is None and b == 0.0) or (a is not None and abs(a - b) <= 1e-6) for a, b in zip(ef, pf))
        if not same:
            print(f'frames run {k} seed {seed:#x}:\n  exe  {ec}\n  port {pc}')
            for i, (a, b) in enumerate(zip(ef, pf)):
                if not ((a is None and b == 0.0) or (a is not None and abs(a - b) <= 1e-6)):
                    print(f'  frame {i} {frames[i]}: fade exe {a} port {b}')
                    break
            sys.exit(1)
        frames_total += len(frames)
    print(f'ok: 4 noises (262144 pixels), {runs * 5} quads, {runs} runs of {frames_total} frames identical')


if __name__ == '__main__':
    main()
