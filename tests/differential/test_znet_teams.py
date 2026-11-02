"""zNetwork's teams' and data's calls against the original x86
(docs/multiplayer-port.md, piece 3b): random sequences of 0x588390 (create a
team), 0x5884c0 / 0x588500 (a player into / out of a team), 0x588590 (destroy
a team), 0x5886a0 / 0x588660 (kick, leave), 0x589830 / 0x589770 / 0x589750
(a player's data set, got, its size), 0x5897b0 / 0x5897f0 (a team's), and the
enumerations 0x5885c0, 0x588200, 0x588300 with their callbacks 0x5893f0,
0x589300, 0x5892c0, through a stand-in IDirectPlay4A that answers as told
(failures included: the error's name as local message 0xc), compared with the
port's through a transport told the same: every DirectPlay call and its
arguments, every answer, every error message, the lists after.

The enumerations' callbacks are the exe's own, called once per member after
the stand-in Enum* returns (Unicorn cannot run the exe from inside a stub).

    cmake --build build && .venv/bin/python tests/differential/test_znet_teams.py

BREAK=1 makes the port's team enumeration take the short name, BREAK=2 its
removal keep the player's team; each must fail.
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

probe.probe_znet_calls.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int32, ctypes.POINTER(ctypes.c_int32),
                                   ctypes.c_int32]
probe.probe_znet_calls.restype = ctypes.c_int32

HLIST, PLAYERS, TEAMS = 0x730794, 0x7307ac, 0x7307bc
INITIALISED, OPEN, DPLAY, LOCAL, HOST = 0x7306c8, 0x730764, 0x730724, 0x73075c, 0x730760
NEW, DELETE, IAT_STRNCPY, LOCALIZE = 0x5abd26, 0x5abd1a, 0x5bb648, 0x575fb0
DPERR_GENERIC, DPERR_BUFFERTOOSMALL = 0x80004005, 0x8877001E
SLOTS = {0x14: ('create', 6), 0x0c: ('add', 3), 0x1c: ('delete', 3), 0x20: ('destroy', 2), 0x24: ('kill', 2),
         0x74: ('setp', 5), 0x6c: ('setg', 5), 0x50: ('getp', 5), 0x3c: ('getg', 5), 0x30: ('enump', 5),
         0x2c: ('enumg', 5), 0x28: ('enumgp', 6), 0x4c: ('caps', 4)}


def number(s):
    return int(s[7:]) if s.startswith('Player ') else int(s[1:])


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_function(NEW, lambda c: self.emu.alloc(c.arg(0)))
        cpu.stub_function(DELETE, lambda c: 0)
        cpu.stub_function(LOCALIZE, lambda c: c.ecx)
        cpu.stub_import(IAT_STRNCPY, self.strncpy)
        self.slot_code = {}
        for off, (name, nargs) in SLOTS.items():
            a = cpu.code(b'\x90' * 16)
            cpu.stub_function(a, self.slot(name), 4 * nargs)
            self.slot_code[off] = a
        self.handler = cpu.code(b'\x90' * 16)
        cpu.stub_function(self.handler, self.on_message)

    def strncpy(self, c):
        dst, src, n = c.arg(0), c.arg(1), c.arg(2)
        out = bytearray()
        while len(out) < n:
            b = c.read(src + len(out), 1)[0]
            if b == 0:
                break
            out.append(b)
        c.write(dst, bytes(out) + bytes(n - len(out)))
        return dst

    def cstr(self, p):
        return self.cpu.read(p, 0x60).split(b'\0')[0].decode()

    def slot(self, name):
        def run(c):
            a = [c.arg(i) for i in range(1, 6)]
            ok = 0 if self.ok else DPERR_GENERIC
            if name == 'create':
                self.log += [10, number(self.cstr(self.emu.u32(a[1] + 8)))]
                c.u32(a[0], self.id)
                return 0 if self.id else DPERR_GENERIC
            if name == 'add':
                self.log += [11, a[0], a[1]]
                return ok
            if name == 'delete':
                self.log += [12, a[0], a[1]]
                return ok
            if name == 'destroy':
                self.log += [13, a[0]]
                return ok
            if name == 'kill':
                self.log += [14, a[0]]
                return ok
            if name in ('setp', 'setg'):
                self.log += [15 if name == 'setp' else 16, a[0], a[2]]
                return ok
            if name in ('getp', 'getg'):
                have = self.emu.u32(a[2])
                self.log += [17 if name == 'getp' else 18, a[0], 1 if a[1] else 0, have]
                c.u32(a[2], self.size)
                return 0 if a[1] and have >= self.size else DPERR_BUFFERTOOSMALL
            if name == 'caps':
                c.u32(a[1] + 4, 0)
                return 0
            return 0  # the enumerations: their callbacks come after
        return run

    def on_message(self, c):
        t, sz = struct.unpack('<HH', c.read(c.edx, 4))
        self.log += [30, sz - 5]
        return 0

    def sentinel(self, at):
        head = self.emu.alloc(12)
        self.cpu.u32(head, head)
        self.cpu.u32(head + 4, head)
        self.cpu.u32(at, head)
        self.cpu.u32(at + 4, 0)

    def dpname(self, short, long_):
        n = self.emu.alloc(16)
        self.cpu.write(n, struct.pack('<IIII', 0x10, 0, self.emu.put(short.encode() + b'\0'),
                                      self.emu.put(long_.encode() + b'\0')))
        return n

    def eax(self):
        return self.cpu.uc.reg_read(UC_X86_REG_EAX)

    def run(self, ops):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.log = []
        for at in (HLIST, PLAYERS, TEAMS):
            self.sentinel(at)
        vtable = emu.alloc(0x100)
        obj = emu.alloc(16)
        for off, a in self.slot_code.items():
            cpu.u32(vtable + off, a)
        cpu.u32(obj, vtable)
        cpu.u32(DPLAY, obj)
        cpu.write(INITIALISED, b'\x01')
        cpu.write(OPEN, b'\x01')
        rec = emu.alloc(0xec)
        cpu.u32(rec + 4, 1)
        cpu.u32(LOCAL, rec)
        cpu.call(0x587aa0, ecx=0xc, edx=self.handler, stack_args=(2,))
        buf = emu.alloc(64)
        for op, a, b, c in ops:
            self.ok, self.size = c, 0
            if op == 0:
                self.id = b
                cpu.call(0x588390, ecx=emu.put(f'P{a}'.encode() + b'\0'))
                r = self.eax()
            elif op == 1:
                cpu.call(0x5884c0, ecx=a, edx=b)
                r = self.eax() & 0xff
            elif op == 2:
                cpu.call(0x588500, ecx=a, edx=b)
                r = self.eax() & 0xff
            elif op == 3:
                cpu.call(0x588590, ecx=a)
                r = self.eax() & 0xff
            elif op == 4:
                cpu.call(0x5886a0, ecx=a)
                r = self.eax() & 0xff
            elif op == 5:
                cpu.call(0x588660)
                r = self.eax() & 0xff
            elif op in (6, 9):
                cpu.call(0x589830 if op == 6 else 0x5897b0, ecx=a, edx=buf, stack_args=(b,))
                r = self.eax() & 0xff
            elif op in (7, 10):
                self.size = c
                size = emu.alloc(4)
                cpu.u32(size, b)
                if op == 7:
                    cpu.call(0x589770, ecx=a, edx=buf if b else 0, stack_args=(size,))
                    got = emu.u32(size)
                else:  # 0x5897f0 takes the size itself on the stack and passes its address
                    self.size_probe = True
                    cpu.call(0x5897f0, ecx=a, edx=buf if b else 0, stack_args=(b,))
                    got = self.size  # written into the exe's own argument slot: what the stand-in said
                r = self.eax() & 0xff
                self.log += [21, got]
            elif op == 8:
                self.size = c
                cpu.call(0x589750, ecx=a)
                r = self.eax()
            elif op == 11:
                for i in range(b):
                    cpu.call(0x5893f0, stack_args=(a + i, 1, self.dpname(f'P{a + i + 100}', f'Player {a + i + 100}'), 0, 0))
                cpu.call(0x5885c0)
                r = self.eax()
            elif op == 12:
                cpu.call(0x588200)
                for i in range(b):
                    cpu.call(0x589300, stack_args=(a + i, 0, self.dpname(f'P{a + i}', f'Player {a + i + 50}'), 0, 0))
                r = emu.u32(TEAMS + 4)  # 0x588200's answer once its callbacks have run: the teams' count
            elif op == 13:
                cpu.call(0x588300)
                ctx = emu.alloc(4)
                cpu.u32(ctx, c)
                head, node = emu.u32(TEAMS), emu.u32(emu.u32(TEAMS))
                teams = []
                while node != head:
                    teams.append(emu.u32(emu.u32(node + 8) + 0x50))
                    node = emu.u32(node)
                if c in teams:
                    for i in range(b):
                        cpu.call(0x5892c0, stack_args=(a + i, 1, 0, 0, ctx))
                r = 1
            self.log += [20, r]
        out = list(self.log)
        players = self.walk(PLAYERS)
        out += [3, len(players)]
        for p in players:
            out += [emu.u32(p + 4), cpu.read(p, 1)[0], emu.u32(p + 0xe4), number(self.cstr(p + 0x44)),
                    number(self.cstr(p + 0x94))]
        teams = self.walk(TEAMS)
        out += [4, len(teams)]
        for t in teams:
            out += [emu.u32(t + 0x50), number(self.cstr(t))]
        return out

    def walk(self, at):
        head = self.emu.u32(at)
        out, node = [], self.emu.u32(head)
        while node != head:
            out.append(self.emu.u32(node + 8))
            node = self.emu.u32(node)
        return out


def main():
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(31)
    fails = cases = 0
    for case in range(250):
        # Players 2..5, teams 200 and 201, players 2..4 in team 200: removals then act on someone.
        ops = [(11, 2, 4, 0), (12, 200, 2, 0), (13, 2, 3, 200)]
        for _ in range(rng.randint(1, 20)):
            op = rng.choice(list(range(14)))
            ok = int(rng.random() < 0.8)
            a, b = rng.choice((2, 3, 4, 200, 201, 300)), rng.choice((2, 3, 4, 200, 201))
            if op == 0:
                ops.append((0, rng.randrange(1, 90), rng.choice((0, 300 + case % 7)), 1))
            elif op in (7, 10):
                ops.append((op, a, rng.choice((0, 8, 32)), rng.choice((0, 8, 16))))
            elif op == 8:
                ops.append((8, a, 0, rng.choice((0, 12))))
            elif op == 11:
                ops.append((11, rng.randrange(2, 8), rng.randint(0, 3), 0))
            elif op == 12:
                ops.append((12, rng.choice((200, 210)), rng.randint(0, 3), 0))
            elif op == 13:
                ops.append((13, rng.randrange(2, 6), rng.randint(0, 3), rng.choice((200, 201, 210))))
            else:
                ops.append((op, a, b if op in (1, 2) else rng.choice((0, 8)), ok))
        theirs = exe.run(ops)
        if brk:
            os.environ['MW3_PROBE_BREAK'] = str(brk)
        flat = (ctypes.c_int32 * (4 * len(ops)))(*[v for o in ops for v in o])
        out = (ctypes.c_int32 * 16384)()
        n = probe.probe_znet_calls(flat, len(ops), out, 16384)
        mine = list(out[:n])
        cases += 1
        if mine != theirs:
            fails += 1
            if fails <= 3:
                k = next((j for j in range(min(len(mine), len(theirs))) if mine[j] != theirs[j]),
                         min(len(mine), len(theirs)))
                print(f'FAIL case {case} at {k} of {len(theirs)}:\n  port     {mine[max(0, k - 10):k + 14]}\n'
                      f'  original {theirs[max(0, k - 10):k + 14]}\n  ops {ops}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} call sequences identical (the DirectPlay calls, answers, errors and lists)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
