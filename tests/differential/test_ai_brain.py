"""The AI controls against the original (docs/ai.md, The AI controls): the
mech's 0x49fe90 (`new(0x320)`, 0x4d8750 over 0x4d8670 and 0x4265a0) and the
vehicle's 0x49ff10 (0x4ac860, 0x4e71c0 over 0x4e7090, 0x4ac750 and its
turret limits 0x4a4af0), both with the pilot's brain 0x4cf330: the settings'
copy 0x4d9bf0 / 0x4d9940 (with the orders' list 0x4d8fe0 and the strings),
the firing state 0x4b6570 (0x4b6390, the aim 0x4b7060, 0x4b6660), the order
deque 0x4e82a0 and the brain's own order 0x4e8180, the step 0x4cd770, the
steering block 0x4ce620, the net lookup (0x58b160, vt+0x28, vt+0x34, the
built-in net at 0x604008), the first order 0x4e9380 (0x4e9d90's push with
0x4e8070, the net's part 0x4c0070 / 0x4c0520, the shared behaviour's state
0x4cbe30), the owner 0x4e8090 and the radio 0x4e7e20 (0x4e7c40, 0x4e7dc0);
then each deleted through its IControls slot 0 (0x4d8bf0 / 0x4e7820: the
brain's destructor 0x4cf630, the firing state's 0x4b65f0, the basic
vehicle controls' 0x4ac800).

The original runs first with its calls out stubbed and answered from one
script; the port then runs on a calltrace.Replay of it. Compared: the call
traces (`new` and `delete` with their blocks, the znet lookup, the sixteen
sound lookups, the owner's calls); every block the original allocated, as
the port's object laid out byte by byte (each byte the original wrote a
field the port sets alike, every other byte the allocator's fill); the
settings' copies apart (the orders' list walked, the strings as text); no
write outside the blocks. The records are every shipped aiv.zrd record (all
48 archives) read by the ported reader, then crafted ones. The vehicle's
turret limits come from fsin / fcos: the x87 at 53 bits (start-up and load).

    cmake --build build && .venv/bin/python tests/differential/test_ai_brain.py [crafted]
"""
import collections
import ctypes
import glob
import os
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from calltrace import CALLBACK, ROOT, Emu, Replay, Trace, call_abs, compare, load_probe, mov_ecx  # noqa: E402

MAKE = (0x49fe90, 0x49ff10)
FILL = 0xcd
BRAIN = (0x5c, 0xa04)       # the brain's place in each controls block
ICTL = (0x30c, 0xcc4)       # the IControls base
SIZE = (0x320, 0xcd8)
KINDS = ['mech controls', 'vehicle controls', 'order block', 'order map', 'aim', 'radio']
VOICES = ['dom', 'alan', 'epona', 'x', 'abcdefghijklmn', '']
P = ctypes.c_void_p
U = ctypes.c_uint32
I = ctypes.c_int32


def setup_probe():
    probe = load_probe()
    probe.probe_app_set_callback.argtypes = [CALLBACK]
    probe.probe_ab_load_aiv.argtypes = [ctypes.c_char_p]
    probe.probe_ab_record.argtypes = [I, P, P, P, I, P, P, P]
    probe.probe_ab_set_record.argtypes = [P, I, P, ctypes.c_char_p, ctypes.c_char_p]
    probe.probe_ab_net.argtypes = [U, I, I]
    probe.probe_ab_make.argtypes = [I, U, U]
    probe.probe_ab_make.restype = U
    probe.probe_ab_delete.argtypes = [U]
    probe.probe_ab_image.argtypes = [U, U, P, P]
    probe.probe_ab_brain.argtypes = [U, P, I, P, P, P]
    probe.probe_ab_vset.argtypes = [U, U, U]
    probe.probe_ab_vslot.argtypes = [U, I, U]
    probe.probe_ab_vslot.restype = U
    return probe


def shipped(probe):
    """Every record of every mission's aiv.zrd: (where, name, raw, orders,
    pilot, voice); orders None for no list, pilot / voice None for none."""
    out = []
    for path in sorted(glob.glob(str(ROOT / 'game' / 'mw3-1.2' / 'zbd' / '*' / 'reader*.zbd'))):
        n = probe.probe_ab_load_aiv(path.encode())
        for i in range(max(n, 0)):
            name, pilot, voice = (ctypes.create_string_buffer(256) for _ in range(3))
            raw = (ctypes.c_uint8 * 0xd0)()
            orders = (ctypes.c_uint8 * (0x20 * 64))()
            has = (I * 3)()
            k = probe.probe_ab_record(i, name, raw, orders, 64, pilot, voice, has)
            entries = [bytes(orders)[0x20 * j:0x20 * j + 0x20] for j in range(min(k, 64))] if has[2] else None
            where = Path(path).parent.name + '/' + Path(path).stem
            out.append((where, name.value.decode('latin1'), bytes(raw), entries,
                        pilot.value.decode('latin1') if has[0] else None,
                        voice.value.decode('latin1') if has[1] else None))
    return out


