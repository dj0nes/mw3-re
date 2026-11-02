"""Pirate's Moon's reader (zrdr_main.cpp, docs/pm-map.md step 1) against PM's
exe, on every entry of every reader archive PM ships.

    cmake --build build && .venv/bin/python -P tests/differential/test_pm_zreader.py [--verbose]

PM's own code runs in the emulator at its own addresses over the fake file
system of test_pm_zar.py: the reader's archive list made by its static
constructor (0x00577c70), the archive opened as the current one (0x00579310)
and each entry loaded by name (0x00578630: the archives, then 0x005791b0 on
the entry's bytes). The port does the same through tests/probe_pm.cpp, with
PM's zgame::Table. Compared: the tree, node by node; then for every string in
it used as a key (and a few no file has), every getter PM has: find
(0x00578950) and find in this list only (0x00578830), the strings (0x00578970,
0x005789b0), float and int (0x005789f0, 0x00578a70, 0x00578ac0, 0x00578af0),
the new keyed float and string values (0x00579090, 0x005790b0), the keyed
lists and booleans (0x005790d0-0x00579170), and on find's hit the tristate
(0x00578e40) and the new int value (0x00578f70).

As MW3's test_zreader.py: a key that is its list's last item, or whose value
is an empty list, makes the getters read past the list (undefined in the
exe, no value in the port, FIX (A)); those compare find only.
"""
import ctypes
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_zar  # noqa: E402
from original import FTOL, IAT_FTOL, ROOT  # noqa: E402
from twod_world import IAT  # noqa: E402

PM = ROOT / 'game' / 'pm'
LIST_CTOR, OPEN_ARCHIVE, LOAD, FIND_PATH = 0x577c70, 0x579310, 0x578630, 0x5780e0
OP_NEW, OP_DELETE = 0x5ce8c6, 0x5ce8c0
FIND, FIND_FLAT, GET_STRING, GET_STRING_FLAT = 0x578950, 0x578830, 0x578970, 0x5789b0
GET_FLOAT, GET_INT, INT_IF, BYTE_IF = 0x5789f0, 0x578a70, 0x578ac0, 0x578af0
FLOAT_VALUE_K, STRING_VALUE_K = 0x579090, 0x5790b0
VEC3_K, VEC2_K, IVEC3_K, IVEC2_K, BOOL_INT_K, BOOL_K = 0x5790d0, 0x5790f0, 0x579110, 0x579130, 0x579150, 0x579170
TRISTATE, INT_VALUE = 0x578e40, 0x578f70
FILL = 0x7fa5a5a5  # a NaN no getter writes
ABSENT = [b'NO_SUCH_KEY', b'', b'name ', b'NAME']


class Emulated(test_pm_zar.Emulated):
    def __init__(self):
        super().__init__()
        cpu = self.cpu
        cpu.stub_import(IAT['malloc'], lambda c: self.alloc(max(c.arg(0), 1)))
        cpu.stub_import(IAT['_stricmp'], self.stricmp)
        cpu.stub_import(IAT['_splitpath'], self.splitpath)
        cpu.import_code(IAT_FTOL, FTOL)  # MSVCRT's _ftol as x86 (0x00578f70 calls it)
        self.native_stub(OP_NEW, lambda c: self.alloc(c.arg(0)))
        self.native_stub(OP_DELETE, lambda c: 0)
        # Not in an archive: the loose-file search (the text half) finds nothing.
        self.native_stub(FIND_PATH, lambda c: 0)
        self.call(LIST_CTOR)

    def stricmp(self, c):
        a, b = self.cstr(c.arg(0)).lower(), self.cstr(c.arg(1)).lower()
        return (a > b) - (a < b)

    def splitpath(self, c):
        p = self.cstr(c.arg(0)).decode('latin1')
        cut = max(p.rfind('\\'), p.rfind('/'), p.rfind(':')) + 1
        name = p[cut:]
        dot = name.rfind('.')
        fname, ext = (name[:dot], name[dot:]) if dot >= 0 else (name, '')
        for slot, s in ((c.arg(1), p[:2] if p[1:2] == ':' else ''), (c.arg(2), p[len(p[:2]) if p[1:2] == ':' else 0:cut]),
                        (c.arg(3), fname), (c.arg(4), ext)):
            if slot:
                self.cpu._uc.mem_write(slot, s.encode('latin1') + b'\0')
        return 0

    def i32(self, a):
        return struct.unpack('<i', struct.pack('<I', self.r32(a)))[0]

    def walk(self, a, out, paths, past_end, empty, strings, prefix=''):
        t, v = self.r32(a), self.r32(a + 4)
        if t == 1:
            out.append(f'I{self.i32(a + 4)} ')
        elif t == 2:
            out.append(f'F{v:08x} ')
        elif t == 3:
            s = self.cstr(v)
            strings.add(s)
            out.append(f'S{len(s)}:' + s.decode('latin1') + ' ')
        elif t == 4:
            count = self.i32(v + 4)
            out.append(f'L{count}[')
            if count <= 1:
                empty.add(a)
            for i in range(1, count + 1):
                p = f'{prefix}{i}'
                paths[v + 8 * i] = p
                if i == count:
                    past_end.add(v + 8 * i)
                else:
                    self.walk(v + 8 * i, out, paths, past_end, empty, strings, p + '/')
            out.append('] ')
        else:
            out.append('? ')

    def query(self, root, key, paths):
        k = self.string(key.decode('latin1'))
        o = self.alloc(16)
        text, out = [], []

        def where(hit):
            return paths.get(hit, '?') if hit else ''

        def s(p):
            return self.cstr(p).decode('latin1') if p else '(null)'

        hit = self.call(FIND, args=[root, k])
        text += [where(hit), where(self.call(FIND_FLAT, args=[root, k, 1]))]
        text += [s(self.call(GET_STRING, args=[root, k])), s(self.call(GET_STRING_FLAT, args=[root, k, 1]))]

        def run(addr, args, n, fill=FILL, byte=False):
            self.cpu._uc.mem_write(o, struct.pack('<4I', fill, fill, fill, fill))
            ok = self.call(addr, args=args + [o]) & 0xff
            vals = list(struct.unpack(f'<{n}I', bytes(self.cpu._uc.mem_read(o, 4 * n))))
            if byte:
                vals = [v & 0xff for v in vals]
            return [ok] + vals

        out += run(GET_FLOAT, [root, k], 1)
        out += run(GET_INT, [root, k], 1)
        out += run(INT_IF, [root, k], 1)[1:]
        out += run(BYTE_IF, [root, k], 1, byte=True)[1:]
        out += run(FLOAT_VALUE_K, [root, k], 1)
        r = run(STRING_VALUE_K, [root, k], 1, fill=1)
        out.append(r[0])
        text.append('(left)' if r[1] == 1 else s(r[1]))
        out += run(VEC3_K, [root, k], 3)
        out += run(VEC2_K, [root, k], 2)
        out += run(IVEC3_K, [root, k], 3)
        out += run(IVEC2_K, [root, k], 2)
        out += run(BOOL_INT_K, [root, k], 1)
        out += run(BOOL_K, [root, k], 1, fill=FILL & 1, byte=True)
        out += run(TRISTATE, [hit], 1)
        out += run(INT_VALUE, [hit], 1)
        return hit, out, '|'.join(text) + '|'


