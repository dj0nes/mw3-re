"""Pirate's Moon's Shift+R on the radar (HudRadar::toggle_map and select_map,
src/mech3/hud.cpp on the pm branch; docs/hud.md section 15, Pirate's Moon)
against PM's exe 0x43f090 with its map choice 0x43ee50, in Unicorn.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_toggle.py [runs]

Each run makes up the radar (on or off the map, small or zoomed, its sizes and
where it steps to), which of the "large", "small" and any maps exist, the map
image's height, the map chosen before and whether the reticle is hidden.
Compared: on or off after, the radius and its square, the place, the network
marker's text place and shown flag, the reticle shown, the changed flag and the
map kept ([0x0062dafc]).
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))
IN = ('active', 'zoomed', 'cx', 'cy', 'r', 'to_r', 'to_x', 'to_y', 'large', 'small', 'any', 'map_h',
      'reticle_hidden', 'map_before')
OUT = ('active', 'radius', 'radius_sq', 'x', 'y', 'text2_x', 'text2_y', 'text2_shown', 'reticle_shown', 'changed',
       'map')


class ToggleIn(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in IN]


class ToggleOut(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in OUT]


probe.probe_pm_radar_toggle.argtypes = [ctypes.POINTER(ToggleIn), ctypes.POINTER(ToggleOut)]
TOGGLE, MAP_KEPT, HIDDEN = 0x43f090, 0x62dafc, 0x10


def run_exe(s):
    cpu = Original(exe='pm')
    cpu.tr = lambda a, size=1: a
    r32 = lambda a: struct.unpack('<I', cpu.read(a, 4))[0]  # noqa: E731
    s32 = lambda a: struct.unpack('<i', cpu.read(a, 4))[0]  # noqa: E731
    w32 = lambda a, v: cpu.write(a, struct.pack('<I', v & 0xffffffff))  # noqa: E731

    def vtable():
        vt = cpu.alloc(0x100)

        def slot(off, fn, pop):
            code = cpu.code(b'\xc3')
            cpu.stub_function(code, fn, pop)
            w32(vt + off, code)

        def set_pos(c):
            w32(c.ecx + 0x14, c.arg(0))
            w32(c.ecx + 0x18, c.arg(1))
            return 0
        slot(0x0c, set_pos, 8)
        slot(0x60, lambda c: w32(c.ecx + 0xc, r32(c.ecx + 0xc) & ~HIDDEN) or 0, 4)
        slot(0x64, lambda c: w32(c.ecx + 0xc, r32(c.ecx + 0xc) | HIDDEN) or 0, 0)
        return vt
    vt = vtable()
    R, H = cpu.alloc(0xe00), cpu.alloc(0x23200)
    RET = H + 0x1464  # the reticle, in the HUD
    w32(R, vt)
    w32(R + 0x65c, vt)
    w32(R + 0x65c + 0xc, HIDDEN)
    w32(R + 0x65c + 0x14, -1)
    w32(R + 0x65c + 0x18, -1)
    w32(R + 0x14, 3)
    w32(R + 0x18, 4)
    w32(R + 0x34, 7)
    w32(R + 0x38, 49)
    cpu.write(R + 0x5a, struct.pack('<H', s.zoomed))
    cpu.write(R + 0x5c, bytes([s.active, 0]))
    for off, v in ((0x60, s.cx), (0x68, s.cy), (0x70, s.r), (0x78, s.to_r), (0x7c, s.to_x), (0x80, s.to_y)):
        w32(R + off, v)
    w32(R + 0xdcc, H)
    w32(RET, vt)
    w32(RET + 0xc, HIDDEN if s.reticle_hidden else 0)
    w32(MAP_KEPT, s.map_before)
    maps = {}
    for m in (1, 2, 3):
        obj, img = cpu.alloc(0x100), cpu.alloc(0x20)
        w32(obj + 0x3c, img)
        cpu.write(img + 6, struct.pack('<h', s.map_h))
        maps[m] = obj
    back = {v: k for k, v in maps.items()}

    def find(c):
        a = c.arg(0)
        name = b''
        while a and cpu.read(a, 1) != b'\0':
            name += cpu.read(a, 1)
            a += 1
        m = s.any if not c.arg(0) else (s.large if name == b'large' else s.small)
        return maps[m] if m else 0
    cpu.stub_function(0x459780, find, 0)  # cdecl
    cpu.stub_function(0x495070, lambda c: r32(MAP_KEPT), 0)
    w32(MAP_KEPT, maps[s.map_before] if s.map_before else 0)
    cpu.call(TOGGLE, ecx=R)
    kept = r32(MAP_KEPT)
    return dict(active=cpu.read(R + 0x5c, 1)[0], radius=s32(R + 0x34), radius_sq=s32(R + 0x38), x=s32(R + 0x14),
                y=s32(R + 0x18), text2_x=s32(R + 0x65c + 0x14), text2_y=s32(R + 0x65c + 0x18),
                text2_shown=0 if r32(R + 0x65c + 0xc) & HIDDEN else 1,
                reticle_shown=0 if r32(RET + 0xc) & HIDDEN else 1, changed=cpu.read(R + 0x5d, 1)[0],
                map=back.get(kept, 0))


def run_port(s):
    o = ToggleOut()
    probe.probe_pm_radar_toggle(ctypes.byref(s), ctypes.byref(o))
    return {n: getattr(o, n) for n in OUT}


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    r = random.Random(1999)
    for k in range(runs):
        s = ToggleIn()
        s.active, s.zoomed = r.choice([0, 1]), r.choice([0, 1])
        s.cx, s.cy, s.r = r.randint(40, 600), r.randint(40, 440), r.randint(20, 120)
        s.to_r, s.to_x, s.to_y = r.randint(0, 180), r.randint(0, 640), r.randint(0, 480)
        s.large, s.small, s.any = (r.choice([0, m, m]) for m in (1, 2, 3))
        s.map_h, s.reticle_hidden, s.map_before = r.randint(40, 300), r.choice([0, 1]), r.choice([0, 1, 2, 3])
        e, p = run_exe(s), run_port(s)
        if e != p:
            print(f'run {k}: ' + ', '.join(f'{n}={getattr(s, n)}' for n in IN))
            for n in OUT:
                if e[n] != p[n]:
                    print(f'  {n}: exe {e[n]} port {p[n]}')
            sys.exit(1)
    print(f'ok: {runs} toggles: on or off, size, place, marker text, reticle, changed and map identical')


if __name__ == '__main__':
    main()
