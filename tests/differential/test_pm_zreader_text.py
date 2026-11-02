"""Pirate's Moon's text files (pm/gamez/zreader/zrdr_main: 0x005782f0, the
balance check 0x00578150, the lists 0x00578300, the tokens 0x00578410 and the
directives 0x00579440 with #include) against PM's exe.

    cmake --build build && .venv/bin/python -P tests/differential/test_pm_zreader_text.py

MW3's cases (test_zreader_text.py: controls files and random ones) and then
files with #include: included files with lists, tokens, directives and their
own #include, split anywhere (a token, a list or a directive's word running
on into the file after), missing ones, unbalanced ones (only the first file
is checked), an #include inside a false #ifdef, and a file that includes
itself until the 32 files are open. Every byte comes through PM's stack of
open files (0x00578720 pushes, 0x00578250 pops at a file's end). Compared:
the result, the tree node by node and the #ifdef depth after each file.
"""
import ctypes
import random
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_zar  # noqa: E402
import test_zreader_text as mw3  # noqa: E402
import text_crt  # noqa: E402
from original import ROOT  # noqa: E402
from twod_world import IAT  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EIP  # noqa: E402

PARSE, PUSH, TOP, DEPTH, DEFINES_CTOR, FIND_PATH, POOL = 0x5782f0, 0x578720, 0x612f08, 0x8c19fc, 0x579360, 0x5780e0, 0x577690
OP_NEW = 0x5ce8c6
TOP_HANDLE = 0x77


class Emulated(test_pm_zar.Emulated):
    def __init__(self):
        super().__init__()
        self.text = {}  # handle -> bytes, for text_crt's ReadFile
        self.crt = text_crt.install(self.cpu, self.text)
        self.cpu.stub_import(IAT['CreateFileA'], self.open_text, 28)
        self.cpu.stub_import(IAT['CloseHandle'], lambda c: 1, 4)
        self.native_stub(OP_NEW, lambda c: self.alloc(c.arg(0)))
        self.native_stub(FIND_PATH, lambda c: c.arg(0) if self.cstr(c.arg(0)).decode('latin1') in self.named else 0)
        self.call(DEFINES_CTOR)  # the (empty) defines list's static constructor
        self.call(POOL, args=[0x200])  # the list elements' pool, made at start (0x004828f0)
        self.named = {}
        self.handle = 0x1000
        self.opened = self.deepest = 0
        self.hooked = False

    def watch_top(self, uc, address, size, user):
        top = struct.unpack('<i', bytes(uc.mem_read(TOP, 4)))[0]
        self.deepest = max(self.deepest, top + 2)  # files open once this push lands

    def open_text(self, c):
        name = self.cstr(c.arg(0)).decode('latin1')
        if name not in self.named:
            return 0xffffffff
        self.handle += 4
        self.opened += 1
        self.text[self.handle] = self.named[name]
        self.crt['pos'][self.handle] = 0
        return self.handle

    def parse(self, data, depth, named):
        self.named = named
        self.text[TOP_HANDLE] = data
        self.crt['pos'][TOP_HANDLE] = 0
        self.cpu._uc.mem_write(TOP, struct.pack('<i', -1))  # an empty stack each file (ours: see parse_text)
        self.cpu._uc.mem_write(DEPTH, struct.pack('<i', depth))
        self.call(PUSH, args=[TOP_HANDLE])
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.watch_top, begin=PUSH, end=PUSH) if not self.hooked else None
        self.hooked = True
        root = self.call(PARSE)
        depth = struct.unpack('<i', bytes(self.cpu._uc.mem_read(DEPTH, 4)))[0]
        if not root:
            return None, depth
        out = []
        self.walk(root, out)
        return ''.join(out), depth

    def walk(self, a, out):
        t, v = self.r32(a), self.r32(a + 4)
        if t == 1:
            out.append(f'I{struct.unpack("<i", struct.pack("<I", v))[0]} ')
        elif t == 2:
            out.append(f'F{v:08x} ')
        elif t == 3:
            s = self.cstr(v)
            out.append(f'S{len(s)}:' + s.decode('latin1') + ' ')
        elif t == 4:
            count = struct.unpack('<i', struct.pack('<I', self.r32(v + 4)))[0]
            out.append(f'L{count}[')
            for i in range(1, count):
                self.walk(v + 8 * i, out)
            out.append('] ')
        else:
            out.append('? ')


