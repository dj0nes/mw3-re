"""The emulated world zTwoD runs in (docs/twod.md, Testing), shared by the
zTwoD tests: every call out of zTwoD stubbed and recorded under the key the
port's Host uses (src/gamez/ztwod/twod_host.h), with the same arguments; the
answers come from a script that depends only on the seed, the key and the
call's arguments, so both sides get the same ones. A fake GDI (7 pixels a
character) and a fake image cache (sizes from the name) stand in for Windows
and zImage; tests/probe_twod.cpp does the same on the rebuild's side.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from calltrace import Emu, Trace, rnd  # noqa: E402
from original import EXES, ROOT  # noqa: E402

READER = ROOT / 'game' / 'mw3-1.2' / 'zbd' / 'reader.zbd'
READ_NODE = 0x557180


def iat_slots():
    """Import name -> IAT slot address, MW3's (original.py maps them to the
    exe that runs)."""
    b = EXES['mw3'][0].read_bytes()
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    nsec = struct.unpack_from('<H', b, pe + 6)[0]
    optsz = struct.unpack_from('<H', b, pe + 20)[0]
    opt = pe + 24
    imp_rva = struct.unpack_from('<I', b, opt + 104)[0]
    secs = []
    for i in range(nsec):
        o = opt + optsz + i * 40
        vsize, va, rsize, raw = struct.unpack_from('<IIII', b, o + 8)
        secs.append((va, max(vsize, rsize), raw))

    def off(rva):
        for va, sz, raw in secs:
            if va <= rva < va + sz:
                return rva - va + raw
        raise ValueError(hex(rva))

    def cstr(o):
        return b[o:b.index(b'\0', o)].decode()
    out = {}
    d = off(imp_rva)
    while True:
        oft, _, _, name, ft = struct.unpack_from('<IIIII', b, d)
        if not name:
            break
        t, slot = off(oft or ft), ft
        while True:
            e = struct.unpack_from('<I', b, t)[0]
            if not e:
                break
            if not e & 0x80000000:
                out[cstr(off(e) + 2)] = 0x400000 + slot
            t += 4
            slot += 4
        d += 20
    return out


IAT = iat_slots()


def text_w(n):
    return 7 * n


def text_h(n):
    return 13 + n % 3


def h32(s):
    return int.from_bytes(hashlib.sha1(s.encode('latin1')).digest()[:4], 'little')


def c_format(emu, fmt, va):
    """printf for the formats the messages can hold: %s %d %i %u %x %c %f %%."""
    out = []
    i = 0
    ap = va
    while i < len(fmt):
        c = fmt[i]
        if c != '%':
            out.append(c)
            i += 1
            continue
        m = re.match(r'%([-+ #0]*)(\d*)(?:\.(\d+))?([lh]?)([sdiuxXcf%])', fmt[i:])
        if not m:
            out.append(c)
            i += 1
            continue
        flags, width, prec, _, conv = m.groups()
        i += m.end()
        if conv == '%':
            out.append('%')
            continue
        if conv == 'f':  # a double on the stack
            d = struct.unpack('<d', struct.pack('<II', emu.r32(ap), emu.r32(ap + 4)))[0]
            ap += 8
            out.append(f'{d:.{int(prec) if prec else 6}f}')
            continue
        v = emu.r32(ap)
        ap += 4
        if conv == 's':
            s = emu.cstr(v) if v else '(null)'
            if prec:
                s = s[:int(prec)]
        elif conv in 'di':
            s = str(struct.unpack('<i', struct.pack('<I', v))[0])
        elif conv == 'u':
            s = str(v)
        elif conv == 'x':
            s = f'{v:x}'
        elif conv == 'X':
            s = f'{v:X}'
        else:
            s = chr(v & 0xff)
        if width:
            s = s.rjust(int(width), '0' if '0' in flags and conv != 's' else ' ') if '-' not in flags \
                else s.ljust(int(width))
        out.append(s)
    return ''.join(out)


class TwodWorld:
    """An emulator with zTwoD's outside stubbed; `t` records both sides."""

    def __init__(self, t, seed):
        self.t = t
        self.seed = seed
        e = self.emu = Emu(t)
        self.cpu = e.cpu
        e.cpu.fpu_control(0x007f)  # 24-bit, as in play (CLAUDE.md)
        e.heap_stubs()
        self.stub_textures = set()
        self.mouse_addr = e.world_alloc(0x40)
        self.mouse_frames = []
        self.frame = 0
        self.file = None
        self.file_pos = 0
        # zVideo's 565 format for 0x00579370 (zvid::pixel_format()'s defaults).
        for a, v in ((0x802738, 8), (0x80273c, 3), (0x802740, 3), (0x802744, 0xf8), (0x802748, 0xfc),
                     (0x80274c, 0xf8)):
            e.w32(a, v)
        self.answers = {}
        self.dc_image, self.dc_color = {}, {}
        t.script = Script(self.answer)
        self._crt()
        self._gdi()
        self._engine()

    # ---- answers ----
    def answer(self, key):
        def f(n, a):
            r = rnd(self.seed, f'{key}#{n}')
            v = self.answers.get(key)
            if v is not None:
                return v(r, n, a) if callable(v) else v
            return 1
        return f

    def fake_texture(self, w, h):
        e = self.emu
        a = e.world_alloc(0x38)
        e.world_w32(a + 0, w * h)
        e.world_w32(a + 4, (w & 0xffff) | (h & 0xffff) << 16)
        self.stub_textures.add(a)
        return a

    def paint(self, img, rect, color):
        """The fake DrawTextA's glyphs: the colour, packed 565, on two pixels of three."""
        e = self.emu
        w, h = e.r32(img + 4) & 0xffff, e.r32(img + 4) >> 16
        px = e.r32(img + 0x10)
        c = ((color & 0xf8) << 8) | (((color >> 8) & 0xfc) << 3) | (((color >> 16) & 0xff) >> 3)
        for y in range(max(rect[1], 0), min(rect[3], h)):
            for x in range(max(rect[0], 0), min(rect[2], w)):
                if (x + y) % 3:
                    e.cpu.write(px + 2 * (y * w + x), struct.pack('<H', c))

    # ---- the C runtime ----
    def _crt(self):
        e, cpu = self.emu, self.cpu

        def ret(v, pop=0):
            def fn(emu, key, a, p):
                emu._ret(v(a) if callable(v) else v, pop)
            return fn

        def malloc(emu, key, a, pop):
            emu._ret(emu.alloc(max(a[0], 4)), 0)

        def calloc(emu, key, a, pop):
            emu._ret(emu.alloc(max(a[0] * a[1], 4)), 0)

        def strncpy(emu, key, a, pop):
            d, s, n = a
            src = emu.cpu.read(s, n) if s else b''
            z = src.find(0)
            data = src[:z] if z >= 0 else src
            emu.cpu.write(d, (data + b'\0' * n)[:n])
            emu._ret(d, 0)

        def strncmp(emu, key, a, pop):
            x, y, n = a
            p, q = emu.cpu.read(x, n), emu.cpu.read(y, n)
            r = 0
            for i in range(n):
                if p[i] != q[i]:
                    r = -1 if p[i] < q[i] else 1
                    break
                if p[i] == 0:
                    break
            emu._ret(r & 0xffffffff, 0)

        def stricmp(emu, key, a, pop):
            x, y = emu.cstr(a[0]).lower(), emu.cstr(a[1]).lower()
            emu._ret(((x > y) - (x < y)) & 0xffffffff, 0)

        def vsnprintf(emu, key, a, pop):
            buf, n, fmt, va = a
            s = c_format(emu, emu.cstr(fmt), va).encode('latin1')
            if len(s) < n:
                emu.cpu.write(buf, s + b'\0')
                emu._ret(len(s), 0)
            else:
                emu.cpu.write(buf, s[:n])
                emu._ret(0xffffffff, 0)

        def vsprintf(emu, key, a, pop):
            buf, fmt, va = a
            s = c_format(emu, emu.cstr(fmt), va).encode('latin1')
            emu.cpu.write(buf, s + b'\0')
            emu._ret(len(s), 0)

        def read_file(emu, key, a, pop):
            handle, buf, n, out, _ = a
            chunk = self.file[self.file_pos:self.file_pos + n]
            self.file_pos += len(chunk)
            emu.cpu.write(buf, chunk)
            emu.w32(out, len(chunk))
            emu._ret(1, 20)

        stubs = {
            'malloc': ('s0', 0, malloc), 'calloc': ('s0,s1', 0, calloc), 'free': ('s0', 0, ret(0)),
            'strncpy': ('s0,s1,s2', 0, strncpy), 'strncmp': ('s0,s1,s2', 0, strncmp),
            '_stricmp': ('s0,s1', 0, stricmp), '_vsnprintf': ('s0,s1,s2,s3', 0, vsnprintf),
            'vsprintf': ('s0,s1,s2', 0, vsprintf), 'ReadFile': ('s0,s1,s2,s3,s4', 20, read_file),
            'CreateMutexA': ('', 12, ret(0x4d00, 12)), 'WaitForSingleObject': ('', 8, ret(0, 8)),
            'ReleaseMutex': ('', 4, ret(1, 4)),
        }
        for name, (spec, pop, fn) in stubs.items():
            e.import_stub(IAT[name], '@' + name, spec, pop, fn)

    # ---- GDI ----
    def _gdi(self):
        e = self.emu
        t = self.t
        w = self
        n = {'dc': 0, 'font': 0, 'sel': 0}

        def counter(kind, base):
            def f(r, k, a):
                n[kind] += 1
                return base + n[kind]
            return f
        self.answers.update({
            'GetStockObject': lambda r, k, a: 0x0f000000 | a[0],
            'CreateFontA': counter('font', 0xf0000000),
            'CreateCompatibleDC': counter('dc', 0xdc000000),
            'SelectObject': counter('sel', 0x5e000000),
            'GetLastError': 5,
        })
        plain = {'GetStockObject': ('s0', 4), 'DeleteObject': ('s0', 4), 'CreateCompatibleDC': ('s0', 4),
                 'SelectObject': ('s0,s1', 8), 'DeleteDC': ('s0', 4),
                 'SetBkColor': ('s0,s1', 8), 'SetBkMode': ('s0,s1', 8), 'GetLastError': ('', 0),
                 'CreateFontA': (','.join(f's{i}' for i in range(13)) + ',zs13', 56)}
        for name, (spec, pop) in plain.items():
            e.import_stub(IAT[name], name, spec, pop)

        def extent(emu, key, a, pop):
            dc, s, ln, out = a
            v = t.handle(key, (dc, emu.cstr(s), ln))
            emu.w32(out, text_w(ln))
            emu.w32(out + 4, text_h(ln))
            emu._ret(v, 16)

        def draw_text(emu, key, a, pop):
            dc, s, ln, rp, fmt = a
            text = emu.cstr(s)
            rect = [emu.r32(rp + 4 * i) for i in range(4)]
            v = t.handle(key, (dc, text, ln, *rect, fmt))
            if fmt & 0x400:
                emu.w32(rp + 8, (rect[0] + text_w(len(text))) & 0xffffffff)
                emu.w32(rp + 12, (rect[1] + text_h(len(text))) & 0xffffffff)
            elif dc in w.dc_image:
                w.paint(w.dc_image[dc], [struct.unpack('<i', struct.pack('<I', x))[0] for x in rect],
                        w.dc_color.get(dc, 0))
            emu._ret(v, 20)

        def text_color(emu, key, a, pop):
            t.handle(key, tuple(a))
            w.dc_color[a[0]] = a[1]
            emu._ret(0, 8)
        e.import_stub(IAT['SetTextColor'], 'SetTextColor', 's0,s1', 8, text_color)

        def metrics(emu, key, a, pop):
            dc, out = a
            v = t.handle(key, (dc,))
            for i, x in enumerate((13, 11, 2, 2, 1)):  # height, ascent, descent, internal, external
                emu.w32(out + 4 * i, x)
            emu._ret(v, 8)
        def get_object(emu, key, a, pop):  # GetObjectA(font, size, &logfont): the font marks the copy
            h, size, out = a
            v = t.handle(key, (h,))
            emu.w32(out, h)
            emu._ret(v, 12)

        def create_indirect(emu, key, a, pop):  # CreateFontIndirectA(&logfont)
            emu._ret(t.handle(key, (emu.r32(a[0]),)), 4)
        e.import_stub(IAT['GetObjectA'], 'GetObjectA', 's0,s1,s2', 12, get_object)
        e.import_stub(IAT['CreateFontIndirectA'], 'CreateFontIndirectA', 's0', 4, create_indirect)
        self.answers['CreateFontIndirectA'] = counter('font', 0xf0000000)
        e.import_stub(IAT['GetTextExtentPoint32A'], 'GetTextExtentPoint32A', 's0,s1,s2,s3', 16, extent)
        e.import_stub(IAT['DrawTextA'], 'DrawTextA', 's0,s1,s2,s3,s4', 20, draw_text)
        e.import_stub(IAT['GetTextMetricsA'], 'GetTextMetricsA', 's0,s1', 8, metrics)

    # ---- the engine around zTwoD ----
    def _engine(self):
        e = self.emu
        t = self.t
        w = self

        def image_get(r, k, a):
            name = a[0]
            if not name or isinstance(name, int):
                return 0
            hv = h32(f'{w.seed}:{name}')
            if hv % 11 == 0:
                return 0  # a missing image
            return w.fake_texture(16 + hv % 200, 8 + (hv >> 8) % 120)

        def font_find(r, k, a):
            if not isinstance(a[0], str) or h32(a[0]) % 9 == 0:
                return 0
            hv = h32(f'{w.seed}:font:{a[0]}')
            f = e.world_alloc(0x58)
            e.world_w32(f + 0x40, 0)
            e.world_w32(f + 0x44, 0x0f0f0000 | (hv & 0xffff))
            e.world_w32(f + 0x48, hv & 0xffffff)
            e.world_w32(f + 0x4c, (hv >> 4) & 0xffffff if hv & 1 else hv & 0xffffff)
            e.world_w32(f + 0x50, (hv >> 9) % 3)
            e.world_w32(f + 0x54, (hv >> 12) & 1)
            return f

        def message(r, k, a):
            if not isinstance(a[0], str) or h32(a[0]) % 7 == 0:
                return 0
            return e.world_string('T:' + a[0])

        def sound_find(r, k, a):
            return 0 if not isinstance(a[0], str) or h32(a[0]) % 5 == 0 else 0x50000 + h32(a[0]) % 0x1000

        self.answers.update({
            '0x005179d0': image_get,
            '0x00517b10': 0,
            '0x00517680': font_find,
            '0x005172e0': lambda r, k, a: a[0],
            '0x00517300': 0,
            '0x00575fb0': message,
            '0x00579500': lambda r, k, a: w.fake_texture(640, 480),
            '0x00579660': lambda r, k, a: 1 if r.random() < 0.9 else 0,
            '0x0056f6f0': sound_find,
            '0x0056e3d0': lambda r, k, a: 0 if r.random() < 0.2 else 0x700000 + k,
            '0x00572a30': lambda r, k, a: (3 + h32(a[0]) % 5) if isinstance(a[0], str) and a[0].startswith('cd') else 0,
            '0x00572af0': lambda r, k, a: 1 if r.random() < 0.5 else 0,
            '[0x0072f394]': lambda r, k, a: 0 if r.random() < 0.05 else 0xdd000000 + k,
        })
        plain = {
            0x576de0: '', 0x576e70: '', 0x516f50: 'zc', 0x5179d0: 'zc', 0x517b10: 'c', 0x517680: 'zc',
            0x5172e0: 'c', 0x517300: 'c', 0x517cc0: 'c', 0x575fb0: 'zc', 0x579500: 'c',
            0x572a30: 'zc', 0x572af0: 'c,d', 0x572c30: '', 0x56f6f0: 'zc', 0x56e3d0: 'c,s0', 0x56e9f0: 'c',
            0x51b1c0: '', 0x5ae170: 'zc,d,s0,s1', 0x5a7b50: 'zs0', 0x5a7c30: 's0', 0x5a7d10: '',
            0x59a530: 'c',
        }
        for addr, spec in plain.items():
            e.stub(addr, f'{addr:#010x}', spec)

        def measure(emu, key, a, pop):
            text, font, wp, hp = a
            t.handle(key, (emu.cstr(text), font))
            n = len(emu.cstr(text))
            emu.w32(wp, text_w(n))
            emu.w32(hp, text_h(n))
            emu._ret(0, pop)
        e.stub(0x51a0d0, '0x0051a0d0', 'c,d,s0,s1', None, measure)

        def grab_rect(emu, key, a, pop):
            buf, rp, into = a
            rect = [struct.unpack('<i', struct.pack('<I', emu.r32(rp + 4 * i)))[0] for i in range(4)]
            v = t.handle(key, (buf, *rect, into))
            rect = [max(rect[0], 0), max(rect[1], 0), min(rect[2], 640), min(rect[3], 480)]
            for i, x in enumerate(rect):
                emu.w32(rp + 4 * i, x & 0xffffffff)
            emu._ret(into if v else 0, pop)
        e.stub(0x579660, '0x00579660', 'c,d,s0', None, grab_rect)

        def blit(emu, key, a, pop):
            img, x, y, flags, rp = a
            if rp:
                t.handle(key, (img, x, y, flags, *[emu.r32(rp + 4 * i) for i in range(4)]))
            else:
                t.handle(key, (img, x, y, flags))
            emu._ret(0, pop)
        e.stub(0x55b1f0, '0x0055b1f0', 'c,d,s0,s1,s2', None, blit)

        def mouse(emu, key, a, pop):
            t.handle(key, ())
            vals = w.mouse_frames[w.frame] if w.frame < len(w.mouse_frames) else [0] * 15
            for i, v in enumerate(vals):
                emu.w32(w.mouse_addr + 4 * i, v)
            emu._ret(w.mouse_addr, pop)
        e.stub(0x51b7b0, '0x0051b7b0', '', None, mouse)

        def image_dc(emu, key, a, pop):
            img, out = a
            dc = t.handle(key, (img,))
            w.dc_image[dc] = img
            emu.w32(out, dc)
            emu._ret(1 if dc else 0, 0)

        def image_dc_release(emu, key, a, pop):
            t.handle(key, tuple(a))
            emu._ret(0, 0)
        e.w32(0x72f394, e.new_stub('[0x0072f394]', 'c,d', 0, image_dc))
        e.w32(0x72f398, e.new_stub('[0x0072f398]', 'c,d', 0, image_dc_release))

    # ---- the tree ----
    def read_tree(self, data, offset):
        """The original zReader's tree of the file at `offset` in `data`."""
        self.file, self.file_pos = data, offset
        root = self.emu.alloc(8)
        self.emu.call(READ_NODE, ecx=0x1234, edx=root)
        return root