def craft(r, rec):
    where, name, raw, orders, pilot, voice = rec
    raw = bytearray(raw)
    for _ in range(r.randrange(0, 6)):
        off = r.randrange(4, 0xd0, 4)
        struct.pack_into('<I', raw, off, r.getrandbits(32))
    struct.pack_into('<i', raw, 4, r.choice([0, 1, 4, 7, 99, -1, r.randrange(100)]))
    struct.pack_into('<I', raw, 0x18, r.choice([0, 1, 2, 3, 0x22, 0x41, 33, 0x3ff, r.getrandbits(32)]))
    if r.random() < 0.5:
        voice = r.choice(VOICES + [None, None])
    if r.random() < 0.3:
        pilot = r.choice(['MSG_AIV_X', '', None])
    if r.random() < 0.5:
        orders = None if r.random() < 0.3 else [bytes(r.getrandbits(8) for _ in range(0x20))
                                               for _ in range(r.randrange(0, 5))]
    return (where + ' crafted', name, bytes(raw), orders, pilot, voice)


# --- the original ----------------------------------------------------------------
def run_original(case):
    t = Trace()
    t.silent |= {'sprintf', 'strdup', 'free', 'new@list'}
    e = Emu(t)
    e.cpu.fpu_control(0x027f)
    w32, a = e.w32, e.world_alloc
    blocks = case['blocks']
    quiet = set()  # the list copies' and strings' blocks
    # The start-up's [0x0060b4a0] = cos(pi/4), the shared behaviour at
    # 0x0060b460 (0x004cbdd0 -> 0x004cbe10), the firing's template
    # (0x004b6500) and the built-in net's vtable.
    e.call(0x4ce5b0)
    e.call(0x4cbdd0)
    e.call(0x4b6500)  # the firing state's template [0x005e8868] (W34)
    w32(0x604008, 0x5bcc68)
    nets = {}
    for kind, addr_kind, net_id in case['nets']:
        n = a(0x20)
        w32(n, (0x5ccda8, 0x5ca6d8)[addr_kind])
        w32(n + 4, net_id)
        nets[kind] = n
    case['net_addr'] = {k: v for k, v in nets.items()}
    case['net_addr']['builtin'] = 0x604008
    # The record (an AivRecord: +4..+0xcf, the orders' list, the strings).
    _, _, raw, orders, pilot, voice = case['rec']
    rec = a(0xe4)
    e.cpu.write(rec + 4, raw[4:0xd0])
    if orders is not None:
        owner, head = a(0xc), a(0x28)
        nodes = [a(0x28) for _ in orders]
        ring = [head] + nodes
        for i, nd in enumerate(ring):
            w32(nd, ring[(i + 1) % len(ring)])
            w32(nd + 4, ring[i - 1])
            if nd != head:
                e.cpu.write(nd + 8, orders[i - 1])
        w32(owner + 4, head)
        w32(owner + 8, len(orders))
        w32(rec + 0xd0, owner)
    if pilot is not None:
        w32(rec + 0xd4, e.world_string(pilot))
    if voice is not None:
        w32(rec + 0xd8, e.world_string(voice))
    # The pilot ([0x0060861c]) and its vehicle's allies.
    if case['pilot'] is not None:
        p = a(0x40)
        w32(0x60861c, p)
        if case['pilot'] >= 0:
            v = a(0x100)
            w32(p + 0x14, v)
            w32(v + 0xc4, case['pilot'])
    owner = 0
    if case['owner']:
        owner = e.object('owner', 0x40, slots={0xec: ('c', 0)})
    case['owner_addr'] = owner

    def op_new(em, key, args, pop):
        size = args[0]
        addr = em.cpu.alloc(max(size, 4))
        if size in (0xc, 0x28):  # the orders' list (0x4d9680): compared apart
            quiet.add(addr)
            em.cpu.write(addr, bytes(max(size, 4)))
            t.handle('new@list', [size])
        else:
            em.cpu.write(addr, bytes([FILL]) * max(size, 4))
            blocks.append((addr, size, FILL))
            t.handle('new', [size])
            t.override('new', addr)
        em._ret(addr, 0)
    e.stub(0x5abd26, 'new', 's0', 0, op_new)

    def op_delete(em, key, args, pop):
        if args[0] and args[0] not in quiet:
            t.handle('delete', [args[0]])
        em._ret(0, 0)
    e.stub(0x5abd1a, 'delete', 's0', 0, op_delete)

    def zalloc(em, key, args, pop):
        size = args[0]
        addr = em.cpu.alloc(size)
        em.cpu.write(addr, bytes(size))
        blocks.append((addr, size, 0))
        t.handle(key, [size])
        t.override(key, addr)
        em._ret(addr, 0)
    e.stub(0x4ac860, '0x004ac860', 'c', 0, zalloc)
    e.stub(0x4ac890, '0x004ac890', 'c,d')

    def strdup(em, key, args, pop):
        s = em.cstr(args[0]).encode('latin1') + b'\0'
        addr = em.cpu.alloc(len(s))
        em.cpu.write(addr, s)
        quiet.add(addr)
        em._ret(addr, 0)
    e.import_stub(0x5bb670, 'strdup', 's0', 0, strdup)
    e.import_stub(0x5bb65c, 'free', 's0', 0)

    def sprintf(em, key, args, pop):
        buf, fmt, x, y = em.args('s0,s1,s2,s3')
        assert em.cstr(fmt) == '%s_%s'
        s = (em.cstr(x) + '_' + em.cstr(y)).encode('latin1') + b'\0'
        em.cpu.write(buf, s)
        em._ret(len(s) - 1, 0)
    e.import_stub(0x5bb658, 'sprintf', '', 0, sprintf)
    e.stub(0x56f6f0, '0x0056f6f0', 'zc')
    e.stub(0x58b160, '0x0058b160', 'c')
    e.stub(0x402510, '0x00402510', 'c')
    t.script = {
        '0x0058b160': lambda n, args: case['net_addr'].get(case['znet'], 0),
        '0x0056f6f0': lambda n, args: 0 if (n * 7 + case['seed']) % 5 == 0 else 0x2a000000 + 0x10 * n,
        'owner.vt+0xec': lambda n, args: case['owner_ret'],
    }
    snap = e.snapshot()
    e.watch_writes([(e.world_base, e.world_base + e.world_size)])
    kind = case['kind']
    net = case['net_addr'].get(case['net'], 0)
    ctl = e.call(MAKE[kind], ecx=owner, edx=net, stack=(rec,))
    mem = {addr: bytes(e.cpu.read(addr, max(size, 4))[:size]) for addr, size, _ in blocks}
    lst, strings = brain_copies(e, ctl + BRAIN[kind]) if ctl else (None, None)
    if ctl and case['delete'] and kind == 0:  # a vehicle's after its slots' check (main)
        delete_original(e, ctl, kind)
    return t, e, snap, ctl, mem, lst, strings


