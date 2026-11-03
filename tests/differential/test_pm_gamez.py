"""Pirate's Moon's gamez.zbd readers (pm/gamez: zimg_texture 0x00531920,
gmod_matl 0x00555e00, cls_zbd's texture lookups 0x00516ab0 / 0x00516ae0)
against PM's exe, as its loader 0x00516bc0 calls them (docs/pm-map.md,
gamez.zbd 41).

    cmake --build build && .venv/bin/python -P tests/differential/test_pm_gamez.py
    PM_GAMEZ_ONLY=nodes ...  (textures, models or nodes: one part alone)

Each of PM's four worlds read alone, then stacked: a world's materials on
top of another's, the second file's textures found by name through its own
directory (the first file's stays the table). Both colour formats (5-6-5,
5-5-5); copies cut in the materials, and one with 600 materials (every
shipped file has 1000). Compared: the results, the texture table, every material slot with
the free and used lists, and the cycles; past a failed read (which ends the
load), each material's flags and links only.
"""
import ctypes
import os
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
HEAP, HEAP_SIZE = 0x40000000, 0x20000000  # every case's allocations, from the start each case
NODES, N_BLOCKS, N_NBLOCKS, N_CAPACITY, N_LIVE, N_FREE, N_BLOCK = 0x516e90, 0x6393c8, 0x6393c0, 0x6393c4, 0x6393cc, 0x60dc6c, 0x60dc68
LIST_HEADS, ITEM_FREE = 0x60db00, 0x63939c
LISTS = (6, 8, 9, 10, 13, 14, 15)
DATA_SIZE = {1: 0x1e8, 2: 0xcc, 3: 0xf8, 4: 0x1c, 5: 0x90, 6: 0x5c, 9: 0x100, 10: 0x94}
MODELS, STRIPS, UNSTRIP, D3D = 0x558ba0, 0x570b70, 0x5726f0, 0x8c1520
M_BLOCKS, M_NBLOCKS, M_BLOCK, M_CAPACITY, M_COUNT, M_FREE = 0x8d4af4, 0x8d4aec, 0x8d4ae8, 0x8d4af0, 0x8d4af8, 0x8d4afc


