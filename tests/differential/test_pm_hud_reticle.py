"""Pirate's Moon's reticle update (HudReticle::update, src/mech3/hud.cpp on the pm
branch; docs/hud.md section 12) against PM's exe 0x445a50 in Unicorn: over the
large radar map the zoom window (reset by 0x445110) and reticle hide; a hidden
reticle with its image kept shows again; the lock timer runs unless the HUD is
off and its mech down.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_reticle.py [runs]

The zoom window starts hidden on both sides and its own update (0x444680, unchanged)
is stubbed on the exe's, so its scale is compared only where this function resets it.
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))


class ReticleIn(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in ('map', 'zoomed', 'hidden', 'locked', 'dead', 'hud_active')] + [
        (n, ctypes.c_float) for n in ('timer', 'dt', 'scale')]


class ReticleOut(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in ('hidden', 'zoom_hidden')] + [
        (n, ctypes.c_float) for n in ('timer', 'scale')]


probe.probe_pm_reticle.argtypes = [ctypes.POINTER(ReticleIn), ctypes.POINTER(ReticleOut)]
UPDATE, HIDDEN = 0x445a50, 0x10


def run_exe(s):
    cpu = Original(exe='pm')
    cpu.tr = lambda a, size=1: a
    cpu.fpu_control(0x007f)
    r32 = lambda a: struct.unpack('<I', cpu.read(a, 4))[0]  # noqa: E731
    w32 = lambda a, v: cpu.write(a, struct.pack('<I', v & 0xffffffff))  # noqa: E731
    R, H, M, vt, mvt = cpu.alloc(0x1000), cpu.alloc(0x100), cpu.alloc(0x100), cpu.alloc(0x100), cpu.alloc(0x100)

    def slot(table, off, fn, pop):
        code = cpu.code(b'\xc3')
        cpu.stub_function(code, fn, pop)
        w32(table + off, code)
    slot(vt, 0x60, lambda c: w32(c.ecx + 0xc, r32(c.ecx + 0xc) & ~HIDDEN) or 0, 4)
    slot(vt, 0x64, lambda c: w32(c.ecx + 0xc, r32(c.ecx + 0xc) | HIDDEN) or 0, 0)
    slot(vt, 0x24, lambda c: 0, 4)
    Z = R + 0xbc
    w32(R, vt)
    w32(Z, vt)
    w32(R + 0xc, HIDDEN if s.hidden else 0)
    w32(Z + 0xc, HIDDEN)
    cpu.write(Z + 0x170, struct.pack('<f', s.scale))
    cpu.write(R + 0x914, bytes([s.locked]))
    cpu.write(R + 0x960, struct.pack('<f', s.timer))
    w32(R + 0x964, H)
    cpu.write(H + 0x70, bytes([s.map]))
    cpu.write(H + 0x6e, struct.pack('<H', s.zoomed))
    w32(H + 4, s.hud_active)
    w32(H + 0x10, M)
    w32(M, mvt)
    slot(mvt, 0xac, lambda c: s.dead, 0)
    cpu.stub_function(0x5c0220, lambda c: 0, 4)
    cpu.call(UPDATE, ecx=R, stack_args=(struct.unpack('<I', struct.pack('<f', s.dt))[0],))
    return dict(hidden=1 if r32(R + 0xc) & HIDDEN else 0, zoom_hidden=1 if r32(Z + 0xc) & HIDDEN else 0,
                timer=struct.unpack('<f', cpu.read(R + 0x960, 4))[0],
                scale=struct.unpack('<f', cpu.read(Z + 0x170, 4))[0])


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    r = random.Random(1999)
    f = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]  # noqa: E731
    for k in range(runs):
        s = ReticleIn()
        s.map, s.zoomed = r.choice([0, 1]), r.choice([0, 1])
        s.hidden, s.locked, s.dead, s.hud_active = (r.choice([0, 1]) for _ in range(4))
        s.timer, s.dt, s.scale = f(r.choice([0.0, r.uniform(0, 4)])), f(r.choice([1 / 30, 1 / 60, 0.1])), f(r.random())
        e = run_exe(s)
        o = ReticleOut()
        probe.probe_pm_reticle(ctypes.byref(s), ctypes.byref(o))
        large = s.map and s.zoomed == 1
        same = (e['hidden'], e['zoom_hidden']) == (o.hidden, o.zoom_hidden) and abs(e['timer'] - o.timer) <= 1e-6 and (
            not large or e['scale'] == o.scale)
        if not same:
            print(f'run {k}: ' + ', '.join(f'{n}={getattr(s, n)}' for n, _ in s._fields_))
            print(f'  exe {e}\n  port hidden {o.hidden} zoom {o.zoom_hidden} timer {o.timer} scale {o.scale}')
            sys.exit(1)
    print(f'ok: {runs} updates: hidden, the zoom window, its reset and the timer identical')


if __name__ == '__main__':
    main()
