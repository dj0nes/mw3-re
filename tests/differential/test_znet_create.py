"""The local player made, 0x5886c0, against the original x86
(docs/multiplayer-port.md, piece 4): with 0..4 other players already listed
(put there by the exe's own players' callback 0x5893f0), a name or none
("noname"), a new id from a stand-in CreatePlayer (+0x18) and caps that say
host or not (+0x4c); the stand-in's Enum* (+0x30, +0x2c, +0x28) find nothing
new and its Receive (+0x64) nothing waiting. Compared with the port's
znet::create_player: the name CreatePlayer was given, the sends (the host's
0xd), the answer, the local player and its host byte, the host id, the
players' list.

    cmake --build build && .venv/bin/python tests/differential/test_znet_create.py

BREAK=1 makes the port's local player never the host; it must fail.
"""
import ctypes
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from test_znet_teams import Exe as TeamsExe, number, PLAYERS, TEAMS, HLIST  # noqa: E402
from test_znet_teams import INITIALISED, OPEN, DPLAY, LOCAL, HOST  # noqa: E402
from test_weiler import probe  # noqa: E402

probe.probe_znet_create.argtypes = [ctypes.c_int32, ctypes.c_uint32, ctypes.c_int32, ctypes.c_int32,
                                    ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_znet_create.restype = ctypes.c_int32
ASYNC = 0x730765
DPERR_NOMESSAGES = 0x887700BE


class Exe(TeamsExe):
    def __init__(self):
        super().__init__()
        cpu = self.cpu
        self.extra = {}
        for off, fn, nargs in ((0x18, self.create, 7), (0x64, self.receive, 6), (0xc4, self.sendex, 10),
                               (0x4c, self.caps, 4)):
            a = cpu.code(b'\x90' * 16)
            cpu.stub_function(a, fn, 4 * nargs)
            self.extra[off] = a

    def create(self, c):
        name = self.emu.u32(c.arg(2) + 8)
        s = self.cstr(name)
        self.log += [10, -1 if s == 'noname' else number(s)]
        c.u32(c.arg(1), self.new_id)
        return 0

    def receive(self, c):
        return DPERR_NOMESSAGES

    def sendex(self, c):
        t = struct.unpack('<H', c.read(c.arg(4), 2))[0]
        self.log += [2, c.arg(2), c.arg(3) & 1, c.arg(5), t]
        return 0

    def caps(self, c):
        c.u32(c.arg(2) + 4, 2 if (self.host_caps and c.arg(1) == self.new_id) else 0)
        return 0

    def make(self, others, new_id, host_caps, name):
        cpu, emu = self.cpu, self.emu
        emu.begin()
        self.log = []
        self.new_id, self.host_caps = new_id, host_caps
        for at in (HLIST, PLAYERS, TEAMS):
            self.sentinel(at)
        vtable = emu.alloc(0x100)
        obj = emu.alloc(16)
        for off, a in self.slot_code.items():
            cpu.u32(vtable + off, a)
        for off, a in self.extra.items():
            cpu.u32(vtable + off, a)
        cpu.u32(obj, vtable)
        cpu.u32(DPLAY, obj)
        cpu.write(INITIALISED, b'\x01')
        cpu.write(OPEN, b'\x01')
        cpu.write(ASYNC, b'\x00')
        cpu.u32(LOCAL, 0)
        cpu.u32(HOST, 0)
        self.ok, self.size, self.id = 1, 0, 0
        for i in range(others):  # the players already there, as the enumeration finds them
            cpu.call(0x5893f0, stack_args=(2 + i, 1, self.dpname(f'P{102 + i}', f'Player {102 + i}'), 0, 0))
        cpu.call(0x5886c0, ecx=emu.put(f'P{name}'.encode() + b'\0') if name >= 0 else 0)
        r = self.eax()
        local = emu.u32(LOCAL)
        out = list(self.log) + [20, r, 21, emu.u32(local + 4) if local else -1, cpu.read(local, 1)[0] if local else -1,
                                22, emu.u32(HOST)]
        players = self.walk(PLAYERS)
        out += [3, len(players)]
        for p in players:
            s, l_ = self.cstr(p + 0x44), self.cstr(p + 0x94)
            out += [emu.u32(p + 4), cpu.read(p, 1)[0], emu.u32(p + 0xe4), -1 if s == 'noname' else number(s),
                    -1 if l_ == 'noname' else number(l_)]
        return out


def main():
    brk = int(os.environ.get('BREAK', '0'))
    exe = Exe()
    fails = cases = 0
    for others in range(5):
        for new_id in (7, 9):
            for host_caps in (0, 1):
                for name in (-1, 42):
                    theirs = exe.make(others, new_id, host_caps, name)
                    out = (ctypes.c_int32 * 1024)()
                    n = probe.probe_znet_create(others, new_id, 0 if brk == 1 else host_caps, name, out, 1024)
                    mine = list(out[:n])
                    # The exe's CreatePlayer gets "noname" for no name; number() on it fails both sides alike: -1
                    cases += 1
                    if mine != theirs:
                        fails += 1
                        if fails <= 3:
                            print(f'FAIL others {others} id {new_id} host {host_caps} name {name}:\n'
                                  f'  port     {mine}\n  original {theirs}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} cases identical (0..4 others, host or not, a name or none)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
