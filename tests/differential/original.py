"""Run functions from the original Mech3.exe 1.2 in the Unicorn x86 emulator.

Only self-contained code works here: anything that calls Windows or DirectX has
no emulated system to call into. That covers the logic the rebuild ports.

The exe is MW3 1.2's by default. Pirate's Moon's (game/pm) is the alternative,
for the function-by-function diff (CLAUDE.md): pass `Original(exe='pm')`, or
set MW3_EXE=pm (or a path to either exe) to run any test against it. The
tests' addresses are MW3 1.2's; on PM's exe each image address given to
call, read, write, u32 and the stubs is translated through
analysis-output/pm-diff/addrmap.json (tools/analysis/pmaddr.py: functions,
instructions, import slots, globals and their blocks; docs/pm-map.md), and
one the map lacks raises Unmapped. Addresses a test passes to `uc` itself or
writes as values are not translated.
"""
import hashlib
import os
import struct
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, Uc
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP,
                                UC_X86_REG_ESP, UC_X86_REG_FS, UC_X86_REG_GDTR,
                                UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES)

ROOT = Path(__file__).resolve().parents[2]
# The exes the tests know: name -> (path, SHA-256, what it is).
EXES = {
    'mw3': (ROOT / 'game' / 'mw3-1.2' / 'Mech3.exe',
            '95bc2c114c9b2e5c5ada8e40ca78cc79470f862e45313ffad0d1e8e6d4d916bf', 'Mech3.exe 1.2'),
    'pm': (ROOT / 'game' / 'pm' / 'Mech3.exe',
           '57cd2ee74bafda3eeb9e9353c7c78a8b7c213a81f3040f1be7c2a164c505ddd9', "Pirate's Moon 1.0 Mech3.exe"),
}


def select(choice=None):
    """The exe to run: a name in EXES or a path to one of them; by default
    MW3_EXE's, else MW3 1.2's. Returns (name, path, sha256, description)."""
    choice = choice or os.environ.get('MW3_EXE') or 'mw3'
    if str(choice) in EXES:
        name = str(choice)
    else:
        path = Path(choice).resolve()
        name = next((k for k, v in EXES.items() if v[0].resolve() == path), None)
        if name is None:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            name = next((k for k, v in EXES.items() if v[1] == digest), None)
            if name is None:
                raise SystemExit(f'{choice} is neither Mech3.exe 1.2 nor Pirate\'s Moon\'s')
            return (name, path) + EXES[name][1:]
    return (name,) + EXES[name]


GAME, EXE, SHA256, _ = select()
IMAGE_BASE = 0x400000
STACK_TOP = 0x30000000
RETURN = 0x2fff0000   # a mapped address nothing executes; emulation stops there
HEAP = 0x10000000
STUBS = 0x2f000000  # one 16-byte slot per imported function we stand in for
CODE = 0x2f001000   # x86 written here for imports that are easier as machine code
TEB = 0x2e000000    # a thread block for fs:[0], the SEH chain MSVC code links into
SINK = 0x2e080000   # PM's exe: the import slots it does not have (AddressMap)

# MSVCRT's _ftol, instruction for instruction: FISTP with the rounding mode set
# to chop, the 64-bit result in EDX:EAX.
FTOL = bytes.fromhex('55 8bec 83ec0c d97dfe 668b45fe 80cc0c 668945fc d96dfc df7df4 d96dfe'
                     '8b45f4 8b55f8 c9 c3')
IAT_FTOL = 0x5bb644


class Unmapped(Exception):
    """An MW3 address with no known place in PM's exe."""


def _text(path):
    """An exe's code section: (its address, its bytes)."""
    data = Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optsize = struct.unpack_from('<H', data, pe + 20)[0]
    for i in range(count):
        o = pe + 24 + optsize + i * 40
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
        if struct.unpack_from('<I', data, o + 36)[0] & 0x20000000:
            return IMAGE_BASE + va, data[raw:raw + rsize]
    return 0, b''


