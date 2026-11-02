"""Pirate's Moon's zArchive, version 2 (src/pm/gamez/zutil/zutl_zar, docs/pm-map.md)
against PM's exe.

    cmake --build build && .venv/bin/python -P tests/differential/test_pm_zar.py [runs] [--verbose]

PM's archive code runs in the emulator (always PM's exe, at its own
addresses: init 0x00597300, open 0x00597340 and its table reader 0x00597610,
close 0x005974a0 and its table writer 0x00597570, create 0x00597400, add
0x00597800, the checksum 0x00597a30, its check 0x00597a10 and the file check
0x00597af0) over a fake file system behind CreateFileA, ReadFile, WriteFile,
SetFilePointer, GetFileSize and CloseHandle. The port (tests/probe_pm.cpp)
does the same on real files. Compared, for every version-2 archive PM ships
(54), the open's result, the count, the checksum, the table's bytes and,
except for the 150 MB of sounds, the archive's CRC, its check and the file
check; then damaged copies (MW3's version 1, cut short, the checksum or a data
byte changed, a smaller count); then random archives read and written (the
written files' bytes; PM's add leaves its stack's leftovers in an entry's
flags, comment and file time, zeroed here as the port writes them).

Where the port departs (FIX (bug), as MW3's zutl_zar): a table count the file
cannot hold fails the open. The exe is not run on those; the port's refusal
is checked.
"""
import ctypes
import random
import shutil
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402
from twod_world import IAT  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

PM = ROOT / 'game' / 'pm'
INIT, DESTROY, OPEN, CLOSE, CREATE, ADD = 0x597300, 0x597320, 0x597340, 0x5974a0, 0x597400, 0x597800
CRC, CHECK, FILE_OK = 0x597a30, 0x597a10, 0x597af0
REALLOC_COPY, LOG_ERROR, CREATED = 0x596520, 0x406cb0, 0x403ac0
ENTRY = 0x94


def load_pm_probe():
    return ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))


def crc32(data, crc=0, table=[]):
    if not table:
        for i in range(256):
            c = i << 24
            for _ in range(8):
                c = ((c << 1) ^ 0x04c11db7) if c & 0x80000000 else c << 1
            table.append(c & 0xffffffff)
    for b in data:
        crc = ((crc << 8) & 0xffffffff) ^ table[((crc >> 24) ^ b) & 0xff]
    return crc


def key(path):
    return path.replace('\\', '/').lower()


