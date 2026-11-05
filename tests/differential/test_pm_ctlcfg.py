"""mech3's controls setup against Pirate's Moon's exe (docs/controls.md, Setup; docs/pm-map.md, its command
table): the axis table's static constructor 0x4111d0, the name map's 0x411890, the default bindings 0x411a10
(110 commands, with 0x411980, the groups 0x4113f0 / 0x411650 / 0x411300, the map 0x411940 / 0x413010), the
controls file reader 0x4128e0 (through the zReader's text branch, 0x412d90 and 0x4304c0), the writer 0x4126e0
and the controls' mode 0x4282c0.

    cmake --build build --target pmprobe && .venv/bin/python -P tests/differential/test_pm_ctlcfg.py

zInput's command map and the messages are stubs on both sides: every call
(bind, the map's reset and refresh, the four binding getters, the joystick's
use, the message lookups) is logged and answered from one model of the
bindings, and the logs must be equal. The files the original opens come
from the same folder the port reads (the write dir). One long run of random
steps on both sides: the defaults (again and again: the screen's "defaults"
button calls them too), controls files (as the game writes them, with
random bindings and axes, and crafted: unknown command names, which bind
command 0, decimal and odd values, missing sections and keys, axes with
neither source, unknown sources, the joystick's stock axes so the table
counts as stock, unknown axis names, which the original writes over the
flags and the groups' list: those bytes are put back after its read and the
port leaves them alone, FIX (bug)), saves (the written text must be equal),
the controls' mode on random tables, the groups freed. After every step the
whole state is compared: every command's entry, the groups and their
commands, the name map in order, the axis table and its flags.
"""
import ctypes
import random
import shutil
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from calltrace import CALLBACK  # noqa: E402
from original import FTOL, IAT_FTOL, Original  # noqa: E402
import test_pm_zar  # noqa: E402
import text_crt  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP  # noqa: E402

NIL_PM = 0x628b40  # the name map's nil, right after PM's 0xb0 commands

probe = test_pm_zar.load_pm_probe()
probe.probe_ctl_init.argtypes = [CALLBACK, ctypes.c_char_p]
probe.probe_ctl_load.argtypes = [ctypes.c_char_p]
probe.probe_ctl_save.argtypes = [ctypes.c_char_p]
probe.probe_ctl_set_table.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint8, ctypes.c_uint8]
probe.probe_ctl_set_mode.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
probe.probe_ctl_set_mode.restype = ctypes.c_uint32
probe.probe_ctl_dump.argtypes = [ctypes.c_char_p, ctypes.c_int]

AXIS_INIT, MAP_INIT, DEFAULTS, LOAD, SAVE, FREE, SET_MODE = (
    0x4111d0, 0x411890, 0x411a10, 0x4128e0, 0x4126e0, 0x411300, 0x4282c0)
TABLE, READY, STOCK, CMDS, GROUPS, GROUP, MAP, NIL = (
    0x627d28, 0x627d68, 0x627d69, 0x627d80, 0x627d74, 0x627d20, 0x627d10, NIL_PM)
POOL, DEFINES = 0x577690, 0x579360
# zInput's, as MW3's names them in the port's calls.
GETTERS = {0x536a80: 0, 0x536aa0: 1, 0x536ac0: 2, 0x536ae0: 3}
GETTER_KEYS = {0x536a80: '0x0051d200', 0x536aa0: '0x0051d210', 0x536ac0: '0x0051d220', 0x536ae0: '0x0051d230'}


