"""The local player's multiplayer launch, 0x46da80, against the original x86
(docs/multiplayer-port.md, piece 5b): random local ids, rosters, skins,
teams, start slots (0..9: the spread 0x46e0a0 at each), the game's
unlimited ammo and outside view, a spawn point found or not, force
feedback or none, and a packed loadout of random length. The calls out are
stood in for and logged: the definition's load, the team, the ammo, the
player data with every byte of the record (0x46dd90's buffer), the scores,
the view, the lobby slot, the spawn point's name, the place, the force
feedback; then the answer, the team masks, the acknowledgement fields and
the state sends' globals. Compared with the port's net_launch_player.

    cmake --build build && .venv/bin/python tests/differential/test_mech_net_launch.py

BREAK=1 gives the port's spread another slot; it must fail.
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from test_weiler import Emulated, probe  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

probe.probe_mech_net_launch.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_uint32),
                                        ctypes.c_int32, ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_mech_net_launch.restype = ctypes.c_int32
FF, IAT_SPRINTF = 0x608620, 0x5bb658
X9C, XA0, XA4 = 0x605f9c, 0x605fa0, 0x605fa4


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


def bits(f):
    return struct.unpack('<i', struct.pack('<f', f))[0]


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_import(IAT_SPRINTF, self.sprintf)
        stubs = {0x479480: (lambda c: self.game, 0), 0x4846a0: (self.dfn, 0), 0x586d10: (lambda c: self.local, 0),
                 0x425650: (lambda c: self.blob, 0), 0x435910: (self.team, 4), 0x4b0220: (self.ammo, 4),
                 0x589830: (self.set_data, 4), 0x4361e0: (self.log_(5), 0), 0x40ae70: (self.view, 4),
                 0x495b10: (self.slot_of, 4), 0x4e0490: (lambda c: 0, 0), 0x4e0680: (self.find, 4),
                 0x4e04f0: (lambda c: 0, 0), 0x4338a0: (self.ff, 4)}
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)
        self.vt_slot = cpu.code(b'\x90' * 16)
        cpu.stub_function(self.vt_slot, lambda c: self.slot0, 4)
        self.vt_place = cpu.code(b'\x90' * 16)
        cpu.stub_function(self.vt_place, self.place, 8)

    def log_(self, code):
        def run(c):
            self.log.append(code)
            return 0
        return run

    def sprintf(self, c):
        fmt = self.cpu.read(c.arg(1), 16).split(b'\0')[0]
        assert fmt == b'remote_%d', fmt
        s = b'remote_%d' % s32(c.arg(2))
        self.cpu.write(c.arg(0), s + b'\0')
        return len(s)

    def dfn(self, c):
        self.log += [1, s32(c.ecx)]
        return 0

    def team(self, c):
        self.log += [2, c.arg(0)]
        return self.team_v

    def ammo(self, c):
        self.log += [3, c.arg(0) & 0xff]
        return 0

    def set_data(self, c):
        size = c.arg(0)
        self.log += [4, c.ecx, size] + list(struct.unpack(f'<{size // 4}i', c.read(c.edx, size // 4 * 4)))
        return 1

    def view(self, c):
        self.log += [6, c.arg(0)]
        return 0

    def slot_of(self, c):
        self.log += [7, c.arg(0)]
        return self.slot2

    def find(self, c):
        name = self.cpu.read(c.arg(0), 16).split(b'\0')[0].decode()
        self.log += [8, int(name[7:])]
        return self.spawn if self.found else 0

    def place(self, c):
        p = c.arg(0)
        self.log += [9] + list(struct.unpack('<3i', c.read(p, 12))) + [s32(c.arg(1))]
        return 0

    def ff(self, c):
        self.log += [10, c.arg(0)]
        return 0

    def run(self, v, words):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.log = []
        local, roster, skin, team, start, unlimited, outside, found = v[:8]
        spawn, ff = v[8:12], v[12]
        self.local, self.team_v, self.found = local, team, found
        self.blob = emu.put(struct.pack(f'<{len(words)}I', *words))
        self.game = emu.alloc(0x100)
        vt = emu.alloc(0x40)
        cpu.u32(vt + 0x34, self.vt_slot)
        cpu.u32(self.game, vt)
        cpu.write(self.game + 0x8d, bytes([unlimited, outside]))
        self.slot0 = emu.alloc(0x20)
        cpu.u32(self.slot0 + 0x10, skin)
        self.slot2 = emu.alloc(0x30)
        cpu.u32(self.slot2 + 0x20, start)
        self.spawn = emu.alloc(0x20)
        cpu.write(self.spawn + 8, struct.pack('<4i', *spawn))
        mvt = emu.alloc(0x100)
        cpu.u32(mvt + 0xdc, self.vt_place)
        m, data = emu.alloc(0x800), emu.alloc(0x20)
        cpu.u32(m, mvt)
        cpu.u32(m + 0x684, data)
        cpu.u32(data + 0xc, roster)
        cpu.u32(m + 0xd4, emu.alloc(16))
        cpu.write(m + 0x668, b'\0')
        cpu.u32(m + 0x66c, 5)
        pilot = emu.alloc(0x100)
        cpu.u32(pilot + 0x14, m)
        cpu.u32(FF, emu.alloc(16) if ff else 0)
        cpu.write(X9C, b'\x01')
        cpu.u32(XA0, bits(7.0))
        cpu.write(XA4, b'\x01')
        cpu.call(0x46da80, ecx=pilot)
        ok = cpu.uc.reg_read(UC_X86_REG_EAX) & 0xff
        u = emu.u32
        self.log += [11, ok, 12, s32(u(m + 0xc0)), s32(u(m + 0xc4)), s32(u(m + 0xc8)),
                     13, cpu.read(m + 0x668, 1)[0], s32(u(m + 0x66c)), 14, cpu.read(X9C, 1)[0], s32(u(XA0)),
                     cpu.read(XA4, 1)[0]]
        return self.log


def main():
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(3)
    fails = cases = 0
    for case in range(600):
        n = rng.randrange(0, 12)
        words = [0x34 + 0xc * n | rng.randrange(19) << 16] + [rng.getrandbits(32) for _ in range(12 + 3 * n)]
        start = rng.randrange(10)
        v = [rng.randrange(1, 9), rng.randrange(19), rng.randrange(5), rng.choice([0, 1, 2, 5, 33, 70, 1000]), start,
             rng.randrange(2), rng.randrange(2), int(rng.random() < 0.8)]
        v += [bits(rng.uniform(-900, 900)) for _ in range(3)] + [bits(rng.uniform(-3.2, 3.2))] + [rng.randrange(2)]
        theirs = exe.run(v, words)
        mine_v = list(v)
        if brk == 1:
            mine_v[4] = (start + 1) % 10
        arr = (ctypes.c_int32 * len(mine_v))(*mine_v)
        blob = (ctypes.c_uint32 * len(words))(*words)
        out = (ctypes.c_int32 * 4096)()
        k = probe.probe_mech_net_launch(arr, blob, len(words), out, 4096)
        mine = list(out[:k])
        cases += 1
        if mine != theirs:
            fails += 1
            if fails <= 2:
                i = next((j for j, (a, b) in enumerate(zip(mine, theirs)) if a != b), min(len(mine), len(theirs)))
                print(f'FAIL case {case} at {i}:\n  port     {mine[max(0, i - 8):i + 8]}\n'
                      f'  original {theirs[max(0, i - 8):i + 8]}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} launches identical')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
