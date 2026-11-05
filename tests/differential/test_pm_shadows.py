"""Pirate's Moon's projected-shadow casters (zmod shadow_*: the init 0x55d740, new 0x55d7b0, size 0x55d8c0,
node, position, colour, enable, the draw 0x55da40 with the node walk 0x55de50 and PM's model caster 0x55dec0,
the blur and ramp) against PM's exe, on mechlib.zbd's trees as test_pm_mechlib.py loads them over c1
(docs/render-shadows.md, docs/pm-map.md).

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_shadows.py [draws per run]

Each run loads some trees, makes casters on their roots (random sizes, colours, positions), and draws them under
random lights and grounds (some from above, which draw nothing); after every draw the caster list is compared
whole: every field to +0x58, the size and pixels of each image drawn so far, the stack's top and the filler's winding. Precisions:
game (the exe's x87 at 24 bits, the port's k24: blank images, as MW3's) and x53 (the exe at 53 bits, the port's
default k53). The texture upload is not here (test_pm_device.py covers PM's device).
"""
import ctypes
import math
from fractions import Fraction
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_mechlib as ml  # noqa: E402
import test_pm_zar  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_ESI  # noqa: E402

INIT, NEW, SIZE, NODE, POSITION, COLOR, ENABLE, DRAW = (
    0x55d740, 0x55d7b0, 0x55d8c0, 0x55d960, 0x55d970, 0x55d990, 0x55da30, 0x55da40)
HEAD, CURRENT, WINDING, D3D_ON, POINTS = 0x68a8b4, 0x8d4784, 0xa4e228, 0x8c4b70, 0x8dabac
SPAN_FN, SPAN = 0xb04688, 0x585710  # the soft target's span filler, set at start (0x579b7f)
TOP_P, FLAG_P, SLOTS, FLAGS = 0x611220, 0x61121c, 0x66f4d0, 0x66f558
# PM's polygon filler (MW3's 0x55d010), cdecl (points, count, winding), and its two calls in the model caster.
FILL, FILL_CALLS = 0x57eff0, (0x55e20d, 0x55e2c6)
# Where a level edge just above a row centre leaves its entry as stack garbage (EDX the entry, ESI the point):
# set as the port's FIX (bug) sets it (test_shadow_image.py's LEVEL_EDGES, here in PM's layout).
LEVEL_EDGES = (0x57f13d, 0x57f240, 0x57f347, 0x57f44a)
MAGIC = Fraction(6755399441055744)
CW = {'game': 0x007f, 'x53': 0x027f}
PORT_PRECISION = {'game': 0, 'x53': 1}


def round_bits(r, bits):
    """r (a positive Fraction) rounded to `bits` significant bits, ties to even (test_shadow_image.py's)."""
    e = r.numerator.bit_length() - r.denominator.bit_length()
    if Fraction(2) ** e > r:
        e -= 1
    ulp = Fraction(2) ** (e - bits + 1)
    return round(r / ulp) * ulp


def magic_x(x, precision):
    """The filler's 16.16 value of x (a float) at the x87 precision `precision`."""
    return int(round_bits(MAGIC + Fraction(x) * 65536, 24 if precision == 24 else 53)) & 0xffffffff


def fb(*xs):
    return struct.pack(f'<{len(xs)}f', *xs)


