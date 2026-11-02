"""BASIC, the stealth object's movement, against the original x86
(docs/editor.md S2; docs/vehicles.md, The stealth object): its step
0x4a1d90 (by the view's camera: the steering 0x4a20d0, or under the
overhead camera, mode 3, the panning 0x4a2000; the rotation 0x4ab990, the
up 0x4a2890, the speed step 0x4a2b10, the move on the plane, the ground
0x4a1f00 over the supports' 0x4a2190, the node's rotation, the last
matrix), and each of the four apart.

    cmake --build build && .venv/bin/python tests/differential/test_veh_basicmode.py [runs]

On the suspension's and the frame's vehicles (test_veh_suspension.py,
test_veh_frame.py: the ground's records, the controls' tables, the engine's
calls stubbed), with a camera of id 3 or another, a heading asked for or
not, mode_alt. The x87 at 24 bits. Compared: the traces (the probes and
positions within the frame test's tolerance), the suspension's and the
frame's images.
"""
import ctypes
import math
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_veh_suspension as S  # noqa: E402
import test_veh_frame as F  # noqa: E402
from calltrace import CALLBACK, load_probe  # noqa: E402

ENTRIES = [(0x4a1d90, 0, 'step'), (0x4a20d0, 1, 'steering'), (0x4a2000, 2, 'panning'), (0x4a1f00, 3, 'ground')]
COVER = {k: 0 for k in ('overhead', 'steered', 'heading asked', 'stopped dead', 'no turn')}


