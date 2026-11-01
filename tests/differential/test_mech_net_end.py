"""The network game's end, 0x4707c0, against the original x86
(docs/multiplayer-port.md, piece 8): the local player's kills and deaths in
the lobby (a slot or none: -1) added to the game's totals, the pilot
written (0x478ce0: stood in for; no file open here), the fade to the
Multi-Exit State (0x48ef00 with 0x605fa8), then the network end (0x46d100,
stood in for on the exe's side; the port's runs, the game not running).
Compared: the lookups, the write, the fade's arguments, the totals.

    cmake --build build && .venv/bin/python tests/differential/test_mech_net_end.py

BREAK=1 hands the port the deaths as the kills; it must fail.
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from test_weiler import Emulated, probe  # noqa: E402

probe.probe_mech_net_end.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_mech_net_end.restype = ctypes.c_int32
PILOT = 0x60861c


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        stubs = {0x479480: (lambda c: self.game, 0), 0x586d10: (lambda c: self.v[0], 0),
                 0x435a30: (self.lookup(20, 4), 4), 0x435b40: (self.lookup(21, 5), 4),
                 0x478ce0: (self.save, 0), 0x48ef00: (self.fade, 0x18), 0x46d100: (lambda c: 0, 0)}
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)

    def lookup(self, code, at):
        def run(c):
            self.log += [code, s32(c.arg(0))]
            return self.v[at] & 0xffffffff
        return run

    def save(self, c):
        self.log.append(50)
        return 0

    def fade(self, c):
        self.log += [56, 1 if c.ecx == 0x605fa8 else 0, s32(c.edx)] + [s32(c.arg(i)) for i in range(6)]
        return 0

    def run(self, v):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.v, self.log = v, []
        self.game = emu.alloc(0x100) if v[1] else 0
        if self.game:
            cpu.u32(self.game + 0x98, v[2]), cpu.u32(self.game + 0x9c, v[3])
        pilot = emu.alloc(0x40)
        cpu.u32(pilot + 0x10, 0xffffffff)  # no file: the header and record are the pilot's own (not here)
        cpu.u32(PILOT, pilot)
        cpu.call(0x4707c0)
        g = self.game
        self.log += [90, s32(emu.u32(g + 0x98)) if g else v[2], s32(emu.u32(g + 0x9c)) if g else v[3]]
        return self.log


def main():
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(14)
    fails = cases = 0
    for n in range(500):
        v = [rng.randrange(1, 9), 1, rng.randrange(0, 50), rng.randrange(0, 50), rng.choice([-1, rng.randrange(0, 20)]),
             rng.choice([-1, rng.randrange(0, 20)])]
        theirs = exe.run(v)
        mine_v = list(v)
        if brk == 1:
            mine_v[4] = v[5]
        arr = (ctypes.c_int32 * 6)(*mine_v)
        out = (ctypes.c_int32 * 256)()
        k = probe.probe_mech_net_end(arr, out, 256)
        mine = list(out[:k])
        cases += 1
        if mine != theirs and not (brk == 1 and v[4] == v[5]):
            fails += 1
            if fails <= 2:
                print(f'FAIL case {n}:\n  port     {mine}\n  original {theirs}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} game ends identical')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