class AddressMap:
    """MW3 1.2's image addresses to PM 1.0's (pmaddr.py's map)."""

    def __init__(self, path=ROOT / 'analysis-output' / 'pm-diff' / 'addrmap.json'):
        import bisect
        import json
        m = json.loads(Path(path).read_text())
        self.exact = {}
        for part in ('data', 'imports', 'instructions', 'functions'):  # a function's entry wins
            self.exact.update({int(a, 16): int(b, 16) for a, b in m[part].items()})
        self.blocks = sorted((int(lo, 16), int(hi, 16), d) for lo, hi, d in m['blocks'])
        self._lo = [b[0] for b in self.blocks]
        self._bisect = bisect.bisect_right
        # imports PM does not have: slots on a scratch page, so a test's stub
        # of them lands somewhere harmless
        for i, a in enumerate(m.get('imports_missing', [])):
            self.exact[int(a, 16)] = SINK + 4 * i
        self._code = None

    def __call__(self, addr, size=1):
        if not IMAGE_BASE <= addr < 0x10000000:
            return addr
        if addr in self.exact:
            return self.exact[addr]
        i = self._bisect(self._lo, addr) - 1
        if i >= 0:
            lo, hi, d = self.blocks[i]
            if addr + size - 1 <= hi + 0x40:  # within the block (its last item's fields too)
                return addr + d
        found = self._by_bytes(addr)
        if found is not None:
            self.exact[addr] = found
            return found
        raise Unmapped(f'{addr:#x} has no place in PM\'s exe')

    def _by_bytes(self, addr):
        """Code the map lacks (a C runtime helper, a hook inside a function
        Ghidra did not make): its first 24 bytes found once in PM's code."""
        if self._code is None:
            self._code = (_text(EXES['mw3'][0]), _text(EXES['pm'][0]))
        (va_a, a), (va_b, b) = self._code
        if not va_a <= addr < va_a + len(a) - 24:
            return None
        key = a[addr - va_a:addr - va_a + 24]
        i = b.find(key)
        if i < 0 or b.find(key, i + 1) >= 0:
            return None
        return va_b + i


class UcProxy:
    """The emulator as a test on PM's exe sees it: hook ranges and memory
    addresses in the image translated (AddressMap), and hook callbacks given
    MW3's addresses back. Anything else goes straight through."""

    def __init__(self, uc, tr):
        self._uc, self._tr = uc, tr
        back = {}
        for a, b in tr.exact.items():
            back.setdefault(b, a)
        self._back, self._blocks = back, [(lo + d, hi + d, d) for lo, hi, d in tr.blocks]

    def __getattr__(self, name):
        return getattr(self._uc, name)

    def _mw3(self, addr):
        if addr in self._back:
            return self._back[addr]
        for lo, hi, d in self._blocks:
            if lo <= addr <= hi + 0x40:
                return addr - d
        return addr

    def mem_read(self, addr, size):
        return self._uc.mem_read(self._tr(addr, size), size)

    def mem_write(self, addr, data):
        return self._uc.mem_write(self._tr(addr, len(data)), data)

    def hook_add(self, htype, callback, user_data=None, begin=1, end=0, *args):
        if begin <= end:
            begin, end = self._tr(begin), self._tr(end)
            if end < begin:
                end = begin

        def wrapped(uc, *a):
            a = list(a)
            if htype & UC_HOOK_CODE and len(a) >= 2:  # (address, size, user)
                a[0] = self._mw3(a[0])
            elif len(a) >= 4:  # memory hooks: (access, address, size, value, user)
                a[1] = self._mw3(a[1])
            return callback(self, *a)
        return self._uc.hook_add(htype, wrapped, user_data, begin, end, *args)


_MAP = None


def to_exe(addr):
    """An MW3 address in the selected exe: itself on MW3's, mapped on PM's."""
    global _MAP
    if GAME != 'pm':
        return addr
    if _MAP is None:
        _MAP = AddressMap()
    return _MAP(addr)


