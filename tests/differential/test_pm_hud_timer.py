"""Pirate's Moon's mission timer split (mech3::time_split, src/mech3/hud.cpp on
the pm branch; docs/hud.md section 7) against PM's exe 0x597c30 in Unicorn, at
the game's 24-bit x87: whole seconds as hours, minutes and seconds, the fix of
MW3's seconds past an hour (01:01:3640) that PM made itself.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_timer.py [runs]
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import FTOL, ROOT, Original  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))
probe.probe_pm_time_split.argtypes = [ctypes.c_float, ctypes.c_void_p]
SPLIT = 0x597c30


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    cpu = Original(exe='pm')
    cpu.tr = lambda a, size=1: a
    cpu.fpu_control(0x007f)
    cpu.import_code(0x5de644, FTOL)
    out = cpu.alloc(16)
    r = random.Random(1999)
    times = [0.0, 59.999, 60.0, 3599.5, 3600.0, 3700.25, 86399.9, 360000.0] + [
        r.choice([r.uniform(0, 120), r.uniform(0, 7200), r.uniform(0, 400000)]) for _ in range(runs)]
    for t in times:
        t = struct.unpack('<f', struct.pack('<f', t))[0]
        cpu.call(SPLIT, stack_args=(struct.unpack('<I', struct.pack('<f', t))[0], out, out + 4, out + 8))
        e = list(struct.unpack('<3i', cpu.read(out, 12)))
        p = (ctypes.c_int32 * 3)()
        probe.probe_pm_time_split(t, p)
        if list(p) != e:
            print(f'{t}: exe {e} port {list(p)}')
            sys.exit(1)
    print(f'ok: {len(times)} times split identically (hours, minutes, seconds)')


if __name__ == '__main__':
    main()
