"""zNetwork's session checks and its send against the original x86
(docs/multiplayer-port.md, piece 1): 0x586ce0 (a network game is up),
0x586d10 (the local player's id), 0x586d20 (the local player is the host)
over every combination of their globals, and 0x5888a0 (the send) with a
stand-in IDirectPlay4A whose SendEx (+0xc4) keeps its arguments, against the
port's znet::send through a recording transport.

    cmake --build build && .venv/bin/python tests/differential/test_znet_session.py

BREAK=1 makes the port's host check ignore the local player, BREAK=2 its
send drop the guaranteed flag; each must fail.
"""
import ctypes
import itertools
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import Original  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402
from test_weiler import probe  # noqa: E402

probe.probe_znet_state.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32,
                                   ctypes.c_uint32, ctypes.POINTER(ctypes.c_int)]
probe.probe_znet_state.restype = None
probe.probe_znet_send.argtypes = [ctypes.c_int, ctypes.c_uint32, ctypes.c_int, ctypes.c_int, ctypes.c_uint32,
                                  ctypes.c_uint32, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
probe.probe_znet_send.restype = None

INITIALISED, OPEN, LOBBY, LOCAL, HOST, ASYNC, DPLAY = 0x7306c8, 0x730764, 0x73072c, 0x73075c, 0x730760, 0x730765, 0x730724
DPERR_PENDING = 0x8000000A
DPERR_GENERIC = 0x80004005


def main():
    brk = int(os.environ.get('BREAK', '0'))
    cpu = Original()
    record = cpu.alloc(0x100)
    cpu.write(record, bytes(0x100))
    fails = cases = 0

    # The checks: every combination of the flags, a local player or none, its
    # id equal to the host's or not.
    for init, opened, lobby, has_local, same in itertools.product((0, 1), (0, 1), (0, 0x1234), (0, 1), (0, 1)):
        local_id, host_id = 0x51, 0x51 if same else 0x77
        cpu.write(INITIALISED, bytes([init]))
        cpu.write(OPEN, bytes([opened]))
        cpu.u32(LOBBY, lobby)
        cpu.u32(HOST, host_id)
        cpu.u32(record + 4, local_id)
        cpu.u32(LOCAL, record if has_local else 0)
        theirs = []
        for fn in (0x586ce0, 0x586d10, 0x586d20):
            cpu.call(fn)
            eax = cpu.uc.reg_read(UC_X86_REG_EAX)
            theirs.append(eax)
        theirs = [theirs[0] & 0xff, theirs[1], theirs[2] & 0xff]
        out = (ctypes.c_int * 3)()
        probe.probe_znet_state(init, opened, lobby, has_local, local_id, 0x51 if (brk == 1 and has_local) else host_id, out)
        mine = [out[0], out[1] & 0xffffffff, out[2]]
        cases += 1
        if mine != theirs:
            fails += 1
            print(f'FAIL checks init {init} open {opened} lobby {lobby:#x} local {has_local} same {same}: port {mine}, original {theirs}')

    # The send: SendEx's arguments and the answer.
    seen = {}
    vtable = cpu.alloc(0x100)
    obj = cpu.alloc(16)
    sendex = cpu.code(b'\x90' * 16)

    def fake_sendex(c):
        seen.update(this=c.arg(0), frm=c.arg(1), to=c.arg(2), flags=c.arg(3), data=c.arg(4), size=c.arg(5),
                    rest=(c.arg(6), c.arg(7), c.arg(8)), msgid=c.arg(9))
        if c.arg(9):
            c.u32(c.arg(9), 77)
        seen['first'] = struct.unpack('<I', c.read(c.arg(4), 4))[0]
        return seen['code']

    cpu.stub_function(sendex, fake_sendex, 40)
    cpu.stub_function(0x58a880, lambda c: 0 if c.ecx else 1, 4)  # the error report: 0 for an error
    cpu.u32(vtable + 0xc4, sendex)
    cpu.u32(obj, vtable)
    cpu.u32(DPLAY, obj)
    msg = cpu.alloc(16)
    for async_, guaranteed, to, result in itertools.product((0, 1), (0, 1), (0, 0x99), (0, 1, 2)):
        size = 12
        cpu.write(msg, bytes([0x0f, 0, size, 0, 1, 2, 3, 4]))
        cpu.write(ASYNC, bytes([async_]))
        cpu.u32(record + 4, 0x51)
        cpu.u32(LOCAL, record)
        seen.clear()
        seen['code'] = (0, DPERR_PENDING, DPERR_GENERIC)[result]
        cpu.call(0x5888a0, ecx=msg, edx=size, stack_args=(guaranteed, to))
        answer = cpu.uc.reg_read(UC_X86_REG_EAX)
        theirs = [answer, 1, seen['to'], seen['size'], seen['flags'] & 1, int(seen['msgid'] != 0), seen['first']]
        ok_args = seen['frm'] == 0x51 and seen['data'] == msg and seen['rest'] == (0, 0, 0) and \
            seen['flags'] == ((guaranteed and 1) | (0x200 if async_ else 0))
        out = (ctypes.c_uint32 * 7)()
        probe.probe_znet_send(1, 0x51, async_, guaranteed if brk != 2 else 0, to, size, result, out)
        mine = list(out)
        cases += 1
        if mine != theirs or not ok_args:
            fails += 1
            print(f'FAIL send async {async_} guaranteed {guaranteed} to {to:#x} result {result}: '
                  f'port {mine}, original {theirs}, the exe\'s other arguments {"right" if ok_args else seen}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} cases identical (the three checks over 32 states, the send over 24)')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
