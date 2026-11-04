"""A node's model box and its box's corners in PM (0x4fef40, 0x4fda40) against
PM's exe on real worlds: the route that sends PM's nodes through the shared
class code (node_model_box by the table, the corners through pm_box). The
sphere the walk culls with comes from these (docs/pm-map.md, The debug start).
Edits reach what shipped data does not: the flag flipped, facades, other types,
lights and morphs on a model.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_nodebox.py
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modeldraw_common import Stats, fbits, same_float  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402

MODEL_BOX, CORNERS = 0x4fef40, 0x4fda40
TOP_P, FLAG_P, SLOTS, FLAGS = 0x611220, 0x61121c, 0x66f4d0, 0x66f558
TOL = 1e-5
ROUNDING = 0.002  # a corner that nearly cancels against a translation of 3,000 at 24 bits


class Emulated(g.Emulated):
    def __init__(self, world):
        super().__init__()
        self.cpu.fpu_control(0x007f)
        self.stack([world], 5, 6, 5, models=True, nodes=True)
        self.corners, self.matrix = self.alloc(96), self.alloc(48)
        self.w32(0xa4e094, self.alloc(12 * 2048))  # the morph scratch, set up by 0x53fe30 in play

    def node(self, i):
        blocks = self.i32(g.N_BLOCKS) & 0xffffffff
        return (self.i32(blocks + 4 * (i // 2500)) & 0xffffffff) + 0xd4 * (i % 2500)

    def box(self, i, world):
        n = self.node(i)
        ret = self.call(MODEL_BOX, args=[n]) & 0xffffffff
        if world:
            self.cpu._uc.mem_write(self.matrix, fbits(*world))
        self.w32(SLOTS, self.matrix)
        self.w32(FLAGS, 0 if world else 1)
        self.w32(TOP_P, SLOTS)
        self.w32(FLAG_P, FLAGS)
        self.cpu._uc.mem_write(self.corners, bytes(96))  # left alone without flag 0x100
        status = self.call(CORNERS, args=[n, self.corners]) & 0xffffffff
        model = self.i32(n + 0x3c) & 0xffffffff
        sphere = self.read(model + 0x48, 16) if model else bytes(16)
        return struct.pack('<III', ret, status, self.i32(n + 0x24) & 0xffffffff) + self.read(n + 0x8c, 24) + sphere + self.read(self.corners, 96)


def main():
    p = test_pm_zar.load_pm_probe()
    V = ctypes.c_void_p
    p.probe_pm_draw_load.argtypes = [ctypes.c_char_p]
    p.probe_pm_node_box.argtypes = [ctypes.c_int, V, V]
    p.probe_pm_flip_flags.argtypes = [ctypes.c_int, ctypes.c_uint32]
    p.probe_pm_flip_flags.restype = None
    p.probe_pm_set_light.argtypes = [ctypes.c_int, V, ctypes.c_int]
    p.probe_pm_set_light.restype = None
    p.probe_pm_set_morphs.argtypes = [ctypes.c_int, V, ctypes.c_int]
    p.probe_pm_set_morphs.restype = None
    p.probe_pm_set_model.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint32]
    p.probe_pm_set_model.restype = None
    buf = ctypes.create_string_buffer(256)
    rng = random.Random(0x4fef40)
    stats, checked, kinds = Stats(), 0, {}
    for w in ('c1', 'c3'):
        path = g.ZBD / w / 'gamez.zbd'
        emu = Emulated(path)
        assert p.probe_pm_draw_load(str(path).encode()) == 0
        for i in range(emu.i32(g.N_CAPACITY)):
            r = emu.read(emu.node(i), 0xd4)
            if not struct.unpack_from('<I', r, 0xd0)[0] & 0x1000000 or struct.unpack_from('<I', r, 0x34)[0] != 5:
                continue
            world = None
            if rng.random() < 0.5:
                world = [rng.uniform(-1, 1) for _ in range(9)] + [rng.uniform(-3000, 3000) for _ in range(3)]
            n = emu.node(i)
            if rng.random() < 0.5:
                emu.w32(n + 0x24, emu.i32(n + 0x24) ^ 0x200)
                p.probe_pm_flip_flags(i, 0x200)
            model = emu.i32(n + 0x3c) & 0xffffffff
            kind, undo = 'no model', []
            if model:
                def set_model(off, v):
                    undo.append((off, emu.i32(model + off) & 0xffffffff))
                    emu.w32(model + off, v)
                    p.probe_pm_set_model(i, off, v)
                def pts(count):
                    return struct.pack(f'<{count * 3}f', *(rng.uniform(-40, 40) for _ in range(count * 3)))
                nv = emu.i32(model + 0x14)
                kind = rng.choice(('as is', 'facade 0', 'facade 1', 'type 2', 'type 3', 'light', 'morph', 'light alone'))
                if kind == 'morph' and nv < 1 or kind == 'light alone' and emu.i32(model + 0x20) > 0:
                    kind = 'as is'
                if kind.startswith('facade'):
                    set_model(0, 1), set_model(4, int(kind[-1]))
                elif kind.startswith('type'):
                    set_model(0, int(kind[-1]))
                elif kind == 'morph':
                    k = rng.randint(1, nv)
                    deltas = pts(k)
                    a = emu.alloc(len(deltas))
                    emu.cpu._uc.mem_write(a, deltas)
                    undo.append((0x44, emu.i32(model + 0x44) & 0xffffffff))
                    emu.w32(model + 0x44, a)
                    p.probe_pm_set_morphs(i, deltas, k)
                    set_model(0x1c, k)
                elif kind.startswith('light'):
                    k = rng.randint(1, 6)
                    points = pts(k)
                    a, rec = emu.alloc(len(points)), emu.alloc(0x4c)
                    emu.cpu._uc.mem_write(a, points)
                    emu.cpu._uc.mem_write(rec, bytes(0x4c))
                    emu.w32(rec + 0xc, k), emu.w32(rec + 0x2c, a)
                    undo.append((0x40, emu.i32(model + 0x40) & 0xffffffff))
                    emu.w32(model + 0x40, rec)
                    p.probe_pm_set_light(i, points, k)
                    set_model(0x20, 1)
                    if kind == 'light alone':
                        set_model(0x14, 0)
            kinds[kind] = kinds.get(kind, 0) + 1
            ours_n = p.probe_pm_node_box(i, (ctypes.c_float * 12)(*world) if world else None, buf)
            ours, theirs = buf.raw[:ours_n], emu.box(i, world)
            head_o, head_t = struct.unpack_from('<III', ours), struct.unpack_from('<III', theirs)
            if head_o != head_t:
                raise SystemExit(f'{w} node {i}: ret, status, flags {head_o} != {head_t}')
            for k, (a, b) in enumerate(zip(struct.unpack_from('<34f', ours, 12), struct.unpack_from('<34f', theirs, 12))):
                if not same_float(a, b, stats, TOL, 'box' if k < 6 else 'sphere' if k < 10 else 'corners', ROUNDING if k >= 10 and world else 0.0):
                    raise SystemExit(f'{w} node {i} ({r[:12]!r}) float {k}: ours {a} exe {b}')
            checked += 1
            for off, v in reversed(undo):  # 0x40, 0x44: both sides keep their own pointer, unread with the count 0
                emu.w32(model + off, v)
                if off not in (0x40, 0x44):
                    p.probe_pm_set_model(i, off, v)
    print(f'pm node box: {checked} nodes, {stats.floats} floats, {stats.exact} exact, off by place {stats.where}, {kinds}')
    if checked < 1000:
        raise SystemExit(f'only {checked} nodes checked')


if __name__ == '__main__':
    main()