class Model:
    """zInput's bindings as both sides see them: a bind sets a command's four
    (masked as 0x51beb0 packs them), a reset clears all."""

    def __init__(self):
        self.b = {}

    def handle(self, key, args):
        if key == '0x0051d2c0':
            cmd, _, k1, k2, joy, mouse = args
            self.b[cmd] = [k1 & 0x7ff, k2 & 0x7ff, joy & 0xf, mouse & 3]
            return cmd
        if key == '0x0051d1d0':
            self.b = {}
            return 0
        if key in ('0x0051d200', '0x0051d210', '0x0051d220', '0x0051d230'):
            which = {'0x0051d200': 0, '0x0051d210': 1, '0x0051d220': 2, '0x0051d230': 3}[key]
            return self.b.get(args[0], [0, 0, 0, 0])[which]
        return 0


class Emulated:
    def __init__(self, folder):
        self.folder = folder
        self.thunks, self.pointers = {}, 0
        self.log = []
        self.model = Model()
        self.out = {}
        # Imports by MW3's slots: the cpu translates those to PM's.
        cpu = self.cpu = Original(exe='pm')
        cpu.import_code(IAT_FTOL, FTOL)
        self.files = {}
        self.crt = text_crt.install(cpu, self.files)
        self.cstr = lambda a: self.bytes_at(a).decode('latin-1')
        cpu.stub_import(0x5bb4bc, lambda c: self.alloc(c.arg(0)))  # operator new
        cpu.stub_import(0x5bb4c8, lambda c: 0)                     # operator delete
        cpu.stub_import(0x5bb4f8, lambda c: c.ecx)                 # std::_Lockit
        cpu.stub_import(0x5bb4f4, lambda c: 0)                     # ~_Lockit
        # CString (MFC42), held as a pointer to a copy of the text.
        empty = self.string('')
        cpu.stub_import(0x5bb454, lambda c: (c.u32(c.ecx, empty), c.ecx)[1])
        cpu.stub_import(0x5bb458, lambda c: (c.u32(c.ecx, self.string(self.cstr(c.arg(0)))), c.ecx)[1], 4)
        cpu.stub_import(0x5bb468, lambda c: 0)
        cpu.stub_import(0x5bb2dc, lambda c: (c.u32(c.ecx, empty), 0)[1])
        # PM's are cdecl (MW3's took ECX, EDX). The log keeps MW3's addresses: the port's probe says those.
        self.stub(0x596e50, self.message)
        self.stub(0x536c00, self.bind)
        self.stub(0x536a40, lambda c: self.call('0x0051d1d0', [c.arg(0) & 0xff]))
        self.stub(0x536a30, lambda c: self.call('0x0051d1c0', []))
        for a, key in GETTER_KEYS.items():
            self.stub(a, lambda c, key=key: self.call(key, [c.arg(0)]))
        self.stub(0x534ad0, lambda c: self.call('0x0051b2c0', []))
        self.stub(0x537420, lambda c: 0)
        self.stub(0x579300, lambda c: 0xffffffff)  # the open's look in the archives' list: its result unused (0x578630)
        # Files: the search 0x5780e0 and CreateFileA find them in the folder.
        self.stub(0x5780e0, lambda c: c.arg(0) if self.host(self.cstr(c.arg(0))).is_file() else 0)
        cpu.stub_import(0x5bb0c8, self.create_file, 28)
        cpu.stub_import(0x5bb0d0, lambda c: 1, 4)
        cpu.stub_import(0x5bb614, self.splitpath)
        cpu.stub_import(0x5bb654, self.strncmp)
        cpu.stub_import(0x5bb620, self.sscanf)
        cpu.stub_import(0x5bb624, lambda c: 1 if 0x30 <= c.arg(0) <= 0x39 else 0)
        cpu.stub_import(0x5bb628, self.fopen)
        cpu.stub_import(0x5bb62c, self.fprintf)
        cpu.stub_import(0x5bb630, lambda c: 0)
        # From here every address is PM's, as the exe hands them to the stubs.
        cpu.tr, cpu.uc = (lambda a, size=1: a), cpu._uc
        self.run(POOL, stack_args=(64,))
        self.run(DEFINES)
        self.run(AXIS_INIT)
        self.run(MAP_INIT)
        self.handles = 0x100

    # PM's addresses as they are: the cpu's own calls and memory take MW3's and translate them.
    def stub(self, addr, fn, pop=0):
        self.cpu.stubs[addr] = (fn, pop)
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.cpu._stub_hit, begin=addr, end=addr)

    def bytes_at(self, a):
        out = bytearray()
        while (b := self.cpu._uc.mem_read(a + len(out), 1)[0]):
            out.append(b)
        return bytes(out)

    def poke(self, a, data):
        self.cpu._uc.mem_write(a, bytes(data))

    def peek(self, a, n):
        return bytes(self.cpu._uc.mem_read(a, n))

    def alloc(self, n):
        a = self.cpu.alloc(max(n, 4))
        self.cpu.write(a, b'\0' * max(n, 4))
        return a

    def string(self, s):
        a = self.alloc(len(s) + 1)
        self.cpu.write(a, s.encode('latin-1') + b'\0')
        return a

    def host(self, path):
        return self.folder / path.replace('\\', '/')

    def call(self, key, args):
        self.log.append((key, tuple(args)))
        return self.model.handle(key, args)

    def message(self, c):
        self.log.append(('0x00576040', (c.arg(0),)))
        return self.string(f'MSG_{c.arg(0):x}')

    def bind(self, c):
        name = self.cstr(c.arg(1)) if c.arg(1) else ''
        return self.call('0x0051d2c0', [c.arg(0), name, c.arg(2), c.arg(3), c.arg(4), c.arg(5)])

    def create_file(self, c):
        p = self.host(self.cstr(c.arg(0)))
        if not p.is_file():
            return 0xffffffff
        self.handles += 1
        self.files[self.handles] = p.read_bytes()
        self.crt['pos'][self.handles] = 0
        return self.handles

    def splitpath(self, c):
        path = self.cstr(c.arg(0))
        cut = max(path.rfind('\\'), path.rfind('/'), path.rfind(':'))
        name = path[cut + 1:]
        dot = name.rfind('.')
        fname, ext = (name[:dot], name[dot:]) if dot >= 0 else (name, '')
        if c.arg(3):
            self.cpu.write(c.arg(3), fname.encode('latin-1') + b'\0')
        if c.arg(4):
            self.cpu.write(c.arg(4), ext.encode('latin-1') + b'\0')
        return 0

    def strncmp(self, c):
        a = self.bytes_at(c.arg(0))[:c.arg(2)]
        b = self.bytes_at(c.arg(1))[:c.arg(2)]
        return (a > b) - (a < b)

    def sscanf(self, c):
        s, fmt = self.bytes_at(c.arg(0)), self.cstr(c.arg(1))
        n, v = (text_crt.scan_hex if fmt == '%x' else text_crt.scan_dec)(s)
        if n:
            self.cpu.u32(c.arg(2), v)
        return n

    def fopen(self, c):
        self.out[self.cstr(c.arg(0))] = []
        self.open_name = self.cstr(c.arg(0))
        return 0x5000

    def fprintf(self, c):
        fmt = self.cstr(c.arg(1))
        out, i, n = [], 2, 0
        while n < len(fmt):
            ch = fmt[n]
            if ch != '%':
                out.append(ch)
                n += 1
                continue
            spec = fmt[n + 1]
            n += 2
            if spec == 's':
                out.append(self.cstr(c.arg(i)))
                i += 1
            elif spec == 'x':
                out.append(f'{c.arg(i):x}')
                i += 1
            elif spec == 'f':
                v = struct.unpack('<d', struct.pack('<II', c.arg(i), c.arg(i + 1)))[0]
                out.append('%f' % v)
                i += 2
            else:
                raise ValueError(fmt)
        self.out[self.open_name].append(''.join(out))
        return 0

    def run(self, addr, **kw):
        try:
            if addr not in self.thunks:
                self.thunks[addr] = self.cpu.code(b'\xb8' + struct.pack('<I', addr) + b'\xff\xe0')  # mov eax, addr; jmp eax
            thunk = self.thunks[addr]
            self.cpu.call(thunk, count=50_000_000, **kw)
        except Exception as e:
            eip = self.cpu.uc.reg_read(UC_X86_REG_EIP)
            raise SystemExit(f'{addr:#x}: the original stopped at {eip:#x}: {e}')
        return self.cpu.uc.reg_read(UC_X86_REG_EAX)

    def load(self, name):
        """The file, with FIX (bug): a first entry with no source and no KEYS takes the path's address as its source
        (0x412a26); the port gives 0."""
        path = self.string(name)
        self.run(LOAD, stack_args=(path,))
        for k in range(4):
            if self.u32(TABLE + 16 * k) == path:
                self.poke(TABLE + 16 * k, bytes(4))
                self.pointers += 1

    def u32(self, a):
        return struct.unpack('<I', self.peek(a, 4))[0]

    def dump(self):
        r32, r8 = self.u32, lambda a: self.peek(a, 1)[0]
        out = ['C']
        for i in range(0xb0):
            b = CMDS + 0x14 * i
            msg, name, ak, axis = r32(b), r32(b + 4), r8(b + 8), r32(b + 0x10)
            if msg or name or ak or axis:
                out.append(f'{i}:{msg:x}:{self.cstr(name) if name else ""}:{ak}:{struct.unpack("<i", struct.pack("<I", axis))[0]};')
        out.append(f' G{struct.unpack("<i", struct.pack("<I", r32(GROUP)))[0]}:')
        begin, end = r32(GROUPS), r32(GROUPS + 4)
        for g in range(begin, end, 4) if begin else []:
            p = r32(g)
            out.append(self.cstr(r32(p)) + ':')
            cb, ce = r32(p + 8), r32(p + 0xc)
            for k in range(cb, ce, 4) if cb else []:
                out.append(f'{r32(k):x},')
            out.append(';')
        out.append(' N')
        nil, head = r32(NIL), r32(MAP + 4)

        def walk(n):
            if n == nil:
                return
            walk(r32(n))
            out.append(f'{self.cstr(r32(n + 0xc))}={r32(n + 0x10):x};')
            walk(r32(n + 8))
        walk(r32(head + 4))
        out.append(' A')
        for k in range(4):
            w = [r32(TABLE + 16 * k + 4 * j) for j in range(4)]
            out.append(f'{w[0]:x},{w[1]:08x},{w[2]:08x},{w[3]:08x};')
        out.append(f'{r8(READY)},{r8(STOCK)}')
        return ''.join(out)


