"""zNetwork's handlers, dispatch and poll against the original x86
(docs/multiplayer-port.md, piece 2): random sequences of 0x587aa0 (add a
handler), 0x587b20 (remove every entry of a type and handler), 0x587a00
(clear) and 0x587c40 (dispatch to each handler of the message's type, in
order) with logging handlers, compared call by call with the port's; then
0x588930 (the poll) over scripted queues through a stand-in IDirectPlay4A
Receive (+0x64): the buffer growing (DPERR_BUFFERTOOSMALL), messages under 4
bytes dropped, the system's (from 0) to 0x588a70 (stubbed, counted), the rest
dispatched, and a maximum.

    cmake --build build && .venv/bin/python tests/differential/test_znet_dispatch.py

BREAK=1 makes the port's removal take only the first match, BREAK=2 its poll
keep messages of 3 bytes; each must fail.
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

probe.probe_znet_handlers.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int32, ctypes.POINTER(ctypes.c_int32),
                                      ctypes.c_int32]
probe.probe_znet_handlers.restype = ctypes.c_int32
probe.probe_znet_poll.argtypes = [ctypes.c_int32, ctypes.POINTER(ctypes.c_int32), ctypes.c_int32,
                                  ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
probe.probe_znet_poll.restype = ctypes.c_int32

LIST, COUNT = 0x730794, 0x730798
INITIALISED, OPEN, DPLAY, BUF, BUFSIZE = 0x7306c8, 0x730764, 0x730724, 0x7307d8, 0x7307dc
NEW, DELETE, REPORT, SYSTEM = 0x5abd26, 0x5abd1a, 0x49bfd0, 0x588a70
DPERR_NOMESSAGES, DPERR_BUFFERTOOSMALL = 0x887700BE, 0x8877001E


class Exe:
    def __init__(self):
        self.emu = Emulated()
        cpu = self.cpu = self.emu.cpu
        cpu.stub_function(NEW, lambda c: self.emu.alloc(c.arg(0)))
        cpu.stub_function(DELETE, lambda c: 0)
        # REPORT (0x49bfd0) is Emulated's ZERR stub already: a second hook would return twice.
        self.log = []
        self.handlers = []
        for i in range(8):
            a = cpu.code(b'\x90' * 16)
            cpu.stub_function(a, self.handler(i))
            self.handlers.append(a)
        self.system = 0
        cpu.stub_function(SYSTEM, self.on_system)

    def handler(self, i):
        def run(c):
            self.log += [i, c.ecx, struct.unpack('<H', c.read(c.edx, 2))[0]]
            return 0
        return run

    def on_system(self, c):
        self.system += 1
        return 0

    def reset(self):
        self.emu.begin()
        self.log = []
        head = self.emu.alloc(12)
        self.cpu.u32(head, head)
        self.cpu.u32(head + 4, head)
        self.cpu.u32(LIST, head)
        self.cpu.u32(COUNT, 0)
        self.msg = self.emu.alloc(16)

    def run(self, ops):
        self.reset()
        for op, a, b in ops:
            if op == 0:
                self.cpu.call(0x587aa0, ecx=a, edx=self.handlers[b], stack_args=(2,))
            elif op == 1:
                self.cpu.call(0x587b20, ecx=a, edx=self.handlers[b])
            elif op == 2:
                self.cpu.call(0x587a00)
            else:
                self.cpu.write(self.msg, struct.pack('<HH', a, 4))
                self.cpu.call(0x587c40, ecx=b, edx=self.msg)
        return list(self.log)


def port_handlers(ops):
    flat = (ctypes.c_int32 * (3 * len(ops)))(*[v for t in ops for v in t])
    out = (ctypes.c_int32 * 4096)()
    n = probe.probe_znet_handlers(flat, len(ops), out, 4096)
    return list(out[:n])


def main():
    brk = int(os.environ.get('BREAK', '0'))
    if brk == 1:
        os.environ['MW3_PROBE_BREAK'] = '1'
    exe = Exe()
    rng = random.Random(1999)
    fails = cases = calls = 0
    for case in range(300):
        ops = []
        for _ in range(rng.randint(1, 30)):
            r = rng.random()
            if r < 0.45:
                ops.append((0, rng.choice((5, 6, 0x1f)), rng.randrange(7)))  # handler 7 (the one that removes) left out
            elif r < 0.6:
                ops.append((1, rng.choice((5, 6, 0x1f)), rng.randrange(7)))
            elif r < 0.63:
                ops.append((2, 0, 0))
            else:
                ops.append((3, rng.choice((5, 6, 0x1f, 9)), rng.randrange(1, 9)))
        ops = [(0, 5, 1), (0, 5, 1), (1, 5, 1), (3, 5, 4)] + ops  # one handler twice, one removal takes both
        theirs = exe.run(ops)
        mine = port_handlers(ops)
        cases += 1
        calls += len(theirs) // 3
        if mine != theirs:
            fails += 1
            if fails <= 3:
                print(f'FAIL handlers case {case}: port {mine[:12]}, original {theirs[:12]}')

    # The poll.
    cpu = exe.cpu
    recv = cpu.code(b'\x90' * 16)
    state = {}

    def receive(c):
        q = state['queue']
        if state['at'] >= len(q):
            return DPERR_NOMESSAGES
        frm, size = q[state['at']]
        have = struct.unpack('<I', c.read(c.arg(5), 4))[0]
        if size > have:
            c.u32(c.arg(5), size)
            return DPERR_BUFFERTOOSMALL
        state['at'] += 1
        c.u32(c.arg(1), frm)
        c.u32(c.arg(5), size)
        if size >= 2:
            c.write(c.arg(4), struct.pack('<H', 0x1f) + bytes(size - 2))
        return 0

    cpu.stub_function(recv, receive, 24)
    for case in range(200):
        queue = [(rng.choice((0, 3, 4, 7)), rng.choice((0, 2, 3, 4, 8, 60, 300))) for _ in range(rng.randint(0, 12))]
        maximum = rng.choice((-1, -1, 0, 1, 3, 5))
        exe.reset()
        vtable = exe.emu.alloc(0x100)  # after the reset, which rewinds the heap
        obj = exe.emu.alloc(16)
        cpu.u32(vtable + 0x64, recv)
        cpu.u32(obj, vtable)
        cpu.u32(DPLAY, obj)
        cpu.write(INITIALISED, b'\x01')
        cpu.write(OPEN, b'\x01')
        cpu.u32(BUF, 0)
        cpu.u32(BUFSIZE, 0)
        cpu.call(0x587aa0, ecx=0x1f, edx=exe.handlers[1], stack_args=(2,))
        state.update(queue=queue, at=0)
        exe.system = 0
        cpu.call(0x588930, ecx=maximum & 0xffffffff, edx=0)
        theirs = [cpu.uc.reg_read(UC_X86_REG_EAX), exe.system] + exe.log
        flat = (ctypes.c_int32 * (2 * len(queue) + 2))(*[v for t in queue for v in t])
        if brk == 2:
            flat = (ctypes.c_int32 * (2 * len(queue) + 2))(*[v if i % 2 == 0 or v != 3 else 4 for i, v in
                                                            enumerate(v for t in queue for v in t)])
        out = (ctypes.c_int32 * 512)()
        n = probe.probe_znet_poll(maximum, flat, len(queue), out, 512)
        mine = list(out[:n])
        mine[0] &= 0xffffffff
        cases += 1
        if mine != theirs:
            fails += 1
            if fails <= 6:
                print(f'FAIL poll case {case} max {maximum} queue {queue}: port {mine[:8]}, original {theirs[:8]}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} cases identical (300 handler sequences, {calls} handler calls; 200 polls)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
