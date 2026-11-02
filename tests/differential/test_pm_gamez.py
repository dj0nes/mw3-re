"""Pirate's Moon's gamez.zbd readers (pm/gamez: zimg_texture 0x00531920,
gmod_matl 0x00555e00, cls_zbd's texture lookups 0x00516ab0 / 0x00516ae0)
against PM's exe, as its loader 0x00516bc0 calls them (docs/pm-map.md,
gamez.zbd 41).

    cmake --build build && .venv/bin/python -P tests/differential/test_pm_gamez.py

Each of PM's four worlds read alone, then stacked: a world's materials on
top of another's, the second file's textures found by name through its own
directory (the first file's stays the table). Both colour formats (5-6-5,
5-5-5); copies cut in the materials, and one with 600 materials (every
shipped file has 1000). Compared: the results, the texture table, every material slot with
the free and used lists, and the cycles; past a failed read (which ends the
load), each material's flags and links only.
"""
import ctypes
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_zar  # noqa: E402
from original import FTOL, IAT_FTOL, ROOT  # noqa: E402
from twod_world import IAT  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

WORLDS = ['c1', 'c2', 'c3', 'c4']
ZBD = ROOT / 'game' / 'pm' / 'zbd'
TEXDIR, MATERIALS, SET_FORMAT = 0x531920, 0x555e00, 0x598330
FILES, NFILES, CURRENT, OPEN, OWN = 0x639c28, 0x639c2c, 0x60ea00, 0x639c30, 0x639c34
TEX_COUNT, TABLE, GROUP = 0x63e1e8, 0x63e1ec, 0x66a23c
BLOCKS, NBLOCKS, CAPACITY, COUNT, FREE, USED = 0x68a8a0, 0x68a898, 0x68a89c, 0x68a8a4, 0x6114f0, 0x6114f4
HANDLE = 0x44


