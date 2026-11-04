"""The script's TextureAdditiveTransparent and MipBias (PM's dispatcher 0x5b2d10) against PM's exe:
the same lines through both, the texture finder answering the same entries. What each line moves
of the cursor, and the images' +0x0c after all of them.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_additive.py
"""
import ctypes
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_light_map as lm  # noqa: E402
import test_pm_zar  # noqa: E402

FIND, BIAS, D3D, ATOF = 0x5319f0, 0x598230, 0x8c4b70, 0x5de640
LINES = ['TextureAdditiveTransparent one', 'TextureAdditiveTransparent two', 'TextureAdditiveTransparent nothing',
         'TextureAdditiveTransparent noimage', 'TextureAdditiveTransparent one', 'MipBias -1.0', 'MipBias 0.5', 'TextureAdditiveTransparentX two']


class Emulated(lm.Emulated):
    def __init__(self):
        super().__init__()
        self.image = [self.alloc(0x38), self.alloc(0x38)]
        self.entry = [self.alloc(0x28), self.alloc(0x28), self.alloc(0x28)]
        self.w32(self.entry[0], self.image[0]), self.w32(self.entry[1], self.image[1]), self.w32(self.entry[2], 0)
        self.cpu._uc.mem_write(self.image[0] + 0xc, struct.pack('<h', 3))
        self.cpu._uc.mem_write(self.image[1] + 0xc, struct.pack('<h', 0))
        self.native_stub(FIND, self.find)
        self.bias, self.calls = [], 0
        self.native_stub(BIAS, lambda c: self.bias.append(c.arg(0)) or 0)
        self.w32(D3D, 1)
        # atof, for the cursor's sake: the bias itself is not applied (fixes.md).
        one = self.alloc(4)
        self.cpu._uc.mem_write(one, struct.pack('<f', 1.0))
        self.w32(ATOF, self.cpu.code(b'\xd9\x05' + struct.pack('<I', one) + b'\xc3'))

    def find(self, c):
        return {b'one': self.entry[0], b'two': self.entry[1], b'noimage': self.entry[2]}.get(self.cstr(c.arg(0)), 0)

    def lines(self):
        out = []
        for line in LINES:
            words = line.split(' ')
            obj = self.alloc(0xc0)
            self.w32(obj + 8, len(words))
            self.w32(obj + 0xc, 1)
            for i, w in enumerate(words):
                p = self.alloc(len(w) + 1)
                self.cpu._uc.mem_write(p, w.encode() + b'\0')
                self.w32(obj + 0x20 + 4 * i, p)
            self.call(lm.DISPATCH, ecx=obj, args=(self.i32(obj + 0x20) & 0xffffffff,))
            out.append(self.i32(obj + 0xc))
        return out, [struct.unpack('<h', bytes(self.cpu._uc.mem_read(a + 0xc, 2)))[0] for a in self.image]


def main():
    p = test_pm_zar.load_pm_probe()
    want, images = Emulated().lines()
    got_images = (ctypes.c_int16 * 2)()
    got = (ctypes.c_int32 * len(LINES))()
    if p.probe_pm_additive('|'.join(LINES).encode(), got_images, len(LINES), got) != 0:
        sys.exit('lines not all run')
    if list(got) != want or list(got_images) != images:
        sys.exit(f'cursors {list(got)}, the exe {want}; images {list(got_images)}, the exe {images}')
    print(f'ok: {len(LINES)} lines, cursors {want}, images {images}')
    sys.stdout.flush()
    os._exit(0)


if __name__ == '__main__':
    main()