class Emulated:
    """PM's exe and a fake file system: files[key] = bytearray."""

    def __init__(self):
        self.cpu = cpu = Original(exe='pm')
        self.files = {}
        self.handles = {}
        self.next_handle = 0x100
        for name, fn, pop in (('CreateFileA', self.create_file, 28), ('ReadFile', self.read_file, 20),
                              ('WriteFile', self.write_file, 20), ('SetFilePointer', self.seek, 16),
                              ('CloseHandle', self.close, 4), ('GetFileSize', self.file_size, 8),
                              ('GetLastError', lambda c: 2, 0), ('FormatMessageA', lambda c: 0, 28),
                              ('LocalFree', lambda c: 0, 4), ('_strdup', self.strdup, 0),
                              ('realloc', self.realloc, 0), ('free', lambda c: 0, 0),
                              ('strncpy', self.strncpy, 0)):
            cpu.stub_import(IAT[name], fn, pop)
        self.native_stub(REALLOC_COPY, self.realloc)  # malloc, copy, free
        self.native_stub(LOG_ERROR, lambda c: 0)
        self.native_stub(CREATED, lambda c: 0)
        self.sizes = {}
        self.thunks = {}
        self.obj = self.alloc(0x1c)

    # PM's own addresses: a stub and a thunk the address map leaves alone.
    def native_stub(self, addr, fn, pop=0):
        self.cpu.stubs[addr] = (fn, pop)
        self.cpu._uc.hook_add(UC_HOOK_CODE, self.cpu._stub_hit, begin=addr, end=addr)

    def call(self, addr, ecx=0, args=()):
        if addr not in self.thunks:  # mov eax, addr; jmp eax
            self.thunks[addr] = self.cpu.code(b'\xb8' + struct.pack('<I', addr) + b'\xff\xe0')
        thunk = self.thunks[addr]
        self.cpu.call(thunk, ecx=ecx, stack_args=args, count=2_000_000_000)
        return self.cpu._uc.reg_read(UC_X86_REG_EAX)

    def r32(self, a):
        return struct.unpack('<I', bytes(self.cpu._uc.mem_read(a, 4)))[0]

    def alloc(self, n):
        a = self.cpu.alloc(max(n, 4))
        self.cpu._uc.mem_write(a, b'\0' * max(n, 4))
        return a

    def cstr(self, a):
        out = bytearray()
        while True:
            b = bytes(self.cpu._uc.mem_read(a + len(out), 64))
            i = b.find(b'\0')
            if i >= 0:
                return bytes(out + b[:i])
            out += b

    def string(self, s):
        b = s.encode('latin1')
        a = self.alloc(len(b) + 1)
        self.cpu._uc.mem_write(a, b + b'\0')
        return a

    # C runtime
    def strdup(self, c):
        return self.string(self.cstr(c.arg(0)).decode('latin1'))

    def realloc(self, c):
        old, n = c.arg(0), c.arg(1)
        if n > 0x10000000:
            return 0
        a = self.alloc(n)
        if old:
            size = self.sizes.get(old, 0)
            self.cpu._uc.mem_write(a, bytes(self.cpu._uc.mem_read(old, min(size, n))))
        self.sizes[a] = n
        return a

    def strncpy(self, c):
        dst, src, n = c.arg(0), c.arg(1), c.arg(2)
        s = self.cstr(src)[:n]
        self.cpu._uc.mem_write(dst, s + b'\0' * (n - len(s)))
        return dst

    # Win32 files
    def create_file(self, c):
        k, disposition = key(self.cstr(c.arg(0)).decode('latin1')), c.arg(4)
        if disposition == 3 and k not in self.files:  # OPEN_EXISTING
            return 0xffffffff
        if disposition == 2:  # CREATE_ALWAYS
            self.files[k] = bytearray()
        h = self.next_handle
        self.next_handle += 4
        self.handles[h] = [k, 0]
        return h

    def read_file(self, c):
        h, buf, n, got = c.arg(0), c.arg(1), c.arg(2), c.arg(3)
        k, pos = self.handles[h]
        data = bytes(self.files[k][pos:pos + n])
        if data:
            self.cpu._uc.mem_write(buf, data)
        self.handles[h][1] = pos + len(data)
        if got:
            self.cpu._uc.mem_write(got, struct.pack('<I', len(data)))
        return 1

    def write_file(self, c):
        h, buf, n, done = c.arg(0), c.arg(1), c.arg(2), c.arg(3)
        k, pos = self.handles[h]
        f = self.files[k]
        data = bytes(self.cpu._uc.mem_read(buf, n)) if n else b''
        if len(f) < pos:
            f.extend(b'\0' * (pos - len(f)))
        f[pos:pos + n] = data
        self.handles[h][1] = pos + n
        if done:
            self.cpu._uc.mem_write(done, struct.pack('<I', n))
        return 1

    def seek(self, c):
        h, dist, method = c.arg(0), struct.unpack('<i', struct.pack('<I', c.arg(1)))[0], c.arg(3)
        k, pos = self.handles[h]
        base = {0: 0, 1: pos, 2: len(self.files[k])}[method]
        if base + dist < 0:
            return 0xffffffff  # fails, the position kept
        self.handles[h][1] = base + dist
        return base + dist

    def file_size(self, c):
        return len(self.files[self.handles[c.arg(0)][0]])

    def close(self, c):
        self.handles.pop(c.arg(0), None)
        return 1

    # the archive
    def open(self, name, crc):
        """[ok, count, checksum, crc, checksum_ok, file_ok], the table's bytes."""
        o = self.obj
        self.call(INIT, ecx=o)
        ok = self.call(OPEN, ecx=o, args=[self.string(name)])
        count, checksum = self.r32(o + 0xc), self.r32(o + 0x18)
        table = bytes(self.cpu._uc.mem_read(self.r32(o + 0x14), count * ENTRY)) if ok and count else b''
        c = good = 0
        if ok and crc:
            c = self.call(CRC, ecx=o)
            good = self.call(CHECK, ecx=o) & 0xff
        self.call(CLOSE, ecx=o)
        self.call(DESTROY, ecx=o)
        file_ok = self.call(FILE_OK, args=[self.string(name)]) & 0xff if crc else 0
        return [ok & 0xff, count, checksum, c, good, file_ok], table

    def write(self, name, blobs, checksum):
        o = self.obj
        self.call(INIT, ecx=o)
        self.call(CREATE, ecx=o, args=[self.string(name)])
        for i, b in enumerate(blobs):
            data = self.alloc(len(b))
            if b:
                self.cpu._uc.mem_write(data, b)
            self.call(ADD, ecx=o, args=[self.string(f'e{i}'), data, len(b), 0, 0])
        self.cpu._uc.mem_write(o + 0x18, struct.pack('<I', checksum))
        self.call(CLOSE, ecx=o)
        self.call(DESTROY, ecx=o)
        return bytes(self.files[key(name)])


