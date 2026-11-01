"""The pilot's respawn in a network game, 0x46dde0, against the original x86
(docs/multiplayer-port.md, piece 8d): respawn on or off (the game's +0x8c;
off, the view's camera 4 and +0x8e cleared), unlimited ammo (+0x8d), random
local ids, teams, lobby slots (the spread 0x46e0a0 at each), rosters, a
cockpit explosion or none, force feedback or none, 0..3 remote mechs and
16 starts each found or not, at random places: the start farthest from the
nearest remote mech (0x46e030, run as the exe's) is taken. The calls out
are stood in for and logged: the mech change, the team, the ammo, the
options, the explosion's restart, the lobby slot, each start's name, the
place, the sensor range, the objectives, the force feedback; the 0x12 sent
by the exe's own sender 0x46f160; then the answer, the team masks and the
game's +0x8e. Compared with the port's net_pilot_respawn.

    cmake --build build && .venv/bin/python tests/differential/test_mech_net_respawn.py

BREAK=1 hides the first start found from the port; it must fail.
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

probe.probe_mech_net_respawn.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_mech_net_respawn.restype = ctypes.c_int32
FF, VIEW, IAT_SPRINTF = 0x608620, 0x5f4c8c, 0x5bb658
RUNNING, MECHS, MECHS_N, PILOT = 0x605fb4, 0x605f84, 0x605f88, 0x60861c
N_IN = 22 + 16 * 5


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


def bits(f):
    return struct.unpack('<i', struct.pack('<f', f))[0]


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_import(IAT_SPRINTF, self.sprintf)
        stubs = {0x479480: (lambda c: self.game, 0), 0x40ae70: (self.view, 4), 0x4783c0: (self.set_mech, 4),
                 0x586d10: (lambda c: self.v[3], 0), 0x435910: (self.team, 4), 0x4b0220: (self.ammo, 4),
                 0x40cb30: (self.log_(13), 0), 0x508460: (self.restart, 0), 0x495b10: (self.slot_of, 4),
                 0x5a85e0: (self.position, 0), 0x4e0490: (lambda c: 0, 0), 0x4e0680: (self.find, 4),
                 0x4e04f0: (lambda c: 0, 0), 0x4d6d20: (self.log_(17), 4), 0x491050: (self.log_(18), 0),
                 0x4338d0: (self.ff(19), 4), 0x4338a0: (self.ff(20), 4), 0x5888a0: (self.send, 8),
                 0x586d20: (lambda c: 0, 0)}
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)
        self.vt_place = cpu.code(b'\x90' * 16)
        cpu.stub_function(self.vt_place, self.place, 8)

    def log_(self, code):
        def run(c):
            self.log.append(code)
            return 0
        return run

    def ff(self, code):
        def run(c):
            self.log += [code, s32(c.arg(0))]
            return 0
        return run

    def sprintf(self, c):
        fmt = self.cpu.read(c.arg(1), 16).split(b'\0')[0]
        assert fmt == b'remote_%d', fmt
        s = b'remote_%d' % s32(c.arg(2))
        self.cpu.write(c.arg(0), s + b'\0')
        return len(s)

    def view(self, c):
        self.log += [10, s32(c.arg(0))]
        return 0

    def set_mech(self, c):
        self.log += [11, s32(c.arg(0))]
        return 0

    def team(self, c):
        self.log += [2, s32(c.arg(0))]
        return self.v[4]

    def ammo(self, c):
        self.log += [3, c.arg(0) & 0xff]
        return 0

    def restart(self, c):
        self.log += [14, s32(c.ecx), s32(c.edx)]
        return 0

    def slot_of(self, c):
        self.log += [7, s32(c.arg(0))]
        return self.slot_rec

    def position(self, c):
        return self.pos_of.get(c.ecx, self.pos_of[self.mech])

    def find(self, c):
        name = self.cpu.read(c.arg(0), 16).split(b'\0')[0].decode()
        i = int(name[7:])
        self.log += [8, i]
        return self.starts[i]

    def place(self, c):
        p = c.arg(0)
        self.log += [9] + list(struct.unpack('<3i', c.read(p, 12))) + [s32(c.arg(1))]
        return 0

    def send(self, c):
        n = c.edx
        data = self.cpu.read(c.ecx, n) + bytes(4)
        self.log += [1, s32(c.arg(1)), 1 if c.arg(0) else 0, n]
        self.log += [struct.unpack('<i', data[k:k + 4])[0] for k in range(0, n, 4)]
        return 101

    def run(self, v):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.v, self.log = v, []
        u = cpu.u32
        self.game = emu.alloc(0x100)
        cpu.write(self.game + 0x8c, bytes([v[0], v[1], v[2], 0]))
        self.slot_rec = emu.alloc(0x40)
        u(self.slot_rec + 0x20, v[5])
        mvt = emu.alloc(0x200)
        u(mvt + 0xdc, self.vt_place)
        u(mvt + 0x168, 0x46f160)  # the plain mech's sender
        m = self.mech = emu.alloc(0x800)
        u(m, mvt)
        data = emu.alloc(0x240)
        u(m + 0x684, data)
        u(data + 0xc, v[6])
        if v[7]:
            u(data + 0x200, 0x5150)
            ck = emu.alloc(0x10)
            u(ck + 4, 0x6160)
            u(data + 0x228, ck)
        u(m + 0xd4, emu.alloc(0x100))
        pilot = emu.alloc(0x100)
        u(pilot + 0x14, m)
        u(PILOT, pilot)
        u(VIEW, emu.alloc(0x100))
        u(FF, emu.alloc(16) if v[8] else 0)
        cpu.write(RUNNING, b'\x01')
        self.pos_of = {m: emu.put(struct.pack('<3i', *v[19:22]))}
        # The remote mechs' list (an MSVC std::list: the head node's next,
        # prev; each node's +8 the mech).
        head = emu.alloc(0x10)
        nodes = []
        for k in range(v[9]):
            o = emu.alloc(0x800)
            self.pos_of[o] = emu.put(struct.pack('<3i', *v[10 + 3 * k:13 + 3 * k]))
            n = emu.alloc(0x10)
            u(n + 8, o)
            nodes.append(n)
        ring = [head] + nodes
        for i, n in enumerate(ring):
            u(n, ring[(i + 1) % len(ring)])
            u(n + 4, ring[i - 1])
        u(MECHS, head)
        u(MECHS_N, v[9])
        self.starts = []
        for i in range(16):
            s = v[22 + 5 * i:27 + 5 * i]
            if s[0]:
                r = emu.alloc(0x40)
                cpu.write(r + 8, struct.pack('<4i', *s[1:5]))
                self.starts.append(r)
            else:
                self.starts.append(0)
        cpu.call(0x46dde0, ecx=pilot)
        ok = cpu.uc.reg_read(UC_X86_REG_EAX) & 0xff
        self.log += [21, ok, 12, s32(emu.u32(m + 0xc0)), s32(emu.u32(m + 0xc4)), s32(emu.u32(m + 0xc8)),
                     22, cpu.read(self.game + 0x8e, 1)[0]]
        return self.log


def case(rng):
    v = [0] * N_IN
    v[0] = int(rng.random() < 0.8)
    v[1], v[2] = rng.randrange(2), rng.randrange(2)
    v[3] = rng.randrange(1, 9)
    v[4] = rng.choice([0, 1, 2, 5, 33, 70, 1000])
    v[5] = rng.randrange(10)
    v[6] = rng.randrange(19)
    v[7], v[8] = rng.randrange(2), rng.randrange(2)
    v[9] = rng.randrange(4)
    for k in range(10, 22):
        v[k] = bits(rng.uniform(-900, 900))
    for i in range(16):
        s = 22 + 5 * i
        v[s] = int(rng.random() < 0.6)
        v[s + 1:s + 4] = [bits(rng.uniform(-900, 900)) for _ in range(3)]
        v[s + 4] = bits(rng.uniform(-3.2, 3.2))
    return v


def main():
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(8)
    fails = cases = off = 0
    for n in range(1500):
        v = case(rng)
        theirs = exe.run(v)
        mine_v = list(v)
        if brk == 1 and v[9]:  # the starts but the nearest found: the port's farthest is another
            found = [i for i in range(16) if v[22 + 5 * i]]
            if len(found) > 1:
                mine_v[22 + 5 * found[0]] = 0
        arr = (ctypes.c_int32 * N_IN)(*mine_v)
        out = (ctypes.c_int32 * 8192)()
        k = probe.probe_mech_net_respawn(arr, out, 8192)
        mine = list(out[:k])
        cases += 1
        off += 1 - v[0]
        if mine != theirs:
            fails += 1
            if fails <= 2:
                i = next((j for j, (a, b) in enumerate(zip(mine, theirs)) if a != b), min(len(mine), len(theirs)))
                print(f'FAIL case {n} at {i}:\n  port     {mine[max(0, i - 8):i + 8]}\n'
                      f'  original {theirs[max(0, i - 8):i + 8]}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} respawns identical ({off} with respawn off)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
