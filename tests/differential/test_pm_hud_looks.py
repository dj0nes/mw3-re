"""Pirate's Moon's weapon list looks (HudWeapons::looks, src/mech3/hud.cpp on
the pm branch; docs/hud.md section 13) against PM's exe 0x4480c0 in Unicorn:
each shown line's mark (select, chain or none) and the next weapon, which PM
picks as the first after the current that can fire, else the first that can.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_looks.py [runs]

Each run makes up 16 lines (a weapon or none, hidden or not, its group, can it
fire), the current group (or none), the fire mode and the current weapon. The
line refresh (0x4478f0) is stubbed on the exe's side and records its mark.
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))


class LooksLine(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in ('weapon', 'hidden', 'group', 'ready')]


probe.probe_pm_looks.argtypes = [ctypes.c_void_p] + [ctypes.c_int32] * 4 + [ctypes.c_void_p, ctypes.c_void_p]
LOOKS, HIDDEN = 0x4480c0, 0x10
LINE0, STRIDE = 0x1264, 0x44e * 4  # the first line's weapon field, the lines' spacing


def run_exe(ls, gi, mode, current, has_group):
    cpu = Original(exe='pm')
    cpu.tr = lambda a, size=1: a
    w32 = lambda a, v: cpu.write(a, struct.pack('<I', v & 0xffffffff))  # noqa: E731
    R, WM, G = cpu.alloc(0x13000), cpu.alloc(0x100), cpu.alloc(0x40)
    SEL, CHN = 0x7701, 0x7702
    w32(R + 0x38, WM)
    w32(R + 0x128, SEL)
    w32(R + 300, CHN)
    w32(WM + 0x2c, gi)
    w32(WM + 0x1c, mode)
    if gi != -1:
        w32(WM + 0x30 + 4 * gi, G if has_group else 0)
    objs, by_obj = {}, {}
    for i, l in enumerate(ls):
        base = R + LINE0 + i * STRIDE
        w32(base - 0x44a * 4, HIDDEN if l[1] else 0)
        if l[0]:
            o = objs.get(l[0]) or cpu.alloc(0x100)
            objs[l[0]] = o
            by_obj[o] = l
            w32(o + 100, l[2])
            w32(base, o)
        else:
            w32(base, 0)
    cpu.stub_function(0x4b7bd0, lambda c: by_obj[c.ecx][3], 0)
    cpu.stub_function(0x4b4370, lambda c: objs.get(current, 0), 0)
    marks = {}
    cpu.stub_function(0x4478f0, lambda c: marks.__setitem__((c.ecx - (R + 0x130)) // STRIDE, c.arg(0)) or 0, 4)
    cpu.call(LOOKS, ecx=R)
    nxt = struct.unpack('<I', cpu.read(R + 0x115bc, 4))[0]
    back = {o: k for k, o in objs.items()}
    out = []
    for i, l in enumerate(ls):
        if l[1] or not l[0]:
            out.append(-1)
        else:
            m = marks.get(i)
            out.append({SEL: 1, CHN: 2, 0: 0}.get(m, 9))
    return back.get(nxt, 0), out


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    r = random.Random(1999)
    for k in range(runs):
        ids = list(range(1, 17))
        ls = []
        for i in range(16):
            w = ids[i] if r.random() < 0.8 else 0
            ls.append((w, int(r.random() < 0.15), r.randrange(5), int(r.random() < 0.6)))
        gi = r.choice([-1, 0, 1, 2, 3, 4])
        mode = r.choice([0, 1, 2])
        current = r.choice([0] + [l[0] for l in ls if l[0]])
        has_group = int(r.random() < 0.9)
        e = run_exe(ls, gi, mode, current, has_group)
        arr = (LooksLine * 16)(*[LooksLine(*l) for l in ls])
        nxt, marks = ctypes.c_int32(), (ctypes.c_int32 * 16)()
        probe.probe_pm_looks(arr, gi, mode, current, has_group, ctypes.byref(nxt), marks)
        p = (nxt.value, list(marks))
        if e != p:
            print(f'run {k}: lines {ls} group {gi} mode {mode} current {current} has_group {has_group}')
            print(f'  exe  next {e[0]} marks {e[1]}\n  port next {p[0]} marks {p[1]}')
            sys.exit(1)
    print(f'ok: {runs} weapon lists: every mark and the next weapon identical')


if __name__ == '__main__':
    main()
