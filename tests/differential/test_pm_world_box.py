"""PM's box writer (pm/gamez/zclass/cls_world, 0x4fe740 and the cell functions 0x505300, 0x505550,
0x506000, 0x505c80, 0x4fd8d0, 0x4ff660) against PM's exe on the four worlds, each side's own reader.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_world_box.py [calls per world]

Random nodes get random flags (the model's and children's bits, 0x80) and two boxes: inside a cell,
across cells, past the world's second area. After each call: the flags, +0x2c, the box chosen and
its values, the node's cell and range, its parents. At the end every cell's list (order, y ranges,
flags), the world's children and the pending count, order included.
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_gamez as g  # noqa: E402
import test_pm_walk  # noqa: E402
import test_pm_zar  # noqa: E402

WRITER, WORLD_UPDATE = 0x4fe740, 0x504fb0


class Emu(test_pm_walk.Emulated):
    def __init__(self, world):
        super().__init__(world)
        self.errors = []
        self.cpu.stubs[test_pm_zar.LOG_ERROR] = (lambda c: self.errors.append(c.arg(2)) or 0, 0)
        self.w32(0x60db84, 0)

    def op(self, n, flags, model, kids, scale):
        uc, r = self.cpu._uc, self.r32
        self.w32(n + 0x24, flags)
        if self.i32(n + 0x34) == 6:
            uc.mem_write(r(n + 0x38) + 0x34, struct.pack('<3f', *scale))
        uc.mem_write(n + 0x8c, struct.pack('<6f', *model))
        uc.mem_write(n + 0xa4, struct.pack('<6f', *kids))
        self.call(WRITER, args=[n])
        box = r(n + 0x70)
        which = {n + 0x74: 0, n + 0x8c: 1, n + 0xa4: 2}.get(box, 3)
        h = struct.unpack('<4h', bytes(uc.mem_read(n + 0x4c, 8)))
        parents = struct.unpack('<h', bytes(uc.mem_read(n + 0x54, 2)))[0]
        return [r(n + 0x24), r(n + 0x2c), which] + [r(box + 4 * k) for k in range(6)] + \
            [v & 0xffffffff for v in h] + [parents] + \
            ([r(r(r(n + 0x58)) + 0x24), r(r(r(n + 0x58)) + 0x2c)] if parents > 0 else [0, 0])

    def lists(self):
        r, i16 = self.r32, lambda a: struct.unpack('<h', bytes(self.cpu._uc.mem_read(a, 2)))[0]
        world = r(r(self.camera + 0x38))
        d = r(world + 0x38)
        cols, rows, table = self.i32(d + 0x98), self.i32(d + 0x9c), r(d + 0xa0)
        out = []
        for z in range(rows):
            for x in range(cols):
                c = r(table + 4 * z) + 0x58 * x
                count = i16(c + 0x3a)
                out += [count, r(c)]
                items = r(c + 0x3c)
                for i in range(count):
                    out += [self.index(r(items + 12 * i)), r(items + 12 * i + 4), r(items + 12 * i + 8)]
        kids = i16(world + 0x56)
        out.append(kids)
        table = r(world + 0x5c)
        out += [self.index(r(table + 4 * i)) for i in range(kids)]
        out.append(self.i32(d + 4))
        out += [r(world + 0x24), r(world + 0x2c)]
        return out


WORST = [0.0]


def compare_cells(p, emu, label, tolerance=0.0):
    """The pending cells updated on both sides (0x504fb0, 0x504a40): flags, y range, sphere."""
    world = emu.r32(emu.r32(emu.camera + 0x38))
    d = emu.r32(world + 0x38)
    pending = emu.i32(d + 4)
    emu.w32(d, 0x40)  # any setting, or the update returns before its cells: no fog bit
    emu.call(WORLD_UPDATE, args=[world])
    p.probe_pm_world_mark()
    p.probe_pm_world_update()
    cols, rows, table = emu.i32(d + 0x98), emu.i32(d + 0x9c), emu.r32(d + 0xa0)
    want = []
    for z in range(rows):
        for x in range(cols):
            c = emu.r32(table + 4 * z) + 0x58 * x
            want += [emu.r32(c)] + [emu.r32(c + o) for o in (0x14, 0x20, 0x28, 0x2c, 0x30, 0x34, 0x48, 0x4c)]
    buf = (ctypes.c_uint32 * (len(want) + 16))()
    n = p.probe_pm_cell_state(buf, len(buf))
    got = list(buf[:n])
    if len(want) != len(got):
        raise SystemExit(f'{label}: cell state {len(want)} words against {len(got)}')
    for at, (a, b) in enumerate(zip(want, got)):
        if a == b:
            continue
        fa, fb = struct.unpack('<2f', struct.pack('<2I', a, b))
        off = abs(fa - fb) / max(1.0, abs(fb))
        WORST[0] = max(WORST[0], off)
        if at % 9 == 0 or off > tolerance:  # the flags exact; the rest as far as the records' turns allow
            raise SystemExit(f'{label}: cell state word {at} (cell {at // 9}, field {at % 9}): exe {a:08x}, port {b:08x}')
    return pending


def main():
    calls = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    p = test_pm_zar.load_pm_probe()
    p.probe_pm_walk_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_box_op.argtypes = [ctypes.c_int, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    p.probe_pm_world_area2.argtypes = [ctypes.c_void_p]
    p.probe_pm_world_area2.restype = None
    p.probe_pm_world_update.restype = None
    p.probe_pm_world_mark.restype = None
    p.probe_pm_cell_state.argtypes = [ctypes.c_void_p, ctypes.c_int]
    p.probe_pm_world_lists.argtypes = [ctypes.c_void_p, ctypes.c_int]
    rng = random.Random(0x4fe740)
    moved = spans = gaps = updated = 0
    for w in ('c1', 'c2', 'c3', 'c4'):
        path = g.ZBD / w / 'gamez.zbd'
        emu = Emu(path)
        if p.probe_pm_walk_load(str(path).encode()) != emu.camera_index:
            raise SystemExit(f'{w}: the camera differs')
        d = emu.r32(emu.r32(emu.r32(emu.camera + 0x38)) + 0x38)
        x0, z0 = struct.unpack('<2f', emu.read(d + 0x34, 8))
        x1, z1 = struct.unpack('<2f', emu.read(d + 0x44, 8))
        cand = [i for i in range(emu.i32(g.N_CAPACITY)) if emu.i32(emu.node(i) + 0xd0) & 0x1000000
                and emu.i32(emu.node(i) + 0x34) in (5, 6) and struct.unpack('<h', emu.read(emu.node(i) + 0x54, 2))[0] > 0]
        lods = [i for i in cand if emu.i32(emu.node(i) + 0x34) == 6]
        for k in range(calls):
            if k == calls * 3 // 4:  # a second area wider than the grid: the clamps
                wide = (x0 - 1000.0, z0 + 1000.0, x1 + 1000.0, z1 - 1000.0)
                emu.cpu._uc.mem_write(d + 0x4c, struct.pack('<4f', *wide))
                p.probe_pm_world_area2((ctypes.c_float * 4)(*wide))
            i = rng.choice(lods) if lods and k % 5 == 0 else rng.choice(cand)
            n = emu.node(i)
            cx, cz = rng.uniform(x0 - 200, x1 + 200), rng.uniform(z1 - 200, z0 + 200)
            size = rng.choice((0.01, 1.0, 20.0, 100.0, 400.0, 3000.0))
            def box():
                dx, dz = rng.uniform(0, size), rng.uniform(0, size)
                return (cx - dx, rng.uniform(-5, 5), cz - dz, cx + dx, rng.uniform(5, 50), cz + dz)
            model, kids = box(), box()
            base = emu.r32(n + 0x24) & ~0x780
            flags = base | rng.choice((0, 0x200, 0x400, 0x600)) | rng.choice((0, 0, 0, 0x80))
            scale = tuple(rng.choice((0.5, 1.0, 1.0, 1.5, 3.0)) for _ in range(3))
            want = emu.op(n, flags, model, kids, scale)
            out = (ctypes.c_uint32 * 16)()
            p.probe_pm_box_op(i, flags, (ctypes.c_float * 6)(*model), (ctypes.c_float * 6)(*kids), (ctypes.c_float * 3)(*scale), out)
            got = list(out)
            if emu.errors or got != want:
                raise SystemExit(f'{w} call {k} node {i} flags {flags:x} errors {emu.errors}\n exe  {want}\n port {got}')
            moved += want[9] != 0xffffffff
            spans += want[9] != want[11] or want[10] != want[12]
            gaps += want[2] == 0
        updated += compare_cells(p, emu, w)
        want = emu.lists()
        buf = (ctypes.c_uint32 * (1 << 22))()
        n = p.probe_pm_world_lists(buf, len(buf))
        got = list(buf[:n])
        if len(want) != len(got):
            raise SystemExit(f'{w}: lists {len(want)} words against {len(got)}')
        if want != got:
            at = next(i for i, (a, b) in enumerate(zip(want, got)) if a != b)
            raise SystemExit(f'{w}: lists differ at word {at}: exe {want[at - 3:at + 4]}, port {got[at - 3:at + 4]}')
    print(f'ok: {calls} calls on 4 worlds; {moved} in a cell, {spans} across cells, {gaps} unions, {updated} cells updated; every list identical')


if __name__ == '__main__':
    main()
