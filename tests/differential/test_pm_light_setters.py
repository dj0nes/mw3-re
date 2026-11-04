"""PM's light setters (pm/gamez/zclass/light, Light.c) against PM's exe: 0x50efc0 orientation,
0x50e6f0 ambient, 0x50e7c0 diffuse, 0x50f600 diffuse colour, 0x50f760 ambient colour, 0x50e890 own
colours. Random lights by PM's offsets, flags with and without the static, own and 0xe0 bits;
every byte after the call, and whether the bake was marked stale (0x4ff750).

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_light_setters.py [cases]
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402

FUNCS = [0x50efc0, 0x50e6f0, 0x50e7c0, 0x50f600, 0x50f760, 0x50e890]
STALE, POINT_COLOUR, STAMP = 0x4ff750, 0x5898c0, 0x60d254
NAMES = ['orientation', 'ambient', 'diffuse', 'diffuse colour', 'ambient colour', 'own colours']


def main():
    cases = int(sys.argv[1]) if len(sys.argv) > 1 else 6000
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_light_setter.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    emu = g.Emulated()
    emu.cpu.fpu_control(0x7f)
    calls = {'stale': 0, 'colour': 0}
    emu.native_stub(STALE, lambda c: calls.__setitem__('stale', calls['stale'] + 1) or 0)
    emu.native_stub(POINT_COLOUR, lambda c: calls.__setitem__('colour', calls['colour'] + 1) or 0)
    node, data = emu.alloc(0xd4), emu.alloc(0x100)
    emu.w32(node + 0x38, data)
    rng = random.Random(0x50e6f0)
    stale_seen = 0
    for n in range(cases):
        fl = rng.choice((0, 0x100, 0x200, 0x300, 0x400, 0x500, 0x700, 0x120, 0x140)) | rng.choice((0, 1, 2, 0x10))
        floats = [rng.choice((1.0, rng.uniform(-2, 3))) for _ in range(0x100 // 4)]
        raw = bytearray(struct.pack(f'<{len(floats)}f', *floats))
        struct.pack_into('<I', raw, 0xe0, fl)
        if rng.random() < 0.5:
            struct.pack_into('<3f', raw, 0xa4, 1.0, 1.0, 1.0)
        which = rng.randrange(6)
        args = [rng.choice((1.0, rng.uniform(-2, 3))) for _ in range(3)]
        if which == 5:
            args[0] = float(rng.choice((0, 1, 2, 3)))
        emu.cpu._uc.mem_write(data, bytes(raw))
        calls['stale'] = 0
        stack = [node] + ([int(args[0])] if which == 5 else [struct.unpack('<I', struct.pack('<f', a))[0] for a in args])
        emu.call(FUNCS[which], args=stack)
        want = bytes(emu.cpu._uc.mem_read(data, 0x100))
        got = ctypes.create_string_buffer(bytes(raw), 0x100)
        r = p.probe_pm_light_setter(which, got, *args)
        if (r >> 8) != calls['stale']:
            raise SystemExit(f'case {n} {NAMES[which]} flags {fl:#x}: stale {r >> 8}, the exe {calls["stale"]}')
        r &= 0xff
        if want != got.raw or r != 0:
            at = next((i for i in range(0, 0x100, 4) if want[i:i + 4] != got.raw[i:i + 4]), -1)
            raise SystemExit(f'case {n} {NAMES[which]} flags {fl:#x} +{at:#x}: {got.raw[at:at + 4].hex()}, the exe {want[at:at + 4].hex()}; result {r}')
        stale_seen += calls['stale']
    print(f'ok: {cases} calls bit for bit; the bake marked stale {stale_seen} times in the exe ')


if __name__ == '__main__':
    main()
