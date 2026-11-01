"""The local mech's network sends against the original x86
(docs/multiplayer-port.md, piece 6a): the mech's vt+0x168 0x46f160 for the
state (0xf, with its shots and damaged parts), the ping (0x10, to the next
player round the list 0x470ab0), a part's damage (0x15) and a shot (0x16),
and the send timer 0x46ef70 with the rate table 0x470b40, on random mechs:
their velocity, heading, torso turn, controls, aim, flags (powered down,
MASC, the AMS, NARC, limp, crouched, the first action, the motion state),
parts' damage, shots, target; the remote players and their pings; the
clock, the game's end state, host or not. The mech's world position, the
torso node's and the target's player are stood in for on both sides; the
kinds of pieces 7 and 8 are logged where they start. Compared: every send
(to, guaranteed, size, every word), the HUD line, the rules check, the
game's end, the answer and the state after.

    cmake --build build && .venv/bin/python tests/differential/test_mech_net_send.py

BREAK=1 hands the port the throttle for the turn; it must fail.
"""
import ctypes
import math
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from test_weiler import Emulated, probe  # noqa: E402
from test_mechsave import CEIL  # noqa: E402
from test_matrix import ASIN, COPYSIGN, IAT_ASIN, IAT_COPYSIGN  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP  # noqa: E402

probe.probe_mech_net_send.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_mech_net_send.restype = ctypes.c_int32
probe.probe_mech_net_in_count.restype = ctypes.c_int32

NAMES = ['kind', 'arg', ('vel', 3), 'qw', 'qy', ('turn_q', 4), 'throttle', 'turn', 'jump', ('torso_at', 3), ('aim', 3),
         'clear', 'active', 'anim_state', 'action', 'limp', 'xbc', 'power', 'masc', 'ams', 'narc', 'crouched',
         'target', 'damaged', 'shots', ('shot', 15), 'torso_internal', 'torso_armor', ('sec_armor', 5),
         ('side_internal', 2), ('parts', 10), ('pos', 3), 'remotes', ('owners', 4), ('echo', 4), ('heard', 4),
         'ping_turn', 'ping_counter', 'acked', 'now', 'host', 'players', 'clock_next', 'end_time', 'end_state',
         'game_time', 'clock_left', 'clock_on', 'next_state', 'next_ping', 'resends', 'rate_setting', 'warned',
         'last_second', 'has_game', 'weapon_locked', 'weapon_slot', 'weapon_target', 'state']
K = {}
_i = 0
for n in NAMES:
    name, size = (n, 1) if isinstance(n, str) else n
    K[name] = _i
    _i += size
COUNT = _i

RUNNING, TURN, COUNTER, ACKED = 0x605fb4, 0x605fc8, 0x5e5690, 0x605fc4
NEXT_STATE, NEXT_PING, RESENDS, RATE, WARNED, LAST = 0x605fa0, 0x605fcc, 0x605fd0, 0x608e28, 0x605fa4, 0x605f9c
TIME, LEFT, ON = 0x60991c, 0x609920, 0x609924
MECHS, MECHS_N, IAT_TIME, IAT_CEIL = 0x605f84, 0x605f88, 0x5bb118, 0x5bb61c
SENDER, EPILOGUE = 0x46f160, 0x46fcdc


def bits(f):
    return struct.unpack('<i', struct.pack('<f', f))[0]


