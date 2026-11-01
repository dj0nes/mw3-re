"""The network mech's creation from a player's record, 0x46d450 with the
record's read 0x46d820, against the original x86 (docs/multiplayer-port.md,
piece 5c): random senders (with a name or none), records asked for at
sizes under and over 8 bytes, got or not, of kind 2 or another, with any
roster and skin; the definition found or not, with 0..4 skins; the loadout
unpacked or not (0x4259c0 stood in for: test_config_unpack.py covers it),
valid or not; the type's data loaded or not; host or not. Logged as the
calls go: the definition, the team, the unpack, Validate, the config kept
or freed, the type's data, the mech made (its owner, loadout and name), the
skin, the sensor range, the host's replay; after: the configs' names, the
missing list, the team masks, the remote mechs' count. Compared with the
port's net_mech_create.

    cmake --build build && .venv/bin/python tests/differential/test_mech_net_create.py

BREAK=1 gives the port's record another kind; it must fail.
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

probe.probe_mech_net_create.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_uint32),
                                        ctypes.c_int32, ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_mech_net_create.restype = ctypes.c_int32
NEW, DELETE, IAT_STRDUP = 0x5abd26, 0x5abd1a, 0x5bb670
CSTR_CTOR, CSTR_FORMAT, CSTR_DTOR = 0x5abdb0, 0x5abdaa, 0x5abda4
MECHS, MECHS_N, CONFIGS, MISSING = 0x605f84, 0x605f88, 0x605f74, 0x605f94


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_function(NEW, lambda c: self.emu.alloc(c.arg(0)))
        cpu.stub_function(DELETE, lambda c: 0)
        cpu.stub_import(IAT_STRDUP, self.strdup)
        stubs = {0x588a60: (self.name, 0), 0x589750: (lambda c: self.v[2], 0), 0x589770: (self.get, 4),
                 0x4846a0: (self.dfn, 0), 0x479480: (lambda c: self.game, 0), 0x435910: (self.team, 4),
                 CSTR_CTOR: (self.cstr_ctor, 0), CSTR_FORMAT: (self.cstr_format, 0), CSTR_DTOR: (lambda c: 0, 0),
                 0x4259c0: (self.unpack, 0), 0x4254d0: (self.valid, 4), 0x49da10: (self.keep, 12),
                 0x45b800: (self.missing, 12), 0x424350: (self.config_free, 0), 0x4aec00: (lambda c: 0, 0),
                 0x4265a0: (lambda c: 0, 8), 0x430d10: (self.data, 0), 0x46e280: (self.make, 0x14),
                 0x414390: (self.skin, 4), 0x4d6d20: (self.sensor, 4), 0x586d20: (lambda c: self.v[9], 0),
                 0x4700a0: (self.log_(28), 0), 0x500c60: (self.replay, 0)}
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)

    def log_(self, code):
        def run(c):
            self.log.append(code)
            return 0
        return run

    def cstr(self, p):
        return self.cpu.read(p, 0x40).split(b'\0')[0].decode() if p else None

    def number(self, s):
        if s is None:
            return -1
        return -2 if s.startswith('(null)') else int(s[1:].split("'")[0])

    def strdup(self, c):
        s = self.cstr(c.arg(0))
        return self.emu.put(s.encode() + b'\0')

    def name(self, c):
        return self.name_at if self.v[1] and c.ecx == self.v[0] else 0

    def get(self, c):
        if not self.v[3]:
            return 0
        n = self.emu.u32(c.arg(0))
        # past a record under 12 bytes the exe reads the heap; the port zeros (FIX (bug))
        self.cpu.write(c.edx, self.rec[:n] + bytes(max(0, 0xc - n)))
        return 1

    def dfn(self, c):
        self.log += [1, s32(c.ecx)]
        return self.dfn_at if self.v[4] else 0

    def team(self, c):
        self.log += [2, c.arg(0)]
        return self.v[11]

    def cstr_ctor(self, c):
        self.cpu.u32(c.ecx, self.emu.put(b'\0'))
        return c.ecx

    def cstr_format(self, c):
        fmt = self.cstr(c.arg(1))
        assert fmt == "%s's Config", fmt
        arg = self.cstr(c.arg(2))
        self.cpu.u32(c.arg(0), self.emu.put(f"{'(null)' if arg is None else arg}'s Config".encode() + b'\0'))
        return 0

    def unpack(self, c):
        if self.v[2] < 0xc + 0x34:  # the port's FIX (bug): no loadout from a record too short for one
            return 0
        self.log += [30, s32(self.emu.u32(c.ecx))]
        if not self.v[6]:
            return 0
        self.cfg = self.emu.alloc(0xd8)
        self.cpu.u32(self.cfg + 0x18, self.emu.put(b'remote config\0'))
        return self.cfg

    def valid(self, c):
        self.log.append(31)
        return self.v[7]

    def keep(self, c):
        self.kept.append(self.emu.u32(c.arg(2)))
        return 0

    def missing(self, c):
        self.missed.append(self.emu.u32(c.arg(2)))  # the player's id
        return 0

    def config_free(self, c):
        self.log += [22, 1 if c.ecx == self.cfg else 0]
        return 0

    def data(self, c):
        self.log += [24, s32(c.ecx), c.edx]
        return 0x200 if self.v[8] else 0

    def make(self, c):
        m = c.ecx
        self.log += [25, c.arg(0), 1 if c.arg(3) == self.cfg else 0, self.number(self.cstr(c.arg(4)))]
        self.cpu.u32(m + 0x6fc, c.arg(0))
        self.cpu.u32(m + 0x6f8, 7)
        self.made.append(m)
        return m

    def skin(self, c):
        self.log += [26, c.arg(0)]
        return 1

    def sensor(self, c):
        self.log.append(27)
        return 0

    def replay(self, c):
        self.log += [29, 1 if c.ecx == 0x4700e0 else 0]
        return 0

    def run(self, v, words):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.v, self.log, self.kept, self.missed, self.made, self.cfg = v, [], [], [], [], None
        rec = struct.pack(f'<{len(words)}I', *words)
        self.rec = (rec + bytes(max(0, v[2] - len(rec))))[:v[2]]
        self.name_at = emu.put(f'P{v[0]}'.encode() + b'\0')
        self.game = emu.alloc(0x100)
        self.dfn_at = emu.alloc(0x40)
        skins = emu.alloc(0x14)
        arr = emu.alloc(4 * max(v[5], 1))
        for i in range(max(v[5], 0)):
            cpu.u32(arr + 4 * i, 0x100 + i)
        cpu.u32(skins + 4, arr)
        cpu.u32(skins + 8, arr + 4 * max(v[5], 0))
        cpu.u32(self.dfn_at + 0x1c, skins)
        head = emu.alloc(12)
        cpu.u32(head, head)
        cpu.u32(head + 4, head)
        cpu.u32(MECHS, head)
        cpu.u32(MECHS_N, 0)
        cpu.call(0x46d450, ecx=v[0], edx=emu.alloc(0x40))
        for cfg in self.kept:
            self.log += [23, self.number(self.cstr(emu.u32(cfg + 0x18)))]
        for p in self.missed:
            self.log += [21, p]
        for m in self.made:
            u = emu.u32
            self.log += [12, s32(u(m + 0xc0)), s32(u(m + 0xc4)), s32(u(m + 0xc8)), s32(u(m + 0x6f8))]
        self.log += [99, len(self.made)]
        return self.log


def main():
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(9)
    fails = cases = made = 0
    for case in range(800):
        n = rng.randrange(0, 6)
        size = 0xc + 0x34 + 0xc * n
        kind = 2 if rng.random() < 0.85 else rng.choice([0, 1, 3])
        words = [kind, rng.randrange(-1, 6) & 0xffffffff, rng.choice([0, 3, 7, 18, 0xffffffff])]
        words += [0x34 + 0xc * n | rng.randrange(19) << 16] + [rng.getrandbits(32) for _ in range(12 + 3 * n)]
        asked = rng.choice([size, size, size, size, 4, 7, 8])
        if asked == 8:
            asked = size if rng.random() < 0.5 else 8
        v = [rng.randrange(1, 8), int(rng.random() < 0.85), asked, int(rng.random() < 0.9), int(rng.random() < 0.9),
             rng.randrange(-1, 5), int(rng.random() < 0.9), int(rng.random() < 0.8), int(rng.random() < 0.9),
             rng.randrange(2), 0, rng.choice([0, 1, 3, 33, 70])]
        if asked == 8:  # too short for a loadout: the port gives none (FIX (bug)); the exe would read past
            v[6] = 0
        v += [0] * (16 - len(v))
        theirs = exe.run(v, words)
        made += theirs[-1]
        mine_words = list(words)
        if brk == 1:
            mine_words[0] = 3
        arr = (ctypes.c_int32 * len(v))(*v)
        blob = (ctypes.c_uint32 * len(mine_words))(*mine_words)
        out = (ctypes.c_int32 * 4096)()
        k = probe.probe_mech_net_create(arr, blob, len(mine_words), out, 4096)
        mine = list(out[:k])
        cases += 1
        if mine != theirs:
            fails += 1
            if fails <= 2:
                i = next((j for j, (a, b) in enumerate(zip(mine, theirs)) if a != b), min(len(mine), len(theirs)))
                print(f'FAIL case {case} {v[:12]} at {i}:\n  port     {mine}\n  original {theirs}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} creations identical ({made} mechs made)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