class Emulated(ml.Emulated):
    def __init__(self):
        super().__init__()
        cpu = self.cpu
        # The filler through a trampoline that clears the stack below it (its edge lists start as garbage there).
        clear = (b'\x57\x51\x50'                      # push edi, ecx, eax
                 b'\x8d\xbc\x24\x00\xf0\xff\xff'      # lea edi, [esp - 0x1000]
                 b'\xb9\x00\x04\x00\x00'              # mov ecx, 0x400
                 b'\x31\xc0\xfc\xf3\xab'              # xor eax, eax; cld; rep stosd
                 b'\x58\x59\x5f')                     # pop eax, ecx, edi
        head = clear + b'\xff\x74\x24\x0c' * 3 + b'\xe8'  # push the three arguments again; call FILL
        addr = cpu.code_next
        rel = FILL - (addr + len(head) + 4)
        entry = cpu.code(head + struct.pack('<i', rel) + b'\x83\xc4\x0c\xc3')  # add esp, 12; ret
        for site in FILL_CALLS:
            cpu._uc.mem_write(site + 1, struct.pack('<i', entry - (site + 5)))
        self.level_hits, self.precision = 0, 24
        for a in LEVEL_EDGES:
            cpu._uc.hook_add(UC_HOOK_CODE, lambda uc, *_: self.level_edge(uc), begin=a, end=a)

    def level_edge(self, uc):
        entry, x = uc.reg_read(UC_X86_REG_EDX), uc.reg_read(UC_X86_REG_ESI)
        x, = struct.unpack('<f', bytes(uc.mem_read(x, 4)))
        uc.mem_write(entry, bytes(4))
        uc.mem_write(entry + 8, struct.pack('<I', magic_x(x, self.precision)))
        self.level_hits += 1

    def shadows_init(self):
        # After run()'s reset, which rewinds the allocator: the stack's base and the turned points' scratch.
        self.base = self.alloc(48)
        self.points = self.alloc(12 * 0x10000)
        self.cpu._uc.mem_write(self.base, fb(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0))
        # As the port's reset_stack, push(base), load_identity: slot 1.
        self.w32(SLOTS + 4, self.base), self.w32(FLAGS + 4, 1)
        self.w32(TOP_P, SLOTS + 4), self.w32(FLAG_P, FLAGS + 4)
        self.w32(D3D_ON, 0), self.w32(POINTS, self.points), self.w32(SPAN_FN, SPAN)
        for a in (HEAD, CURRENT, WINDING):
            self.w32(a, 0)
        self.casters = []
        self.call(INIT)

    def op(self, op, cid=0, a=0, b=0, f=()):
        rec = self.casters[cid - 1] if 0 < cid <= len(self.casters) else 0
        if op == 0:
            self.shadows_init()
        elif op == 1:
            self.casters.append(self.call(NEW) & 0xffffffff)
            return len(self.casters)
        elif op == 2:
            self.call(SIZE, args=[rec, a, b])
        elif op == 3:
            self.call(NODE, args=[rec, self.loaded[a] if 0 <= a < len(self.loaded) else 0])
        elif op == 4:
            self.call(ENABLE, args=[rec, a])
        elif op == 5:
            self.call(COLOR, args=[rec, a])
        elif op == 6:
            p = self.alloc(12)
            self.cpu._uc.mem_write(p, fb(*f[:3]))
            self.call(POSITION, args=[rec, p])
        elif op == 7:
            p = self.alloc(12)
            self.cpu._uc.mem_write(p, fb(*f[:3]))
            r = self.call(DRAW, args=[rec, p, struct.unpack('<I', fb(f[3]))[0]])
            return struct.unpack('<i', struct.pack('<I', r & 0xffffffff))[0]
        return 0

    def dump(self):
        out, c, guard = [], self.i32(HEAD) & 0xffffffff, 0
        while c and guard < 64:
            r = self.read(c, 0x90)
            node, = struct.unpack_from('<I', r, 0)
            cid = self.casters.index(c) + 1 if c in self.casters else -1
            ni = self.loaded.index(node) if node in self.loaded else (-2 if node else -1)
            out.append(struct.pack('<ii', cid, ni) + r[4:0x4c] + r[0x58:0x5c])
            img, = struct.unpack_from('<I', r, 0x5c)
            out.append(struct.pack('<i', int(img != 0)))
            if img:
                w, h = struct.unpack('<hh', self.read(img + 4, 4))
                pix = self.i32(img + 0x10) & 0xffffffff
                out.append(struct.pack('<3i', w, h, int(pix != 0)))
                if pix:
                    out.append(self.read(pix, 2 * w * h))
            c = struct.unpack_from('<I', r, 0x8c)[0]  # PM's +0x88 / +0x8c: MW3's +0x84 / +0x88 (the port's prev, next)
            guard += 1
        out.append(struct.pack('<ii', ((self.i32(TOP_P) & 0xffffffff) - SLOTS) // 4, self.i32(WINDING)))
        return b''.join(out)


def parse(b):
    """[(id, node, floats 4..0x4c, enabled, image (w, h, pixels) or None)], tail."""
    out, o = [], 0
    while len(b) - o > 8:
        cid, node = struct.unpack_from('<ii', b, o)
        fields = b[o + 8:o + 8 + 0x48]
        enabled, has = struct.unpack_from('<ii', b, o + 0x50)
        o += 0x58
        img = None
        if has:
            w, h, pix = struct.unpack_from('<3i', b, o)
            o += 12
            px = b[o:o + 2 * w * h] if pix else b''
            o += len(px)
            img = (w, h, px)
        out.append((cid, node, fields, enabled, img))
    return out, b[o:]


def light(rng):
    if rng.random() < 0.08:
        return (rng.uniform(-1, 1), rng.uniform(0, 1), rng.uniform(-1, 1))  # from below: nothing drawn
    y = -rng.uniform(0.2, 1.0)
    a = rng.uniform(0, 2 * math.pi)
    s = math.sqrt(max(0.0, 1 - y * y))
    return (s * math.cos(a), y, s * math.sin(a))


def compare(want, got, mode, where, drawn):
    if want == got:
        return
    a, ta = parse(want)
    b, tb = parse(got)
    if len(a) != len(b) or ta != tb:
        raise SystemExit(f'{where}: casters {len(a)}/{len(b)}, tail {ta.hex()}/{tb.hex()}')
    for x, y in zip(a, b):
        if x[:2] != y[:2] or x[3] != y[3]:
            raise SystemExit(f'{where}: caster {x[:2]} / {y[:2]} enabled {x[3]}/{y[3]}')
        if x[2] != y[2]:
            fa, fg = struct.unpack('<18f', x[2]), struct.unpack('<18f', y[2])
            for k, (p, q) in enumerate(zip(fa, fg)):
                if k == 5:  # the colour, a packed int
                    ok = x[2][20:24] == y[2][20:24]
                else:
                    ok = p == q or (mode != 'game' and abs(p - q) <= 1e-5 * max(1.0, abs(p)))
                if not ok:
                    raise SystemExit(f'{where}: caster {x[0]} field +{4 + 4 * k:#x}: exe {p!r} port {q!r}')
        if (x[4] is None) != (y[4] is None) or (x[4] and x[4][:2] != y[4][:2]):
            raise SystemExit(f'{where}: caster {x[0]} image {x[4] and x[4][:2]} / {y[4] and y[4][:2]}')
        if x[4] and x[0] not in drawn:  # malloc's, untouched until a draw clears it
            continue
        if x[4] and x[4][2] != y[4][2]:  # identical at both precisions
            pa, pb = x[4][2], y[4][2]
            n = sum(1 for k in range(0, len(pa), 2) if pa[k:k + 2] != pb[k:k + 2])
            k = next(k for k in range(0, len(pa), 2) if pa[k:k + 2] != pb[k:k + 2]) // 2
            raise SystemExit(f'{where}: caster {x[0]} image ({x[4][0]}x{x[4][1]}): {n} pixels differ, first at '
                             f'({k % x[4][0]}, {k // x[4][0]}): exe {pa[2*k:2*k+2].hex()} port {pb[2*k:2*k+2].hex()}')


def main():
    draws = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_mechlib.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int]
    p.probe_pm_mechlib_copies.argtypes = [ctypes.c_void_p, ctypes.c_int]
    p.probe_pm_shadow_op.argtypes = [ctypes.c_int] * 4 + [ctypes.c_void_p]
    p.probe_pm_shadow_dump.argtypes = [ctypes.c_void_p, ctypes.c_int]
    buf = ctypes.create_string_buffer(1 << 27)
    trees = [n for n, _, _ in ml.entries(ml.MECHLIB.read_bytes()) if n not in ('version', 'format', 'materials')]
    c1 = ml.ZBD / 'c1' / 'gamez.zbd'
    emu = Emulated()
    rng = random.Random(1)
    totals = {'draws': 0, 'drawn': 0, 'lit': 0, 'pixels': 0}

    def port_op(op, cid=0, a=0, b=0, f=()):
        arr = (ctypes.c_float * 4)(*(list(f) + [0.0] * 4)[:4])
        return p.probe_pm_shadow_op(op, cid, a, b, arr)

    def both(op, cid=0, a=0, b=0, f=()):
        want, got = emu.op(op, cid, a, b, f), port_op(op, cid, a, b, f)
        if want != got:
            raise SystemExit(f'op {op} caster {cid}: exe returns {want}, port {got}')
        return want

    runs = [('game', trees[k::8][:10]) for k in range(4)] + [('x53', trees[k::8][:10]) for k in range(4, 8)]
    for r, (mode, names) in enumerate(runs):
        emu.run(c1, ml.MECHLIB, names)
        p.probe_pm_mechlib_copies((ctypes.c_int32 * 1)(0), 0)
        if p.probe_pm_mechlib(str(c1).encode(), str(ml.MECHLIB).encode(), '|'.join(names).encode(), buf, len(buf)) < 0:
            raise SystemExit('probe_pm_mechlib failed')
        emu.cpu.fpu_control(CW[mode])
        emu.precision = 24 if mode == 'game' else 53
        both(0)
        port_op(8, a=PORT_PRECISION[mode])
        ids, drawn = [], set()
        for i in range(len(names)):
            cid = both(1)
            ids.append(cid)
            both(2, cid, rng.choice((32, 64, 64, 128)), rng.choice((32, 64, 64, 128)))
            both(3, cid, i if rng.random() > 0.05 else -1)
            both(4, cid, 0 if rng.random() < 0.05 else 1)
            both(5, cid, rng.randrange(0x10000))
            both(6, cid, f=(rng.uniform(-3, 3), rng.uniform(0, 8), rng.uniform(-3, 3)))
        for d in range(draws):
            cid = rng.choice(ids)
            if rng.random() < 0.15:
                both(6, cid, f=(rng.uniform(-3, 3), rng.uniform(0, 8), rng.uniform(-3, 3)))
            lt = light(rng)
            ret = both(7, cid, f=lt + (rng.uniform(-3, 1),))
            n = emu.dump()
            m = p.probe_pm_shadow_dump(buf, len(buf))
            if m < 0:
                raise SystemExit('dump too big')
            if ret == 0:
                drawn.add(cid)
            compare(n, buf.raw[:m], mode, f'run {r} ({mode}) draw {d}', drawn)
            totals['draws'] += 1
            totals['drawn'] += ret == 0
            for c in parse(n)[0]:
                if c[0] == cid and c[4] and ret == 0:
                    px = c[4][2]
                    lit = sum(1 for k in range(0, len(px), 2) if px[k:k + 2] != b'\xff\xff')
                    totals['lit'] += lit > 0
                    totals['pixels'] += len(px) // 2
        print(f'run {r} ({mode}): {len(names)} trees, {draws} draws; so far {totals}, {emu.level_hits} level edges')
    print(f"ok: {totals['draws']} draws ({totals['drawn']} drawn, {totals['lit']} with shadow pixels, "
          f"{totals['pixels']} pixels, {emu.level_hits} level edges) identical")


if __name__ == '__main__':
    main()
