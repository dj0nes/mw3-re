"""PM's model lights against its exe: the blink and step animation at 0x543b10's
start, and 0x547dd0's points and lens flares. No shipped world model has any, so they
are made here. The exe batches its opaque points and hands them over at the end;
each side's points are compared in the order they were made, within the points only:
how they fall among blended polygons is for the eye check (docs/pm-map.md).

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_model_lights.py [draws]
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modeldraw_common import Stats, fbits, same_float, scenario  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_modeldraw as md  # noqa: E402
import test_pm_records as rec  # noqa: E402
import test_pm_zar  # noqa: E402

DT, PACK, FLARE, PRIORITY, D3D_POINT, TL = 0x8c1b4c, 0x598420, 0x588910, 0xa4e090, 0x8c4c08, 0x8c4c54
TOL, ROUNDING = 0.0, 0.0  # bit for bit at 24 bits (the 24-bit audit, pm-map.md)


class Emulated(rec.Emulated):
    def __init__(self, world):
        super().__init__(world)
        self.events = []
        self.colours = []
        self.batch_at = 0
        self.native_stub(PACK, self.pack)
        self.native_stub(FLARE, self.flare)
        s = self.cpu.code(b'\xc3')
        self.native_stub(s, self.point)
        self.w32(D3D_POINT, s)

    def at_end(self, uc, address, size, user):
        pass

    def pack(self, c):
        self.colours.append(c.arg(0) & 0xffff)
        return 0

    def flare(self, c):
        self.events.append((2, struct.unpack('<3f', self.read(c.arg(0), 12)), struct.unpack('<3f', self.read(c.arg(1), 12)), c.arg(2) & 0xffff))
        return 0

    def point(self, c):
        if c.arg(0) == 0:  # the batch: the opaque ones, in the order made
            return 0
        self.events.append((1, struct.unpack('<3f', self.read(c.arg(0), 12)), c.arg(1) & 0xffff, c.arg(2)))
        return 0

    def run(self, i, world, dt):
        self.cpu._uc.mem_write(self.world, fbits(*world))
        self.w32(md.SLOTS, self.world)
        self.w32(md.FLAGS, 0)
        self.w32(md.TOP_P, md.SLOTS)
        self.w32(md.FLAG_P, md.FLAGS)
        self.cpu._uc.mem_write(DT, fbits(dt))
        self.events, self.colours = [], []
        start = self.i32(0x8c4c5c) & 0xffffffff
        node = self.node(i)
        self.model = self.i32(node + 0x3c) & 0xffffffff
        self.call(rec.DRAW, args=[node, 0])
        end = self.i32(TL) & 0xffffffff
        opaque = []
        for k in range((end - start) // 0x20):
            opaque.append((1, struct.unpack('<3f', self.read(start + k * 0x20, 12)), self.colours[k], 0xff))
        return self.events, opaque


def main():
    draws = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    p = test_pm_zar.load_pm_probe()
    F, V, I = ctypes.c_float, ctypes.c_void_p, ctypes.c_int
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_draw_view.argtypes = [V, V, F, V, F, V]
    p.probe_pm_draw_view.restype = None
    p.probe_pm_model_view.argtypes = [V, V]
    p.probe_pm_model_view.restype = None
    p.probe_pm_set_model.argtypes = [I, I, ctypes.c_uint32]
    p.probe_pm_set_model.restype = None
    p.probe_pm_set_model_lights.argtypes = [I, I, V, V, I]
    p.probe_pm_set_model_lights.restype = None
    p.probe_pm_model_lights.argtypes = [I, V, F, V, I]
    buf = ctypes.create_string_buffer(1 << 20)
    md.Emulated.probe = p
    rng = random.Random(0x547dd0)
    stats = Stats()
    seen = {}
    path = g.ZBD / 'c1' / 'gamez.zbd'
    emu = Emulated(path)
    assert p.probe_pm_draw_load(str(path).encode()) == 0
    nodes = []
    for i in range(emu.i32(g.N_CAPACITY)):
        r = emu.read(emu.node(i), 0xd4)
        model = struct.unpack_from('<I', r, 0x3c)[0]
        if struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 and model and emu.i32(model) == 0:
            nodes.append(i)
    for d in range(draws):
        i = rng.choice(nodes)
        model = emu.i32(emu.node(i) + 0x3c) & 0xffffffff
        world, cam, proj, *_ = scenario(rng, (0, 0, 0), 50.0)
        near = proj[6]
        rect = (proj[0], proj[1], proj[0] + proj[2] * 2 - 0.001, proj[1] + proj[3] * 2 - 0.001)
        emu.view(cam, proj, near, rect, 0.0)
        views = emu.read(md.VIEW, 48) + emu.read(md.CAMERA_M, 48)
        p.probe_pm_draw_view((F * 12)(*cam), (F * 8)(*proj), near, (F * 4)(*rect), 0.0, views)
        count, per = rng.randint(1, 4), 4
        records, verts = b'', []
        recs = emu.alloc(0x4c * count)
        for k in range(count):
            b = bytearray(0x4c)
            kind = rng.choice((0, 1, 1, 2))
            far = rng.choice((0.0, rng.uniform(5, 200)))
            struct.pack_into('<iifi', b, 0, kind, rng.choice((0, 1)), rng.uniform(0, 0.1), rng.randint(1, per))
            struct.pack_into('<fI', b, 0x10, rng.uniform(0, 0.2), rng.randint(0, 3) | rng.randint(0, 0xffff) << 16)
            if kind == 1 and rng.random() < 0.2:  # the step past 0xffff carries into the colour: on the first frame
                struct.pack_into('<f', b, 8, 0.0)
                struct.pack_into('<fI', b, 0x10, 1.0, 0xffff | rng.randint(0, 0xfffe) << 16)
                struct.pack_into('<i', b, 0xc, 2)  # vertices 0, 1, 0..: the carried colour shows at 1
            struct.pack_into('<h', b, 0x18, rng.choice((0, 0, 2, -3)))
            struct.pack_into('<H', b, 0x28, rng.randint(0, 0xffff))
            struct.pack_into('<3fi', b, 0x30, far * rng.uniform(0, 0.9), far, 255.0 / max(far, 1.0) * rng.uniform(0.5, 2), rng.choice((0, 0, 1)))
            v = [rng.uniform(-40, 40) for _ in range(per * 3)]
            a = emu.alloc(per * 12)
            emu.cpu._uc.mem_write(a, fbits(*v))
            struct.pack_into('<I', b, 0x2c, a)
            emu.cpu._uc.mem_write(recs + k * 0x4c, bytes(b))
            records += bytes(b)
            verts += v
        undo = []
        for off, v in ((0x20, count), (0x10, 0)):  # no polygons: the draw ends after its lights
            undo.append((off, emu.i32(model + off) & 0xffffffff))
            emu.w32(model + off, v)
            p.probe_pm_set_model(i, off, v)
        emu.w32(model + 0x40, recs)
        p.probe_pm_set_model_lights(i, count, records, (F * len(verts))(*verts), per)
        # Several frames on the same lights, the steps spanning their periods: the
        # stepping, its carry into the colour and the blink's swap are state.
        for frame in range(rng.randint(1, 8)):
            dt = rng.choice((0.0, rng.uniform(0, 0.05), rng.uniform(0, 0.2)))
            events, opaque = emu.run(i, world, dt)
            n = p.probe_pm_model_lights(i, (F * 12)(*world), dt, buf, len(buf))
            raw = buf.raw[:n]
            ours, at = [], 0
            while at < n:
                kind = struct.unpack_from('<i', raw, at)[0]
                if kind == 1:
                    ours.append((1, struct.unpack_from('<3f', raw, at + 4), struct.unpack_from('<I', raw, at + 16)[0], struct.unpack_from('<i', raw, at + 20)[0]))
                    at += 24
                else:
                    ours.append((2, struct.unpack_from('<3f', raw, at + 4), struct.unpack_from('<3f', raw, at + 16), struct.unpack_from('<I', raw, at + 28)[0]))
                    at += 32
            ours_opaque = [e for e in ours if e[0] == 1 and e[3] == 0xff]
            ours_rest = [e for e in ours if not (e[0] == 1 and e[3] == 0xff)]
            for label, a, b in (('opaque', opaque, ours_opaque), ('others', events, ours_rest)):
                if len(a) != len(b):
                    raise SystemExit(f'draw {d} node {i}: {len(b)} {label}, the exe {len(a)}: {b[:3]} {a[:3]}')
                for x, y in zip(a, b):
                    seen[f'{label} {x[0]}'] = seen.get(f'{label} {x[0]}', 0) + 1
                    if x[0] != y[0] or (x[0] == 1 and (x[2:] != y[2:])) or (x[0] == 2 and x[3] != y[3]):
                        raise SystemExit(f'draw {d} node {i} {label}: {y}, the exe {x}')
                    for ax, ay in zip(x[1], y[1]):
                        if not same_float(ax, ay, stats, TOL, label, ROUNDING):
                            raise SystemExit(f'draw {d} node {i} {label}: {y}, the exe {x}')
                    if x[0] == 2:
                        for ax, ay in zip(x[2], y[2]):
                            if not same_float(ax, ay, stats, TOL, 'flare', ROUNDING):
                                raise SystemExit(f'draw {d} node {i}: flare {y}, the exe {x}')
        for off, v in undo:
            emu.w32(model + off, v)
            p.probe_pm_set_model(i, off, v)
    print(f'ok: {draws} draws; {stats.floats} floats, {stats.exact} exact, off by place {stats.where}; {seen}')
    if len(seen) < 3:
        raise SystemExit('points or flares never reached')


if __name__ == '__main__':
    main()
