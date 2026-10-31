"""Call-trace comparison for orchestration code (docs/game-loop.md, Testing).

The application, its loop and the game states mostly call other systems in a
fixed order. To check them, every call out of the code under test is stubbed:
in the emulator each callee address (or a slot of a fake object's vtable) is a
stub, and in the rebuild each hook method reports to the probe's callback. Both
sides go to the same `Trace.handle`, which records (key, args) and answers from
a script that depends only on the key and how often it was asked, so the two
runs get the same answers as long as they make the same calls. The recorded
traces must be equal.

Keys are the callee's address as "0x%08x", "<object>.vt+0x%x" for virtual calls
on fake objects, or a Windows API name.
"""
import bisect
import ctypes
import random
import re
import struct
import sys
from pathlib import Path

from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP,
                                UC_X86_REG_ESP)

sys.path.insert(0, str(Path(__file__).parent))
from original import EXE, FTOL, GAME, IAT_FTOL, ROOT, Original, to_exe  # noqa: E402

ASM = ROOT / 'analysis-output' / 'mech3.asm'
_ret_cache = {}
_asm_index = None
_asm_lines = None

CALLBACK = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint32),
                            ctypes.POINTER(ctypes.c_char_p), ctypes.c_int32)


def load_probe():
    probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libmw3probe.*'))))
    return probe


def _asm():
    global _asm_index, _asm_lines
    if _asm_index is None:
        _asm_lines = ASM.read_text().split('\n')
        _asm_index = {}
        for i, line in enumerate(_asm_lines):
            m = re.match(r'^\s+([0-9a-f]+):', line)
            if m:
                _asm_index[int(m.group(1), 16)] = i
    return _asm_index, _asm_lines


_starts_cache = None


def _starts():
    """Function starts: every function Ghidra or the vtable scan found."""
    global _starts_cache
    if _starts_cache is None:
        d = ROOT / 'analysis-output' / 'decomp'
        _starts_cache = sorted(int(p.name[2:-2], 16) for p in d.iterdir()
                               if p.name.startswith('0x') and p.name.endswith('.c'))
    return _starts_cache


class PmImage:
    """PM's image as MW3's addresses see it (snapshot): a byte through the
    address map, 0 where the map has none."""

    def __init__(self, cpu):
        self.cpu = cpu

    def __len__(self):
        return 0x420000

    def __getitem__(self, off):
        try:
            return self.cpu.read(0x400000 + off, 1)[0]
        except Exception:
            return 0


def ret_pop(addr):
    """Bytes a function pops on return, from its first `ret` in the disassembly."""
    if addr in _ret_cache:
        return _ret_cache[addr]
    index, lines = _asm()
    pop = 0
    starts = _starts()
    j = bisect.bisect_right(starts, addr)
    end = starts[j] if j < len(starts) else addr + 0x10000
    # mech3.asm lost sync around a jump table, or is MW3's listing while
    # another exe runs (original.select): decode this one from the exe.
    if addr not in index or GAME != 'mw3':
        import subprocess
        at = to_exe(addr)  # PM's exe: the twin's code
        stop = end if GAME == 'mw3' else at + 0x4000
        out = subprocess.run(['objdump', '-d', '--no-show-raw-insn', '--x86-asm-syntax=intel', f'--start-address={at:#x}',
                              f'--stop-address={stop:#x}', str(EXE)],
                             capture_output=True, text=True).stdout
        lines = out.split('\n')
        index = {addr: 0}
        if GAME != 'mw3':
            end = stop
    i = index[addr]
    for line in lines[i:i + 4000]:
        m = re.match(r'^\s+([0-9a-f]+):', line)
        if m and int(m.group(1), 16) >= end:  # a tail jump: no ret of its own
            break
        body = line.split(':', 1)[-1].strip()
        if body.startswith('jmp\tdword ptr [0x5b'):  # an import thunk: callers say
            pop = None
            break
        if body.startswith('ret'):
            parts = body.split()
            pop = int(parts[1], 16) if len(parts) > 1 else 0
            break
    _ret_cache[addr] = pop
    return pop


