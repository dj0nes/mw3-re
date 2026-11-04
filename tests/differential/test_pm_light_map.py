"""The script's LightMapTexture (PM's dispatcher 0x5b2d10, docs/pm-map.md,
gmod_matl.c) against PM's exe: the same lines through both dispatchers, with
the texture's add (0x532450) answering the same fake entries.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_light_map.py
"""
import ctypes
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402

DISPATCH, ADD, LIGHT_MAP = 0x5b2d10, 0x532450, 0x683cb0
LINES = ['LightMapTexture lightmap', 'LightMapTexture other.tif', 'LightMapTextureX prefix', 'LightMapTextur short',
         'lightmaptexture lower', 'LightMapTexture', 'LightMapTexture last extra', 'Lightning miss']


class Emulated(g.Emulated):
    def __init__(self):
        super().__init__()
        self.asked = b''
        self.native_stub(ADD, self.add)
        for name, fn in (('strncmp', self.strncmp), ('strchr', self.strchr), ('strlen', self.strlen)):
            if name in g.IAT:
                self.cpu.stub_import(g.IAT[name], fn)

    def add(self, c):
        name = c.arg(0)
        self.asked += (self.cstr(name) if name else b'(none)') + b'\n'
        self.calls += 1
        return 0x70000000 + 0x10 * (self.calls - 1)

    def strncmp(self, c):
        a, b, n = self.cstr(c.arg(0))[:c.arg(2)], self.cstr(c.arg(1))[:c.arg(2)], c.arg(2)
        return 0 if a == b else (1 if a > b else 0xffffffff)

    def strchr(self, c):
        s = self.cstr(c.arg(0))
        i = s.find(bytes([c.arg(1) & 0xff]))
        return c.arg(0) + i if i >= 0 else 0

    def strlen(self, c):
        return len(self.cstr(c.arg(0)))

    def cstr(self, p):
        out = b''
        while True:
            b = self.read(p + len(out), 1)
            if b == b'\0':
                return out
            out += b

    def run(self, lines):
        self.calls = 0
        self.w32(LIGHT_MAP, 0)
        out = b''
        for line in lines:
            words = line.split(' ')
            obj = self.alloc(0xc0)
            self.w32(obj + 8, len(words))
            self.w32(obj + 0xc, 1)
            for i, w in enumerate(words):
                p = self.alloc(len(w) + 1)
                self.cpu._uc.mem_write(p, w.encode() + b'\0')
                self.w32(obj + 0x20 + 4 * i, p)
            argv0 = self.i32(obj + 0x20) & 0xffffffff
            self.call(DISPATCH, ecx=obj, args=(argv0,))
            out += struct.pack('<Iii', self.i32(LIGHT_MAP) & 0xffffffff, self.i32(obj + 0xc), self.i32(obj + 0x14))
        return out + self.asked


def main():
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_light_map.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int]
    buf = ctypes.create_string_buffer(1 << 16)
    want = Emulated().run(LINES)
    n = p.probe_pm_light_map('|'.join(LINES).encode(), buf, len(buf))
    got = buf.raw[:n]
    if got != want:
        for i in range(len(LINES)):
            a, b = want[12 * i:12 * i + 12], got[12 * i:12 * i + 12]
            if a != b:
                sys.exit(f'{LINES[i]!r}: exe {struct.unpack("<Iii", a)}, port {struct.unpack("<Iii", b)}')
        sys.exit(f'names: exe {want[12 * len(LINES):]!r}, port {got[12 * len(LINES):]!r}')
    print(f'ok: {len(LINES)} lines identical (light map, cursor, misses, names)')
    sys.stdout.flush()
    import os
    os._exit(0)


if __name__ == '__main__':
    main()
