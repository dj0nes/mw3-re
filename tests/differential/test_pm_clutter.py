"""PM's clutter settings (pm/gamez/zclass/cls_clutter, 0x513030 and 0x513310) against PM's exe, on
the four worlds' templates.zrd and crafted entries, with every named node in the list and with every
third one missing. The anim lookup and the hit callbacks are stubbed and logged in both.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_clutter.py

Compared: the log's order, every field of every record, every node's flags. Rotations bit for bit;
slopes go through x87 fcos, within 1e-6.
"""
import ctypes
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT  # noqa: E402
import test_pm_zar  # noqa: E402
import test_pm_zreader as zr  # noqa: E402

sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import zrd  # noqa: E402

READ, ANIM, WATCH, WATCH2, ZERR, STRDUP_IAT = 0x513030, 0x524b50, 0x5a4d80, 0x5a4dc0, 0x406cb0, '_strdup'
LISTS, LIST_DIRTY, BEGIN, END, CAP = 0x60db00, 0x60db84, 0x639bf4, 0x639bf8, 0x639bfc
PM = ROOT / 'game' / 'pm'


def fnv(b):
    v = 2166136261
    for c in b:
        v = ((v ^ c) * 16777619) & 0xffffffff
    return v


class Emu(zr.Emulated):
    def __init__(self, path, rel):
        super().__init__()
        self.files[test_pm_zar.key(rel)] = bytearray(path.read_bytes())
        if not self.call(zr.OPEN_ARCHIVE, args=[self.string(rel.replace('/', '\\')), 1]):
            raise SystemExit(f'{rel}: PM does not open it')
        self.cpu.fpu_control(0x7f)
        self.log = []
        self.nodes = []
        self.cpu.stub_import(test_pm_zar.IAT[STRDUP_IAT], self.strdup)
        self.native_stub(ANIM, self.anim)
        self.native_stub(WATCH, self.watch)
        self.native_stub(WATCH2, lambda c: 0)
        self.cpu.stubs[ZERR] = (self.zerr, 0)  # the bench's own hook stays on it

    def w32(self, a, v):
        self.cpu._uc.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def anim(self, c):
        if not c.arg(0):
            return 0
        v = fnv(self.cstr(c.arg(0)))
        self.log += [0xa0000000, v]
        return v

    def watch(self, c):
        self.log += [0xb0000000, self.nodes.index(c.arg(1)) + 1]
        return 1

    def zerr(self, c):
        self.log += [0xc0000000, c.arg(0) << 16 | c.arg(2)]
        return 0

    def make(self, name):
        n = self.alloc(0xd4)
        self.cpu._uc.mem_write(n, name.encode()[:35] + b'\0')
        self.w32(n + 0x34, 5)
        self.w32(n + 0x24, 0x10)
        self.nodes.append(n)
        return n

    def run(self, file, names):
        uc, w = self.cpu._uc, self.w32
        self.log, self.nodes = [], []
        items = []
        for name in names:
            n = self.make(name)
            a, b = self.make(name + '_a'), self.make(name + '_b')
            kids = self.alloc(8)
            uc.mem_write(kids, struct.pack('<II', a, b))
            uc.mem_write(n + 0x56, struct.pack('<h', 2))
            w(n + 0x5c, kids)
            items.append((n, self.alloc(0x10)))
        for k, (n, item) in enumerate(items):
            uc.mem_write(item, struct.pack('<IIII', n, items[k - 1][1] if k else 0,
                                           items[k + 1][1] if k + 1 < len(items) else 0, 1))
        head = self.alloc(12)
        uc.mem_write(head, struct.pack('<III', items[0][1] if items else 0, items[-1][1] if items else 0, 0))
        w(LISTS + 24, head)
        w(LIST_DIRTY, 0)
        for a in (BEGIN, END, CAP):
            w(a, 0)
        self.call(READ, args=[self.string(file)])
        r = self.r32
        out = [len(self.log)] + self.log
        begin, end = r(BEGIN), r(END)
        out.append((end - begin) // 4)
        text = ''
        self.slopes = set()

        def nid(n):
            return self.nodes.index(n) + 1 if n else 0
        for p in range(begin, end, 4):
            t = r(p)
            text += (self.cstr(r(t)).decode('latin1') if r(t) else '') + '\n'
            out += [r(t + o) for o in range(0x14, 0x3c, 4)]
            out.append(r(t + 0x3c) & 0xff)
            out += [r(t + o) for o in range(0x40, 0x60, 4)]
            self.slopes |= {len(out) - 2, len(out) - 1}
            out.append(r(t + 0x60) & 0xff)
            for o in (0x64, 0x70, 0x7c):
                out += [r(t + o), r(t + o + 4), nid(r(t + o + 8))]
            a, b = r(t + 8), r(t + 0xc)
            out.append((b - a) // 8 if a else 0)
            for s in range(a, b, 8):
                out += [r(s), nid(r(s + 4))]
        out.append(len(self.nodes))
        out += [r(n + 0x24) for n in self.nodes]
        return out, text


def compare(label, emu, probe, base, rel, file, names):
    want, wtext = emu.run(file, names)
    buf = (ctypes.c_uint32 * (1 << 18))()
    text = ctypes.create_string_buffer(1 << 18)
    n = probe.probe_pm_clutter(str(base).encode(), rel.encode(), file.encode(), '\n'.join(names).encode(), buf,
                               len(buf), text, len(text))
    if n < 0:
        raise SystemExit(f'{label}: the port cannot open it')
    got = list(buf[:n])
    if text.value.decode('latin1') != wtext:
        raise SystemExit(f'{label}: names differ\n exe {wtext!r}\n port {text.value!r}')
    if len(got) != len(want):
        raise SystemExit(f'{label}: {len(want)} words against {len(got)}\n exe {want[:30]}\n port {got[:30]}')
    for i, (a, b) in enumerate(zip(want, got)):
        if a != b:
            fa, fb = [struct.unpack('<f', struct.pack('<I', v))[0] for v in (a, b)]
            if i in emu.slopes and abs(fa - fb) < 1e-6:  # x87 fcos
                continue
            raise SystemExit(f'{label}: word {i}: exe {a:08x}, port {b:08x}\n exe {want[max(0, i - 6):i + 3]}')
    return wtext.count('\n')


def names_in(text):
    return sorted(set(re.findall(r"[A-Za-z0-9_]+\.flt", text)))


def tree(v):
    if isinstance(v, int):
        return struct.pack('<ii', 1, v)
    if isinstance(v, float):
        return struct.pack('<if', 2, v)
    if isinstance(v, str):
        b = v.encode('latin-1')
        return struct.pack('<iI', 3, len(b)) + b
    return struct.pack('<iI', 4, len(v) + 1) + b''.join(tree(x) for x in v)


CRAFTED = [
    ['node', ['a.flt'], 'substitute', [[1.0, 'a.flt'], [3, 'b.flt'], [2.0, 'nosuch.flt'], [2.0]],
     'rotation_range', [[10.0, 20, 30.0], [40, 50.0, 60]], 'min_slope', [5], 'max_slope', [45.0], 'align_normal', [],
     'scale_range', [2, 3.5], 'translate_uv_range', [[-1, 1.5], [2, -3]], 'far_fade_range', [[1, 2], [3, 4]],
     'OnWeaponHit', ['health', [0.5], 'anim', ['boom'], 'model', ['b.flt']],
     'OnCrater', ['health', [2], 'model', ['nosuch.flt']], 'OnCollide', ['health', [0.0], 'anim', ['bang']]],
    ['node', ['bare.flt']],
    ['nothing', [1]],
    ['node', ['zero.flt'], 'substitute', [[0.0, 'a.flt'], [0, 'b.flt']]],
    ['node', ['again.flt'], 'substitute', [[1.0, 'a.flt']], 'OnWeaponHit', ['anim', ['no_health']]],
    ['node', ['ints.flt'], 'scale_range', [1, 2], 'substitute', [[1, 'c.flt']]],
]


def main():
    probe = test_pm_zar.load_pm_probe()
    probe.probe_pm_clutter.argtypes = [ctypes.c_char_p] * 4 + [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    runs = templates = 0
    for world in ('c1', 'c2', 'c3', 'c4'):
        rel = f'zbd/{world}/reader.zbd'
        data = (PM / rel).read_bytes()
        top = next(zrd.node(data[s:s + n], 0)[0] for name, s, n in zrd.entries(data) if name == 'templates.zrd')
        names = names_in(repr(top))
        emu = Emu(PM / rel, rel)
        for scene in (names, [n for i, n in enumerate(names) if i % 3]):
            templates += compare(f'{world} {len(scene)} nodes', emu, probe, PM, rel, 'templates.zrd', scene)
            runs += 1
    with tempfile.TemporaryDirectory() as tmp:
        body = tree(CRAFTED)
        entry = struct.pack('<II', 0, len(body)) + b'templates.zrd'.ljust(64, b'\0')
        path = Path(tmp) / 'crafted.zbd'
        path.write_bytes(body + entry.ljust(test_pm_zar.ENTRY, b'\0') + struct.pack('<III', 2, 1, test_pm_zar.crc32(body)))
        emu = Emu(path, 'crafted.zbd')
        for scene in (['a.flt', 'b.flt', 'c.flt'], ['b.flt'], []):
            compare(f'crafted {scene}', emu, probe, tmp, 'crafted.zbd', 'templates.zrd', scene)
            runs += 1
    print(f'ok: {runs} runs identical, {templates} shipped templates')


if __name__ == '__main__':
    main()