class Original:
    def __init__(self, exe=None):
        self.game, self.exe, sha, what = select(exe)
        self.tr = AddressMap() if self.game == 'pm' else (lambda a, size=1: a)
        data = Path(self.exe).read_bytes()
        if hashlib.sha256(data).hexdigest() != sha:
            raise SystemExit(f'{self.exe} is not {what}')
        self._uc = Uc(UC_ARCH_X86, UC_MODE_32)
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        count = struct.unpack_from('<H', data, pe + 6)[0]
        optsize = struct.unpack_from('<H', data, pe + 20)[0]
        end = 0
        sections = []
        for i in range(count):
            o = pe + 24 + optsize + i * 40
            vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
            sections.append((va, data[raw:raw + rsize]))
            end = max(end, va + max(vsize, rsize))
        size = (end + 0xfff) & ~0xfff
        self._uc.mem_map(IMAGE_BASE, size)
        self._uc.mem_write(IMAGE_BASE, data[:0x400])
        for va, body in sections:
            self._uc.mem_write(IMAGE_BASE + va, body)
        self._uc.mem_map(STACK_TOP - 0x100000, 0x100000)
        self._uc.mem_map(HEAP, 0x1000000)
        self.heap_next = HEAP
        self._uc.mem_map(STUBS, 0x1000)
        self._uc.mem_write(STUBS, b'\xc3' * 0x1000)
        self.stubs = {}
        self._uc.hook_add(UC_HOOK_CODE, self._stub_hit, begin=STUBS, end=STUBS + 0xfff)
        self._uc.mem_map(CODE, 0x1000)
        self._uc.mem_map(TEB, 0x2000)
        self._uc.mem_map(SINK, 0x1000)
        self.u32(TEB, 0xffffffff)
        # A GDT with flat 32-bit data (1) and an FS segment based at TEB (2).
        gdt = TEB + 0x1000

        def seg(base, limit, access):
            return (limit & 0xffff) | ((base & 0xffffff) << 16) | (access << 40) | \
                (((limit >> 16) & 0xf) << 48) | (0xc << 52) | ((base >> 24) << 56)
        self.write(gdt + 8, struct.pack('<Q', seg(0, 0xfffff, 0x92)))
        self.write(gdt + 16, struct.pack('<Q', seg(TEB, 0x1, 0x92)))
        self._uc.reg_write(UC_X86_REG_GDTR, (0, gdt, 0x18 - 1, 0))
        for reg in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES):
            self._uc.reg_write(reg, 1 << 3)
        self._uc.reg_write(UC_X86_REG_FS, 2 << 3)
        self.code_next = CODE
        # In play the x87 runs at 24-bit precision (Direct3D 6, no
        # DDSCL_FPUPRESERVE; CLAUDE.md). MW3_X87=24 starts every run there;
        # the default is the power-on 64-bit (0x037f).
        if os.environ.get('MW3_X87') == '24':
            self.fpu_control(0x007f)
        # A test's own use of the emulator: on PM's exe through the map too.
        self.uc = UcProxy(self._uc, self.tr) if self.game == 'pm' else self._uc

    def fpu_control(self, cw):
        """Loads the x87 control word (0x037f power-on, 0x007f single precision)."""
        slot = self.alloc(4)
        self.u32(slot, cw)
        self.call(self.code(b'\xd9\x2d' + struct.pack('<I', slot) + b'\xc3'))

    def alloc(self, size):
        addr = self.heap_next
        self.heap_next += (size + 0xf) & ~0xf
        return addr

    def write(self, addr, data):
        self._uc.mem_write(self.tr(addr, len(data)), bytes(data))

    def read(self, addr, size):
        return bytes(self._uc.mem_read(self.tr(addr, size), size))

    def u32(self, addr, value):
        self._uc.mem_write(self.tr(addr, 4), struct.pack('<I', value & 0xffffffff))

    def call(self, addr, ecx=0, edx=0, stack_args=(), count=50_000_000):
        """Call a __fastcall/__stdcall function; returns when it returns."""
        esp = STACK_TOP - 0x1000
        for arg in reversed(stack_args):
            esp -= 4
            self.u32(esp, arg)
        esp -= 4
        self.u32(esp, RETURN)
        self._uc.reg_write(UC_X86_REG_ESP, esp)
        self._uc.reg_write(UC_X86_REG_ECX, ecx & 0xffffffff)
        self._uc.reg_write(UC_X86_REG_EDX, edx & 0xffffffff)
        addr = self.tr(addr)
        self._uc.emu_start(addr, RETURN, count=count)
        if self._uc.reg_read(UC_X86_REG_EIP) != RETURN:
            raise RuntimeError(f'{addr:#x} did not return within {count} instructions')

    def arg(self, n):
        """Stack argument n (0-based) of the function being entered."""
        esp = self._uc.reg_read(UC_X86_REG_ESP)
        return struct.unpack('<I', self.read(esp + 4 + 4 * n, 4))[0]

    @property
    def ecx(self):
        return self._uc.reg_read(UC_X86_REG_ECX)

    @property
    def edx(self):
        return self._uc.reg_read(UC_X86_REG_EDX)

    def stub_import(self, iat_slot, fn, pop_bytes=0):
        """Stand in for an imported Windows or C runtime function.

        `iat_slot` is the import table address the exe calls through, such as
        0x5bb0c4 for ReadFile. `fn(self)` returns EAX; `pop_bytes` is what the
        callee pops (stdcall), 0 for cdecl.
        """
        addr = STUBS + 16 * len(self.stubs)
        self.stubs[addr] = (fn, pop_bytes)
        self.u32(iat_slot, addr)  # u32 translates the slot

    def stub_function(self, addr, fn, pop_bytes=0):
        """Stand in for one of the exe's own functions: runs `fn(self)` on entry
        and returns EAX from it, popping `pop_bytes` of arguments."""
        addr = self.tr(addr)
        self.stubs[addr] = (fn, pop_bytes)
        self._uc.hook_add(UC_HOOK_CODE, self._stub_hit, begin=addr, end=addr)

    def code(self, code):
        """Write a little x86 routine; returns its address."""
        addr = self.code_next
        self.write(addr, code)
        self.code_next += (len(code) + 0xf) & ~0xf
        return addr

    def import_code(self, iat_slot, code):
        """Stand in for an import with x86 machine code, such as FTOL."""
        self.write(self.code_next, code)
        self.u32(iat_slot, self.code_next)
        self.code_next += (len(code) + 0xf) & ~0xf

    def _stub_hit(self, uc, address, size, user_data):
        if address not in self.stubs:  # unstubbed (a test that runs what another stubs)
            return
        fn, pop_bytes = self.stubs[address]
        eax = fn(self)
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack('<I', self.read(esp, 4))[0]
        uc.reg_write(UC_X86_REG_EAX, (eax or 0) & 0xffffffff)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop_bytes)
        uc.reg_write(UC_X86_REG_EIP, ret)