class Trace:
    """Records calls from both sides and answers them from one script."""

    def __init__(self, script=None, seed=0):
        self.log = []
        self.counts = {}
        self.script = script or {}
        self.seed = seed
        self.silent = set()
        self.answers = {}

    def reset(self):
        self.log = []
        self.counts = {}

    def override(self, key, value):
        """A stub's answer other than the script's (a constructor's `this`)."""
        self.answers[(key, self.counts[key] - 1)] = value & 0xffffffff

    def answer(self, key, args):
        n = self.counts.get(key, 0)
        self.counts[key] = n + 1
        s = self.script.get(key)
        if s is None:
            for pat, v in self.script.items():
                if isinstance(pat, str) and pat.endswith('*') and key.startswith(pat[:-1]):
                    s = v
                    break
        if s is None:
            return 0
        if callable(s):
            return s(n, args) or 0
        if isinstance(s, (list, tuple)):
            return s[n] if n < len(s) else s[-1]
        return s

    def handle(self, key, args):
        args = tuple(a if isinstance(a, str) else a & 0xffffffff for a in args)
        if key not in self.silent and not key.startswith('@'):
            self.log.append((key, args))
        n = self.counts.get(key, 0)
        v = self.answer(key, args) & 0xffffffff
        self.answers[(key, n)] = v
        return v

    # The rebuild's side.
    def callback(self):
        def cb(key, vals, strs, n):
            args = [strs[i].decode('latin1') if strs[i] is not None else vals[i] for i in range(n)]
            return self.handle(key.decode(), args)
        self._cb = CALLBACK(cb)  # keep it alive
        return self._cb


class Replay(Trace):
    """The rebuild's side of a run whose original side ran first: every call
    gets the answer the original's stub gave at the same occurrence, and
    peek/poke go to a model of the original's memory (its state before the run,
    what stubs wrote into it, and what the rebuild wrote)."""

    MEMORY = ('peek32', 'peek8', 'poke32', 'poke8')

    def __init__(self, original, emu, snapshot):
        super().__init__()
        self.recorded = original.answers
        self.emu = emu
        self.snapshot = snapshot
        self.shadow = {}
        self.silent = set(original.silent) | set(self.MEMORY)

    def read8(self, a):
        if a in self.shadow:
            return self.shadow[a]
        if a in self.emu.world_bytes:
            return self.emu.world_bytes[a]
        return self.snapshot(a)

    def handle(self, key, args):
        args = tuple(a if isinstance(a, str) else a & 0xffffffff for a in args)
        if key == 'peek32':
            return sum(self.read8(args[0] + i) << (8 * i) for i in range(4))
        if key == 'peek8':
            return self.read8(args[0])
        if key == 'poke32':
            for i in range(4):
                self.shadow[args[0] + i] = (args[1] >> (8 * i)) & 0xff
            return 0
        if key == 'poke8':
            self.shadow[args[0]] = args[1] & 0xff
            return 0
        if key not in self.silent and not key.startswith('@'):
            self.log.append((key, args))
        n = self.counts.get(key, 0)
        self.counts[key] = n + 1
        if (key, n) in self.recorded:
            return self.recorded[(key, n)]
        if key in self.script:  # answers the original never gave (probe-only keys)
            s = self.script[key]
            return (s(n, args) if callable(s) else s) & 0xffffffff
        return 0


def rnd(seed, key):
    """A random generator that depends only on the seed and the key."""
    return random.Random(f'{seed}:{key}')