class Port:
    def __init__(self):
        self.p = test_pm_zar.load_pm_probe()
        self.p.probe_pm_zrdr_text.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int32)]
        self.p.probe_pm_zrdr_dump.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self.buf = ctypes.create_string_buffer(1 << 22)

    def parse(self, folder, path, depth):
        d = ctypes.c_int32(depth)
        r = self.p.probe_pm_zrdr_text(str(folder).encode(), str(path).encode(), ctypes.byref(d))
        if r < 0:
            raise SystemExit(f'{path}: not readable')
        if r == 0:
            return None, d.value
        if self.p.probe_pm_zrdr_dump(self.buf, len(self.buf)) < 0:
            raise SystemExit('dump too large')
        return self.buf.value.decode('latin1'), d.value


def include_case(r, n):
    """(top file, {name: bytes}): files that #include each other."""
    nl = r.choice(['\n', '\r\n'])
    names = [f'inc{i}.zrd' for i in range(4)]
    named = {}

    def inc(depth):
        k = r.random()
        if k < 0.15:
            return nl + '#include missing.zrd' + nl
        name = r.choice(names[:2] if depth > 1 else names)
        return nl + '#include ' + name + r.choice([nl, ' ', '\t'])

    for i, name in enumerate(names):
        body = mw3.block(r, 1, nl)
        k = r.random()
        if k < 0.3:
            body += inc(i + 1)
        elif k < 0.45:
            body = '(' + body  # left open: only the first file's balance is checked
        elif k < 0.55:
            body += ' ' + r.choice(mw3.NAMES)  # a token running on into the file after
        elif k < 0.6:
            body += nl + '#if'  # a directive's word running on
        named[name] = body.encode('latin1')
    named['self.zrd'] = b'(a 1) #include self.zrd '
    k = n % 6
    if k == 0:
        body = '(' + mw3.block(r, 0, nl) + inc(0) + mw3.block(r, 1, nl) + ')' + nl
    elif k == 1:
        body = '(start #include self.zrd end)'
    elif k == 2:
        body = '(x' + nl + '#ifdef NOPE' + nl + ' skipped ' + inc(0) + ' after ' + nl + '#endif' + nl + 'y)' + nl
    elif k == 3:
        body = '(' + inc(0) + ')' + inc(0)
    elif k == 4:
        body = '(' + ''.join(inc(0) + mw3.token(r) + ' ' for _ in range(r.randrange(1, 4))) + ')'
    else:
        body = '#include ' + r.choice(names)  # at the end of the file: no space, nothing opened
    return body.encode('latin1'), named


def main():
    emu = Emulated()
    port = Port()
    r = random.Random(20261102)
    depth = 0
    cases = trees = unbalanced = includes = 0
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        path = folder / 'case.zrd'
        datas = [(mw3.controls_file(r), {}) for _ in range(40)] + [(mw3.random_file(r, n), {}) for n in range(500)]
        datas += [include_case(r, n) for n in range(300)]
        for i, (data, named) in enumerate(datas):
            for f in folder.iterdir():
                f.unlink()
            path.write_bytes(data)
            for name, body in named.items():
                (folder / name).write_bytes(body)
            try:
                want, want_d = emu.parse(data, depth, named)
            except Exception as e:
                eip = emu.cpu._uc.reg_read(UC_X86_REG_EIP)
                raise SystemExit(f'case {i}: the exe stopped at {eip:#x} ({e}) on {data[:400]!r}')
            got, got_d = port.parse(folder, path, depth)
            if want != got or want_d != got_d:
                print(f'case {i}: depth exe {want_d} port {got_d}')
                print('file:', data[:600], {k: v[:200] for k, v in named.items()})
                print('exe: ', (want or 'None')[:800])
                print('port:', (got or 'None')[:800])
                raise SystemExit(1)
            depth = want_d
            cases += 1
            trees += want is not None
            unbalanced += want is None
            includes += bool(named)
    print(f'ok: {cases} text files ({includes} with #include, {emu.opened} files included, up to '
          f'{min(emu.deepest, 32)} open; {trees} trees, {unbalanced} unbalanced) identical, '
          f'{emu.crt["printf"]} too-long tokens; #ifdef depth {depth} at the end on both sides')


if __name__ == '__main__':
    main()
