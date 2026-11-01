"""The world's events against the original x86 (docs/multiplayer-port.md,
piece 7b): the crater veto 0x46fd30 (the pilot's shot or another's, made
here or from a message, host or not), a crater in 0x46fde0 (the host's make
and resend with bit 7, the others' bit-7 ones), an effect sent 0x46feb0
(the field base stations' left out, none while one heard replays) and in
0x46ff50 (replayed unless one like it is logged), a drop point sent
0x46ff90 and in 0x46ffd0, the clock in 0x470740 (the end's reason, its
line, the game ended 3 s on) and the host's replay of a crater 0x4700e0.
The crater's make, the materials by index, the anim log's lookup and
replay and the station's repair are stood in for on both sides. Compared:
every call and send, the veto's answer and the order's radius after, the
game's end state and time, the echo flag.

    cmake --build build && .venv/bin/python tests/differential/test_mech_net_world.py

BREAK=1 gives the port's crater message a host bit it did not have; it must fail.
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

probe.probe_mech_net_world.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_mech_net_world.restype = ctypes.c_int32
probe.probe_mech_net_wd_count.restype = ctypes.c_int32

NAMES = ['kind', 'host', 'local', 'len', ('msg', 24), ('order', 10), ('defaults', 10), 'pilot_shot', 'made', 'has',
         'has_game', 'end_state', 'end_time', 'clock']
K, _i = {}, 0
for n in NAMES:
    name, size = (n, 1) if isinstance(n, str) else n
    K[name], _i = _i, _i + size
COUNT = _i
PILOT, TIME, LEFT, ECHO, DEFAULTS, IAT_STRSTR = 0x60861c, 0x60991c, 0x609920, 0x605fb8, 0x60bfc0, 0x5bb5f8


def bits(f):
    return struct.unpack('<i', struct.pack('<f', f))[0]


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_import(IAT_STRSTR, self.strstr)
        stubs = {0x40ef10: (lambda c: self.pilot if self.v[K['pilot_shot']] else self.other, 0),
                 0x535ff0: (lambda c: c.ecx - 0x1000, 0), 0x586d20: (lambda c: self.v[K['host']], 0),
                 0x586d10: (lambda c: self.v[K['local']], 0), 0x536070: (self.take, 0), 0x4ffaa0: (self.make, 0),
                 0x5888a0: (self.send, 8), 0x50b680: (self.has, 0), 0x50ca90: (self.replay, 0),
                 0x45a140: (self.station, 0), 0x479480: (lambda c: self.game, 0), 0x40fe70: (self.hud, 0)}
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)

    def strstr(self, c):
        hay = self.cpu.read(c.arg(0), 0x20).split(b'\0')[0]
        needle = self.cpu.read(c.arg(1), 0x20).split(b'\0')[0]
        i = hay.find(needle)
        return c.arg(0) + i if i >= 0 else 0

    def take(self, c):
        self.log += [40, s32(c.ecx)]
        return 0x1000 + c.ecx

    def make(self, c):
        o, r = c.ecx, lambda a: s32(self.emu.u32(a))  # noqa: E731
        self.log += [41, r(o + 4), r(o + 8), r(o + 0x10), r(o + 0x14), r(o + 0x18), r(o + 0x1c), r(o + 0x20)]
        return self.v[K['made']]

    def send(self, c):
        n = c.edx
        data = self.cpu.read(c.ecx, n) + bytes(4)
        self.log += [1, s32(c.arg(1)), 1 if c.arg(0) else 0, n]
        self.log += [struct.unpack('<i', data[k:k + 4])[0] for k in range(0, n, 4)]
        return 101

    def has(self, c):
        self.log.append(44)
        return self.v[K['has']]

    def replay(self, c):
        self.log += [43, self.cpu.read(ECHO, 1)[0]] + list(struct.unpack('<20i', c.read(c.ecx, 0x50)))
        return 0

    def station(self, c):
        self.log += [45, s32(self.emu.u32(c.ecx + 0x6fc)), s32(c.edx)]
        return 0

    def hud(self, c):
        self.log += [2, s32(c.arg(0)), s32(c.arg(1))]
        return 0

    def run(self, v):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.v, self.log = v, []
        u = cpu.u32
        self.pilot, self.other = emu.alloc(0x800), emu.alloc(0x800)
        pl = emu.alloc(0x40)
        u(pl + 0x14, self.pilot)
        u(PILOT, pl)
        cpu.write(DEFAULTS, struct.pack('<10i', *v[K['defaults']:K['defaults'] + 10]))
        cpu.write(ECHO, b'\0')
        u(TIME, bits(120.25))
        u(LEFT, v[K['clock']])
        self.game = 0
        if v[K['has_game']]:
            g = self.game = emu.alloc(0x100)
            u(g + 0x84, v[K['end_state']]), u(g + 0x80, v[K['end_time']])
        msg = emu.put(struct.pack('<24i', *v[K['msg']:K['msg'] + 24])[:v[K['len']]] + bytes(0x60))
        kind = v[K['kind']]
        if kind == 0:
            o = emu.put(struct.pack('<10i', *v[K['order']:K['order'] + 10]))
            u(o + 0x24, 0x77)
            cpu.call(0x46fd30, ecx=o)
            self.log += [47, s32(cpu.uc.reg_read(UC_X86_REG_EAX)), s32(emu.u32(o + 0x14))]
        elif kind == 1:
            cpu.call(0x46fde0, ecx=msg)
        elif kind == 2:
            cpu.call(0x46feb0, ecx=msg)
        elif kind == 3:
            cpu.call(0x46ff50, ecx=self.other, stack_args=(msg,))
        elif kind == 4:
            cpu.call(0x46ff90, ecx=v[K['order']])
        elif kind == 5:
            who = emu.alloc(0x800)
            u(who + 0x6fc, 5)
            cpu.call(0x46ffd0, ecx=who, stack_args=(who, msg))
        elif kind == 6:
            cpu.call(0x470740, ecx=msg)
            self.log.insert(0, s32(emu.u32(LEFT)))
            self.log.insert(0, 46)
        else:  # the exe's history keeps the material's pointer, the port's its index (FIX (A))
            words = list(v[K['order']:K['order'] + 10])
            words[2] += 0x1000
            w = emu.put(struct.pack('<10i', *words))
            cpu.call(0x4700e0, ecx=w)
        g = self.game
        self.log += [90, s32(emu.u32(g + 0x84)) if g else v[K['end_state']], s32(emu.u32(g + 0x80)) if g else v[K['end_time']],
                     cpu.read(ECHO, 1)[0]]
        return self.log


def case(rng):
    v = [0] * COUNT
    kind = v[K['kind']] = rng.randrange(8)
    v[K['host']] = rng.randrange(2)
    v[K['local']] = rng.randrange(1, 9)
    v[K['pilot_shot']] = rng.randrange(2)
    v[K['made']] = rng.choice([0, 0, -1, 1])
    v[K['has']] = rng.randrange(2)
    v[K['has_game']] = 1
    v[K['end_state']] = rng.choice([0, 0, 1, 2, 3])
    v[K['end_time']] = bits(rng.uniform(0, 300))
    v[K['clock']] = bits(rng.uniform(0, 600))
    order = [rng.randrange(0, 3), rng.randrange(6, 20), 0x1000 + rng.randrange(0, 40), 0, bits(rng.uniform(0.5, 4)),
             bits(rng.choice([rng.uniform(1, 12), -rng.uniform(1, 12), 0.0])),
             bits(rng.uniform(-900, 900)), bits(rng.uniform(-50, 50)), bits(rng.uniform(-900, 900)), 0]
    if kind == 4:
        order[0] = rng.randrange(0, 30)
    if kind == 7:
        order[2] = rng.randrange(0, 40)  # a history entry keeps the index
    v[K['order']:K['order'] + 10] = order
    v[K['defaults']:K['defaults'] + 10] = [0, rng.randrange(6, 20), 0, 0, bits(rng.uniform(0.5, 4)), bits(5.0), 0, 0, 0, 0]
    if kind == 1:
        msg = struct.pack('<HHII4f', 0x19, 0x1c, rng.choice([0, 0x80, 0x81]), rng.randrange(0, 40),
                          rng.uniform(-900, 900), rng.uniform(-50, 50), rng.uniform(-900, 900), rng.uniform(1, 12))
    elif kind in (2, 3):
        rtype = rng.choice([1, 2, 3, 4, 5])
        name = rng.choice([b'boom_small', b'mfb_station_01_door', b'fire', b'x_mfb_station_01'])
        rec = struct.pack('<iI32si9i', rtype, rng.getrandbits(32), name.ljust(32, b'\0'), rng.randrange(-1, 50),
                          *(rng.getrandbits(31) for _ in range(9)))
        msg = rec if kind == 2 else struct.pack('<HH', 0x1a, 0x54) + rec
    elif kind == 5:
        msg = struct.pack('<HHI', 0x1b, 8, rng.randrange(0, 30))
    elif kind == 6:
        msg = struct.pack('<HHIf', 0x11, 0xc, rng.choice([0, 1, 2, 3, 4 | 1]), rng.uniform(0, 600))
        if (struct.unpack('<I', msg[4:8])[0] & 3) and not v[K['has_game']]:
            v[K['has_game']] = 1  # the exe reads the game unchecked
    else:
        msg = b'\0' * 4
    v[K['len']] = len(msg)
    words = struct.unpack(f'<{(len(msg) + 3) // 4}i', msg + bytes(-len(msg) % 4))
    v[K['msg']:K['msg'] + len(words)] = words
    return v


def main():
    assert COUNT == probe.probe_mech_net_wd_count(), (COUNT, probe.probe_mech_net_wd_count())
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(12)
    fails = cases = 0
    kinds = {}
    for n in range(3000):
        v = case(rng)
        theirs = exe.run(v)
        mine_v = list(v)
        if brk == 1 and v[K['kind']] == 1:
            mine_v[K['msg'] + 1] |= 0x80
        arr = (ctypes.c_int32 * COUNT)(*mine_v)
        out = (ctypes.c_int32 * 4096)()
        k = probe.probe_mech_net_world(arr, out, 4096)
        mine = list(out[:k])
        cases += 1
        kinds[v[K['kind']]] = kinds.get(v[K['kind']], 0) + 1
        if mine != theirs:
            fails += 1
            if fails <= 2:
                print(f'FAIL case {n} kind {v[K["kind"]]}:\n  port     {mine}\n  original {theirs}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} world events identical (by handler {dict(sorted(kinds.items()))})')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
