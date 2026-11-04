"""PM's device functions (pm/gamez/zvideo/zvid_tl and the shared flushes) against PM's exe.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_device.py [steps]

The exe's 0x59d450, 0x59de30, 0x59e500, 0x59e830, 0x59eb60, 0x5a0060, 0x5a0390, 0x5a0f30,
0x5a0ea0 and its flushes 0x59f550, 0x59f9e0 run in Unicorn against a Direct3D stub that keeps
the state its calls leave. Every draw is compared in order: primitive, that state, the vertex
bytes; every fog colour sent. A strip the exe draws with CULLMODE CW is compared as the
triangles the cull keeps, as the port hands them over. Light map sets come with their shader.
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from unicorn.x86_const import UC_X86_REG_ESP  # noqa: E402

import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402

THIS, VTABLE, TL_P, PRIMITIVE, FLAGS, BASE, INDICES, SETS_P = 0xb09f08, 0xb09f0c, 0x8c4c54, 0x8c4c40, 0x8c4c50, 0x8c4c5c, 0x8c4c60, 0x8ccc60
QUEUE, POOL, OVER, Q_N, Q_V, O_N = 0xb0da78, 0xb0da7c, 0xb0da80, 0xb06fc8, 0xb06fcc, 0xb09eb0
FOG, MASKS, SHIFTS, CULL, FOG_SEND = 0x8c4c28, 0xb06190, 0xb0619c, 0x59b870, 0x59d0f0
FLAT, LIT, TEXTURED, TEX_LIT, TEX_VERTEX, PROJECTED, SETS, INDEXED_T, INDEXED_F = 0x59d450, 0x59de30, 0x59e500, 0x59e830, 0x59eb60, 0x5a0060, 0x5a0390, 0x5a0f30, 0x5a0ea0
FLUSH_T, FLUSH_O = 0x59f550, 0x59f9e0
# The port's DeviceCall kinds, in order.
K_FLAT, K_LIT, K_TEX, K_TEX_LIT, K_TEX_V, K_SETS, K_IDX_T, K_IDX_F, K_PROJ = range(9)
FIELDS = ('texture', 'u', 'v', 'blend', 'multiply', 'additive', 'depth_write', 'depth_always', 'alpha_modulate', 'fog', 'flat', 'select')
FORMAT = (8, 3, 3, 0xf800, 0x7e0, 0x1f)  # 565: shifts, masks


def fbits(*x):
    return struct.pack(f'<{len(x)}f', *x)


class Cpu:
    """The bench's raw addresses (Original's are MW3's, mapped), as `c` too."""
    def __init__(self):
        self.e = g.Emulated()
        self.e.cpu.fpu_control(0x7f)

    def u32(self, a, v):
        self.e.w32(a, v)

    def write(self, a, data):
        self.e.cpu._uc.mem_write(a, bytes(data))

    def read(self, a, n):
        return bytes(self.e.cpu._uc.mem_read(a, n))

    def alloc(self, n):
        return self.e.alloc(n)

    def call(self, a, stack_args=()):
        return self.e.call(a, args=list(stack_args))

    def arg(self, n):
        esp = self.e.cpu._uc.reg_read(UC_X86_REG_ESP)
        return struct.unpack('<I', self.read(esp + 4 + n * 4, 4))[0]