class Port:
    def __init__(self, folder):
        self.log = []
        self.model = Model()

        def cb(key, vals, strs, n):
            args = [strs[i].decode('latin-1') if strs[i] is not None else vals[i] for i in range(n)]
            key = key.decode()
            self.log.append((key, tuple(args)))
            return self.model.handle(key, args) & 0xffffffff
        self.cb = CALLBACK(cb)
        probe.probe_ctl_init(self.cb, str(folder).encode())

    def dump(self):
        buf = ctypes.create_string_buffer(1 << 16)
        if probe.probe_ctl_dump(buf, len(buf)) < 0:
            raise SystemExit('dump too large')
        return buf.value.decode('latin-1')


COMMANDS = ['CMD_THROTTLE_UP', 'CMD_TURN_LEFT', 'CMD_TRIGGER1', 'CMD_JJ_UP', 'CMD_MASC', 'CMD_FREELOOK',
            'CMD_MISSION_COMPLETE', 'CMD_WEPGRP_3', 'CMD_HEADLIGHT_TOGGLE', 'CMD_DIRECT_PAUSE']
SOURCES = ['X', 'Y', 'Z', 'Rx', 'Ry', 'Rz', 'Slider0', 'Slider1', 'W', '']
AXES = ['Throttle', 'Twist', 'Pitch', 'LR']
STOCK_AXES = ['(Throttle\tjoystick(Z)\tslope(-0.500000)\tintercept(0.500000)\tdeadzone(0.050000))',
              '(Twist\tjoystick(Rz)\tslope(-1.000000)\tintercept(0.000000)\tdeadzone(0.000000))',
              '(Pitch\tjoystick(Y)\tslope(1.000000)\tintercept(0.000000)\tdeadzone(0.000000))',
              '(LR\tjoystick(X)\tslope(-1.000000)\tintercept(0.000000)\tdeadzone(0.000000))']