def fl(b):
    return struct.unpack('<f', struct.pack('<i', b))[0]


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.import_code(IAT_CEIL, CEIL)
        cpu.import_code(IAT_ASIN, ASIN)  # quat_angles' (0x00523bc0)
        cpu.import_code(IAT_COPYSIGN, COPYSIGN)
        cpu.stub_import(IAT_TIME, lambda c: self.v[K['now']])
        stubs = {0x5a85e0: (lambda c: self.pos_at, 0), 0x4ed530: (self.torso_pos, 0), 0x4e7010: (lambda c: self.vehicle, 0),
                 0x41d510: (self.part, 4), 0x5888a0: (self.send, 8), 0x586d20: (lambda c: self.v[K['host']], 0),
                 0x479480: (lambda c: self.game, 0), 0x40fe70: (self.hud, 0), 0x4707c0: (self.log_(4), 0),
                 0x576c10: (self.crc, 4), 0x5abd26: (lambda c: self.emu.alloc(c.arg(0)), 0),
                 0x5abd1a: (lambda c: 0, 0), 0x46eba0: (self.log_(6), 0), 0x586d10: (lambda c: 9, 0),
                 0x460000: (lambda c: self.emu.u32(c.ecx + 4), 0)}  # a part's mech (+0x04 here)
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)
        cpu.stub_function(0x470740, self.log_(7), 0)  # the clock heard here too (test_mech_net_events.py)

    def other(self, kind):
        def run(uc, address, size, user):
            self.log += [5, kind]
            uc.reg_write(UC_X86_REG_EIP, EPILOGUE)
        return run

    def log_(self, code):
        def run(c):
            self.log.append(code)
            return 0
        return run

    def torso_pos(self, c):
        self.cpu.write(c.edx, struct.pack('<3i', *self.v[K['torso_at']:K['torso_at'] + 3]))
        return c.edx

    def part(self, c):
        return self.parts.get(c.arg(0), 0)

    def send(self, c):
        n = c.edx
        data = self.cpu.read(c.ecx, n) + bytes(4)
        self.log += [1, s32(c.arg(1)), 1 if c.arg(0) else 0, n]
        self.log += [struct.unpack('<i', data[k:k + 4])[0] for k in range(0, n, 4)]
        self.send_id += 1
        return self.send_id

    def hud(self, c):
        self.log += [2, c.arg(1)]
        return 0

    def crc(self, c):  # the host's rules check, left out of the port (FIX (trap)): here it always passes
        return self.emu.u32(self.game + 0x88)

    def run(self, v):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.v, self.log, self.send_id = v, [], 100
        u, w8, wf = cpu.u32, lambda a, b: cpu.write(a, bytes([b & 0xff])), lambda a, f: cpu.u32(a, f)
        m = emu.alloc(0x800)
        mv = emu.alloc(0x200)
        u(mv + 0xa0, 0x41cc10)
        u(mv + 0x160, 0x41ec50)  # the player's id: local_id
        u(mv + 0x168, SENDER)
        u(m, mv)
        w8(m + 0x8, v[K['active']])
        for i in range(3):
            u(m + 0x30 + 4 * i, v[K['vel'] + i])
        u(m + 0x48, v[K['qw']])
        u(m + 0x50, v[K['qy']])
        if v[K['target']]:
            u(m + 0x6c, emu.alloc(16))
            self.vehicle = emu.alloc(0x800)
            u(self.vehicle + 0x6fc, v[K['target']])
        w8(m + 0xbc, v[K['xbc']])
        wm = emu.alloc(0x40)
        u(wm + 4, v[K['state']])
        for i in range(3):
            u(wm + 0xc + 4 * i, v[K['aim'] + i])
        u(m + 0xd4, wm)
        w8(m + 0xdc, v[K['power']] & 1)
        w8(m + 0xde, v[K['power']] >> 1 & 1)
        u(m + 0xfc, v[K['clear']])
        ctl, vb = emu.alloc(0x80), emu.alloc(8)
        u(vb, 0xfffffffc)
        u(vb + 4, 0x5c)
        u(ctl + 4, vb)
        u(ctl + 0x60, 0x5c3428)
        u(ctl + 0x14, v[K['turn']])
        u(ctl + 0x20, v[K['jump']])
        u(ctl + 0x44, v[K['throttle']])
        u(m + 0x37c, ctl)
        torso = emu.alloc(0x1c0)
        u(torso, 0x5c29e0)
        u(torso + 8, emu.alloc(16))
        u(torso + 0x38, v[K['torso_internal']])
        u(torso + 0x40, v[K['torso_armor']])
        for i in range(4):
            u(torso + 0x118 + 4 * i, v[K['turn_q'] + i])
        for i in range(5):
            u(torso + 0x174 + 4 * (i + 1), v[K['sec_armor'] + i])
        u(torso + 0x1a0, v[K['side_internal']])
        u(torso + 0x1a4, v[K['side_internal'] + 1])
        u(m + 0x39c, torso)
        self.parts = {1: torso, 7: torso, 8: torso}
        for k, pid in enumerate((2, 3, 4, 5, 6)):
            p = emu.alloc(0x80)
            u(p, 0x5c2840)
            u(p + 0x38, v[K['parts'] + 2 * k])
            u(p + 0x40, v[K['parts'] + 2 * k + 1])
            self.parts[pid] = p
        if v[K['ams']]:
            a = emu.alloc(0x20)
            w8(a, 1 if v[K['ams']] == 2 else 0)
            u(m + 0x3f0, a)
        head = emu.alloc(12)
        if v[K['action']] >= 0:
            node = emu.alloc(12)
            u(head, node), u(head + 4, node), u(node, head), u(node + 4, head), u(node + 8, v[K['action']])
            u(m + 0x484, 1)
        else:
            u(head, head), u(head + 4, head)
        u(m + 0x480, head)
        u(m + 0x570, v[K['narc']])
        u(m + 0x574, v[K['masc']])
        u(m + 0x578, v[K['anim_state']])
        w8(m + 0x586, v[K['crouched']])
        u(m + 0x5ac, v[K['limp']])
        shots = emu.alloc(12)
        u(shots, shots), u(shots + 4, shots)
        for k in range(v[K['shots']]):
            rec = emu.alloc(16)
            u(rec, v[K['shot'] + 3 * k]), u(rec + 8, v[K['shot'] + 3 * k + 1]), u(rec + 12, v[K['shot'] + 3 * k + 2])
            self.push(shots, rec)
        u(m + 0x678, shots)
        u(m + 0x67c, v[K['shots']])
        u(m + 0x680, v[K['damaged']])
        self.pos_at = emu.put(struct.pack('<3i', *v[K['pos']:K['pos'] + 3]))
        lst = emu.alloc(12)
        u(lst, lst), u(lst + 4, lst)
        u(MECHS, lst)
        u(MECHS_N, v[K['remotes']])
        remotes = []
        nvt, pvt = emu.alloc(0x200), emu.alloc(0x200)  # a network mech's vt+0x58 (itself); a plain mech's (0)
        u(nvt + 0x58, 0x4d2a70), u(nvt + 0x160, 0x41ec50), u(pvt + 0x58, 0x40bae0), u(pvt + 0x160, 0x41ec50)
        for k in range(v[K['remotes']]):
            r = emu.alloc(0x800)
            u(r, nvt)
            u(r + 0x6fc, v[K['owners'] + k]), u(r + 0x6e0, v[K['echo'] + k]), u(r + 0x6d8, v[K['heard'] + k])
            self.push(lst, r)
            remotes.append(r)
        plain = emu.alloc(0x800)
        u(plain, pvt)
        weapon = emu.alloc(0x80)
        u(weapon + 0x1c, v[K['weapon_slot']])
        w8(weapon + 0x5c, v[K['weapon_locked']])
        wt = v[K['weapon_target']]
        u(weapon + 0x14, 0 if wt == 0 else remotes[wt - 1] if wt <= len(remotes) else plain)
        w8(RUNNING, 1)
        u(TURN, v[K['ping_turn']]), u(COUNTER, v[K['ping_counter']]), u(ACKED, v[K['acked']])
        u(NEXT_STATE, v[K['next_state']]), u(NEXT_PING, v[K['next_ping']]), u(RESENDS, v[K['resends']])
        u(RATE, v[K['rate_setting']]), w8(WARNED, v[K['warned']]), w8(LAST, v[K['last_second']])
        u(TIME, v[K['game_time']]), u(LEFT, v[K['clock_left']]), w8(ON, v[K['clock_on']])
        self.game = 0
        if v[K['has_game']]:
            g = self.game = emu.alloc(0x100)
            u(g + 0x50, v[K['players']]), u(g + 0x7c, v[K['clock_next']]), u(g + 0x80, v[K['end_time']])
            u(g + 0x84, v[K['end_state']]), u(g + 0x88, 0x1234)
        tobj = 0 if wt == 0 else remotes[wt - 1] if wt <= len(remotes) else plain
        part = emu.alloc(0x80)
        u(part + 4, tobj)  # its mech, for the 0x460000 stand-in
        if v[K['arg']] >= 0:
            node = emu.alloc(0x40)
            u(node + 0x28, v[K['arg']])
            u(part + 8, node)
        ret = 0
        if v[K['kind']] == 100:
            cpu.call(0x46ef70, ecx=m)
        else:
            arg = {0x16: weapon, 0x13: tobj, 0x21: part, 0x11: self.game}.get(v[K['kind']], v[K['arg']])
            cpu.call(SENDER, ecx=m, stack_args=(v[K['kind']], arg))
            ret = s32(cpu.uc.reg_read(UC_X86_REG_EAX))
        self.log += [90, ret, s32(emu.u32(m + 0x680)), emu.u32(m + 0x67c)]
        n = emu.u32(shots)
        while n != shots:
            rec = emu.u32(n + 8)
            self.log += [s32(emu.u32(rec + 4 * i)) for i in range(4)]
            n = emu.u32(n)
        g = self.game
        self.log += [91, s32(emu.u32(m + 0x66c)), cpu.read(m + 0x668, 1)[0], s32(emu.u32(NEXT_STATE)),
                     s32(emu.u32(NEXT_PING)), s32(emu.u32(RESENDS)), cpu.read(WARNED, 1)[0], cpu.read(LAST, 1)[0],
                     s32(emu.u32(TURN)), s32(emu.u32(COUNTER)),
                     s32(emu.u32(g + 0x84)) if g else v[K['end_state']],
                     s32(emu.u32(g + 0x7c)) if g else v[K['clock_next']]]
        for r in remotes:
            self.log += [92, s32(emu.u32(r + 0x6f0)), s32(emu.u32(r + 0x6f4))]
        return self.log

    def push(self, head, value):
        cpu, u = self.cpu, self.emu.u32
        last = u(head + 4)
        node = self.emu.alloc(12)
        cpu.u32(node, head), cpu.u32(node + 4, last), cpu.u32(node + 8, value)
        cpu.u32(last, node), cpu.u32(head + 4, node)