def delete_original(e, ctl, kind):
    ictl = ctl + ICTL[kind]
    e.call(e.r32(e.r32(ictl)), ecx=ictl, stack=(1,))


def brain_copies(e, b):
    """The brain's copy of the record's orders (each 0x20 bytes) and strings."""
    owner = e.r32(b + 0xd0)
    lst = None
    if owner:
        head = e.r32(owner + 4)
        lst, nd = [], e.r32(head)
        while nd != head and len(lst) < 100:
            lst.append(bytes(e.cpu.read(nd + 8, 0x20)))
            nd = e.r32(nd)
    strings = tuple(e.cstr(e.r32(b + o)) if e.r32(b + o) else None for o in (0xd4, 0xd8))
    return lst, strings


# --- the port ---------------------------------------------------------------------
def run_port(probe, case, t, e, snap):
    rep = Replay(t, e, snap)
    probe.probe_app_set_callback(rep.callback())
    probe.probe_ab_reset()
    for kind, addr_kind, net_id in case['nets']:
        probe.probe_ab_net(case['net_addr'][kind], addr_kind, net_id)
    probe.probe_ab_net(0x604008, 2, 10000)
    _, _, raw, orders, pilot, voice = case['rec']
    n = -1 if orders is None else len(orders)
    buf = (ctypes.c_uint8 * (0x20 * max(n, 1)))(*b''.join(orders or [bytes(0x20)]))
    probe.probe_ab_set_record((ctypes.c_uint8 * 0xd0)(*raw), n, buf,
                              pilot.encode('latin1') if pilot is not None else None,
                              voice.encode('latin1') if voice is not None else None)
    net = case['net_addr'].get(case['net'], 0)
    ctl = probe.probe_ab_make(case['kind'], case['owner_addr'], net)
    return rep, ctl


