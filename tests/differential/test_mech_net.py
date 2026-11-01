"""The mission's network side against the original x86
(docs/multiplayer-port.md, piece 5a): random scripts of the start 0x46ceb0,
the end 0x46d100, messages through the in-game router 0x46d880 and players
leaving 0x46d3b0, with the remote mechs' list [0x605f84] carried from one
step to the next. What the exe calls out to is stood in for and logged:
zNetwork's registrations (the handlers left after an end), the handlers
the router picks with what they are given, the creation 0x46d450 (a mech
made or not), the new mech's position (vt+0x18), timeGetTime and the dead
reckoning (vt+0x158), the HUD line, the sensors' forget, the mech's
delete, the lobby slot, the player data cleared, the pilot's mech, the
force feedback, the configs freed. Compared with the port's net_start,
net_end, net_route and net_player_left through hooks that log the same;
after each step the list (owners and the time each was heard) and, after
starts and ends, the flag, the crater veto, the anim log's hook and tag.

    cmake --build build && .venv/bin/python tests/differential/test_mech_net.py

BREAK=1 makes the probe's created mech another player's (the first state
is then not applied); BREAK=2 gives the port another local id (the end
clears the wrong player's data). Each must fail.
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from test_weiler import Emulated, probe  # noqa: E402

probe.probe_mech_net.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int32, ctypes.POINTER(ctypes.c_int32),
                                 ctypes.c_int32]
probe.probe_mech_net.restype = ctypes.c_int32

MECHS, MECHS_N, MISSING, MISSING_N = 0x605f84, 0x605f88, 0x605f94, 0x605f98
CONFIGS, RUNNING, VETO, ANIM_NET, ANIM_MASK = 0x605f74, 0x605fb4, 0x60bf88, 0x60c714, 0x60c718
GAIN, DAMP, PILOT, FF, DT, IAT_TIME = 0x5e55f0, 0x5e55ec, 0x60861c, 0x608620, 0x72c52c, 0x5bb118
DELETE = 0x5abd1a
HANDLER_CODES = {0x46d880: 1, 0x46d3b0: 2, 0x4d72a0: 3, 0x4709d0: 4}
# The router's handlers: code, what they are given, the bytes they pop.
ROUTES = {0x46e4d0: (1, 'm', 4), 0x470a00: (2, 'm', 4), 0x470740: (3, 'msg', 0), 0x46ed30: (4, 'm', 4),
          0x46eba0: (5, 'msg', 0), 0x4708e0: (6, 'm', 4), 0x58b380: (7, 'm', 4), 0x46ed10: (8, 'm', 4),
          0x4705b0: (9, 'arg', 8), 0x46ff50: (10, 'm', 4), 0x46ffd0: (11, 'arg', 8), 0x470490: (12, 'chat', 8),
          0x46fff0: (13, 'm', 4), 0x46fde0: (14, 'msg', 0)}


def bits(f):
    return struct.unpack('<i', struct.pack('<f', f))[0]


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_function(DELETE, lambda c: 0)
        cpu.stub_import(IAT_TIME, self.time)
        for a, (code, kind, pop) in ROUTES.items():
            cpu.stub_function(a, self.route(code, kind), pop)
        stubs = {0x587aa0: (self.on, 4), 0x587b20: (self.off, 0), 0x46d450: (self.create, 0),
                 0x588a60: (self.name, 0), 0x40fe70: (self.hud, 0), 0x4aa6b0: (self.forget, 0),
                 0x479480: (lambda c: self.game, 0), 0x495b10: (self.slot_of, 4), 0x41e580: (self.log_(70), 0),
                 0x586d10: (lambda c: self.local, 0), 0x589830: (self.set_data, 4), 0x433530: (self.log_(72), 0),
                 0x424350: (self.config_free, 0)}
        for a, (fn, pop) in stubs.items():
            cpu.stub_function(a, fn, pop)
        self.vt_code = {}
        for off, fn in ((0, self.free), (0x18, self.set_position), (0x158, self.reckon)):
            a = cpu.code(b'\x90' * 16)
            cpu.stub_function(a, fn, 4)
            self.vt_code[off] = a

    # --- the stand-ins ---------------------------------------------------
    def log_(self, code):
        def run(c):
            self.log.append(code)
            return 0
        return run

    def owner(self, m):
        return s32(self.emu.u32(m + 0x6fc)) if m else -1

    def time(self, c):
        self.clock += 1
        return self.clock

    def route(self, code, kind):
        def run(c):
            if kind == 'msg':
                self.log += [code, struct.unpack('<H', c.read(c.ecx, 2))[0]]
            elif kind == 'arg':
                self.log += [code, self.owner(c.arg(0))]
            elif kind == 'chat':
                self.log += [code, self.owner(c.ecx), c.arg(0)]
            else:
                self.log += [code, self.owner(c.ecx)]
            return 0
        return run

    def on(self, c):
        self.log += [60, c.ecx, HANDLER_CODES[c.edx], c.arg(0)]
        self.handlers.append((c.ecx, c.edx))
        return 1

    def off(self, c):
        self.handlers = [h for h in self.handlers if h != (c.ecx, c.edx)]
        return 1

    def nodes(self, head_at):
        head = self.emu.u32(head_at)
        out, n = [], self.emu.u32(head)
        while n != head:
            out.append(n)
            n = self.emu.u32(n)
        return out

    def push_back(self, head_at, count_at, value):
        cpu, head = self.cpu, self.emu.u32(head_at)
        last = self.emu.u32(head + 4)
        node = self.emu.alloc(12)
        cpu.u32(node, head)
        cpu.u32(node + 4, last)
        cpu.u32(node + 8, value)
        cpu.u32(last, node)
        cpu.u32(head + 4, node)
        cpu.u32(count_at, self.emu.u32(count_at) + 1)

    def create(self, c):
        self.log += [15, c.ecx]
        if self.create_ok:
            m = self.emu.alloc(0x800)
            self.cpu.u32(m, self.vtable)
            self.cpu.u32(m + 0x6fc, c.ecx)
            self.push_back(MECHS, MECHS_N, m)
        return 0

    def free(self, c):  # vt+0 (1): the destructor's walk of the list, by value
        m = c.ecx
        self.log += [40, self.owner(m)]
        for n in self.nodes(MECHS):
            if self.emu.u32(n + 8) == m:
                nxt, prv = self.emu.u32(n), self.emu.u32(n + 4)
                self.cpu.u32(prv, nxt)
                self.cpu.u32(nxt + 4, prv)
                self.cpu.u32(MECHS_N, self.emu.u32(MECHS_N) - 1)
        return 0

    def set_position(self, c):
        p = c.arg(0)
        self.log += [41, self.owner(c.ecx)] + list(struct.unpack('<3i', c.read(p, 12)))
        return 0

    def reckon(self, c):
        self.log += [42, self.owner(c.ecx), s32(c.arg(0))]
        return 0

    def name(self, c):
        return self.names[c.ecx]

    def hud(self, c):
        s = self.cpu.read(c.arg(2), 8).split(b'\0')[0].decode()
        self.log += [50, s32(c.arg(0)), c.arg(1), int(s[1:]) if s else -1]
        return 0

    def forget(self, c):
        self.log += [51, self.owner(c.ecx)]
        return 0

    def slot_of(self, c):
        assert c.ecx == self.game + 0x48
        self.log += [52, c.arg(0)]
        self.slot_asked = True
        return self.slot if self.slot_ok else 0

    def set_data(self, c):
        self.log += [71, c.ecx, 1 if c.edx else 0, c.arg(0)]
        return 1

    def config_free(self, c):
        self.log += [73, c.ecx]
        return 0

    # --- a script ---------------------------------------------------------
    def sentinel(self, at, count_at):
        head = self.emu.alloc(12)
        self.cpu.u32(head, head)
        self.cpu.u32(head + 4, head)
        self.cpu.u32(at, head)
        self.cpu.u32(count_at, 0)

    def run(self, dt_bits, local, ops):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.log, self.handlers, self.clock, self.local = [], [], 1000, local
        self.sentinel(MECHS, MECHS_N)
        self.sentinel(MISSING, MISSING_N)
        for a in (CONFIGS, CONFIGS + 4, CONFIGS + 8, VETO, ANIM_NET, ANIM_MASK):
            cpu.u32(a, 0)
        cpu.write(RUNNING, b'\0')
        cpu.u32(GAIN, bits(-6.5))
        cpu.u32(DAMP, bits(-3.5))
        cpu.u32(DT, dt_bits)
        self.vtable = emu.alloc(0x200)
        for off, a in self.vt_code.items():
            cpu.u32(self.vtable + off, a)
        self.game, self.slot = emu.alloc(0x100), emu.alloc(0x40)
        pilot = emu.alloc(0x100)
        cpu.u32(PILOT, pilot)
        self.names = {i: emu.put(f'P{i}'.encode() + b'\0') for i in range(1, 9)}
        for o in ops:
            kind = o[0]
            if kind == 1:
                self.create_ok = o[3]
                msg = emu.alloc(0x40)
                cpu.write(msg, struct.pack('<HH3i', o[2], 0x40, o[4], o[5], o[6]))
                cpu.call(0x46d880, ecx=o[1], edx=msg)
            elif kind == 2:
                self.slot_ok, self.slot_asked = o[2], False
                cpu.write(self.slot + 0x18, b'\0')
                msg = emu.put(bytes([6, 0, 8, 0, 0, 0, 0, 0]))
                cpu.call(0x46d3b0, ecx=o[1], edx=msg)
                if self.slot_ok and self.slot_asked:
                    self.log += [53, cpu.read(self.slot + 0x18, 1)[0]]
            elif kind == 3:
                for _ in range(o[1]):
                    self.push_back(MISSING, MISSING_N, 0)
                was = cpu.read(RUNNING, 1)[0]
                for off in (0x7c, 0x80, 0x84):
                    cpu.u32(self.game + off, 5)
                cpu.call(0x46ceb0)
                if not was:
                    self.log += [80] + [emu.u32(self.game + off) for off in (0x7c, 0x80, 0x84)]
                    self.log += [79, s32(emu.u32(GAIN)), s32(emu.u32(DAMP))]
                self.log += [75, emu.u32(MISSING_N)]
            elif kind == 4:
                cpu.u32(pilot + 0x14, emu.alloc(16) if o[1] else 0)
                cpu.u32(FF, emu.alloc(16) if o[2] else 0)
                arr = emu.alloc(4 * max(o[3], 1))
                for k in range(o[3]):
                    cpu.u32(arr + 4 * k, 0 if (o[4] >> k) & 1 else k + 1)
                cpu.u32(CONFIGS, arr)
                cpu.u32(CONFIGS + 4, arr + 4 * o[3])
                cpu.u32(CONFIGS + 8, arr + 4 * o[3])
                for _ in range(o[5]):
                    self.push_back(MISSING, MISSING_N, 0)
                cpu.call(0x46d100)
                n = (emu.u32(CONFIGS + 4) - emu.u32(CONFIGS)) // 4
                self.log += [62, len(self.handlers), 74, n, 75, emu.u32(MISSING_N)]
                cpu.u32(CONFIGS + 4, emu.u32(CONFIGS))
            if kind >= 3:
                v, a = emu.u32(VETO), emu.u32(ANIM_NET)
                self.log += [76, 1 if v == 0x46fd30 else 2 if v else 0, 77, cpu.read(RUNNING, 1)[0],
                             78, 1 if a == 0x46feb0 else 2 if a else 0, s32(emu.u32(ANIM_MASK))]
            mechs = [emu.u32(n + 8) for n in self.nodes(MECHS)]
            assert len(mechs) == emu.u32(MECHS_N)
            self.log += [99, len(mechs)]
            for m in mechs:
                self.log += [self.owner(m), s32(emu.u32(m + 0x73c))]
        return self.log


def script(rng):
    ops = []
    for _ in range(rng.randrange(8, 40)):
        r = rng.random()
        if r < 0.62:
            t = 0xf if rng.random() < 0.35 else rng.randrange(0xc, 0x23)
            ops.append([1, rng.randrange(1, 7), t, int(rng.random() < 0.8)] +
                       [bits(rng.uniform(-500, 500)) for _ in range(3)])
        elif r < 0.77:
            ops.append([2, rng.randrange(1, 7), int(rng.random() < 0.6), 0, 0, 0, 0])
        elif r < 0.89:
            ops.append([3, rng.randrange(0, 3), 0, 0, 0, 0, 0])
        else:
            ops.append([4, rng.randrange(2), rng.randrange(2), rng.randrange(0, 4), rng.randrange(16),
                        rng.randrange(0, 3), 0])
    return ops


def main():
    exe = Exe()
    rng = random.Random(5)
    fails = cases = 0
    for case in range(400):
        dt_bits, local = bits(rng.uniform(0.005, 0.2)), rng.randrange(1, 7)
        ops = script(rng)
        theirs = exe.run(dt_bits, local, ops)
        flat = [dt_bits, local] + [x for o in ops for x in o]
        arr = (ctypes.c_int32 * len(flat))(*flat)
        out = (ctypes.c_int32 * 8192)()
        n = probe.probe_mech_net(arr, len(flat), out, 8192)
        mine = list(out[:n])
        cases += 1
        if mine != theirs:
            fails += 1
            if fails <= 2:
                i = next((k for k, (a, b) in enumerate(zip(mine, theirs)) if a != b), min(len(mine), len(theirs)))
                print(f'FAIL case {case} at {i}:\n  port     {mine[max(0, i - 12):i + 12]}\n'
                      f'  original {theirs[max(0, i - 12):i + 12]}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} scripts identical (starts, ends, routed messages, players leaving)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
