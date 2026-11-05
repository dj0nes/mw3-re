"""Pirate's Moon's weapon list layout (HudWeapons::layout, src/mech3/hud.cpp on
the pm branch; docs/hud.md section 13) against PM's exe 0x447da0 in Unicorn: the
current group's bracket (2 lower than MW3's) and the panel's height (3 taller
when any weapon is listed).

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_layout.py [runs]

The list runs without its HUD here, so the equipment list's placing (the height
plus 6, unchanged from MW3) is not reached.
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))
LAYOUT = 0x447da0


def run_exe(counts, current, x, y):
    cpu = Original(exe='pm')
    cpu.tr = lambda a, size=1: a
    w32 = lambda a, v: cpu.write(a, struct.pack('<I', v & 0xffffffff))  # noqa: E731
    s32 = lambda v: struct.unpack('<i', struct.pack('<I', v))[0]  # noqa: E731
    R, WM, vt = cpu.alloc(0x13000), cpu.alloc(0x100), cpu.alloc(0x100)

    def slot(off, fn, pop):
        code = cpu.code(b'\xc3')
        cpu.stub_function(code, fn, pop)
        w32(vt + off, code)
    shown = []
    slot(0x68, lambda c: x, 0)
    slot(0x6c, lambda c: y, 0)
    slot(0x64, lambda c: 0, 0)
    slot(0x0c, lambda c: 0, 8)
    slot(0x60, lambda c: shown.append(c.ecx) or 0, 4)
    w32(R, vt)
    w32(R + 0x38, WM)
    w32(R + 0x3c, vt)  # the bracket (param_1[0xf])
    w32(R + 0x452c * 4, vt)  # the panel
    w32(WM + 0x2c, current)
    for i, n in enumerate(counts):
        if n >= 0:
            g = cpu.alloc(0x100)
            w32(g + 8, n)
            w32(WM + 0x30 + 4 * i, g)
    for i in range(16):
        w32(R + (i * 0x44e + 0x4c) * 4, vt)
    pts = {}
    cpu.stub_function(0x5c3fe0, lambda c: pts.__setitem__(c.arg(0), (s32(c.arg(1)), s32(c.arg(2)))) or 0, 12)
    cpu.stub_function(0x447840, lambda c: 0, 8)
    got = {}
    cpu.stub_function(0x43bb70, lambda c: got.__setitem__('h', s32(c.arg(1))) or 0, 8)
    cpu.call(LAYOUT, ecx=R)
    flat = []
    for i in range(4):
        flat += list(pts.get(i, (-1, -1)))
    return flat, got['h'], 1 if (R + 0x3c) in shown else 0


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    r = random.Random(1999)
    for k in range(runs):
        left, counts = 16, []
        for _ in range(5):
            n = r.choice([-1, 0, 0, r.randint(0, min(left, 6))])
            counts.append(n)
            left -= max(n, 0)
        current, x, y = r.choice([0, 1, 2, 3, 4, 7]), r.randint(0, 640), r.randint(0, 480)
        e = run_exe(counts, current, x, y)
        c = (ctypes.c_int32 * 5)(*counts)
        pts, h, sh = (ctypes.c_int32 * 8)(), ctypes.c_int32(), ctypes.c_int32()
        probe.probe_pm_layout(c, current, x, y, pts, ctypes.byref(h), ctypes.byref(sh))
        p = (list(pts), h.value, sh.value)
        if e != p:
            print(f'run {k}: counts {counts} current {current} at {x},{y}\n  exe  {e}\n  port {p}')
            sys.exit(1)
    print(f'ok: {runs} layouts: the bracket, its showing and the height identical')


if __name__ == '__main__':
    main()