class Emu:
    """The original's side: an emulator whose outgoing calls go to a Trace."""

    def __init__(self, trace):
        self.cpu = Original()
        self.cpu.import_code(IAT_FTOL, FTOL)
        self.t = trace
        self.uc = self.cpu.uc
        self.stub_base = 0x2e100000
        self.uc.mem_map(self.stub_base, 0x10000)
        self.uc.mem_write(self.stub_base, b'\xc3' * 0x10000)
        self.stub_next = self.stub_base
        self.slots = {}
        self.uc.hook_add(UC_HOOK_CODE, self._hit, begin=self.stub_base,
                         end=self.stub_base + 0xffff)
        self.heap_blocks = {}
        # Fake objects and strings of the modules not ported live here, apart
        # from the heap the original's own `new` uses.
        self.world_base = 0x2c000000
        self.world_size = 0x400000
        self.uc.mem_map(self.world_base, self.world_size)
        self.world_next = self.world_base
        self.world_bytes = {}  # what stubs wrote, for the rebuild's reads
        self.writes = set()    # what the code under test wrote in watched ranges
        self.watch = []
        self.ignore = []

    def world_alloc(self, n):
        a = self.world_next
        self.world_next += (n + 0xf) & ~0xf
        return a

    def world_string(self, s):
        a = self.world_alloc(len(s) + 1)
        self.cpu.write(a, s.encode('latin1') + b'\0')
        return a

    def world_w32(self, a, v):
        """A stub's write: the rebuild sees it through peek."""
        self.w32(a, v)
        for i in range(4):
            self.world_bytes[a + i] = (v >> (8 * i)) & 0xff

    def watch_writes(self, ranges, ignore=()):
        """Record the bytes the code under test writes in `ranges` (lo, hi),
        except in `ignore`; compare them with the rebuild's pokes."""
        self.watch = list(ranges)
        self.ignore = list(ignore)

        def hook(uc, access, address, size, value, user):
            if not (self.stub_base <= uc.reg_read(UC_X86_REG_EIP) < self.stub_base + 0x10000):
                for i in range(size):
                    a = address + i
                    if any(lo <= a < hi for lo, hi in self.watch) and \
                            not any(lo <= a < hi for lo, hi in self.ignore):
                        self.writes.add(a)
        for lo, hi in self.watch:
            self.uc.hook_add(UC_HOOK_MEM_WRITE, hook, begin=lo, end=hi - 1)

    def snapshot(self):
        """The memory as it is now, for the rebuild's reads."""
        image = (0x400000, self.cpu.read(0x400000, 0x420000)) if GAME == 'mw3' else (0x400000, PmImage(self.cpu))
        regions = [image,
                   (self.world_base, self.cpu.read(self.world_base, self.world_next - self.world_base)),
                   (0x10000000, self.cpu.read(0x10000000, self.cpu.heap_next - 0x10000000))]

        def read8(a):
            for base, data in regions:
                if base <= a < base + len(data):
                    return data[a - base]
            return 0
        return read8

    def r32(self, a):
        return struct.unpack('<I', self.cpu.read(a, 4))[0]

    def r8(self, a):
        return self.cpu.read(a, 1)[0]

    def cstr(self, a, limit=4096):
        b = self.cpu.read(a, limit)
        return b[:b.index(0)].decode('latin1') if 0 in b else b.decode('latin1')

    def w32(self, a, v):
        self.cpu.u32(a, v)

    def w8(self, a, v):
        self.cpu.write(a, bytes([v & 0xff]))

    def f32(self, a):
        return struct.unpack('<f', self.cpu.read(a, 4))[0]

    def wf(self, a, v):
        self.cpu.write(a, struct.pack('<f', v))

    def args(self, spec):
        """Arguments named by `spec`: c/d (ECX/EDX), cl/dl (their low bytes),
        sN (stack dword N), sbN (its low byte); a z prefix (zc, zd, zsN) reads
        the C string the value points to."""
        uc = self.uc
        esp = uc.reg_read(UC_X86_REG_ESP)
        out = []
        for tok in spec.split(',') if spec else []:
            if tok.startswith('z'):
                p = self.args(tok[1:])[0]
                out.append(self.cstr(p) if p else 0)
                continue
            if tok.startswith('*'):
                out.append(self.r32(self.args(tok[1:])[0]))
                continue
            if tok == 'c':
                out.append(uc.reg_read(UC_X86_REG_ECX))
            elif tok == 'd':
                out.append(uc.reg_read(UC_X86_REG_EDX))
            elif tok == 'cl':
                out.append(uc.reg_read(UC_X86_REG_ECX) & 0xff)
            elif tok == 'dl':
                out.append(uc.reg_read(UC_X86_REG_EDX) & 0xff)
            elif tok.startswith('sb'):
                out.append(self.r32(esp + 4 + 4 * int(tok[2:])) & 0xff)
            elif tok.startswith('s'):
                out.append(self.r32(esp + 4 + 4 * int(tok[1:])))
            else:
                raise ValueError(tok)
        return out

    def _ret(self, value, pop, then=None):
        uc = self.uc
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret = self.r32(esp)
        uc.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        if then is not None:  # run a thunk that returns for us
            uc.reg_write(UC_X86_REG_EIP, then)
            return
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)
        uc.reg_write(UC_X86_REG_EIP, ret)

    def _hit(self, uc, address, size, user):
        if address not in self.slots:
            return
        key, spec, pop, fn = self.slots[address]
        a = self.args(spec)
        if fn is not None:
            fn(self, key, a, pop)
            return
        self._ret(self.t.handle(key, a), pop)

    def stub(self, addr, key=None, spec='', pop=None, fn=None):
        """Stub one of the exe's functions (or an import thunk, with `pop`)."""
        if pop is None:
            pop = ret_pop(addr)
            if pop is None:
                raise ValueError(f'{addr:#x}: import thunk, give pop')
        key = key or f'{addr:#010x}'
        if addr not in self.slots:  # one hook per address; a later stub replaces
            self.uc.hook_add(UC_HOOK_CODE, self._hit, begin=addr, end=addr)
        self.slots[addr] = (key, spec, pop, fn)

    def new_stub(self, key, spec='', pop=0, fn=None):
        addr = self.stub_next
        self.stub_next += 16
        self.slots[addr] = (key, spec, pop, fn)
        return addr

    def import_stub(self, iat, key, spec='', pop=0, fn=None):
        self.w32(iat, self.new_stub(key, spec, pop, fn))

    def object(self, name, size=0x100, slots=None, vtable_size=0x100, fields=None):
        """A fake object whose vtable slots are stubs keyed "<name>.vt+<off>".
        `slots` maps an offset to (spec, pop) or (spec, pop, fn)."""
        obj = self.world_alloc(size)
        vt = self.world_alloc(vtable_size)
        self.w32(obj, vt)
        for off, s in (slots or {}).items():
            spec, pop = s[0], s[1]
            fn = s[2] if len(s) > 2 else None
            self.w32(vt + off, self.new_stub(f'{name}.vt+{off:#x}', spec, pop, fn))
        for off, v in (fields or {}).items():
            self.w32(obj + off, v)
        return obj

    def alloc(self, n):
        a = self.cpu.alloc(n)
        self.cpu.write(a, b'\0' * n)
        return a

    def thunk(self, code):
        a = self.alloc(len(code) + 16)
        self.cpu.write(a, code)
        return a

    def heap_stubs(self, new=0x5abd26, delete=0x5abd1a):
        """operator new / delete (cdecl import thunks): a bump allocator."""
        def op_new(emu, key, a, pop):
            emu._ret(emu.alloc(max(a[0], 4)), 0)

        def op_delete(emu, key, a, pop):
            emu._ret(0, 0)
        self.stub(new, 'new', 's0', 0, op_new)
        self.stub(delete, 'delete', 's0', 0, op_delete)
        self.t.silent |= {'new', 'delete'}

    def call(self, addr, ecx=0, edx=0, stack=(), count=200_000_000):
        self.cpu.call(addr, ecx=ecx, edx=edx, stack_args=stack, count=count)
        return self.uc.reg_read(UC_X86_REG_EAX)