def zero_junk(data, count):
    """An entry's flags, comment and file time: the exe's stack leftovers."""
    d = bytearray(data)
    table = len(d) - 12 - count * ENTRY
    for i in range(count):
        e = table + i * ENTRY
        d[e + 0x48:e + ENTRY] = b'\0' * (ENTRY - 0x48)
    return bytes(d)


class Port:
    def __init__(self):
        self.p = load_pm_probe()
        self.p.probe_pm_zar_open.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int,
                                             ctypes.POINTER(ctypes.c_uint32), ctypes.c_char_p, ctypes.c_int,
                                             ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
        self.p.probe_pm_zar_write.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p,
                                              ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
                                              ctypes.c_uint32]

    def open(self, folder, name, crc):
        out = (ctypes.c_uint32 * 6)()
        cap = 4096 * ENTRY
        table = ctypes.create_string_buffer(cap)
        spans = (ctypes.c_uint32 * 4096)()
        self.p.probe_pm_zar_open(str(folder).encode(), name.encode(), int(crc), out, table, cap, spans, 4096)
        r = list(out)
        return r, table.raw[:r[1] * ENTRY] if r[0] else b'', list(spans[:r[1]]) if r[0] else []

    def write(self, folder, name, blobs, checksum):
        data = b''.join(blobs)
        offs, lens, at = [], [], 0
        for b in blobs:
            offs.append(at)
            lens.append(len(b))
            at += len(b)
        n = max(len(blobs), 1)
        self.p.probe_pm_zar_write(str(folder).encode(), name.encode(), len(blobs), data or b'\0',
                                  (ctypes.c_uint32 * n)(*offs), (ctypes.c_uint32 * n)(*lens), checksum)
        return (Path(folder) / name).read_bytes()


def archive(blobs, checksum=None, version=2, count=None):
    out = bytearray()
    table = bytearray()
    for i, b in enumerate(blobs):
        e = struct.pack('<II', len(out), len(b)) + f'e{i}'.encode().ljust(64, b'\0')
        table += e.ljust(ENTRY, b'\0')
        out += b
    c = crc32(b''.join(blobs)) if checksum is None else checksum
    n = len(blobs) if count is None else count
    if version == 1:
        return bytes(out + table + struct.pack('<II', 1, n))
    return bytes(out + table + struct.pack('<III', 2, n, c))