class Script(dict):
    """Answers every key through `f` (calltrace.Trace looks keys up)."""

    def __init__(self, f):
        super().__init__()
        self.f = f

    def get(self, key, default=None):
        return self.f(key)

    def __contains__(self, key):
        return True

    def __getitem__(self, key):
        return self.f(key)

    def items(self):
        return []


def entries(data):
    version, count = struct.unpack_from('<II', data, len(data) - 8)
    table = len(data) - 8 - count * 0x94
    for i in range(count):
        start, length = struct.unpack_from('<II', data, table + i * 0x94)
        name = data[table + i * 0x94 + 8:table + i * 0x94 + 72].split(b'\0')[0].decode()
        yield name, start, length


def parse_node(data, pos):
    """The binary zReader format as Python values: int, float, str, list."""
    t = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    if t == 1:
        return struct.unpack_from('<i', data, pos)[0], pos + 4
    if t == 2:
        return struct.unpack_from('<f', data, pos)[0], pos + 4
    if t == 3:
        n = struct.unpack_from('<I', data, pos)[0]
        return data[pos + 4:pos + 4 + n].decode('latin1'), pos + 4 + n
    if t == 4:
        n = struct.unpack_from('<I', data, pos)[0]
        pos += 4
        out = []
        for _ in range(n - 1):
            v, pos = parse_node(data, pos)
            out.append(v)
        return out, pos
    raise ValueError(t)