def push_imm(v):
    return b'\x68' + struct.pack('<I', v & 0xffffffff)


def mov_ecx(v):
    return b'\xb9' + struct.pack('<I', v & 0xffffffff)


def mov_edx(v):
    return b'\xba' + struct.pack('<I', v & 0xffffffff)


def mov_eax(v):
    return b'\xb8' + struct.pack('<I', v & 0xffffffff)


def call_abs(here, target):
    """`call target` assembled at `here` (5 bytes)."""
    return b'\xe8' + struct.pack('<i', target - (here + 5))


def _same(x, y, ulps):
    if x == y:
        return True
    if x[0] != y[0] or len(x[1]) != len(y[1]) or x[0] not in ulps:
        return False
    for i, (p, q) in enumerate(zip(x[1], y[1])):
        if p == q:
            continue
        if i not in ulps[x[0]] or isinstance(p, str) or isinstance(q, str):
            return False
        # float bits: equal sign, within one unit in the last place
        if (p ^ q) & 0x80000000 or abs(p - q) > 1:
            return False
    return True


def compare(name, a, b, limit=8, ulps=None):
    """Two traces (lists of (key, args)) must be equal; reports the first split.
    `ulps` maps a key to the argument indices that are floats allowed to differ
    by one unit in the last place (x87 extended against double rounding)."""
    ulps = ulps or {}
    if len(a) == len(b) and all(_same(x, y, ulps) for x, y in zip(a, b)):
        return True
    n = next((i for i, (x, y) in enumerate(zip(a, b)) if not _same(x, y, ulps)),
             min(len(a), len(b)))
    lo = max(0, n - 4)
    print(f'{name}: traces differ at call {n} (original {len(a)} calls, rebuild {len(b)})')
    for i in range(lo, min(n + limit, max(len(a), len(b)))):
        x = a[i] if i < len(a) else None
        y = b[i] if i < len(b) else None
        mark = '  ' if x == y else '!!'
        print(f'  {mark} {i:4d} original {fmt(x):50s} rebuild {fmt(y)}')
    return False


def fmt(e):
    if e is None:
        return '-'
    key, args = e
    return key + '(' + ', '.join(repr(a) if isinstance(a, str) else f'{a:#x}' for a in args) + ')'
