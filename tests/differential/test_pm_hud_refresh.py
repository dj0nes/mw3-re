"""Pirate's Moon's weapon line refresh (HudWeaponLine::refresh_line,
src/mech3/hud.cpp on the pm branch; docs/hud.md section 13) against PM's exe
0x4478f0 in Unicorn, at the game's 24-bit x87: the recharge bar (shown at 0.87
while recharging, PM's), the light (yellow also while recharging, PM's), the
count, the look and the recycling flag returned.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_refresh.py [runs]
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))


class RefreshIn(ctypes.Structure):
    _fields_ = [(n, ctypes.c_float) for n in ('recycle', 'damage', 'value')] + [
        (n, ctypes.c_int32) for n in ('enabled', 'ammo', 'count')]


class RefreshOut(ctypes.Structure):
    _fields_ = [('ret', ctypes.c_int32), ('value', ctypes.c_float)] + [
        (n, ctypes.c_int32) for n in ('x111b', 'light', 'count', 'look_r', 'look_g')]


probe.probe_pm_refresh.argtypes = [ctypes.POINTER(RefreshIn), ctypes.POINTER(RefreshOut)]
REFRESH, LIGHTS = 0x4478f0, 0x6296a4


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def run_exe(s):
    cpu = Original(exe='pm')
    cpu.tr = lambda a, size=1: a
    cpu.fpu_control(0x007f)
    w32 = lambda a, v: cpu.write(a, struct.pack('<I', v & 0xffffffff))  # noqa: E731
    L, W, lvt, wvt, tvt, vals = (cpu.alloc(n) for n in (0x1200, 0x100, 0x100, 0x100, 0x100, 16))
    cpu.write(vals, struct.pack('<2f', s.recycle, s.damage))
    out = {}

    def slot(vt, off, fn, pop):
        code = cpu.code(b'\xc3')
        cpu.stub_function(code, fn, pop)
        w32(vt + off, code)
    # The line: its vtable (set_value +0x8c records), the weapon at +0x1134.
    w32(L, lvt)
    slot(lvt, 0x8c, lambda c: out.__setitem__('value', struct.unpack('<f', struct.pack('<I', c.arg(0)))[0]) or 0, 4)
    slot(lvt, 0x64, lambda c: 0, 0)
    w32(L + 0x1134, W)
    cpu.write(L + 0x14c, struct.pack('<f', s.value))
    w32(L + 0x1128, s.count)
    w32(L + 0xaf8, tvt)  # the count's text (+0x2be * 4)
    slot(tvt, 0x7c, lambda c: out.__setitem__('text', c.arg(2)) or 0, 0)
    # The weapon: vt+8, its recycle (vt+0x40) and ammo (vt+0x20), enabled +0x78.
    w32(W, wvt)
    slot(wvt, 8, lambda c: 0, 0)
    w32(wvt + 0x40, cpu.code(b'\xd9\x05' + struct.pack('<I', vals) + b'\xc3'))
    slot(wvt, 0x20, lambda c: s.ammo, 0)
    cpu.write(W + 0x78, bytes([s.enabled]))
    cpu.write(0x4b6f60, b'\xd9\x05' + struct.pack('<I', vals + 4) + b'\xc3')  # the damage
    for i in range(3):
        w32(LIGHTS + 4 * i, 0x7a00 + i)
    images = []
    cpu.stub_function(0x5bba50, lambda c: images.append(c.arg(0)) or 0, 4)
    cpu.stub_function(0x447590, lambda c: out.__setitem__('look', (c.arg(0) & 0xff, c.arg(1) & 0xff)) or 0, 8)
    cpu.call(REFRESH, ecx=L, stack_args=(0,))
    ret = cpu._uc.reg_read(UC_X86_REG_EAX) & 0xff
    light = images[-1] - 0x7a00 if images and 0x7a00 <= images[-1] < 0x7a03 else -1
    count = struct.unpack('<i', cpu.read(L + 0x1128, 4))[0]
    look = out.get('look', (-1, -1))
    return dict(ret=1 if ret else 0, value=out.get('value'), x111b=cpu.read(L + 0x1133, 1)[0], light=light,
                count=count, look_r=1 if look[0] else 0, look_g=1 if look[1] else 0)


def run_port(s):
    o = RefreshOut()
    probe.probe_pm_refresh(ctypes.byref(s), ctypes.byref(o))
    return dict(ret=o.ret, value=o.value, x111b=o.x111b, light=o.light, count=o.count, look_r=o.look_r,
                look_g=o.look_g)


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    r = random.Random(1999)
    for k in range(runs):
        s = RefreshIn()
        s.recycle = f32(r.choice([1.0, 1.0, 0.0, 1.3, -0.2, r.random()]))
        s.damage = f32(r.choice([0.0, 0.3, 0.5, 0.6, 0.8, 0.9, 1.0, r.random()]))
        s.value = f32(r.choice([1.0, 0.5, 0.0]))
        s.enabled, s.ammo, s.count = int(r.random() < 0.85), r.choice([0, 3, 20, -1]), r.choice([0, 3, 20])
        e, p = run_exe(s), run_port(s)
        same = all(e[n] == p[n] for n in e if n != 'value') and abs(e['value'] - p['value']) <= 1e-6
        if not same:
            print(f'run {k}: ' + ', '.join(f'{n}={getattr(s, n)}' for n, _ in s._fields_))
            for n in e:
                if e[n] != p[n]:
                    print(f'  {n}: exe {e[n]} port {p[n]}')
            sys.exit(1)
    print(f'ok: {runs} refreshes: the bar, light, count, look and recycling flag identical')


if __name__ == '__main__':
    main()