def find(node, key):
    """zrdr::find: the value after the first string `key`, depth first."""
    if not isinstance(node, list):
        return None
    for i, v in enumerate(node):
        if isinstance(v, list):
            r = find(v, key)
            if r is not None:
                return r
        elif v == key and i + 1 < len(node):
            return node[i + 1]
    return None


def write_node(v):
    """A Python value in the binary zReader format (parse_node's inverse)."""
    if isinstance(v, bool):
        v = int(v)
    if isinstance(v, int):
        return struct.pack('<Ii', 1, v)
    if isinstance(v, float):
        return struct.pack('<If', 2, v)
    if isinstance(v, str):
        b = v.encode('latin1')
        return struct.pack('<II', 3, len(b)) + b
    if isinstance(v, list):
        return struct.pack('<II', 4, len(v) + 1) + b''.join(write_node(x) for x in v)
    raise TypeError(v)


def synthetic_dialog(seed):
    """A dialog with every key the loader reads, shipped or not: two-colour and
    aligned fonts, BACKGROUND_TEXT and _SOUNDS (CD tracks among them), labels by
    font number and by offset, nested labels, flashes, check buttons with all
    their TEXTs, wrapped text primitives, a capturing cursor."""
    r = rnd(seed, 'synthetic')

    def col():
        return [r.randrange(256), r.randrange(256), r.randrange(256)]

    def label():
        k = r.randrange(4)
        msg = r.choice(['MSG_A', 'MSG_B', 'MSG_LONG_NAME', 'MSG_PCT'])
        if k == 0:
            return [msg, r.randrange(-5, 60), r.randrange(-5, 40), r.randrange(4)]
        if k == 1:
            return [msg, 'offset', [r.randrange(80), r.randrange(40)], 'font', [r.choice(['main_red', 'small'])]]
        if k == 2:
            return [[msg, 3, 4, 1], ['MSG_B', 'offset', [10, 20]]]
        return [msg]
    fonts = [[0, 'Arial', 14, col(), 1, 700, 'CENTER'],
             [1, 'Impact', 20, [col(), col()]],
             [2, 'Lucida Console', 12, col(), 0, 400, 'RIGHT'],
             [3, 'Arial', 9, col(), 1, 400, 'BOGUS'],
             [5, 'Impact', 11, col(), 0, 300, 'LEFT']]
    buttons = []
    for i in range(r.randrange(2, 6)):
        b = ['POSITION', [r.randrange(600), r.randrange(440)]]
        if r.random() < 0.8:
            b += ['BITMAP', ['btn%d' % r.randrange(5)] + (['offset', [2, 3]] if r.random() < 0.3 else [])]
        for state in ('ROLLOVER', 'DISABLE', 'ACTIVATE'):
            if r.random() < 0.7:
                st = []
                if r.random() < 0.7:
                    st += ['BITMAP', ['%s%d' % (state.lower(), i), 'offset', [1, 1]]]
                if r.random() < 0.7:
                    st += ['SOUND', ['snd_%s' % state.lower()] + ([0.5] if r.random() < 0.5 else [])]
                if r.random() < 0.6:
                    st += ['LABEL', label()]
                if r.random() < 0.4:
                    st += ['FLASH', ['RATE', [r.choice([0.0, 0.25, 0.8])], 'COLOR', [col()]]]
                b += [state, st]
        if r.random() < 0.8:
            b += ['LABEL', label()]
        if r.random() < 0.4:
            b += ['FLASH', ['RATE', [0.5], 'COLOR', [col()]]]
        if r.random() < 0.4:
            b += ['CHECKED', ['BITMAP', ['checked%d' % i], 'TEXT', label()]]
            if r.random() < 0.6:
                b += ['DISABLE_UNSEL', ['BITMAP', ['du%d' % i], 'TEXT', ['MSG_DU', 4, 5, r.randrange(3)]]]
            if r.random() < 0.6:
                b += ['DISABLE_SEL', ['BITMAP', ['ds%d' % i], 'TEXT', label()]]
        buttons += ['B%d' % i, b]
    if r.random() < 0.7:  # a slider
        buttons += ['SLIDE', ['POSITION', [r.randrange(500), r.randrange(400)], 'BITMAP', ['bar_empty'],
                              'FILLBITMAP', ['bar_full'] + ([r.randrange(-5, 20), r.randrange(-5, 20)]
                                                            if r.random() < 0.6 else [])]]
    if r.random() < 0.8:  # a list box ("above" below the count: the original overruns its array otherwise)
        n = r.randrange(1, 7)
        lb = ['POSITION', [r.randrange(500), r.randrange(400)], 'BITMAP', ['lb'],
              'SELECTED_FONT', r.randrange(3) if r.random() < 0.4 else [r.choice(['small', 'main_red'])],
              'LIST_FONT', ['small'], 'SPACING', r.choice([12, 15, 18]),
              'LIST_OFFSET', [[r.randrange(-10, 10), r.randrange(-10, 10)], [r.randrange(-10, 10), r.randrange(30)]],
              'LISTSIZE', ([n, r.randrange(0, n)] if r.random() < 0.6 else [n])]
        if r.random() < 0.5:
            lb += ['ACTIVE_RECT', [0, 0, r.randrange(50, 200), r.randrange(10, 80)]]
        if r.random() < 0.5:
            lb += ['LISTWIDTH', r.randrange(20, 120)]
        buttons += ['LIST', lb]
    prims = []
    for i in range(r.randrange(1, 4)):
        if r.random() < 0.5:
            p = ['TEXT', ['MSG_P%d' % i], 'POSITION', [r.randrange(600), r.randrange(400)]]
            if r.random() < 0.5:
                p += ['WORDWRAP', [r.randrange(50, 300), r.randrange(10, 100)]]
            p += ['FONT', [r.randrange(6)]] if r.random() < 0.6 else ['font', ['small']]
        else:
            p = ['BITMAP', ['prim%d' % i], 'POSITION', [r.randrange(600), r.randrange(400)]]
            if r.random() < 0.5:
                p += ['CENTER', [1]]
        prims += ['P%d' % i, p]
    body = ['IMAGE_PATH', ['synthetic;shared'], 'FONTS', fonts,
            'BACKGROUND_IMAGES', [['bg_a', 0, 0], ['bg_b', 30, 40], ['bg_solo']],
            'BACKGROUND_TEXT', [['MSG_T%d' % i, r.randrange(600), r.randrange(400), r.randrange(6)]
                                for i in range(r.randrange(1, 5))],
            'BACKGROUND_SOUNDS', [['cd_track'], ['loop_a', 0.75], ['loop_b']],
            'BUTTONS', buttons, 'PRIMITIVES', prims,
            'CURSOR', ['BITMAP', ['cursor_x'], 'CENTER', [r.randrange(2)], 'CAPTURE', [1]]]
    return [['SHARED_IMAGE_PATH', ['common'], 'SYNTH', body]]