def port_copies(probe, ctl):
    orders = (ctypes.c_uint8 * (0x20 * 100))()
    pilot, voice = ctypes.create_string_buffer(256), ctypes.create_string_buffer(256)
    has = (I * 3)()
    n = probe.probe_ab_brain(ctl, orders, 100, pilot, voice, has)
    lst = None if n < 0 else [bytes(orders)[0x20 * j:0x20 * j + 0x20] for j in range(n)]
    strings = (pilot.value.decode('latin1') if has[0] else None, voice.value.decode('latin1') if has[1] else None)
    return lst, strings


def compare_blocks(probe, name, case, mem):
    bad = []
    for addr, size, fill in case['blocks']:
        data = mem[addr]
        img = (ctypes.c_uint8 * max(size, 4))(*([fill] * max(size, 4)))
        mask = (ctypes.c_uint8 * max(size, 4))()
        k = probe.probe_ab_image(addr, size, img, mask)
        if k < 0:
            bad.append(f'block {addr:#x} ({size:#x} bytes): no port object ({k})')
            continue
        step = BRAIN[k] + 0x244 if k < 2 else -1
        for i in range(size):
            if mask[i] == 2:
                if i == step and data[i] != fill & 0xfe:
                    bad.append(f'{KINDS[k]} +{i:#x} (the step\'s flags): original {data[i]:#04x}')
                continue
            if mask[i]:
                if data[i] != img[i]:
                    bad.append(f'{KINDS[k]} +{i:#x}: original {data[i]:#04x} port {img[i]:#04x}')
            elif data[i] != fill:
                bad.append(f'{KINDS[k]} +{i:#x}: original writes {data[i]:#04x}, the port has no field')
        if len(bad) > 12:
            break
    if bad:
        print(f'{name}: blocks differ:')
        for b in bad[:16]:
            print('   ', b)
        return False
    return True


# --- cases --------------------------------------------------------------------------
def make_case(seed, rec, crafted):
    r = random.Random(seed)
    case = {'seed': seed, 'rec': rec, 'blocks': []}
    case['kind'] = seed % 2 if not crafted else r.choice([0, 1])
    # The nets: the vehicle's own (compiled), one that is not compiled, and
    # what znet answers for the record's first item.
    case['nets'] = [('compiled', 1, 4), ('base', 0, 7), ('other', 1, 9)]
    case['net'] = 'compiled' if not crafted or r.random() < 0.6 else r.choice(['base', 'none'])
    case['znet'] = r.choice(['other', 'base', 'none', 'compiled'])
    case['pilot'] = None if r.random() < 0.3 else r.choice([-1, 2, 4, 6, 0, 0xfffffffc, 1])
    case['owner'] = r.random() < 0.5
    case['owner_ret'] = r.choice([0, 0x12345678])
    case['delete'] = case['owner'] or case['kind'] == 0  # the exe's vehicle dtor reads its owner
    return case


SLOTS = [  # (IControls slot or primary 'p'+slot, port slot, takes an argument, a float answer)
    (0x18, 0, None, False), (0x1c, 1, None, True), (0x24, 2, None, True), (0x30, 3, None, True),
    (0x20, 4, 'f', None), (0x28, 5, 'f', None), (0x10, 6, 'b', None), (0x70, 7, None, False),
    ('p', 8, None, False), ('p20', 9, None, False), ('p4', 10, None, False), ('p8', 11, None, False),
    (0x14, 12, None, None)]