def value(r):
    k = r.random()
    if k < 0.7:
        return f'0x{r.randrange(0, 0x800):x}'
    return r.choice(['28', '28x', '0X1c', 'abc', '0x7ff', '0x800', '0xffffffff', '-5x', '9'])


def random_file(r, unknown_axis):
    lines = ['(']
    if r.random() < 0.85:
        lines.append('\tKEYS (')
        names = r.sample(COMMANDS, r.randrange(0, len(COMMANDS)))
        if r.random() < 0.3:
            names.append(r.choice(['CMD_BOGUS', 'CMD_NOT_A_COMMAND', 'keys']))
        for name in names:
            s = f'\t\t({name}'
            for k in ('keya', 'keyb', 'mousebtn', 'joybtn'):
                if r.random() < 0.5:
                    s += f'\t{k}({value(r)})'
            lines.append(s + ')')
        lines.append('\t)')
    if r.random() < 0.85:
        lines.append('\tAXES (')
        if r.random() < 0.2:
            stock = list(STOCK_AXES)
            if r.random() < 0.5:  # one value off: no longer stock
                i = r.randrange(4)
                stock[i] = stock[i].replace(r.choice(['deadzone(0.0', 'slope(', 'intercept(0.0']),
                                            r.choice(['deadzone(0.1', 'slope(2', 'intercept(0.2']), 1)
            lines += ['\t\t' + a for a in r.sample(stock, 4)]
            if r.random() < 0.4:  # a later entry for an axis: the stock mark stays
                lines.append(f'\t\t({r.choice(AXES)}\tjoystick(Slider0)\tslope(0.250000))')
        else:
            for _ in range(r.randrange(0, 6)):
                name = r.choice(AXES)
                s = f'\t\t({name}'
                k = r.random()
                if k < 0.45:
                    s += f'\tjoystick({r.choice(SOURCES)})'
                elif k < 0.85:
                    s += f'\tmouse({r.choice(["X", "Y", "Z", "Rz", ""])})'
                for key, lo, hi in (('slope', -2, 2), ('intercept', -1, 1), ('deadzone', 0, 0.3)):
                    if r.random() < 0.7:
                        s += f'\t{key}({r.uniform(lo, hi):f})'
                lines.append(s + ')')
        if unknown_axis:
            lines.insert(r.randrange(len(lines) - 0, len(lines) + 1),
                         f'\t\t(Yaw\tjoystick(Y)\tslope({r.uniform(-2, 2):f})\tdeadzone(0.2))')
        lines.append('\t)')
    lines += [')', '']
    return '\n'.join(lines).encode()