def run(probe, seed, entry, which, label):
    extra = {}

    def prep(e, r, t, v, ctl):
        mb = ctl + 0x764
        e.w32(ctl, F.BASE_VT)
        e.w32(ctl + 4, F.BASE_VBT)
        ictl = ctl + 4 + e.r32(F.BASE_VBT + 4)
        e.w32(ictl, 0x5c8aa0)
        turn = S.f32(r.uniform(-1, 1)) if r.random() < 0.75 else 0.0
        f = [S.f32(r.uniform(-1, 1)) if r.random() < 0.7 else 0.0, turn]
        h = r.uniform(-math.pi, math.pi)
        f += [S.f32(-math.sin(h)), S.f32(-math.cos(h)), S.f32(r.uniform(-2, 2))]
        f += [S.f32(r.uniform(-3, 3)) if r.random() < 0.6 else 0.0, S.f32(r.uniform(-30, 30))]
        f += [S.f32(r.uniform(-1, 1)) for _ in range(3)]
        f += [r.choice([40.0, 10.0]), r.choice([2.5, 0.6]), r.choice([30.0, 20.0]), r.choice([30.0, 20.0]),
              r.choice([30.0, 20.0]), r.choice([10000.0, 40.0]), r.choice([10.0, 2.0])]
        f += [S.f32(r.uniform(-4, 4)) for _ in range(36)]
        u = [0, 0, int(r.random() < 0.3), 0, 0, 0, 0, 0]
        k = 0
        for off in (8, 0xc, 0x748, 0x750, 0x54, 0x70, 0x78, 0x314, 0x318, 0x31c):
            e.w32(ctl + off, S.fb(f[k]))
            k += 1
        for off in (4, 0xc, 0x14, 0x18, 0x1c, 0x58, 0x5c):
            e.w32(mb + off, S.fb(f[k]))
            k += 1
        for i in range(36):
            e.w32(mb + 0x194 + 4 * i, S.fb(f[k + i]))
        e.w32(mb, u[0])
        e.w32(ctl + 0x760, u[2])
        yaw = S.f32(r.uniform(-7, 7)) if r.random() < 0.2 else S.f32(r.uniform(-3, 3))
        e.w32(ctl + 0x330, S.fb(yaw))
        probe.probe_vf_more(0, yaw)
        alt = r.choice([2.0, 0.5, 6.0])
        e.w32(mb + 0x88, S.fb(alt))
        probe.probe_vb_basic_alt(alt)
        overhead = r.random() < 0.35
        cam = e.world_alloc(0x10)
        e.w32(cam + 4, 3 if overhead else r.choice([0, 1]))
        e.w32(e.r32(0x5f4c8c) + 0x58, cam)
        e.stub(0x4fbd80, spec='c,s0,s1,s2')
        e.stub(0x5a85c0, spec='c')
        probe.probe_vf_extra((ctypes.c_float * len(f))(*f), (ctypes.c_uint32 * 8)(*u))
        x70 = f[5]
        COVER['overhead' if overhead else 'steered'] += which in (0, 1, 2)
        COVER['heading asked'] += u[2] and which in (0, 1)
        COVER['no turn'] += turn == 0.0
        COVER['stopped dead'] += overhead and turn != 0.0 and x70 != 0.0 and (turn > 0) == (x70 > 0)
        extra.update(ctl=ctl, v=v)

    x = S.build(probe, seed, prep)
    e, ctl, v = x['e'], x['ctl'], x['v']
    e.call(entry, ecx=v)
    probe.probe_vb_basic_run(which)
    name = f'BASIC {label} seed {seed} (dt {x["dt"]:.3f})'
    if not F.traces_match(name, x['t'].log, x['rep'].log):
        return False
    if not S.check_image(probe, name, x, rel=4e-6):
        return False
    w = [S.bf(e.r32(ctl + o)) for o in (0x320, 0x324, 0x328, 0x330, 0x54, 0x64, 0x6c, 0x70, 0x78, 0x7c, 0x80, 0x84, 0x314, 0x318, 0x31c)]
    w += [S.bf(e.r32(ctl + 0xac + 4 * i)) for i in range(45)]
    w += [S.bf(e.r32(ctl + 0x160 + 4 * i)) for i in range(45)]
    w += [S.bf(e.r32(ctl + 0x278 + 4 * i)) for i in range(12)]
    w += [S.bf(e.r32(v + 0x48 + 4 * i)) for i in range(4)]
    w += [S.bf(e.r32(v + 0x30 + 4 * i)) for i in range(3)] + [S.bf(e.r32(v + 0x3c + 4 * i)) for i in range(3)]
    got, gu = (ctypes.c_float * len(w))(), (ctypes.c_uint32 * 3)()
    probe.probe_vf_image(got, gu)
    names = ['angles0.x', 'angles0.y', 'angles0.z', 'yaw', 'turn rate', 'velocity.x', 'velocity.z', 'x70.x', 'x70.z',
             'x7c.x', 'x7c.y', 'x7c.z', 'x314.x', 'x314.y', 'x314.z'] + [f'point {i}' for i in range(90)] + \
            [f'last matrix[{i}]' for i in range(12)]
    for i, (a, b) in enumerate(zip(w[:-10], list(got)[:-10])):
        if not S.same(a, b, 4e-6):
            print(f'{name}: {names[i]} original {a} rebuild {b}')
            return False
    wu = [e.r32(ctl + 0x760)]
    if wu[0] != gu[2]:
        print(f'{name}: the heading asked for original {wu[0]} rebuild {gu[2]}')
        return False
    return True


def main():
    probe = load_probe()
    probe.probe_app_set_callback.argtypes = [CALLBACK]
    probe.probe_vs_setup.argtypes = [ctypes.c_uint32] * 4 + [ctypes.c_void_p, ctypes.c_void_p]
    probe.probe_vs_image.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    probe.probe_vf_extra.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    probe.probe_vf_image.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    probe.probe_vf_more.argtypes = [ctypes.c_uint32, ctypes.c_float]
    probe.probe_vb_basic_alt.argtypes = [ctypes.c_float]
    probe.probe_vb_basic_run.argtypes = [ctypes.c_int32]
    probe.probe_zones_mode.argtypes = [ctypes.c_int32]
    probe.probe_vb_hooks(1)
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    runs = int(args[0]) if args else 400
    for seed in range(runs):
        entry, which, label = ENTRIES[seed % 4]
        if not run(probe, seed, entry, which, label):
            sys.stdout.flush(); os._exit(1)
    missing = [k for k, n in COVER.items() if n == 0]
    if missing:
        print('not reached:', missing)
        sys.stdout.flush(); os._exit(1)
    print(f'ok: {runs} BASIC steps, steerings, pannings and grounds as the original')
    sys.stdout.flush(); os._exit(0)


if __name__ == '__main__':
    main()