def frac(rng):
    return rng.choice([0.0, 1.0, rng.random(), rng.random(), 0.5, 1.0 / 255])


def case(rng):
    v = [0] * COUNT
    v[K['kind']] = rng.choice([0xf, 0xf, 0xf, 0x10, 0x15, 0x16, 100, 100, 0x11, 0x12, 0x13, 0x17, 0x21])
    v[K['arg']] = rng.randrange(-1, 12)
    if v[K['kind']] == 0x15:
        v[K['arg']] = rng.randrange(0, 12)
    for i in range(3):
        v[K['vel'] + i] = bits(rng.choice([rng.uniform(-30, 30), rng.uniform(-300, 300), 0.0]))
    yaw = rng.uniform(-math.pi, math.pi)
    v[K['qw']], v[K['qy']] = bits(math.cos(yaw / 2)), bits(math.sin(yaw / 2))
    q = [rng.uniform(-1, 1) for _ in range(4)]
    n = math.sqrt(sum(x * x for x in q))
    for i in range(4):
        v[K['turn_q'] + i] = bits(q[i] / n)
    for k in ('throttle', 'turn', 'jump'):
        v[K[k]] = bits(rng.choice([0.0, rng.uniform(-1, 1), rng.uniform(-2, 2), 1.0]))
    for i in range(3):
        v[K['torso_at'] + i] = bits(rng.uniform(-500, 500))
        v[K['aim'] + i] = bits(rng.uniform(-500, 500))
        v[K['pos'] + i] = bits(rng.uniform(-900, 900))
    if rng.random() < 0.1:
        for i in range(3):
            v[K['aim'] + i] = v[K['torso_at'] + i]  # aiming at itself: no direction
    v[K['clear']] = rng.randrange(2)
    v[K['active']] = int(rng.random() < 0.9)
    v[K['anim_state']] = rng.randrange(0, 40)
    v[K['action']] = rng.choice([-1, -1, rng.randrange(0, 20)])
    v[K['limp']] = rng.choice([-1, 0, 1, 2])
    v[K['xbc']] = rng.randrange(32)
    v[K['power']] = rng.randrange(4)
    v[K['masc']] = bits(rng.choice([0.0, 2.5]))
    v[K['ams']] = rng.randrange(3)
    v[K['narc']] = bits(rng.choice([0.0, 0.0, 3.0, -1.0]))
    v[K['crouched']] = rng.randrange(2)
    v[K['target']] = rng.choice([0, 0, rng.randrange(1, 9)])
    v[K['damaged']] = rng.choice([0, 0, rng.getrandbits(10), 0x1fe, 1 << rng.randrange(10)])
    v[K['shots']] = rng.choice([0, 0, rng.randrange(1, 6)])
    for k in range(5):
        v[K['shot'] + 3 * k] = rng.randrange(0, 20)
        v[K['shot'] + 3 * k + 1] = rng.randrange(0, 5)
        v[K['shot'] + 3 * k + 2] = rng.choice([0, rng.randrange(1, 9)])
    v[K['torso_internal']], v[K['torso_armor']] = bits(frac(rng)), bits(frac(rng))
    for i in range(5):
        v[K['sec_armor'] + i] = bits(frac(rng))
    v[K['side_internal']], v[K['side_internal'] + 1] = bits(frac(rng)), bits(frac(rng))
    for i in range(10):
        v[K['parts'] + i] = bits(frac(rng))
    v[K['remotes']] = rng.randrange(0, 5)
    owners = rng.sample(range(1, 9), 4)
    for k in range(4):
        v[K['owners'] + k] = owners[k]
        v[K['echo'] + k] = rng.randrange(0, 100)
        v[K['heard'] + k] = rng.randrange(0, 50000)
    v[K['ping_turn']] = rng.choice([0, owners[rng.randrange(4)]])
    v[K['ping_counter']] = rng.randrange(1, 40)
    v[K['acked']] = rng.randrange(0, 50000)
    v[K['now']] = rng.randrange(50000, 90000)
    v[K['host']] = rng.randrange(2)
    v[K['players']] = rng.randrange(0, 10)
    t = rng.uniform(0, 600)
    v[K['game_time']] = bits(t)
    v[K['clock_next']] = bits(t + rng.uniform(-3, 3))
    v[K['end_time']] = bits(t + rng.uniform(-3, 3))
    v[K['end_state']] = rng.choice([0, 0, 1, 2, 3])
    v[K['clock_left']] = bits(rng.choice([rng.uniform(0, 600), rng.uniform(0, 70), rng.uniform(0, 4), 1.0, 60.0]))
    v[K['clock_on']] = rng.randrange(2)
    v[K['next_state']] = bits(t + rng.uniform(-0.5, 0.5))
    v[K['next_ping']] = bits(t + rng.uniform(-3, 3))
    v[K['resends']] = rng.choice([0, 5, 24, 25, 26])
    v[K['rate_setting']] = rng.choice([0, 1, 2, 3])
    v[K['warned']], v[K['last_second']] = rng.randrange(2), rng.randrange(2)
    v[K['has_game']] = int(rng.random() < 0.9)
    if v[K['kind']] in (0x10, 0x11):
        v[K['has_game']] = 1  # the host's ping reads the game unchecked; it is only sent from the timer, which has one
    v[K['weapon_locked']] = rng.randrange(2)
    v[K['weapon_slot']] = rng.randrange(0, 20)
    v[K['weapon_target']] = rng.randrange(0, 6)
    v[K['state']] = rng.randrange(0, 4)
    if v[K['kind']] == 0x21:  # a network mech's part: the exe reads its mech's +0x6fc unchecked
        v[K['remotes']] = max(v[K['remotes']], 1)
        v[K['weapon_target']] = rng.randrange(1, v[K['remotes']] + 1)
    return v


