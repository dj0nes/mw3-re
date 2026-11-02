"""A remote mech's receive side against the original x86
(docs/multiplayer-port.md, piece 6b): the state's apply 0x46e4d0 on random
network mechs and states (inactive or not, the velocity, throttle, turn,
torso, aim with its latency extrapolation, heading, passive radar, target:
the pilot, another network mech or none; shots and damaged parts at sizes
that hold them or not; power, AMS, MASC, NARC, crouch, jump and the motion
state's changes, a respawn included), the dead reckoning 0x46ed70 (snapped,
blended, inactive), the ping's answer 0x470a00 (ours or not) and the send
done 0x4709d0. The mech's own calls (power, MASC, NARC, crouch, the limp,
an action), the weapons' aim and fire, the parts' setters, the target's
pick, the radar's light, the set position and the respawn are logged on
both sides. Compared: those calls and the network mech's state after, the
floats within float rounding (x87 at 24 bits against the port's).

    cmake --build build && .venv/bin/python tests/differential/test_mech_net_apply.py

Then the fixed path (bug fixes on, FIX (bug)): the aim's lag correction
takes the latency in milliseconds, as the gap is; the exe given the
latency x 1000 computes the same, and the applies are compared so.

BREAK=1 gives the port's apply another throttle byte; it must fail.
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
from test_matrix import ASIN, COPYSIGN, IAT_ASIN, IAT_COPYSIGN  # noqa: E402

probe.probe_mech_net_apply.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_mech_net_apply.restype = ctypes.c_int32
probe.probe_mech_net_rx_count.restype = ctypes.c_int32
probe.probe_mech_net_bugs.argtypes = [ctypes.c_int32]

NAMES = ['mode', 'len', ('msg', 64), 'flags_in', ('pos_in', 3), ('vel_in', 3), ('heading_q', 4), ('aim_q', 4), 'heard',
         'latency', ('orient', 4), ('world', 3), ('torso_at', 3), 'xbc', 'power', 'masc', 'narc', 'crouched',
         'anim_state', 'ams', 'local', 'now', 'dt', 'ping_id', 'ping_sent', 'acked', 'pilot_sent', 'remotes',
         ('owners', 4)]
K, _i = {}, 0
for n in NAMES:
    name, size = (n, 1) if isinstance(n, str) else n
    K[name], _i = _i, _i + size
COUNT = _i
MECHS, MECHS_N, IAT_TIME, PILOT, ACKED = 0x605f84, 0x605f88, 0x5bb118, 0x60861c, 0x605fc4
GAIN, DAMP = 0x5e55f0, 0x5e55ec
MECH_CALLS = {0x41cc30: 4, 0x41df50: 4, 0x41ac10: 4, 0x41bda0: 4, 0x415a90: 0, 0x41d5f0: 8, 0x416780: 8}


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
        cpu.import_code(IAT_ASIN, ASIN)
        cpu.import_code(IAT_COPYSIGN, COPYSIGN)
        cpu.stub_import(IAT_TIME, lambda c: self.v[K['now']])
        stubs = {0x5a85e0: (lambda c: self.world_at, 0), 0x4ed530: (self.torso_pos, 0), 0x586d10: (lambda c: self.v[K['local']], 0),
                 0x41d510: (lambda c: self.parts.get(c.arg(0), 0), 4), 0x4b0bc0: (self.aim, 4), 0x4b08c0: (self.fire, 12),
                 0x4a7b40: (self.pick, 4), 0x46ed30: (self.respawn, 4), 0x5a85c0: (lambda c: 0, 0)}
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)
        for a, pop in MECH_CALLS.items():
            cpu.stub_function(a, self.mech_call(a), pop)
        self.vt = {}
        for off, fn, pop in ((0xb8, self.log_(14), 0), (0x18, self.set_position, 4)):
            a = cpu.code(b'\x90' * 16)
            cpu.stub_function(a, fn, pop)
            self.vt[off] = a
        self.part_set = {}
        for off, kind in ((0x3c, 0), (0x40, 1)):
            a = cpu.code(b'\x90' * 16)
            cpu.stub_function(a, self.part_setter(kind), 8)
            self.part_set[off] = a

    def log_(self, code):
        def run(c):
            self.log.append(code)
            return 0
        return run

    def mech_call(self, fn):
        def run(c):
            self.log += [10, fn, 0 if fn == 0x415a90 else s32(c.arg(0))]
            return 0
        return run

    def part_setter(self, kind):
        def run(c):
            self.log += [13, self.part_index[c.ecx], kind, s32(c.arg(0)), s32(c.arg(1))]
            return 0
        return run

    def torso_pos(self, c):
        self.cpu.write(c.edx, struct.pack('<3i', *self.v[K['torso_at']:K['torso_at'] + 3]))
        return c.edx

    def aim(self, c):
        self.log += [11] + list(struct.unpack('<3i', c.read(c.arg(0), 12)))
        return 0

    def fire(self, c):
        self.log += [12, s32(c.arg(0)), s32(c.arg(1)), s32(c.arg(2))]
        return 1

    def code_of(self, p):
        if p == 0:
            return 0
        if p == self.pilot_mech:
            return 1
        return 2 + self.remotes.index(p) if p in self.remotes else -1

    def pick(self, c):
        self.log += [15, self.code_of(c.arg(0))]
        return 0

    def respawn(self, c):  # ECX the mech, the static 0x12 message on the stack
        self.log += [16] + list(struct.unpack('<3i', c.read(c.arg(0) + 4, 12)))
        return 0

    def set_position(self, c):
        self.log += [20] + list(struct.unpack('<3i', c.read(c.arg(0), 12)))
        return 0

    def run(self, v):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.v, self.log = v, []
        u, w8 = cpu.u32, lambda a, b: cpu.write(a, bytes([b & 0xff]))
        m = emu.alloc(0x800)
        mv = emu.alloc(0x200)
        u(mv + 0xa0, 0x41cc10)
        for off, a in self.vt.items():
            u(mv + off, a)
        u(m, mv)
        tr = emu.alloc(0x40)
        u(m + 0xc, tr)
        for i in range(4):
            u(m + 0x48 + 4 * i, v[K['orient'] + i])
        w8(m + 0xbc, v[K['xbc']])
        u(m + 0xd4, emu.alloc(0x40))
        w8(m + 0xdc, v[K['power']] & 1)
        w8(m + 0xde, v[K['power']] >> 1 & 1)
        ctl, vb = emu.alloc(0x80), emu.alloc(8)
        u(vb, 0xfffffffc), u(vb + 4, 0x5c), u(ctl + 4, vb), u(ctl + 0x60, 0x5c3428)
        u(m + 0x37c, ctl)
        torso = emu.alloc(0x1c0)
        u(torso + 8, emu.alloc(16))
        u(torso + 0x118, bits(1.0))  # the torso's turn at rest, as the port's
        u(m + 0x39c, torso)
        pvt = emu.alloc(0x80)
        for off, a in self.part_set.items():
            u(pvt + off, a)
        u(torso, pvt)
        self.parts, self.part_index = {1: torso, 7: torso, 8: torso}, {torso: 1}
        for pid in (2, 3, 4, 5, 6):
            p = emu.alloc(0x80)
            u(p, pvt)
            self.parts[pid], self.part_index[p] = p, pid
        if v[K['ams']]:
            a = emu.alloc(0x20)
            w8(a, 1 if v[K['ams']] == 2 else 0)
            u(m + 0x3f0, a)
        u(m + 0x570, v[K['narc']]), u(m + 0x574, v[K['masc']]), u(m + 0x578, v[K['anim_state']])
        w8(m + 0x586, v[K['crouched']])
        u(m + 0x6f0, v[K['ping_sent']]), u(m + 0x6f4, v[K['ping_id']]), u(m + 0x6f8, v[K['latency']])
        u(m + 0x738, v[K['flags_in']])
        for i in range(3):
            u(m + 0x704 + 4 * i, v[K['pos_in'] + i])
            u(m + 0x760 + 4 * i, v[K['vel_in'] + i])
        for i in range(4):
            u(m + 0x740 + 4 * i, v[K['aim_q'] + i])
            u(m + 0x750 + 4 * i, v[K['heading_q'] + i])
        u(m + 0x73c, v[K['heard']])
        self.world_at = emu.put(struct.pack('<3i', *v[K['world']:K['world'] + 3]))
        self.pilot_mech = emu.alloc(0x800)
        u(self.pilot_mech + 0x66c, v[K['pilot_sent']])
        pilot = emu.alloc(0x40)
        u(pilot + 0x14, self.pilot_mech)
        u(PILOT, pilot)
        u(ACKED, v[K['acked']])
        u(GAIN, bits(-6.5)), u(DAMP, bits(-0.3))
        lst = emu.alloc(12)
        u(lst, lst), u(lst + 4, lst)
        u(MECHS, lst)
        self.remotes = []
        for k in range(v[K['remotes']]):
            r = emu.alloc(0x800)
            u(r + 0x6fc, v[K['owners'] + k])
            last = emu.u32(lst + 4)
            node = emu.alloc(12)
            u(node, lst), u(node + 4, last), u(node + 8, r), u(last, node), u(lst + 4, node)
            self.remotes.append(r)
        u(MECHS_N, len(self.remotes))
        n = v[K['len']]
        raw = struct.pack('<64i', *v[K['msg']:K['msg'] + 64])[:n]
        mode = v[K['mode']]
        if mode == 3:  # the exe's 0xe holds a pointer to DirectPlay's send complete: +0x1c the id, +0x24 the time
            sc = emu.alloc(0x30)
            cpu.write(sc + 0x1c, raw[4:8])
            cpu.write(sc + 0x24, raw[8:12])
            msg = emu.put(raw[:4] + struct.pack('<I', sc) + bytes(8))
        else:
            msg = emu.put(raw + bytes(16))
        if mode == 0:
            cpu.call(0x46e4d0, ecx=m, stack_args=(msg,))
        elif mode == 1:
            cpu.call(0x46ed70, ecx=m, stack_args=(v[K['dt']],))
        elif mode == 2:
            cpu.call(0x470a00, ecx=m, stack_args=(msg,))
        else:
            cpu.call(0x4709d0, ecx=0, edx=msg)
        r = lambda a: s32(emu.u32(a))  # noqa: E731
        self.log.append(90)
        self.log += [r(m + 0x700 + 4 * i) for i in range(15)]
        self.log += [r(m + 0x760 + 4 * i) for i in range(3)]
        self.log += [r(m + 0x750 + 4 * i) for i in range(4)]
        self.log += [r(m + 0x740 + 4 * i) for i in range(4)]
        self.log += [r(m + 0x48 + 4 * i) for i in range(4)]
        self.log += [r(tr + 4 * i) for i in range(9)]
        self.log += [r(torso + 0x118 + 4 * i) for i in range(4)]
        ams = emu.u32(m + 0x3f0)
        self.log += [91, r(m + 0x73c), cpu.read(m + 0xbc, 1)[0], cpu.read(ams, 1)[0] if ams else -1, r(ctl + 0x44),
                     r(ctl + 0x48), r(ctl + 0x4c), cpu.read(ctl + 0x3d, 1)[0], r(m + 0x6f8)]
        self.log += [r(m + 0x6dc + 4 * i) for i in range(5)]
        self.log += [92, r(m + 0x6d8), cpu.read(self.pilot_mech + 0x668, 1)[0], r(ACKED)]
        return self.log


def state_message(rng, v):
    shots, parts = rng.choice([0, 0, rng.randrange(1, 6)]), rng.choice([0, 0, rng.randrange(1, 5)])
    flags = rng.getrandbits(32) & ~(0x7f << 12) & ~(0xf << 19)
    if rng.random() < 0.85:
        flags &= ~2  # active, mostly
    flags |= shots << 12 | parts << 19
    size = 0x3c + shots + 4 * parts
    full = rng.choice([size, size, size, 0x3c, 0x3c + shots, size + 4])
    b = bytearray(max(full, size) + 8)
    struct.pack_into('<HH3f', b, 0, 0xf, full, *(rng.uniform(-900, 900) for _ in range(3)))
    struct.pack_into('<3h', b, 0x10, *(rng.randrange(-32768, 32768) for _ in range(3)))
    struct.pack_into('<3f', b, 0x18, rng.uniform(-1.6, 1.6), rng.uniform(-1, 1), rng.uniform(-0.6, 0.6))
    struct.pack_into('<2b', b, 0x24, rng.randrange(-127, 128), rng.randrange(-127, 128))
    struct.pack_into('<3f', b, 0x28, rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8), rng.uniform(0, 800))
    owners = v[K['owners']:K['owners'] + v[K['remotes']]]
    target = rng.choice([0, v[K['local']], rng.randrange(1, 9)] + list(owners))
    struct.pack_into('<II', b, 0x34, target, flags)
    for k in range(shots):
        b[0x3c + k] = rng.getrandbits(8) & 0x7f
    for k in range(parts):
        struct.pack_into('<I', b, 0x3c + shots + 4 * k, rng.randrange(0, 11) | rng.getrandbits(24) << 8)
    return bytes(b[:full])


def case(rng):
    v = [0] * COUNT
    v[K['mode']] = rng.choice([0, 0, 0, 1, 1, 2, 3])
    v[K['local']] = rng.randrange(1, 9)
    v[K['remotes']] = rng.randrange(0, 5)
    owners = rng.sample(range(1, 9), 4)
    for k in range(4):
        v[K['owners'] + k] = owners[k]
    v[K['flags_in']] = rng.getrandbits(32) & ~(2 if rng.random() < 0.8 else 0)
    for i in range(3):
        v[K['pos_in'] + i] = bits(rng.uniform(-900, 900))
        v[K['vel_in'] + i] = bits(rng.uniform(-40, 40))
        v[K['world'] + i] = bits(rng.uniform(-900, 900))
        v[K['torso_at'] + i] = bits(rng.uniform(-900, 900))
    h = rng.uniform(-1.6, 1.6)
    hq = [math.cos(h), 0.0, math.sin(h), 0.0]
    for i in range(4):
        v[K['heading_q'] + i] = bits(hq[i])
    aq = rng.choice([[1.0, 0.0, 0.0, 0.0], [rng.uniform(0.5, 1), rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5), 0.0]])
    for i in range(4):
        v[K['aim_q'] + i] = bits(aq[i])
    o = rng.uniform(-1.6, 1.6)
    for i, x in enumerate([math.cos(o), 0.0, math.sin(o), 0.0]):
        v[K['orient'] + i] = bits(x)
    v[K['now']] = rng.randrange(100000, 200000)
    v[K['heard']] = v[K['now']] - rng.choice([0, 1, 2, 50, 300])
    v[K['latency']] = bits(rng.choice([0.0, 0.05, 0.3]))
    v[K['xbc']] = rng.randrange(32)
    v[K['power']] = rng.randrange(4)
    v[K['masc']] = bits(rng.choice([0.0, 2.0]))
    v[K['narc']] = bits(rng.choice([0.0, 3.0]))
    v[K['crouched']] = rng.randrange(2)
    v[K['anim_state']] = rng.choice([0, 1, 5, 9, 10, 11, 12, 13, rng.randrange(32)])
    v[K['ams']] = rng.randrange(3)
    v[K['dt']] = bits(rng.choice([0.0, 0.016, 0.033, 0.1]))
    v[K['ping_id']] = rng.randrange(1, 30)
    v[K['ping_sent']] = v[K['now']] - rng.randrange(0, 900)
    v[K['acked']] = rng.randrange(0, 200000)
    v[K['pilot_sent']] = rng.randrange(100, 110)
    mode = v[K['mode']]
    if mode == 0:
        msg = state_message(rng, v)
    elif mode == 2:
        msg = struct.pack('<HH4I', 0x10, 0x14, rng.randrange(1, 40),
                          v[K['ping_id']] if rng.random() < 0.7 else rng.randrange(1, 30),
                          rng.randrange(0, 200000), rng.randrange(0, 500))
    elif mode == 3:
        msg = struct.pack('<HH2I', 0xe, 12, v[K['pilot_sent']] if rng.random() < 0.6 else rng.randrange(100, 110),
                          rng.randrange(0, 200000))
    else:
        msg = b'\0' * 4
    v[K['len']] = len(msg)
    words = struct.unpack(f'<{(len(msg) + 3) // 4}i', msg + bytes(-len(msg) % 4))
    for k, w in enumerate(words):
        v[K['msg'] + k] = w
    return v


def same(mine, theirs):
    """Equal but floats within rounding; the aim points (code 11), the torso's
    place plus a rotated range, to the rounding of those (~1000 m) terms."""
    if len(mine) != len(theirs):
        return False
    aim = {i + k for i, b in enumerate(theirs) if b == 11 for k in (1, 2, 3)}
    for i, (a, b) in enumerate(zip(mine, theirs)):
        if a == b:
            continue
        x, y = fl(a), fl(b)
        scale = max(abs(x), abs(y), 1000.0 if i in aim else 1.0)
        if not (abs(x) > 1e-30 and abs(y) > 1e-30 and math.isfinite(x) and abs(x - y) <= 3e-6 * scale):
            return False
    return True


def main():
    assert COUNT == probe.probe_mech_net_rx_count(), (COUNT, probe.probe_mech_net_rx_count())
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(8)
    fails = cases = 0
    modes = {}
    for n in range(3000):
        v = case(rng)
        theirs = exe.run(v)
        mine_v = list(v)
        if brk == 1 and v[K['mode']] == 0 and v[K['len']] > 0x24:
            b = bytearray(struct.pack('<64i', *v[K['msg']:K['msg'] + 64]))
            b[0x24] = (b[0x24] + 1) & 0xff
            mine_v[K['msg']:K['msg'] + 64] = struct.unpack('<64i', b)
        arr = (ctypes.c_int32 * COUNT)(*mine_v)
        out = (ctypes.c_int32 * 4096)()
        k = probe.probe_mech_net_apply(arr, out, 4096)
        mine = list(out[:k])
        cases += 1
        modes[v[K['mode']]] = modes.get(v[K['mode']], 0) + 1
        if not same(mine, theirs):
            fails += 1
            if fails <= 2:
                i = next((j for j, (a, b) in enumerate(zip(mine, theirs)) if not same([a], [b]) and j not in {q + k for q, t in enumerate(theirs) if t == 11 for k in (1, 2, 3)}),
                         min(len(mine), len(theirs)))
                print(f'FAIL case {n} mode {v[K["mode"]]} at {i}:\n  port     {mine[max(0, i - 6):i + 8]}\n'
                      f'  original {theirs[max(0, i - 6):i + 8]}')
    # The fixed path: the port with bug fixes on against the exe given the latency in milliseconds.
    probe.probe_mech_net_bugs(1)
    fixed = changed = 0
    rng = random.Random(18)
    for n in range(800):
        v = case(rng)
        v[K['mode']] = 0
        if not v[K['len']] or v[K['len']] < 0x3c:
            continue
        v[K['latency']] = bits(rng.choice([0.05, 0.1, 0.3]))
        ms = list(v)
        ms[K['latency']] = bits(fl(v[K['latency']]) * 1000.0)
        theirs = exe.run(ms)
        theirs[-10] = v[K['latency']]  # the latency kept (before the ping words and the 92 block) is the port's seconds
        original = exe.run(v)
        arr = (ctypes.c_int32 * COUNT)(*v)
        out = (ctypes.c_int32 * 4096)()
        k = probe.probe_mech_net_apply(arr, out, 4096)
        mine = list(out[:k])
        fixed += 1
        changed += theirs != original
        if not same(mine, theirs):
            fails += 1
            if fails <= 2:
                i = next((j for j, (a, b) in enumerate(zip(mine, theirs)) if not same([a], [b])), None)
                print(f'FAIL fixed path case {n} at {i}: port {mine[max(0, (i or 0) - 4):(i or 0) + 5]} '
                      f'original {theirs[max(0, (i or 0) - 4):(i or 0) + 5]}')
    probe.probe_mech_net_bugs(0)
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    assert changed > 0, 'the fix never changed an aim'
    print(f'ok: fixed path: {fixed} applies as the exe with the latency in ms ({changed} aims carried on further)')
    print(f'ok: {cases} cases identical (applies {modes.get(0, 0)}, reckonings {modes.get(1, 0)}, '
          f'pings {modes.get(2, 0)}, sends done {modes.get(3, 0)})')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