class Emulated(test_pm_zar.Emulated):
    def __init__(self):
        self.heap = None
        super().__init__()
        cpu = self.cpu
        cpu._uc.mem_map(HEAP, HEAP_SIZE)
        self.heap = HEAP
        cpu.import_code(IAT_FTOL, FTOL)
        cpu.stub_import(IAT['fread'], self.fread)
        cpu.stub_import(IAT['fseek'], self.fseek)
        cpu.stub_import(IAT['ftell'], lambda c: self.pos)
        cpu.stub_import(IAT['malloc'], lambda c: self.sized(c.arg(0)))
        cpu.stub_import(IAT['calloc'], lambda c: self.sized(c.arg(0) * c.arg(1)))
        cpu.stub_import(IAT['strrchr'], self.strrchr)
        cpu.stub_import(IAT['strchr'], self.strchr)
        self.data, self.pos = b'', 0
        self.converted = []  # the models PM's renderer conversion was given (left out of the port)
        self.native_stub(STRIPS, lambda c: self.converted.append(('strips', c.arg(0))) or 0)
        self.native_stub(UNSTRIP, lambda c: self.converted.append(('unstrip', c.arg(0))) or 0)

    def alloc(self, n):
        if self.heap is None:  # the bench's own set-up
            return super().alloc(n)
        n = max(n, 4)
        a = self.heap
        self.heap += (n + 0xf) & ~0xf
        assert self.heap < HEAP + HEAP_SIZE, 'the test heap is full'
        self.cpu._uc.mem_write(a, b'\0' * n)
        return a

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
        for a in (CURRENT, FREE, USED, M_FREE):
            self.w32(a, -1)
        for a in (M_BLOCKS, M_NBLOCKS, M_CAPACITY, M_COUNT):
            self.w32(a, 0)
        self.w32(M_BLOCK, 750)
        for a in (N_BLOCKS, N_NBLOCKS, N_CAPACITY, N_LIVE, ITEM_FREE):
            self.w32(a, 0)
        self.w32(N_FREE, -1)
        self.w32(N_BLOCK, 2500)
        for k in range(16):  # the class lists' heads
            self.w32(self.i32(LIST_HEADS + 4 * k) & 0xffffffff, 0)
            self.w32(self.i32(0x60db40 + 4 * k) & 0xffffffff, 0)
        self.w32(D3D, 1)  # Direct3D, as the port
        self.converted = []
        self.heap = HEAP

    def thunk(self, addr):
        if addr not in self.thunks:
            self.thunks[addr] = self.cpu.code(b'\xb8' + struct.pack('<I', addr) + b'\xff\xe0')
        return self.thunks[addr]

    def stack(self, paths, rb, gb, bb, models=False, nodes=False):
        self.reset(rb, gb, bb)
        self.unresolved = self.m_unread = self.n_unread = 1 << 30
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
            mod = 0
            if models and tex >= 0 and mat >= 0:
                self.pos = struct.unpack_from('<i', header, 0x18)[0]
                mbefore = self.i32(M_CAPACITY)
                mod = self.signed(self.call(MODELS, args=[HANDLE, mat]))
                if mod < 0:
                    self.m_unread = mbefore  # a failed read's slots: their headers only
            nod = 0
            if nodes and tex >= 0 and mat >= 0 and mod >= 0:
                self.pos = struct.unpack_from('<i', header, 0x24)[0]
                nbefore = self.i32(N_CAPACITY)
                nod = self.signed(self.call(NODES, args=[struct.unpack_from('<i', header, 0x1c)[0], HANDLE, mod]))
                if nod < 0:
                    self.n_unread = nbefore  # a failed read's slots only
            out += struct.pack('<ii', tex, mat) + (struct.pack('<i', mod) if models else b'')
            out += struct.pack('<i', nod) if nodes else b''
            self.w32(OPEN, 0)
            self.w32(CURRENT, -1)
            self.w32(OWN, 0)
            if tex < 0 or mat < 0:
                break
        return (out + self.tables() + (self.model_dump() if models and not nodes else b'')
                + (self.node_dump() if nodes else b''))

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


    def pool_index(self, p, blocks_at, nblocks_at, per, size):
        p &= 0xffffffff
        if p == 0:
            return -1
        blocks = self.i32(blocks_at) & 0xffffffff
        for b in range(self.i32(nblocks_at)):
            base = self.i32(blocks + 4 * b) & 0xffffffff
            if base <= p < base + per * size:
                return b * per + (p - base) // size
        return -2

    def nodes_at(self, p, n, step=4):
        return b''.join(struct.pack('<i', self.pool_index(self.i32(p + step * k), N_BLOCKS, N_NBLOCKS, 2500, 0xd4))
                        for k in range(max(n, 0)))

    item_set = False  # +0xcc as 0 or 1 (test_pm_mechlib: its nodes keep their list-7 item)

    def node_dump(self):
        """Every node in PM's layout: the slot (its pointers as indices), the
        live ones' links and class data, the partitions, the class lists."""
        ptr = lambda a: self.i32(a) & 0xffffffff
        cap = self.i32(N_CAPACITY)
        out = struct.pack('<iii', cap, self.i32(N_LIVE), self.i32(N_FREE))
        blocks = ptr(N_BLOCKS)
        for i in range(cap):
            s = ptr(blocks + 4 * (i // 2500)) + 0xd4 * (i % 2500)
            r = bytearray(self.read(s, 0xd4))
            cls, data = struct.unpack_from('<iI', r, 0x34)
            np_, nc = struct.unpack_from('<hh', r, 0x54)
            box = struct.unpack_from('<I', r, 0x70)[0]
            sel = {s + 0x74: 0, s + 0x8c: 1, s + 0xa4: 2}.get(box, -1)
            model = self.pool_index(struct.unpack_from('<I', r, 0x3c)[0], M_BLOCKS, M_NBLOCKS, 750, 0x68)
            for off in (0x38, 0x3c, 0x48, 0x58, 0x5c, 0x70):
                r[off:off + 4] = b'\0' * 4
            if self.item_set:
                r[0xcc:0xd0] = struct.pack('<I', 1 if r[0xcc:0xd0] != b'\0' * 4 else 0)
            out += bytes(r)
            if i >= self.n_unread:
                continue
            out += struct.pack('<ii', sel, model)
            live = r[0xd3] & 1
            if not live:
                continue
            out += self.nodes_at(ptr(s + 0x58), np_) + self.nodes_at(ptr(s + 0x5c), nc)
            if cls not in DATA_SIZE or not data:
                continue
            d = bytearray(self.read(data, DATA_SIZE[cls]))
            extra = b''
            if cls == 1:
                extra = self.nodes_at(data, 4)
                d[0:16] = b'\0' * 16
            elif cls == 2:
                rows, cols = struct.unpack_from('<i', d, 0x9c)[0], struct.unpack_from('<i', d, 0x98)[0]
                nl, ns = struct.unpack_from('<i', d, 0xb0)[0], struct.unpack_from('<i', d, 0xbc)[0]
                extra = self.nodes_at(ptr(data + 0xb4), nl) + self.nodes_at(ptr(data + 0xc0), ns)
                for k in range(max(nl, 0)):  # the lights' data pointers, copied at the end
                    extra += struct.pack('<i', int(ptr(ptr(data + 0xb8) + 4 * k) == ptr(ptr(ptr(data + 0xb4) + 4 * k) + 0x38)))
                for z in range(max(rows, 0)):
                    row = ptr(ptr(data + 0xa0) + 4 * z)
                    for x in range(max(cols, 0)):
                        pr = bytearray(self.read(row + 0x58 * x, 0x58))
                        n, lst = struct.unpack_from('<h', pr, 0x3a)[0], struct.unpack_from('<I', pr, 0x3c)[0]
                        pr[0x3c:0x40] = b'\0' * 4
                        extra += bytes(pr)
                        for k in range(max(n, 0)):
                            e = self.read(lst + 12 * k, 12)
                            extra += self.nodes_at(lst + 12 * k, 1) + e[4:]
                for off in (0xa0, 0xb4, 0xb8, 0xc0, 0xc4):
                    d[off:off + 4] = b'\0' * 4
            elif cls == 9:
                extra = self.nodes_at(ptr(data + 0xfc), struct.unpack_from('<i', d, 0xf8)[0])
                d[0xfc:0x100] = b'\0' * 4
            elif cls == 10:
                extra = self.nodes_at(ptr(data + 0x90), struct.unpack_from('<i', d, 0x8c)[0])
                d[0x90:0x94] = b'\0' * 4
            out += bytes(d) + extra
        for k in LISTS:  # each class list, head to tail
            item, seq = ptr(ptr(LIST_HEADS + 4 * k)), []
            while item:
                seq.append(self.pool_index(self.i32(item), N_BLOCKS, N_NBLOCKS, 2500, 0xd4))
                item = ptr(item + 8)
            out += struct.pack(f'<i{len(seq)}i', len(seq), *seq)
        return out

    def mat_index(self, p):
        p &= 0xffffffff
        if p == 0:
            return -1
        blocks = self.i32(BLOCKS) & 0xffffffff
        for b in range(self.i32(NBLOCKS)):
            base = self.i32(blocks + 4 * b) & 0xffffffff
            if base <= p < base + 1000 * 0x2c:
                return b * 1000 + (p - base) // 0x2c
        return -2

    def model_dump(self):
        cap = self.i32(M_CAPACITY)
        out = struct.pack('<iii', cap, self.i32(M_COUNT), self.i32(M_FREE))
        blocks = self.i32(M_BLOCKS) & 0xffffffff
        ptr = lambda a: self.i32(a) & 0xffffffff
        for i in range(cap):
            s = ptr(blocks + 4 * (i // 750)) + 0x68 * (i % 750)
            h = self.read(s, 0x68)
            cnt = lambda off: struct.unpack_from('<i', h, off)[0]
            out += h[0:0x34] + h[0x48:0x60] + h[0x64:0x68]
            if i >= self.m_unread:
                continue
            for off, at in ((0x14, 0x38), (0x18, 0x3c), (0x1c, 0x44)):
                if cnt(off) > 0:
                    out += self.read(ptr(s + at), 12 * cnt(off))
            for k in range(max(cnt(0x20), 0)):
                l = self.read(ptr(s + 0x40) + 0x4c * k, 0x4c)
                out += l[0:0x28] + l[0x28:0x2a] + l[0x30:0x4c]
                n = struct.unpack_from('<i', l, 0xc)[0]
                if n > 0:
                    out += self.read(struct.unpack_from('<I', l, 0x2c)[0], 12 * n)
            polys = ptr(s + 0x34)
            for k in range(max(cnt(0x10), 0)):
                q = self.read(polys + 0x28 * k, 0x28)
                flags, prio, mc, uvs, zone = struct.unpack_from('<Ii', q, 0) + struct.unpack_from('<i', q, 0x10) + \
                    struct.unpack_from('<I', q, 0x18) + struct.unpack_from('<I', q, 0x24)
                n = flags & 0x3ff
                out += struct.pack('<IiiiI', flags, prio, mc, 1 if uvs else 0, zone)
                if n:
                    out += self.read(struct.unpack_from('<I', q, 8)[0], 4 * n)
                if n and flags & 0x1000 and struct.unpack_from('<I', q, 0xc)[0]:
                    out += self.read(struct.unpack_from('<I', q, 0xc)[0], 4 * n)
                mats = struct.unpack_from('<I', q, 0x14)[0]
                for j in range(max(mc, 0)):
                    out += struct.pack('<i', self.mat_index(self.i32(mats + 4 * j)))
                    if uvs:
                        out += self.read(ptr(uvs + 4 * j), 8 * n)
                if struct.unpack_from('<I', q, 0x1c)[0]:  # mechlib's: only with the file's +0x1c
                    out += self.read(struct.unpack_from('<I', q, 0x1c)[0], 12 * n)
            refs = ptr(s + 0x60)
            for k in range(max(cnt(0x5c), 0)):
                r = self.read(refs + 12 * k, 12)
                m, use, poly = struct.unpack('<IiI', r)
                first = (poly - polys) // 0x28 if cnt(0x10) > 0 else -1
                out += struct.pack('<iii', self.mat_index(m), use, first)
        return out


def main():
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_gamez_stack.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                       ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    buf = ctypes.create_string_buffer(1 << 27)
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
    # The models (0x00558ba0, 0x005587e0) after the materials: each world, two
    # stacks, and c1 cut in its models (the counts, the slots, the arrays).
    mcases = [[w] for w in worlds] + [[worlds[2], worlds[0]], [worlds[0], worlds[3]]]
    mstart, nodes = struct.unpack_from('<i', data, 0x18)[0], struct.unpack_from('<i', data, 0x24)[0]
    for k, cut in enumerate((mstart + 7, mstart + 12 + 0x68 * 900 + 5, (mstart + nodes) // 2, nodes - 9)):
        path = scratch / f'mcut{k}.zbd'
        path.write_bytes(data[:cut])
        mcases += [[path], [worlds[2], path]]
    # c1's textures and materials, then one crafted model: what no shipped
    # world has (light colours with fractions, a polygon with two materials
    # each with its own UVs), in a pool of 750 slots with one used.
    crafted = scratch / 'crafted.zbd'
    head = bytearray(0x64)
    struct.pack_into('<IIIIiiiiii', head, 0, 0, 0, 0x80, 1, 1, 3, 3, 0, 1, 0)
    struct.pack_into('<i', head, 0x5c, 1)
    light = bytearray(0x4c)
    struct.pack_into('<i', light, 0xc, 2)
    struct.pack_into('<fff', light, 0x1c, 7.6, 3.5, 15.7)  # rounding crosses a packed bit
    poly = bytearray(0x28)
    struct.pack_into('<IiIIiII', poly, 0, 3 | 0x1000, 2, 0, 0, 2, 0, 1)
    struct.pack_into('<I', poly, 0x24, 7)
    body = (struct.pack('<iii', 1, 1, -1) + bytes(head) + struct.pack('<i', 0)
            + struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 1, 0) + struct.pack('<9f', 0, 0, 1, 0, 0, 1, 0, 0, 1)
            + bytes(light) + struct.pack('<6f', 1, 2, 3, 4, 5, 6) + bytes(poly)
            + struct.pack('<3I', 0, 1, 2) + struct.pack('<3I', 0, 1, 2) + struct.pack('<2i', 3, 5)
            + struct.pack('<6f', 0, 0, 1, 0, 0, 1) + struct.pack('<6f', .5, .5, .25, .5, .5, .25)
            + struct.pack('<9f', *([0.5] * 9)) + struct.pack('<iiI', 3, 1, 0))
    crafted.write_bytes(data[:mstart] + body)
    mcases += [[crafted], [worlds[2], crafted]]
    # The nodes (0x00516e90, 0x005171a0) after the models: each world, a
    # stack, and c1 cut in its nodes (the slots, the world's partitions, the
    # last node's links).
    ncases = [[w] for w in worlds] + [[worlds[2], worlds[0]]]
    for k, cut in enumerate((nodes + 0xd4 * 1000 + 9, nodes + (len(data) - nodes) // 2, len(data) - 6)):
        path = scratch / f'ncut{k}.zbd'
        path.write_bytes(data[:cut])
        ncases += [[path], [worlds[2], path]]
    emu = Emulated()
    runs = converted = 0
    for fmt in ((5, 6, 5), (5, 5, 5)):
        only = os.environ.get('PM_GAMEZ_ONLY')  # textures, models or nodes: one part alone (development)
        for models, todo in ((0, cases), (1, mcases), (2, ncases)):
            if only and only != ('textures', 'models', 'nodes')[models]:
                continue
            for case in todo:
                want = emu.stack(case, *fmt, models=bool(models), nodes=models == 2)
                n = p.probe_pm_gamez_stack('|'.join(map(str, case)).encode(), *fmt, models, buf, len(buf))
                if n < 0:
                    raise SystemExit(f'{case}: probe {n}')
                got = buf.raw[:n]
                name = "+".join(Path(c).parent.name if c.name == "gamez.zbd" else c.stem for c in case)
                if want != got:
                    at = next((i for i in range(min(len(want), len(got))) if want[i] != got[i]), min(len(want), len(got)))
                    print(f'{name} {fmt} models {models}: differ at byte {at} of {len(want)}/{len(got)}')
                    print('exe: ', want[max(0, at - 16):at + 32].hex())
                    print('port:', got[max(0, at - 16):at + 32].hex())
                    raise SystemExit(1)
                runs += 1
                converted += len(emu.converted)
                r = struct.unpack_from(f'<{(2 + min(models, 1) + (models == 2)) * len(case)}i', want)
                print(f'{name} {fmt}{" models" if models else ""}: results {r}'
                      + (f', {len(emu.converted)} models to the strip conversion' if models == 1 else '')
                      + (' nodes' if models == 2 else ''))
    print(f'ok: {runs} runs identical (textures and materials: 4 worlds, 3 stacks, 5 cut or short files; models: '
          f'4 worlds, 2 stacks, 4 cut files, a crafted model alone and stacked; nodes: 4 worlds, a stack, 3 cut files alone and stacked; 2 colour formats); {converted} models went to PM\'s strip conversion, '
          f'which the port leaves out')

if __name__ == '__main__':
    main()