class Emu:
    def __init__(self):
        cpu = self.cpu = Cpu()
        self.log = []
        self.state = dict(texture=-1, u=1, v=1, blend=0, multiply=0, additive=0, depth_write=1, depth_always=0,
                          alpha_modulate=0, fog=0, flat=0, select=0)
        self.src, self.dest, self.cull = 5, 6, 1
        vt = cpu.alloc(0x100)
        cpu.u32(THIS, 0xdeadbeef), cpu.u32(VTABLE, vt)

        def method(off, fn, pop):  # COM pops its own arguments
            a = cpu.e.cpu.code(b'\xc3')
            cpu.u32(vt + off, a)
            cpu.e.native_stub(a, lambda c: fn(cpu), pop)
        method(0x58, lambda c: self.render_state(c.arg(1), c.arg(2)) or 0, 12)
        method(0xa0, lambda c: self.stage(c.arg(1), c.arg(2), c.arg(3)) or 0, 16)
        method(0x98, lambda c: self.texture(c.arg(1), c.arg(2)) or 0, 12)
        method(0x70, self.draw, 24)
        method(0x74, self.draw_indexed, 32)
        cpu.u32(QUEUE, cpu.alloc(0xbb8 * 0x24)), cpu.u32(POOL, cpu.alloc(0x2ee0 * 0x20)), cpu.u32(OVER, cpu.alloc(0xbb8 * 0x80c))
        cpu.u32(FLAGS, 0x18), cpu.u32(PRIMITIVE, 6)
        for a, v in ((0xb0a058, 1), (0xb09fac, 5), (0xb09fb0, 6), (0xb09fb4, 1), (0xb09f20, 0)):
            cpu.u32(a, v)
        cpu.write(MASKS, struct.pack('<3I', *FORMAT[3:])), cpu.write(SHIFTS, struct.pack('<3I', *FORMAT[:3]))
        self.tl = cpu.alloc(0x20 * 80)
        self.sets = cpu.alloc(0x2358)
        self.surfaces, self.entries = [], []

    def add_texture(self, handle, kind, u, v, stretch):
        cpu = self.cpu
        surface, entry, image = cpu.alloc(0x20), cpu.alloc(0x28), cpu.alloc(0x38)
        cpu.write(surface + 8, struct.pack('<I', handle))
        cpu.write(surface + 0x10, struct.pack('<3I', kind, u, v))
        cpu.write(image + 0xc, struct.pack('<h', stretch))
        cpu.u32(entry, image), cpu.u32(entry + 8, surface)
        self.surfaces.append(surface), self.entries.append(entry)

    def render_state(self, state, value):
        st = self.state
        if state == 9:
            st['flat'] = int(value == 1)
        elif state == 14:
            st['depth_write'] = int(value != 0)
        elif state in (19, 20):
            if state == 19:
                self.src = value
            else:
                self.dest = value
            st['multiply'] = int(self.src == 1 and self.dest == 3)
            st['additive'] = int(self.src == 5 and self.dest == 2)
        elif state == 22:
            self.cull = value
        elif state == 23:
            st['depth_always'] = int(value == 8)
        elif state == 27:
            st['blend'] = int(value != 0)
        elif state == 28:
            st['fog'] = int(value != 0)
        elif state == 34:
            self.log.append(('fog', value >> 16 & 255, value >> 8 & 255, value & 255))
        else:
            raise SystemExit(f'unexpected SetRenderState({state}, {value})')

    def stage(self, stage, kind, value):
        if stage != 0:
            raise SystemExit(f'SetTextureStageState stage {stage}')
        if kind == 1:
            self.state['select'] = int(value == 2)
        elif kind == 4:
            self.state['alpha_modulate'] = int(value == 4)
        elif kind in (13, 14):
            self.state['u' if kind == 13 else 'v'] = value
        elif kind != 11 or value != 0:
            raise SystemExit(f'unexpected SetTextureStageState({kind}, {value})')

    def texture(self, stage, t):
        if stage != 0:
            raise SystemExit(f'SetTexture stage {stage}')
        self.state['texture'] = t if t else -1

    def record(self, prim, verts):
        if prim == 5 and self.cull == 2:
            kept = b''
            n = len(verts) // 32
            for i in range(n - 2):
                a, b, c = i, i + 1 + (i & 1), i + 2 - (i & 1)
                p = [struct.unpack_from('<2f', verts, k * 32) for k in (a, b, c)]
                if (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]) <= 0:
                    kept += verts[a * 32:a * 32 + 32] + verts[b * 32:b * 32 + 32] + verts[c * 32:c * 32 + 32]
            if kept:
                self.log.append(('draw', 4, len(kept) // 32, tuple(self.state[f] for f in FIELDS), kept))
            return
        self.log.append(('draw', prim, len(verts) // 32, tuple(self.state[f] for f in FIELDS), verts))

    def draw(self, c):
        prim, fvf, verts, count = c.arg(1), c.arg(2), c.arg(3), c.arg(4)
        if fvf != 0x1c4:
            raise SystemExit(f'DrawPrimitive fvf {fvf:#x}')
        self.record(prim, bytes(c.read(verts, 32 * count)))
        return 0

    def draw_indexed(self, c):
        prim, verts, indices, n = c.arg(1), c.arg(3), c.arg(5), c.arg(6)
        idx = struct.unpack(f'<{n}H', bytes(c.read(indices, n * 2)))
        self.record(prim, b''.join(bytes(c.read(verts + i * 32, 32)) for i in idx))
        return 0

    def take(self):
        out, self.log = self.log, []
        return out


def parse(words):
    out, at = [], 0
    while at < len(words):
        if words[at] == 6:
            out.append(('fog', *words[at + 1:at + 4]))
            at += 4
            continue
        prim, count = words[at + 1], words[at + 2]
        state = tuple(words[at + 3:at + 15])
        at += 15
        out.append(('draw', prim, count, state, struct.pack(f'<{count * 8}i', *words[at:at + count * 8])))
        at += count * 8
    return out


def compare(want, got, what):
    if [x[:3] if x[0] == 'draw' else x for x in want] != [x[:3] if x[0] == 'draw' else x for x in got]:
        raise SystemExit(f'{what}: {[x[:3] for x in got][:6]}, the exe {[x[:3] for x in want][:6]}')
    for k, (a, b) in enumerate(zip(want, got)):
        if a[0] == 'draw':
            if a[3] != b[3]:
                raise SystemExit(f'{what} draw {k}: {dict(zip(FIELDS, b[3]))}, the exe {dict(zip(FIELDS, a[3]))}')
            for v in range(a[2]):
                x, y = a[4][v * 32:v * 32 + 32], b[4][v * 32:v * 32 + 32]
                if a[3][0] == -1:  # untextured: u, v are what the buffer held
                    x, y = x[:24], y[:24]
                if x != y:
                    raise SystemExit(f'{what} draw {k} vertex {v}: {y.hex()}, the exe {x.hex()}')


def main():
    steps = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
    p = test_pm_zar.load_pm_probe()
    F, V, I = ctypes.c_float, ctypes.c_void_p, ctypes.c_int
    p.probe_pm_device_call.argtypes = [I, I, V, I, I, I, I, I, F, I, I, I, V, V, V, V, I]
    p.probe_pm_device_flush.argtypes = [I, V, I]
    rng = random.Random(0x59e500)
    emu = Emu()
    surfaces = []
    for i in range(8):
        # Every type with the additive bit on and off, and bit 0 (clamp) beside it.
        s = (1000 + i, (1, 1, 1, 1, 4, 4, 5, 5)[i], rng.choice((1, 3)), rng.choice((1, 3)), (0, 4, 1, 5, 0, 4, 0, 4)[i])
        surfaces += s
        emu.add_texture(*s)
    p.probe_pm_device_start((ctypes.c_int32 * 40)(*surfaces), (ctypes.c_int32 * 6)(5, 6, 5, *FORMAT[3:]))
    cap = 1 << 22
    out = (ctypes.c_int32 * cap)()
    seen, compared = {}, 0

    def words(n):
        if n < 0:
            raise SystemExit('log too long')
        return list(out[:n])

    fog = (0.0, 0.0, 0.0)
    for step in range(steps):
        if rng.random() < 0.1:
            fog = tuple(float(rng.randrange(256)) for _ in range(3))
            emu.cpu.write(FOG, fbits(*fog))
            emu.cpu.call(FOG_SEND)
            p.probe_pm_device_fog((F * 3)(*fog))
            n = p.probe_pm_device_flush(2, out, cap)
            compare(emu.take(), parse(words(n)), f'step {step} fog')
        kind = rng.choice((K_FLAT, K_LIT, K_TEX, K_TEX_LIT, K_TEX_V, K_SETS, K_IDX_T, K_IDX_F, K_PROJ))
        fade = rng.choice((1.0, 1.0, 1.0, 0.5, 0.25))
        overlay = int(rng.random() < 0.15)
        strip = kind not in (K_IDX_T, K_IDX_F, K_PROJ) and rng.random() < 0.2
        count = 3 if kind in (K_PROJ,) else rng.randint(3, 9)
        if kind in (K_IDX_T, K_IDX_F):
            count = 3 * rng.randint(1, 6)
        elif rng.random() < 0.05 and not overlay and kind in (K_TEX, K_TEX_LIT, K_TEX_V, K_LIT):
            count = rng.randint(65, 80)
        if kind == K_SETS:
            fade, overlay = 1.0, 0
        tl = []
        for _ in range(count):
            tl += [rng.uniform(0, 640), rng.uniform(0, 480), rng.uniform(0.001, 1)]
            tl += [tl[-1]]
            tl += [struct.unpack('<f', struct.pack('<I', rng.getrandbits(32)))[0] for _ in range(2)]
            tl += [rng.uniform(-1, 2), rng.uniform(-1, 2)]
        raw = struct.pack(f'<{count * 8}I', *[struct.unpack('<I', struct.pack('<f', x))[0] for x in tl])
        ftl = ctypes.create_string_buffer(raw)
        tex = rng.randrange(8)
        if kind == K_SETS:  # the base is opaque; 0x5a0390 has no other path
            pass
        rgb, alpha = rng.getrandbits(16), rng.choice((255, 255, 255, 128))
        shade = rng.choice((1, 2))
        cull, prim = (2, 5) if strip else (1, 6)
        cpu = emu.cpu
        cpu.write(emu.tl, raw)
        cpu.u32(TL_P, emu.tl)
        cpu.u32(PRIMITIVE, prim)
        cpu.call(CULL, stack_args=(cull,))
        whole = struct.unpack('<f', struct.pack('<f', alpha * fade))[0]
        nsets = -1
        kinds, stex, uvs = [], [], []
        if kind == K_FLAT:
            cpu.call(FLAT, stack_args=(rgb, int(whole), count, overlay))
        elif kind == K_LIT:
            cpu.call(LIT, stack_args=(int(whole), count, overlay))
        elif kind in (K_TEX, K_TEX_LIT, K_TEX_V):
            cpu.call({K_TEX: TEXTURED, K_TEX_LIT: TEX_LIT, K_TEX_V: TEX_VERTEX}[kind], stack_args=(count, emu.entries[tex], struct.unpack('<I', fbits(fade))[0], overlay))
        elif kind == K_PROJ:
            cpu.call(PROJECTED, stack_args=(0, count, emu.entries[tex], struct.unpack('<I', fbits(fade))[0], overlay))
        elif kind == K_SETS:
            nsets = rng.randint(0, 4)
            cpu.u32(emu.sets, nsets)
            for k in range(nsets):
                kinds.append(rng.choice((0, 1, 1)))
                stex.append(rng.randrange(8))
                uvs += [rng.uniform(-1, 2) for _ in range(count * 2)]
                cpu.u32(emu.sets + 0x20 + k * 4, kinds[k])
                cpu.u32(emu.sets + 4 + k * 4, emu.surfaces[stex[k]])
                cpu.write(emu.sets + 0x1558 + k * 0x200, fbits(*uvs[k * count * 2:(k + 1) * count * 2]))
            cpu.u32(SETS_P, emu.sets)
            cpu.call(SETS, stack_args=(count, emu.entries[tex]))
        else:
            cpu.write(INDICES, struct.pack(f'<{count}H', *range(count)))
            cpu.u32(BASE, emu.tl)
            if kind == K_IDX_T:
                cpu.call(INDEXED_T, stack_args=(count, count, emu.entries[tex], shade))
            else:
                cpu.call(INDEXED_F, stack_args=(count, count, shade))
        cpu.u32(PRIMITIVE, 6)
        cpu.call(CULL, stack_args=(1,))
        want = emu.take()
        n = p.probe_pm_device_call(kind, count, ftl, cull, prim, rgb, alpha, tex if kind not in (K_FLAT, K_LIT, K_IDX_F) else -1, fade, overlay, shade, nsets,
                                   (ctypes.c_int32 * 8)(*kinds), (ctypes.c_int32 * 8)(*stex), (F * max(len(uvs), 1))(*uvs) if uvs else None, out, cap)
        got = parse(words(n))
        what = f'step {step} kind {kind} count {count} fade {fade} overlay {overlay} cull {cull} tex {tex} {surfaces[tex * 5:tex * 5 + 5]}'
        compare([x for x in want if x[0] != 'fog'], [x for x in got if x[0] != 'fog'], what)
        seen[kind] = seen.get(kind, 0) + 1
        compared += len(want)
        if rng.random() < 0.1:
            for which, fn in ((0, FLUSH_T), (1, FLUSH_O)):
                emu.cpu.call(fn)
                want = emu.take()
                got = parse(words(p.probe_pm_device_flush(which, out, cap)))
                compare(want, got, f'step {step} flush {which}')
                compared += len(want)
                seen[f'flush {which}'] = seen.get(f'flush {which}', 0) + sum(x[0] == 'draw' for x in want)
                seen['fog in flush'] = seen.get('fog in flush', 0) + sum(x[0] == 'fog' for x in want)
                seen['additive'] = seen.get('additive', 0) + sum(x[0] == 'draw' and x[3][5] for x in want)
    print(f'ok: {compared} device calls compared; {dict(sorted(seen.items(), key=str))}')


if __name__ == '__main__':
    main()