def vehicle_slots(probe, name, case, e, ctl, got, rep):
    """2b2: the AI vehicle controls' slots (the thunks' arithmetic) on both sides."""
    r = random.Random(case['seed'] * 31 + 5)
    ictl = ctl + 0xcc4
    vt, pvt = e.r32(ictl), e.r32(ctl)
    if case['owner_addr']:
        alive = r.choice([0, 1])
        e.cpu.write(case['owner_addr'] + 8, bytes([alive]))
        rep.shadow[case['owner_addr'] + 8] = alive  # the port reads it through the replay
    for slot, port, arg, is_float in SLOTS:
        if slot in ('p', 'p20', 'p4', 'p8') and slot == 'p' and not case['owner_addr']:
            continue
        for off, v in ((8, r.uniform(-1, 1)), (0xc, r.uniform(-1, 1))):
            e.w32(ctl + off, struct.unpack('<I', struct.pack('<f', v))[0])
        probe.probe_ab_vset(got, e.r32(ctl + 8), e.r32(ctl + 0xc))
        a = struct.unpack('<I', struct.pack('<f', r.uniform(-1, 1)))[0] if arg == 'f' else r.choice([0, 1, 2, 0x100])
        if isinstance(slot, str):
            fn = e.r32(pvt + {'p': 0x1c, 'p20': 0x20, 'p4': 4, 'p8': 8}[slot])
            ecx = ctl
        else:
            fn, ecx = e.r32(vt + slot), ictl
        if is_float:
            slot_mem, cw, here = e.alloc(16), e.alloc(4), e.alloc(64)
            e.cpu.write(cw, struct.pack('<H', 0x027f))
            code = b'\xdb\xe3\xd9\x2d' + struct.pack('<I', cw) + mov_ecx(ecx)
            code += call_abs(here + len(code), fn) + b'\xd9\x1d' + struct.pack('<I', slot_mem) + b'\xc3'
            e.cpu.write(here, code)
            e.call(here)
            want = e.r32(slot_mem)
        else:
            res = e.call(fn, ecx=ecx, stack=(a,) if arg else ())
            if slot == 0x20:
                want = e.r32(ctl + 8)
            elif slot == 0x28:
                want = e.r32(ctl + 0xc)
            elif slot == 0x10:
                want = e.r32(ctl + 0x390)
            elif slot == 0x14:
                want = e.r32(ctl + 8) ^ e.r32(ctl + 0xc)
            elif slot in (0x18, 0x70, 'p'):
                want = res & 0xff
            else:
                want = res
        mine = probe.probe_ab_vslot(got, port, a)
        if want != mine:
            print(f'{name}: vehicle slot {slot if isinstance(slot, str) else hex(slot)}: original {want:#x} rebuild {mine:#x}')
            return False
    return True


def main():
    probe = setup_probe()
    crafted_n = int(sys.argv[1]) if len(sys.argv) > 1 else 600
    recs = shipped(probe)
    stats = collections.Counter()
    calls = blocks = 0
    work = [(i, recs[i], False) for i in range(len(recs))]
    r = random.Random(7)
    work += [(len(recs) + i, craft(r, r.choice(recs)), True) for i in range(crafted_n)]
    for seed, rec, crafted in work:
        case = make_case(seed, rec, crafted)
        name = f'case {seed} ({rec[0]} {rec[1]}, {"vehicle" if case["kind"] else "mech"})'
        try:
            t, e, snap, ctl, mem, lst, strings = run_original(case)
        except Exception:
            print(f'{name}: the original faulted')
            raise
        rep, got = run_port(probe, case, t, e, snap)
        if (ctl != 0) != (got != 0):
            print(f'{name}: controls original {ctl:#x} port {got:#x}')
            raise SystemExit(1)
        if not compare_blocks(probe, name, case, mem):
            raise SystemExit(1)
        plst, pstrings = port_copies(probe, got)
        if lst != plst or strings != pstrings:
            print(f'{name}: the settings\' copies differ: orders {lst} / {plst}, strings {strings} / {pstrings}')
            raise SystemExit(1)
        if e.writes:
            print(f'{name}: the original wrote outside its blocks: {sorted(e.writes)[:8]}')
            raise SystemExit(1)
        if case['kind'] and ctl:
            case['seed'] = seed
            if not vehicle_slots(probe, name, case, e, ctl, got, rep):
                raise SystemExit(1)
            stats['vehicle slots'] += 1
            if case['delete']:
                delete_original(e, ctl, 1)
        if case['delete']:
            probe.probe_ab_delete(got)
        if not compare(name, t.log, rep.log):
            raise SystemExit(1)
        calls += len(t.log)
        blocks += len(case['blocks'])
        stats['crafted' if crafted else 'shipped'] += 1
        stats['vehicles' if case['kind'] else 'mechs'] += 1
        stats['radios'] += any(k == 'new' and a == (0x40,) for k, a in t.log)
        stats['no order'] += not any(k == 'new' and a == (0xf70,) for k, a in t.log)
        stats['built-in net'] += case['net'] != 'compiled' and case['znet'] in ('none', 'base')
        stats['deleted'] += case['delete']
    print('  ' + ', '.join(f'{k} {v}' for k, v in sorted(stats.items())))
    print(f'{len(work)} cases, {calls} calls, {blocks} blocks: the rebuilt AI controls match the original')


if __name__ == '__main__':
    # Unicorn processes can hang at interpreter exit: leave through os._exit.
    code = 0
    try:
        main()
    except SystemExit as ex:
        code = ex.code if isinstance(ex.code, int) else 1
    except BaseException:
        import traceback
        traceback.print_exc()
        code = 1
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(code)