class Port:
    def __init__(self):
        self.p = test_pm_zar.load_pm_probe()
        self.p.probe_pm_zrdr_load.argtypes = [ctypes.c_char_p] * 3
        self.p.probe_pm_zrdr_dump.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self.p.probe_pm_zrdr_query.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32),
                                               ctypes.c_char_p, ctypes.c_int]
        self.buf = ctypes.create_string_buffer(16 << 20)

    def load(self, archive, entry):
        r = self.p.probe_pm_zrdr_load(str(PM).encode(), archive.encode(), entry.encode())
        if r < 0:
            raise SystemExit(f'port: {archive} does not open')
        if r == 0:
            return None
        if self.p.probe_pm_zrdr_dump(self.buf, len(self.buf)) < 0:
            raise SystemExit('dump too large')
        return self.buf.value.decode('latin1')

    def query(self, key):
        out = (ctypes.c_uint32 * 64)()
        text = ctypes.create_string_buffer(1 << 16)
        n = self.p.probe_pm_zrdr_query(key, FILL, out, text, len(text))
        if n < 0:
            raise SystemExit('query text too large')
        return list(out[:n]), text.value.decode('latin1')


def entries(data):
    version, count = struct.unpack_from('<II', data, len(data) - 12)
    assert version == 2
    table = len(data) - 12 - count * 0x94
    for i in range(count):
        e = table + i * 0x94
        yield data[e + 8:e + 72].split(b'\0')[0].decode('latin1')


def main():
    verbose = '--verbose' in sys.argv
    archives = sorted(p for p in (PM / 'zbd').rglob('reader*.zbd'))
    if not archives:
        raise SystemExit(f'needs {PM}/zbd')
    port = Port()
    files = keys = past = 0
    for path in archives:
        rel = path.relative_to(PM).as_posix()
        data = path.read_bytes()
        emu = Emulated()
        emu.files[test_pm_zar.key(rel)] = bytearray(data)
        if not emu.call(OPEN_ARCHIVE, args=[emu.string(rel.replace('/', '\\')), 1]):
            raise SystemExit(f'{rel}: PM does not open it')
        mark = emu.cpu.heap_next
        for name in entries(data):
            emu.cpu.heap_next = mark
            emu.cpu._uc.mem_write(mark, b'\0' * 0x100000)
            root = emu.call(LOAD, args=[emu.string(name), 0, 0])
            out, paths, past_end, empty, strings = [], {}, set(), set(), set()
            if root:
                emu.walk(root, out, paths, past_end, empty, strings)
            want = ''.join(out) if root else None
            got = port.load(rel, name)
            if want != got:
                raise SystemExit(f'{rel} {name}: trees differ\n  exe  {str(want)[:300]}\n  port {str(got)[:300]}')
            for k in sorted(strings)[:400] + ABSENT:
                hit, ev, et = emu.query(root, k, paths)
                pv, pt = port.query(k)
                short = hit in past_end or (hit and emu.r32(hit) == 4 and emu.i32(emu.r32(hit + 4) + 4) <= 1)
                if short:
                    ev, pv, et, pt = [], [], et.split('|')[0], pt.split('|')[0]
                    past += 1
                if ev != pv or et != pt:
                    raise SystemExit(f'{rel} {name}: key {k!r}\n  exe  {et} {ev}\n  port {pt} {pv}')
                keys += 1
            files += 1
            if verbose:
                print(f'{rel} {name}: {len(strings)} keys')
    print(f'ok: {len(archives)} archives, {files} files, identical trees; {keys} keys, every getter agrees '
          f'({past} past-the-end reads compared on find only)')


if __name__ == '__main__':
    main()