def mech3ax_rules(name, data, port_open, crc):
    """mech3ax's PM archive reader (crates/archive/src/read.rs; its mode by file,
    crates/lib/src/read.rs): the readers' checksum is their CRC, motion's,
    mechlib's and the sounds' 0; every entry starts where the one before
    ended and ends by the table; motion's lengths are all 1, backfilled from
    the next entry's start. The port's spans must be those lengths."""
    r, table, spans = port_open
    base = name.replace('\\', '/').split('/')[-1].lower()
    mode = {'motion.zbd': 'motion', 'mechlib.zbd': 'zero', 'soundsh.zbd': 'zero', 'soundsl.zbd': 'zero'}.get(base, 'reader')
    count, checksum = r[1], r[2]
    table_start = len(data) - 12 - count * ENTRY
    starts = [struct.unpack_from('<I', table, i * ENTRY)[0] for i in range(count)]
    lengths = [struct.unpack_from('<I', table, i * ENTRY + 4)[0] for i in range(count)]
    why = []
    if mode == 'motion':
        if set(lengths) != {1}:
            why.append('a motion length is not 1')
        lengths = [b - a for a, b in zip(starts, starts[1:] + [table_start])]
    at = 0
    for a, n in zip(starts, lengths):
        if a != at or a + n > table_start:
            why.append(f'an entry at {a}, expected {at}')
            break
        at = a + n
    if mode == 'reader' and crc and r[3] != checksum:
        why.append('the CRC is not the checksum')
    if mode != 'reader' and checksum != 0:
        why.append('the checksum is not 0')
    if spans != lengths:
        why.append("the port's spans are not mech3ax's lengths")
    if why:
        print(f'MECH3AX {name}: ' + '; '.join(why))
    return 1 if why else 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    verbose = '--verbose' in sys.argv
    runs = int(args[0]) if args else 200
    rng = random.Random(1)
    emu, port = Emulated(), Port()
    bad = 0
    checked = {'shipped': 0, 'crc matches the stored': 0, 'damaged': 0, 'random reads': 0, 'random writes': 0,
               'fix departs': 0, "mech3ax's rules": 0}

    def compare(label, name, crc, folder):
        nonlocal bad
        e, et = emu.open(name, crc)
        p, pt, _ = port.open(folder, name, crc)
        if e != p or et != pt:
            bad += 1
            print(f'MISMATCH {label}: exe {e} port {p}' + ('' if et == pt else ' (tables differ)'))
        elif verbose:
            print(f'  {label}: {e}')
        return e

    # What PM ships: every version-2 archive, the emulator reading it from the install's bytes.
    for path in sorted(PM.rglob('*.zbd')):
        data = path.read_bytes()
        if len(data) < 12 or struct.unpack_from('<I', data, len(data) - 12)[0] != 2:
            continue
        name = str(path.relative_to(PM))
        emu.files[key(name)] = bytearray(data)
        crc = 'sounds' not in name
        e = compare(name, name, crc, PM)
        checked['shipped'] += 1
        if crc and e[3] == e[2]:
            checked['crc matches the stored'] += 1
        bad += mech3ax_rules(name, data, port.open(PM, name, crc), crc)
        checked["mech3ax's rules"] += 1
        del emu.files[key(name)]

    tmp = Path(tempfile.mkdtemp(prefix='pm_zar_'))
    try:
        def case(label, data, crc=True):
            (tmp / 'a.zbd').write_bytes(data)
            emu.files['a.zbd'] = bytearray(data)
            compare(label, 'a.zbd', crc, tmp)

        # Damaged copies of a campaign reader and MW3's own reader.zbd.
        src = (PM / 'zbd' / 'c1' / 'readermp1.zbd').read_bytes()
        mw3 = (ROOT / 'game' / 'mw3-1.2' / 'zbd' / 'c1' / 'readermp1.zbd')
        damaged = [('MW3 version 1', mw3.read_bytes() if mw3.exists() else archive([b'x' * 9], version=1)),
                   ('cut to 11 bytes', src[:11]),
                   ('version 3', src[:-12] + struct.pack('<I', 3) + src[-8:]),
                   ('version 1 in a version 2 table', src[:-12] + struct.pack('<I', 1) + src[-8:]),
                   ('checksum + 1', src[:-4] + struct.pack('<I', (struct.unpack_from('<I', src, len(src) - 4)[0] + 1) & 0xffffffff)),
                   ('a data byte', bytes([src[0] ^ 0x40]) + src[1:]),
                   ('count - 1', src[:-8] + struct.pack('<I', struct.unpack_from('<I', src, len(src) - 8)[0] - 1) + src[-4:])]
        for label, data in damaged:
            case(label, data)
            checked['damaged'] += 1
        # The port's fix: counts the file cannot hold. Only the port runs.
        # (The exe opens them: its table seek fails and the entries are whatever the buffer held.)
        for label, data in (('count past the file', src[:-8] + struct.pack('<I', 0x1000000) + src[-4:]),
                            ('count negative', src[:-8] + struct.pack('<I', 0xffffffff) + src[-4:]),
                            ('the tail only', src[-12:])):
            (tmp / 'a.zbd').write_bytes(data)
            p, _, _ = port.open(tmp, 'a.zbd', True)
            if p[0]:
                bad += 1
                print(f'MISMATCH {label}: the port opened it')
            checked['fix departs'] += 1

        for run in range(runs):
            blobs = [bytes(rng.getrandbits(8) for _ in range(rng.choice([0, 1, 7, rng.randrange(1, 3000)])))
                     for _ in range(rng.randrange(0, 20))]
            checksum = None if rng.random() < 0.6 else rng.getrandbits(32)
            case(f'random {run}', archive(blobs, checksum))
            checked['random reads'] += 1
            if run % 4 == 0:
                c = rng.getrandbits(32)
                e = zero_junk(emu.write('w.zbd', blobs, c), len(blobs))
                p = port.write(tmp, 'w.zbd', blobs, c)
                if e != p:
                    bad += 1
                    print(f'MISMATCH write {run}: {len(e)} bytes vs {len(p)}' +
                          (f', first difference at {next(i for i in range(min(len(e), len(p))) if e[i] != p[i])}'
                           if e[:min(len(e), len(p))] != p[:min(len(e), len(p))] else ''))
                checked['random writes'] += 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print('pm_zar: ' + ', '.join(f'{k} {v}' for k, v in checked.items()) + f'; {bad} mismatches')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