class Emulated(test_pm_zar.Emulated):
    def __init__(self):
        super().__init__()
        cpu = self.cpu
        cpu.import_code(IAT_FTOL, FTOL)
        cpu.stub_import(IAT['fread'], self.fread)
        cpu.stub_import(IAT['fseek'], self.fseek)
        cpu.stub_import(IAT['ftell'], lambda c: self.pos)
        cpu.stub_import(IAT['malloc'], lambda c: self.sized(c.arg(0)))
        cpu.stub_import(IAT['calloc'], lambda c: self.sized(c.arg(0) * c.arg(1)))
        cpu.stub_import(IAT['strrchr'], self.strrchr)
        cpu.stub_import(IAT['strchr'], self.strchr)
        self.data, self.pos = b'', 0

    def sized(self, n):
        a = self.alloc(n)
        self.sizes[a] = n
        return a

    def w32(self, a, v):
        self.cpu._uc.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def i32(self, a):
        return struct.unpack('<i', bytes(self.cpu._uc.mem_read(a, 4)))[0]

    def read(self, a, n):
        return bytes(self.cpu._uc.mem_read(a, n))

    def fread(self, c):
        buf, size, count, f = c.arg(0), c.arg(1), c.arg(2), c.arg(3)
        assert f == HANDLE
        chunk = self.data[self.pos:self.pos + size * count]
        self.pos += len(chunk)
        self.cpu._uc.mem_write(buf, chunk)
        return len(chunk) // size if size else 0

    def fseek(self, c):
        off, whence = struct.unpack('<i', struct.pack('<I', c.arg(1)))[0], c.arg(2)
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else len(self.data) + off
        return 0

    def strrchr(self, c):
        s, ch = c.arg(0), c.arg(1) & 0xff
        i = self.cstr(s).rfind(bytes([ch]))
        return s + i if i >= 0 else 0

    def strchr(self, c):
        s, ch = c.arg(0), c.arg(1) & 0xff
        i = self.cstr(s).find(bytes([ch]))
        return s + i if i >= 0 else 0

    def reset(self, rb, gb, bb):
        self.cpu.call(self.thunk(SET_FORMAT), stack_args=(rb, gb, bb, 0, 0, 0))
        for a in (FILES, NFILES, OPEN, OWN, TEX_COUNT, GROUP, BLOCKS, NBLOCKS, CAPACITY, COUNT):
            self.w32(a, 0)
        for a in (CURRENT, FREE, USED):
            self.w32(a, -1)

    def thunk(self, addr):
        if addr not in self.thunks:
            self.thunks[addr] = self.cpu.code(b'\xb8' + struct.pack('<I', addr) + b'\xff\xe0')
        return self.thunks[addr]

    def stack(self, paths, rb, gb, bb):
        self.reset(rb, gb, bb)
        self.unresolved = 1 << 30
        out = b''
        for path in paths:
            self.data, self.pos = Path(path).read_bytes(), 0
            header = self.data[:0x28]
            n = self.i32(NFILES)
            files = self.realloc_files(n + 1)
            self.w32(files + 0x2c * n, self.string(str(path)))
            self.cpu._uc.mem_write(files + 0x2c * n + 4, header)
            self.w32(NFILES, n + 1)
            self.w32(CURRENT, n)
            self.w32(OPEN, HANDLE)
            tex = mat = 0
            textures, count = struct.unpack_from('<ii', header, 0x10)[0], struct.unpack_from('<i', header, 0xc)[0]
            if self.i32(TEX_COUNT) == 0:
                self.pos = textures
                tex = self.signed(self.call(TEXDIR, args=[count, HANDLE]))
                self.w32(OWN, 1)
            self.pos = struct.unpack_from('<i', header, 0x14)[0]
            before = self.i32(CAPACITY)
            if tex >= 0:
                mat = self.signed(self.call(MATERIALS, args=[HANDLE]))
            if mat < 0:
                self.unresolved = before  # a failed read's materials keep the file's indices
            out += struct.pack('<ii', tex, mat)
            self.w32(OPEN, 0)
            self.w32(CURRENT, -1)
            self.w32(OWN, 0)
            if tex < 0 or mat < 0:
                break
        return out + self.tables()

    def realloc_files(self, n):
        old = self.i32(FILES) & 0xffffffff
        a = self.sized(0x2c * n)
        if old:
            self.cpu._uc.mem_write(a, self.read(old, 0x2c * (n - 1)))
        self.w32(FILES, a)
        return a

    @staticmethod
    def signed(v):
        return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]

    def tex_index(self, p):
        p &= 0xffffffff
        return -1 if p == 0 else (p - TABLE) // 0x2c

    def tables(self):
        n = self.i32(TEX_COUNT)
        out = struct.pack('<i', n)
        for i in range(n):
            e = self.read(TABLE + 0x2c * i, 0x2c)
            name = e[0xc:0x20].split(b'\0')[0][:20]  # a C string: what follows its end is unread
            out += name.ljust(20, b'\0') + e[0x20:0x28] + struct.pack('<i', 1 if struct.unpack_from('<I', e, 0x28)[0] else 0)
        cap = self.i32(CAPACITY)
        out += struct.pack('<iiii', cap, self.i32(COUNT), self.i32(FREE), self.i32(USED))
        blocks = self.i32(BLOCKS) & 0xffffffff
        slot = lambda i: (self.i32(blocks + 4 * (i // 1000)) & 0xffffffff) + 0x2c * (i % 1000)
        cycles = b''
        for i in range(cap):
            m = self.read(slot(i), 0x2c)
            tex = struct.unpack_from('<i', m, 0x10)[0]
            if i >= self.unresolved:  # a failed read: its flags and links only
                out += m[0:2] + m[0x28:0x2c]
                continue
            if not (m[1] & 0x20) and (m[1] & 1):
                tex = self.tex_index(tex)
            out += m[0:0x10] + struct.pack('<i', tex) + m[0x14:0x24] + m[0x28:0x2c]
            if not (m[1] & 0x20) and (m[1] & 4):
                c = self.i32(slot(i) + 0x24) & 0xffffffff
                raw = self.read(c, 0x18)
                cycles += raw
                arr = self.i32(c + 0x18) & 0xffffffff
                for k in range(struct.unpack_from('<I', raw, 0x10)[0]):
                    cycles += struct.pack('<i', self.tex_index(self.i32(arr + 4 * k)))
        return out + cycles


def main():
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_gamez_stack.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                       ctypes.c_void_p, ctypes.c_int]
    buf = ctypes.create_string_buffer(1 << 24)
    worlds = [ZBD / w / 'gamez.zbd' for w in WORLDS]
    cases = [[w] for w in worlds] + [[worlds[0], worlds[1]], [worlds[2], worlds[0]], [worlds[3], worlds[1], worlds[2]]]
    # Cut copies of c1: in the materials' counts, their block, the cycles,
    # and of c3 stacked on c1: in the second file's block.
    scratch = Path(tempfile.mkdtemp(prefix='pm_gamez_'))
    data = worlds[0].read_bytes()
    mats, models = struct.unpack_from('<ii', data, 0x14)
    for k, cut in enumerate((mats + 6, mats + 16 + 0x2c * 100 + 7, models - 40, models - 3)):
        path = scratch / f'cut{k}.zbd'
        path.write_bytes(data[:cut])
        cases += [[path], [worlds[2], path]]
    # c1 with 600 materials (every shipped file has 1000, its used ones below
    # 400), the cycles after them: the last block's rest goes on the free list.
    short = scratch / 'short.zbd'
    short.write_bytes(data[:mats] + struct.pack('<i', 600) + data[mats + 4:mats + 16 + 600 * 0x2c]
                      + data[mats + 16 + 1000 * 0x2c:])
    cases += [[short], [worlds[2], short]]
    emu = Emulated()
    runs = 0
    for fmt in ((5, 6, 5), (5, 5, 5)):
        for case in cases:
            want = emu.stack(case, *fmt)
            n = p.probe_pm_gamez_stack('|'.join(map(str, case)).encode(), *fmt, buf, len(buf))
            if n < 0:
                raise SystemExit(f'{case}: probe {n}')
            got = buf.raw[:n]
            if want != got:
                at = next((i for i in range(min(len(want), len(got))) if want[i] != got[i]), min(len(want), len(got)))
                print(f'{[Path(c).parent.name for c in case]} {fmt}: differ at byte {at} of {len(want)}/{len(got)}')
                print('exe: ', want[max(0, at - 16):at + 32].hex())
                print('port:', got[max(0, at - 16):at + 32].hex())
                raise SystemExit(1)
            runs += 1
            r = struct.unpack_from(f'<{2 * len(case)}i', want)
            print(f'{"+".join(Path(c).parent.name if c.name == "gamez.zbd" else c.stem for c in case)} {fmt}: results {r}')
    print(f'ok: {runs} runs identical (4 worlds alone, 3 stacks, 4 cut files and a 600-material one alone and stacked; 2 colour formats)')


if __name__ == '__main__':
    main()