FLOAT_WORDS = (6, 7, 8, 10, 11, 12)  # a state's +0x18 heading, +0x1c/+0x20 torso, +0x28/+0x2c aim turn, +0x30 range


def same(mine, theirs):
    """Equal but the state messages' computed floats (x87 at 24 bits against
    the port's), within float rounding."""
    if len(mine) != len(theirs):
        return False
    loose = set()
    i = 0
    while i < len(theirs):
        if theirs[i] == 1 and i + 4 < len(theirs) and theirs[i + 4] & 0xffff == 0xf:
            base = i + 4
            loose.update(base + w for w in FLOAT_WORDS)
            i = base + (theirs[i + 3] + 3) // 4
        else:
            i += 1
    for k, (a, b) in enumerate(zip(mine, theirs)):
        if a == b:
            continue
        if k not in loose:
            return False
        x, y = fl(a), fl(b)
        if not (abs(x - y) <= 2e-6 * max(1.0, abs(y))):
            return False
    return True


def main():
    assert COUNT == probe.probe_mech_net_in_count(), (COUNT, probe.probe_mech_net_in_count())
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(6)
    fails = cases = 0
    kinds = {}
    for n in range(3000):
        v = case(rng)
        theirs = exe.run(v)
        mine_v = list(v)
        if brk == 1:
            mine_v[K['turn']] = mine_v[K['throttle']]
        arr = (ctypes.c_int32 * COUNT)(*mine_v)
        out = (ctypes.c_int32 * 8192)()
        k = probe.probe_mech_net_send(arr, out, 8192)
        mine = list(out[:k])
        cases += 1
        kinds[v[K['kind']]] = kinds.get(v[K['kind']], 0) + 1
        if not same(mine, theirs):
            fails += 1
            if fails <= 2:
                i = next((j for j, (a, b) in enumerate(zip(mine, theirs)) if a != b), min(len(mine), len(theirs)))
                print(f'FAIL case {n} kind {v[K["kind"]]:#x} at {i}:\n  port     {mine[max(0, i - 8):i + 8]}\n'
                      f'  original {theirs[max(0, i - 8):i + 8]}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} sends identical (by kind {dict(sorted(kinds.items()))}; 100 the timer)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
