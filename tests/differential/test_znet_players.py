"""zNetwork's system events' translation 0x588a70 against the original x86
(docs/multiplayer-port.md, piece 3): random sequences of DirectPlay system
messages (players and teams made, destroyed, joined to and taken out of a
team, their data and names changed, the host handed over, a send done, the
session lost), built as the DPMSG structs the exe reads, through the exe's
own translation with its lists (0x45b800, 0x49ce90), the dispatcher and
logging handlers for the local types 2..0xe, a stand-in GetPlayerCaps (+0x4c)
and SendEx (+0xc4); compared with the port's: each local message (type,
size, sender), each send, and the players' and teams' lists after.

    cmake --build build && .venv/bin/python tests/differential/test_znet_players.py

BREAK=1 makes the port's caps say nobody is the host, BREAK=2 its events
lose their team; each must fail.
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from test_weiler import Emulated, probe  # noqa: E402

probe.probe_znet_events.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int32, ctypes.c_uint32, ctypes.c_uint32,
                                    ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_znet_events.restype = ctypes.c_int32

HLIST, HCOUNT = 0x730794, 0x730798
PLAYERS, PCOUNT, TEAMS, TCOUNT = 0x7307ac, 0x7307b0, 0x7307bc, 0x7307c0
INITIALISED, OPEN, DPLAY, LOCAL, HOST, ASYNC, DESC = 0x7306c8, 0x730764, 0x730724, 0x73075c, 0x730760, 0x730765, 0x730730
NEW, DELETE, IAT_STRNCPY, LOCALIZE = 0x5abd26, 0x5abd1a, 0x5bb648, 0x575fb0
CREATE, DESTROY, ADD, DELETE_FROM, LOST, BECAME_HOST, SETDATA, SETNAME, SENDDONE = 3, 5, 7, 0x21, 0x31, 0x101, 0x102, 0x103, 0x10d


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_function(NEW, lambda c: self.emu.alloc(c.arg(0)))
        cpu.stub_function(DELETE, lambda c: 0)
        cpu.stub_function(LOCALIZE, lambda c: c.ecx)  # the name itself, as the port's with no table
        cpu.stub_import(IAT_STRNCPY, self.strncpy)
        self.handler = cpu.code(b'\x90' * 16)
        cpu.stub_function(self.handler, self.on_message)
        self.caps = cpu.code(b'\x90' * 16)
        cpu.stub_function(self.caps, self.get_caps, 16)
        self.sendex = cpu.code(b'\x90' * 16)
        cpu.stub_function(self.sendex, self.on_send, 40)

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

    def on_message(self, c):
        t, sz = struct.unpack('<HH', c.read(c.edx, 4))
        self.log += [1, t, sz, c.ecx]
        return 0

    def get_caps(self, c):
        c.u32(c.arg(2) + 4, 2 if c.arg(1) in self.hosts else 0)
        return 0

    def on_send(self, c):
        t = struct.unpack('<H', c.read(c.arg(4), 2))[0]
        self.log += [2, c.arg(2), c.arg(3) & 1, c.arg(5), t]
        return 0

    def sentinel(self, at):
        head = self.emu.alloc(12)
        self.cpu.u32(head, head)
        self.cpu.u32(head + 4, head)
        self.cpu.u32(at, head)
        self.cpu.u32(at + 4, 0)

    def name(self, text):
        return self.emu.put(text.encode() + b'\0')

    def run(self, events, local_id, host_id):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.log = []
        self.hosts = {e[2] for e in events if e[5]}
        for at in (HLIST, PLAYERS, TEAMS):
            self.sentinel(at)
        vtable = emu.alloc(0x100)
        obj = emu.alloc(16)
        cpu.u32(vtable + 0x4c, self.caps)
        cpu.u32(vtable + 0xc4, self.sendex)
        cpu.u32(obj, vtable)
        cpu.u32(DPLAY, obj)
        cpu.write(INITIALISED, b'\x01')
        cpu.write(OPEN, b'\x01')
        cpu.write(ASYNC, b'\x00')
        rec = emu.alloc(0xec)
        cpu.u32(rec + 4, local_id)
        cpu.u32(LOCAL, rec)
        cpu.u32(HOST, host_id)
        cpu.u32(DESC, emu.alloc(0x50))
        for t in range(2, 0xf):
            cpu.call(0x587aa0, ecx=t, edx=self.handler, stack_args=(2,))
        for kind, ptype, ident, group, num, _ in events:
            m = emu.alloc(0x40)
            if kind == CREATE:
                short, long_ = self.name(f'P{num}'), self.name(f'Player {num}')
                cpu.write(m, struct.pack('<IIIIII', kind, ptype, ident, 1, 0, 0) +
                          struct.pack('<IIII', 0x10, 0, short, long_) + struct.pack('<II', 0, 0))
            elif kind in (ADD, DELETE_FROM):
                cpu.write(m, struct.pack('<III', kind, group, ident))
            else:
                cpu.write(m, struct.pack('<III', kind, ptype, ident))
            cpu.call(0x588a70, ecx=m)
        out = list(self.log)
        players = self.walk(PLAYERS)
        out += [3, len(players)]
        for r in players:
            short = cpu.read(r + 0x44, 0x50).split(b'\0')[0].decode()
            long_ = cpu.read(r + 0x94, 0x50).split(b'\0')[0].decode()
            out += [emu.u32(r + 4), cpu.read(r, 1)[0], emu.u32(r + 0xe4), int(short[1:]), int(long_[7:])]
        teams = self.walk(TEAMS)
        out += [4, len(teams)]
        for t in teams:
            out += [emu.u32(t + 0x50), int(cpu.read(t, 0x50).split(b'\0')[0].decode()[1:])]
        out.append(cpu.read(rec, 1)[0])
        return out

    def walk(self, at):
        head = self.emu.u32(at)
        out, node = [], self.emu.u32(head)
        while node != head:
            out.append(self.emu.u32(node + 8))
            node = self.emu.u32(node)
        return out


def event(rng, known_players, known_teams, local_id):
    r = rng.random()
    pid = rng.choice(known_players + [rng.randrange(1, 9)]) if known_players else rng.randrange(1, 9)
    tid = rng.choice(known_teams + [rng.randrange(100, 104)]) if known_teams else rng.randrange(100, 104)
    num = rng.randrange(1, 99)
    if r < 0.3:
        return (CREATE, 1, pid, 0, num, int(rng.random() < 0.2))
    if r < 0.4:
        return (CREATE, 2, tid, 0, num, 0)
    if r < 0.5:
        return (ADD, 0, pid, tid, 0, 0)
    if r < 0.57:
        return (DELETE_FROM, 0, pid, tid, 0, 0)
    if r < 0.67:
        return (DESTROY, 1, rng.choice((pid, local_id)), 0, 0, 0)
    if r < 0.72:
        return (DESTROY, 2, tid, 0, 0, 0)
    if r < 0.78:
        return (SETDATA, rng.choice((1, 2)), rng.choice((pid, tid)), 0, 0, 0)
    if r < 0.82:
        return (SETNAME, 1, pid, 0, 0, 0)
    if r < 0.88:
        return (SENDDONE, 0, 0, 0, 0, 0)
    if r < 0.94:
        return (BECAME_HOST, 0, 0, 0, 0, 0)
    return (LOST, 0, 0, 0, 0, 0)


def main():
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    rng = random.Random(2026)
    fails = cases = msgs = 0
    for case in range(250):
        local_id = rng.randrange(1, 9)
        host_id = rng.choice((local_id, rng.randrange(1, 9)))
        events, players, teams = [], [], []
        for _ in range(rng.randint(1, 25)):
            e = event(rng, players, teams, local_id)
            events.append(e)
            if e[0] == CREATE:
                (players if e[1] == 1 else teams).append(e[2])
        theirs = exe.run(events, local_id, host_id)
        port_events = [(k, t, i, (0 if brk == 2 else g), n, (0 if brk == 1 else h)) for k, t, i, g, n, h in events]
        flat = (ctypes.c_int32 * (6 * len(events)))(*[v for e in port_events for v in e])
        out = (ctypes.c_int32 * 8192)()
        n = probe.probe_znet_events(flat, len(events), local_id, host_id, out, 8192)
        mine = list(out[:n])
        # 0xe carries the send's id and time here, its pointer there: 12 bytes, not 8.
        i = 0
        while i < len(mine) and mine[i] in (1, 2):
            if mine[i] == 1 and mine[i + 1] == 0xe and mine[i + 2] == 12:
                mine[i + 2] = 8
            i += 4 if mine[i] == 1 else 5
        msgs += theirs.count(1)
        cases += 1
        if mine != theirs:
            fails += 1
            if fails <= 4:
                k = next((j for j in range(min(len(mine), len(theirs))) if mine[j] != theirs[j]), min(len(mine), len(theirs)))
                print(f'FAIL case {case} local {local_id} host {host_id} at {k} of {len(theirs)}:\n  port     {mine[max(0, k - 12):k + 16]}\n'
                      f'  original {theirs[max(0, k - 12):k + 16]}\n  events {events}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} event sequences identical (their local messages, sends and lists)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
