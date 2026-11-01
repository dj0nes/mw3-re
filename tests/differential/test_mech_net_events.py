"""The in-game events' receivers against the original x86
(docs/multiplayer-port.md, piece 7a): a posture 0x46ed10, a respawn 0x46ed30
(alive or dead; its rebuild 0x46e0f0 and place 0x418e90 logged), a kill
0x46eba0 (the HUD's line with its names: a network mech's player, the
pilot's, "Unknown"; the host's scoring 0x470170: suicides, kills between
teams or within one, the score limit ending the game, the 0x14 scores sent),
the scores in 0x4708e0, a collision's push in 0x4705b0 and out 0x470320, a
part shot off 0x46fff0. The lobby's scores and the HUD's are stood in for on
both sides. Compared: every call and send, the pilot's attacker and
velocity, the game's end state and clock after.

    cmake --build build && .venv/bin/python tests/differential/test_mech_net_events.py

BREAK=1 hands the port the kill's victim and killer swapped; it must fail.
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from test_weiler import Emulated, probe  # noqa: E402

probe.probe_mech_net_events.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_mech_net_events.restype = ctypes.c_int32
probe.probe_mech_net_ev_count.restype = ctypes.c_int32
probe.probe_mech_net_bugs.argtypes = [ctypes.c_int32]

NAMES = ['kind', 'len', ('msg', 16), 'local', 'host', 'remotes', ('owners', 4), 'who', 'active', 'hud_scores',
         'has_game', 'limit', 'end_state', 'lobby', ('slots', 32), ('shove', 3)]
K, _i = {}, 0
for n in NAMES:
    name, size = (n, 1) if isinstance(n, str) else n
    K[name], _i = _i, _i + size
COUNT = _i
MECHS, MECHS_N, PILOT, VIEW, TIME = 0x605f84, 0x605f88, 0x60861c, 0x5f4c8c, 0x60991c
NEW, DELETE = 0x5abd26, 0x5abd1a
ENTRY = {0: (0x46ed10, 'mech'), 1: (0x46ed30, 'mech'), 2: (0x46eba0, 'msg_ecx'), 3: (0x4708e0, 'mech'),
         4: (0x4705b0, 'shove'), 5: (0x46fff0, 'mech'), 6: (0x470320, 'out')}


def bits(f):
    return struct.unpack('<i', struct.pack('<f', f))[0]


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_function(NEW, lambda c: self.emu.alloc(c.arg(0)))
        cpu.stub_function(DELETE, lambda c: 0)
        stubs = {0x416780: (self.call_(0x416780, 1), 8), 0x46e0f0: (self.rebuild, 0), 0x418e90: (self.reset_at, 4),
                 0x40fe70: (self.hud, 0), 0x588a60: (self.name, 0), 0x586d10: (lambda c: self.v[K['local']], 0),
                 0x586d20: (lambda c: self.v[K['host']], 0), 0x479480: (lambda c: self.game, 0),
                 0x435910: (self.team, 4), 0x435ac0: (self.lobby(20), 8), 0x4359b0: (self.lobby(21), 8),
                 0x435a50: (self.kills, 4), 0x4361e0: (self.log_(24), 0), 0x4359f0: (self.lobby_set(22), 8),
                 0x435b00: (self.lobby_set(23), 8), 0x446ad0: (self.log_(25), 0), 0x430aa0: (lambda c: self.hud_at, 0),
                 0x5888a0: (self.send, 8), 0x41d510: (lambda c: self.parts.get(c.arg(0), 0), 4)}
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)
        self.code = {}
        for name, fn, pop in (('pilot_name', lambda c: self.pilot_name, 0), ('apply', self.apply, 8),
                              ('set_internal', self.set_internal, 8)):
            a = cpu.code(b'\x90' * 16)
            cpu.stub_function(a, fn, pop)
            self.code[name] = a

    def log_(self, code):
        def run(c):
            self.log.append(code)
            return 0
        return run

    def call_(self, fn, arg_index):
        def run(c):
            self.log += [10, fn, s32(c.arg(0))]
            return 0
        return run

    def rebuild(self, c):
        self.log += [11, s32(self.emu.u32(c.ecx + 0x6fc))]
        return 0

    def reset_at(self, c):
        self.log += [12] + list(struct.unpack('<3i', c.read(c.arg(0), 12)))
        return 0

    def num(self, p):
        if not p:
            return -3
        s = self.cpu.read(p, 16).split(b'\0')[0].decode()
        return -1 if s == 'Unknown' else -2 if s == 'Pilot' else int(s[1:])

    def hud(self, c):
        id_ = c.arg(1)
        self.log += [2, s32(c.arg(0)), id_, self.num(c.arg(2))] + ([self.num(c.arg(3))] if id_ == 0xf09 else [])
        return 0

    def name(self, c):
        return self.names.get(c.ecx, 0)

    def slot(self, pid):
        for k in range(self.v[K['lobby']]):
            if self.v[K['slots'] + 4 * k] == pid:
                return k
        return -1

    def team(self, c):
        k = self.slot(c.arg(0))
        return 0 if k < 0 else self.v[K['slots'] + 4 * k + 1]

    def lobby(self, code):
        def run(c):
            self.log += [code, s32(c.arg(0))]
            return 0
        return run

    def lobby_set(self, code):
        def run(c):
            self.log += [code, s32(c.arg(0)), s32(c.arg(1))]
            return 0
        return run

    def kills(self, c):
        k = self.slot(c.arg(0))
        return 0 if k < 0 else self.v[K['slots'] + 4 * k + 2]

    def send(self, c):
        n = c.edx
        data = self.cpu.read(c.ecx, n) + bytes(4)
        self.log += [1, s32(c.arg(1)), 1 if c.arg(0) else 0, n]
        self.log += [struct.unpack('<i', data[k:k + 4])[0] for k in range(0, n, 4)]
        return 101

    def apply(self, c):
        self.log += [30, s32(c.arg(0)), s32(c.arg(1))]
        return 0

    def set_internal(self, c):
        self.log += [31, s32(c.arg(0)), s32(c.arg(1))]
        return 0

    def code_of(self, p):
        if p == 0:
            return 0
        if p == self.pilot_mech:
            return 1
        return 2 + self.remotes.index(p) if p in self.remotes else -1

    def run(self, v):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.v, self.log = v, []
        u = cpu.u32
        self.names = {i: emu.put(f'P{i}'.encode() + b'\0') for i in range(1, 9)}
        self.pilot_name = emu.put(b'Pilot\0')
        nvt = emu.alloc(0x200)
        u(nvt + 0x58, 0x4d2a70), u(nvt + 0xa4, 0x4709c0)
        pvt = emu.alloc(0x200)
        u(pvt + 0xa4, self.code['pilot_name'])
        lst = emu.alloc(12)
        u(lst, lst), u(lst + 4, lst)
        u(MECHS, lst)
        self.remotes = []
        for k in range(v[K['remotes']]):
            r = emu.alloc(0x800)
            u(r, nvt), u(r + 0x6fc, v[K['owners'] + k])
            last = emu.u32(lst + 4)
            node = emu.alloc(12)
            u(node, lst), u(node + 4, last), u(node + 8, r), u(last, node), u(lst + 4, node)
            self.remotes.append(r)
        u(MECHS_N, len(self.remotes))
        who = self.remotes[v[K['who']]] if v[K['who']] < len(self.remotes) else 0
        if who:
            cpu.write(who + 8, bytes([v[K['active']]]))
        p = self.pilot_mech = emu.alloc(0x800)
        u(p, pvt)
        tvt = emu.alloc(0x80)
        u(tvt + 0x38, self.code['apply'])
        torso = emu.alloc(0x200)
        u(torso, tvt)
        u(p + 0x39c, torso)
        partvt = emu.alloc(0x80)
        u(partvt + 0x3c, self.code['set_internal'])
        self.parts = {}
        for pid in (1, 2, 3, 4, 5, 6, 7, 8):
            if pid in (1, 7, 8):
                self.parts[pid] = torso
                u(tvt + 0x3c, self.code['set_internal'])
            else:
                q = emu.alloc(0x80)
                u(q, partvt)
                self.parts[pid] = q
        pilot = emu.alloc(0x40)
        u(pilot + 0x14, p)
        u(PILOT, pilot)
        view = emu.alloc(0x80)
        u(view + 100, p)
        u(VIEW, view)
        self.hud_at = emu.alloc(0x18000)
        u(self.hud_at + 0x17970, 0 if v[K['hud_scores']] else 0x10)
        u(TIME, bits(77.5))
        self.game = 0
        if v[K['has_game']]:
            g = self.game = emu.alloc(0x100)
            u(g + 0x50, v[K['lobby']]), u(g + 0x70, v[K['limit']]), u(g + 0x7c, bits(-1.0)), u(g + 0x84, v[K['end_state']])
            head = emu.alloc(12)
            u(head, head), u(head + 4, head)
            for k in range(v[K['lobby']]):
                sl = emu.alloc(0x40)
                u(sl + 4, v[K['slots'] + 4 * k]), u(sl + 0x24, v[K['slots'] + 4 * k + 2]), u(sl + 0x28, v[K['slots'] + 4 * k + 3])
                last = emu.u32(head + 4)
                node = emu.alloc(12)
                u(node, head), u(node + 4, last), u(node + 8, sl), u(last, node), u(head + 4, node)
            u(g + 0x4c, head)
        msg = emu.put(struct.pack('<16i', *v[K['msg']:K['msg'] + 16])[:v[K['len']]] + bytes(16))
        addr, how = ENTRY[v[K['kind']]]
        if how == 'mech':
            if who:
                cpu.call(addr, ecx=who, stack_args=(msg,))
        elif how == 'msg_ecx':
            cpu.call(addr, ecx=msg)
        elif how == 'shove':
            if who:
                cpu.call(addr, ecx=who, stack_args=(who, msg))
        else:
            if who:
                cpu.call(addr, stack_args=(who,) + tuple(v[K['shove']:K['shove'] + 3]))
        g = self.game
        self.log += [90, self.code_of(emu.u32(p + 0x664))] + [s32(emu.u32(p + 0x30 + 4 * i)) for i in range(3)]
        self.log += [s32(emu.u32(g + 0x84)) if g else v[K['end_state']], s32(emu.u32(g + 0x7c)) if g else bits(-1.0),
                     self.code_of(emu.u32(who + 0x664)) if who else -9]
        return self.log


def case(rng):
    v = [0] * COUNT
    kind = v[K['kind']] = rng.randrange(7)
    v[K['local']] = rng.randrange(1, 9)
    v[K['host']] = rng.randrange(2)
    v[K['remotes']] = rng.randrange(1, 5)
    owners = rng.sample([i for i in range(1, 9) if i != v[K['local']]], 4)
    for k in range(4):
        v[K['owners'] + k] = owners[k]
    v[K['who']] = rng.randrange(v[K['remotes']] + 1)
    v[K['active']] = rng.randrange(2)
    v[K['hud_scores']] = rng.randrange(2)
    v[K['has_game']] = int(rng.random() < 0.9)
    v[K['limit']] = rng.choice([1, 3, 10])
    v[K['end_state']] = rng.choice([0, 0, 1, 2])
    ids = [v[K['local']]] + owners[:v[K['remotes']]]
    v[K['lobby']] = rng.randrange(0, 9)
    lobby_ids = rng.sample(range(1, 9), min(8, v[K['lobby']]))
    for i in ids:  # the players in the game in the lobby, mostly
        if i not in lobby_ids and len(lobby_ids) < 8 and rng.random() < 0.8:
            lobby_ids.append(i)
    v[K['lobby']] = len(lobby_ids)
    for k, pid in enumerate(lobby_ids):
        v[K['slots'] + 4 * k:K['slots'] + 4 * k + 4] = [pid, rng.randrange(1, 4), rng.randrange(0, 12), rng.randrange(0, 12)]
    some = lambda: rng.choice(ids + [rng.randrange(1, 12)])  # noqa: E731
    if kind == 0:
        msg = struct.pack('<HHi', 0x17, 8, rng.randrange(0, 20))
    elif kind == 1:
        msg = struct.pack('<HH3f', 0x12, 0x10, *(rng.uniform(-900, 900) for _ in range(3)))
    elif kind == 2:
        a = some()
        msg = struct.pack('<HHII', 0x13, 0xc, a, a if rng.random() < 0.2 else some())
    elif kind == 3:
        n = rng.randrange(0, 5)  # whole within the 64 bytes the test carries
        msg = struct.pack('<HHh', 0x14, 6 + 12 * n, n) + b''.join(
            struct.pack('<III', some(), rng.randrange(20), rng.randrange(20)) for _ in range(n))
    elif kind == 4:
        msg = struct.pack('<HH3f', 0x18, 0x10, rng.uniform(0, 40), rng.uniform(-9, 9), rng.uniform(-9, 9))
    elif kind == 5:
        # parts 0-10 and 15 (the probe's legs are stand-ins without calves or feet)
        msg = struct.pack('<HHIIi', 0x21, 0x10, some(), v[K['local']] if rng.random() < 0.7 else some(),
                          rng.choice(list(range(0, 11)) + [15]))
    else:
        msg = b'\0' * 4
        v[K['shove']:K['shove'] + 3] = [bits(rng.uniform(0, 40)), bits(rng.uniform(-9, 9)), bits(rng.uniform(-9, 9))]
    msg = msg[:64]
    v[K['len']] = len(msg)
    words = struct.unpack(f'<{(len(msg) + 3) // 4}i', msg + bytes(-len(msg) % 4))
    v[K['msg']:K['msg'] + len(words)] = words
    if kind in (0, 1, 3, 4, 5, 6) and v[K['who']] >= v[K['remotes']]:
        v[K['who']] = 0  # a handler of a mech runs on one
    return v


def main():
    assert COUNT == probe.probe_mech_net_ev_count(), (COUNT, probe.probe_mech_net_ev_count())
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(10)
    fails = cases = 0
    kinds = {}
    for n in range(4000):
        v = case(rng)
        theirs = exe.run(v)
        mine_v = list(v)
        if brk == 1 and v[K['kind']] == 2:
            mine_v[K['msg'] + 1], mine_v[K['msg'] + 2] = mine_v[K['msg'] + 2], mine_v[K['msg'] + 1]
        arr = (ctypes.c_int32 * COUNT)(*mine_v)
        out = (ctypes.c_int32 * 4096)()
        k = probe.probe_mech_net_events(arr, out, 4096)
        mine = list(out[:k])
        cases += 1
        kinds[v[K['kind']]] = kinds.get(v[K['kind']], 0) + 1
        if mine != theirs:
            fails += 1
            if fails <= 2:
                i = next((j for j, (a, b) in enumerate(zip(mine, theirs)) if a != b), min(len(mine), len(theirs)))
                print(f'FAIL case {n} kind {v[K["kind"]]} at {i}:\n  port     {mine}\n  original {theirs}')
    # The fixed path (bug fixes on): a push makes the pusher the pilot's attacker, not its own.
    probe.probe_mech_net_bugs(1)
    shoves = 0
    for n in range(600):
        v = case(rng)
        if v[K['kind']] != 4:
            continue
        theirs = exe.run(v)
        want = list(theirs)
        at = len(want) - 8  # the 90 block: attacker, velocity (3), end state, clock, the sender's attacker
        want[at + 1], want[at + 7] = 2 + v[K['who']], 0
        arr = (ctypes.c_int32 * COUNT)(*v)
        out = (ctypes.c_int32 * 4096)()
        k = probe.probe_mech_net_events(arr, out, 4096)
        shoves += 1
        if list(out[:k]) != want:
            fails += 1
            print(f'FAIL fixed path case {n}: port {list(out[:k])}\n  want {want}')
            break
    probe.probe_mech_net_bugs(0)
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: fixed path: {shoves} pushes credit the pusher')
    print(f'ok: {cases} events identical (by handler {dict(sorted(kinds.items()))})')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