def compare(step, what, emu, port):
    a, b = emu.dump(), port.dump()
    if a != b:
        print(f'step {step} ({what}): state differs')
        for i, (x, y) in enumerate(zip(a.split(';'), b.split(';'))):
            if x != y:
                print(f'  original {x!r}\n  port     {y!r}')
                break
        raise SystemExit(1)
    if emu.log != port.log:
        n = next((i for i, (x, y) in enumerate(zip(emu.log, port.log)) if x != y), min(len(emu.log), len(port.log)))
        print(f'step {step} ({what}): calls differ at {n} (original {len(emu.log)}, port {len(port.log)})')
        for i in range(max(0, n - 2), n + 4):
            print('  ', emu.log[i] if i < len(emu.log) else '-', '|', port.log[i] if i < len(port.log) else '-')
        raise SystemExit(1)


def main():
    r = random.Random(1999)
    tmp = Path(tempfile.mkdtemp())
    counts = {}
    try:
        emu, port = Emulated(tmp), Port(tmp)
        compare(0, 'start', emu, port)
        for step in range(1, 401):
            k = r.random()
            emu.log.clear()
            port.log.clear()
            if step < 3 or k < 0.15:
                what = 'defaults'
                a = emu.run(DEFAULTS)
                b = probe.probe_ctl_defaults()
                if a != b:
                    raise SystemExit(f'step {step}: defaults returned {a} and {b}')
            elif k < 0.6:
                unknown_axis = r.random() < 0.1
                what = 'load' + (' (unknown axis)' if unknown_axis else '')
                name = f'keys\\p{step}.zrd' if r.random() < 0.9 else 'keys\\missing.zrd'
                if 'missing' not in name:
                    p = emu.host(name)
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_bytes(random_file(r, unknown_axis))
                saved = emu.peek(READY + 2, 14)
                emu.load(name)
                if unknown_axis:
                    counts['overwrites'] = counts.get('overwrites', 0) + (emu.peek(READY + 2, 14) != saved)
                    emu.poke(READY + 2, saved)  # FIX (bug): the port writes nothing there
                probe.probe_ctl_load(name.encode())
            elif k < 0.75:
                what = 'save'
                name = f'keys\\s{step}.zrd'
                s = emu.string(name)
                emu.run(SAVE, stack_args=(s,))
                probe.probe_ctl_save(name.encode())
                want = ''.join(emu.out[name])
                got = emu.host(name).read_text()
                if want != got:
                    print(f'step {step}: written files differ\n--- original\n{want}\n--- port\n{got}')
                    raise SystemExit(1)
                # The saved file read back on the next steps sometimes.
                if r.random() < 0.5:
                    emu.load(name)
                    probe.probe_ctl_load(name.encode())
                    what = 'save and load'
            elif k < 0.95:
                what = 'set_mode'
                if r.random() < 0.6:
                    words = [r.choice([0, 1, 3, 9, 10, 11, r.randrange(12)]) if j % 4 == 0 else
                             struct.unpack('<I', struct.pack('<f', r.uniform(-2, 2)))[0] for j in range(16)]
                    ready, stock = r.choice([0, 1, 1]), r.choice([0, 1])
                    for j, w in enumerate(words):
                        emu.poke(TABLE + 4 * j, struct.pack('<I', w))
                    emu.poke(READY, bytes([ready, stock]))
                    probe.probe_ctl_set_table((ctypes.c_uint32 * 16)(*words), ready, stock)
                mode, flags, held = r.choice([0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x15]), r.randrange(1 << 8), r.randrange(2)
                obj = emu.alloc(0x80)
                emu.cpu.u32(obj + 8, flags)
                emu.cpu.write(obj + 0x3e, bytes([held]))
                emu.run(SET_MODE, ecx=obj, stack_args=(mode,))
                h = ctypes.c_uint8(held)
                got = probe.probe_ctl_set_mode(flags, ctypes.byref(h), mode)
                if (emu.u32(obj + 8), emu.cpu.read(obj + 0x3e, 1)[0]) != (got, h.value):
                    raise SystemExit(f'step {step}: set_mode fields differ')
            else:
                what = 'free'
                emu.run(FREE)
                probe.probe_ctl_free()
            counts[what] = counts.get(what, 0) + 1
            compare(step, what, emu, port)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'{emu.pointers} first entries took the path\'s address')
    print('ok: 400 steps identical (' + ', '.join(f'{k} {v}' for k, v in sorted(counts.items())) + ')')


if __name__ == '__main__':
    main()
