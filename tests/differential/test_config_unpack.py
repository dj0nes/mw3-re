"""Another player's loadout from its packed form, 0x4259c0, against the
original x86 (docs/multiplayer-port.md, piece 5b): every dfn's built-in
loadouts packed by the original (0x425650, as the local player's record has
it), then changed (armour bits, the engine past the table, endo and XL
flags, items' kinds, ids, techs, counts, groups and parts; items dropped,
ammo doubled, a size word too short), and unpacked by both sides. Compared:
the whole loadout (test_mechsave.py's dump), its weight and Validate.

    cmake --build build && .venv/bin/python tests/differential/test_config_unpack.py

BREAK=1 hands the port the packed form without its last item; it must fail.
"""
import ctypes
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from test_mechsave import Emulated, weapon_labs, WEAPONS, MECHS, VALIDATE  # noqa: E402
from test_zreader import READER, entries  # noqa: E402
from test_mechdef import probe  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

PACK, UNPACK = 0x425650, 0x4259c0
probe.probe_config_unpack.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]


def packed(emu, c):
    emu.cpu.call(PACK, ecx=c)
    p = emu.cpu.uc.reg_read(UC_X86_REG_EAX)
    n = emu.u32(p) & 0xffff
    return list(struct.unpack(f'<{(n + 3) // 4}I', emu.cpu.read(p, (n + 3) // 4 * 4)))


def change(rng, words):
    w = list(words)
    n = ((w[0] & 0xffff) - 0x34) // 0xc
    pick = rng.randrange(9)
    if pick == 0:
        w[1] ^= 1 << rng.randrange(32)
    elif pick == 1:
        w[1] = (w[1] & ~(0x7f << 2)) | (rng.choice([0, 40, 78, 79, 80, 127]) << 2)
    elif pick == 2 and n:
        k = 13 + 3 * rng.randrange(n)
        w[k] = (w[k] & ~7) | rng.choice([0, 1, 2, 3, 4, 7])
    elif pick == 3 and n:
        k = 13 + 3 * rng.randrange(n)
        w[k] ^= 1 << rng.randrange(3, 32)
    elif pick == 4 and n:
        k = 13 + 3 * rng.randrange(n)
        w[k + 1] = rng.randrange(-1, 5) & 0xffffffff
        w[k + 2] = rng.randrange(-1, 9) & 0xffffffff
    elif pick == 5 and n:  # an item dropped, the size word with it
        k = 13 + 3 * rng.randrange(n)
        del w[k:k + 3]
        w[0] -= 0xc
    elif pick == 6:  # ammo twice: the second onto the first, unless over a ton
        for k in range(n):
            if w[13 + 3 * k] & 7 == 2:
                w += w[13 + 3 * k:16 + 3 * k]
                w[0] += 0xc
                break
    elif pick == 7:  # a size word short of the items (fewer read)
        w[0] = (w[0] & ~0xffff) | max(0x34, (w[0] & 0xffff) - 0xc * rng.randrange(1, 3))
    elif pick == 8:
        w[2 + rng.randrange(11)] = rng.randrange(0, 80)
    return w


def unpack_exe(emu, words):
    cpu = emu.cpu
    mark = cpu.heap_next
    p = cpu.alloc(4 * len(words) + 16)
    cpu.write(p, struct.pack(f'<{len(words)}I', *words) + bytes(16))
    cpu.call(UNPACK, ecx=p)
    c = cpu.uc.reg_read(UC_X86_REG_EAX)
    out = bytearray(emu.dump_config(c)) + emu.fweight(c, -1)
    cpu.call(VALIDATE, ecx=c, stack_args=(0,))
    out.append(cpu.uc.reg_read(UC_X86_REG_EAX) & 0xff)
    emu.unpatch()
    cpu.heap_next = mark
    return bytes(out)


def same(want, got):
    """Byte for byte but the weight (an x87 sum), within float rounding."""
    if len(got) != len(want) or got[:-5] != want[:-5] or got[-1:] != want[-1:]:
        return False
    a, b = struct.unpack('<f', want[-5:-1])[0], struct.unpack('<f', got[-5:-1])[0]
    return a == b or abs(a - b) <= 1e-6 * max(1.0, abs(a))


def main():
    brk = int(os.environ.get('BREAK', '0'))
    data = READER.read_bytes()
    labs = weapon_labs(data)
    table = (ctypes.c_float * (WEAPONS * 5))()
    present = (ctypes.c_uint8 * WEAPONS)()
    for i, lab in enumerate(labs):
        if lab:
            present[i] = 1
            table[5 * i:5 * i + 5] = [lab[0], lab[1], 0, 0, lab[4]]
            ctypes.memmove(ctypes.addressof(table) + 20 * i + 8, struct.pack('<ii', lab[2], lab[3]), 8)
    probe.probe_set_weapon_labs(table, present)
    emu = Emulated(data, labs)
    buf = ctypes.create_string_buffer(1 << 16)
    rng = random.Random(11)
    cases = fails = valid = stock = stock_valid = 0
    for name, start, _ in entries(data):
        if not name.startswith('dfn_'):
            continue
        mech = MECHS.index(name[4:-4])
        for c in emu.stock_configs(start, mech):
            base = packed(emu, c)
            for k in range(12):
                words = base if k == 0 else change(rng, base)
                want = unpack_exe(emu, words)
                mine_words = words
                if brk == 1 and ((words[0] & 0xffff) - 0x34) // 0xc > 0:
                    mine_words = list(words)
                    mine_words[0] -= 0xc
                arr = (ctypes.c_uint32 * len(mine_words))(*mine_words)
                n = probe.probe_config_unpack(arr, len(mine_words), buf, len(buf))
                cases += 1
                valid += want[-1]
                if k == 0:
                    stock, stock_valid = stock + 1, stock_valid + want[-1]
                got = buf.raw[:max(n, 0)]
                if not same(want, got):
                    fails += 1
                    if fails <= 3:
                        at = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), None)
                        print(f'FAIL {name} case {k}: original {len(want)} bytes, port {len(got)}, first difference at {at}')
    if fails:
        print(f'{fails} of {cases} differ')
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {cases} packed loadouts identical ({valid} valid; stock {stock_valid} of {stock})')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
