"""The stealth object against the original x86 (docs/editor.md, The
stealth object; mech3/veh_stealth): the constructor 0x4a4de0 (the basic
vehicle's 0x4ac530 and the markers 0x4ac3d0, a new node named "Stealth" in
the world, the entity's node and list, the position at the origin, the
node's intersect flag and flag 8 off, the controls' motion mode and state
zeroed, the overlay, +0x09 cleared, vehicle.zrd's `stealth` with its
common_mode and motion block, the points, aiv.zrd's `stealth` record
placing it when there is one), the overlay 0x4a55c0 with its image
0x4a5850 run for real (only the texture lookup 0x59bb10 stubbed), and the
controls' answers (0x4a5260, 0x4d72a0, 0x4a5270), and their frame 0x4a5280
(the commands and the mouse stubbed; the curve 0x4abcf0 the exe's, its
_CIpow, always to the power 2, as the x87's square), and the stealth's own
frame 0x4a5050 with its reticle 0x4a5a00 (editor.md S4).

    cmake --build build && .venv/bin/python tests/differential/test_veh_stealth.py [runs]

Every call out of the constructor is stubbed (the overlay too, checked on
its own). Compared: the traces, the stealth's +0x09, its overlay, the
controls' +0x764 and +0x38; the overlay's and image's fields; the answers.
"""
import ctypes
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from unicorn.x86_const import UC_X86_REG_ECX  # noqa: E402
from calltrace import Emu, Replay, Trace, compare, load_probe, rnd  # noqa: E402

U, I = ctypes.c_uint32, ctypes.c_int32
WORLD = 0x608c84
COVER = {k: 0 for k in ('node', 'no node', 'record', 'no record', 'mouse lost', 'mouse read', 'ramp', 'no ramp',
                         'reticle centred', 'reticle off centre')}


def f2b(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def make(seed, t):
    e = Emu(t)
    e.heap_stubs()
    r = rnd(seed, 'case')
    case = {'node': r.random() < 0.8, 'record': r.random() < 0.5,
            'mode': r.choice([1, 3, 4]), 'state': r.choice([2, 4, 7])}
    ctl = e.world_alloc(0xa40)
    e.w32(ctl + 0x764, case['mode'])
    e.w32(ctl + 0x38, case['state'])
    node = e.world_alloc(0x40)
    root, defn, common, lst = (e.world_alloc(0x40) for _ in range(4))
    e.w32(defn + 4, lst)
    rec = e.world_alloc(0x40)
    for i, v in enumerate((r.uniform(-500, 500), r.uniform(0, 50), r.uniform(-500, 500))):
        e.wf(rec + 8 + 4 * i, v)
    e.wf(rec + 0x14, r.uniform(-3, 3))

    def answer(value):
        def fn(em, key, a, pop):
            em.t.handle(key, a)
            em.t.override(key, value)
            em._ret(value, pop)
        return fn

    def construct(em, key, a, pop):
        em.w32(a[0] + 0xe4, a[1])
        em._ret(em.t.handle(key, a), pop)
    e.stub(0x4ac530, spec='c,s0,s1', fn=construct)

    def marker(em, key, a, pop):
        c = em.uc.reg_read(UC_X86_REG_ECX)
        em.t.handle(key, a)
        em.t.override(key, c)
        em._ret(c, pop)
    e.stub(0x59b830, spec='zs0,s1,s2,s3', fn=marker)
    e.stub(0x4fb740, spec='', fn=answer(node if case['node'] else 0))
    world = e.object('world', slots={0: ('s0', 4)})
    e.w32(WORLD, world)
    e.stub(0x4eb300, spec='c,zd')

    def set_node(em, key, a, pop):
        em.w32(a[0] + 4, a[1])
        em._ret(em.t.handle(key, a), pop)
    e.stub(0x4d6c90, spec='c,s0,s1,s2,s3', fn=set_node)
    e.stub(0x5a7f50, spec='c')

    def position(em, key, a, pop):
        p = a[1]
        em._ret(em.t.handle(key, (a[0], em.r32(p), em.r32(p + 4), em.r32(p + 8))), pop)
    e.stub(0x5a8550, key='vt+0x18', spec='c,s0', fn=position)
    e.stub(0x4eb6d0, spec='c,d')
    e.stub(0x4eb620, spec='c,d')

    def layer(em, key, a, pop):
        c = em.uc.reg_read(UC_X86_REG_ECX)
        em.t.handle(key, a)
        em.t.override(key, c)
        em._ret(c, pop)
    e.stub(0x4a55c0, spec='s0', fn=layer)
    e.stub(0x556740, spec='zc,d', fn=answer(root))
    finds = iter([defn, common])
    e.stub(0x5569e0, spec='c,zd', fn=lambda em, k, a, p: answer(next(finds))(em, k, a, p))
    e.stub(0x49ec00, spec='c,s0,zs1,s2')
    e.stub(0x49ec90, spec='c,s0,zs1,zs2,s3')
    e.stub(0x49f660, spec='c')
    e.stub(0x556820, spec='c')
    e.stub(0x4e0490, spec='')
    e.stub(0x4e0680, spec='zs0', fn=answer(rec if case['record'] else 0))

    def place(em, key, a, pop):
        p = a[1]
        em._ret(em.t.handle(key, (a[0], em.r32(p), em.r32(p + 4), em.r32(p + 8), a[2])), pop)
    e.stub(0x4a3630, spec='c,s0,s1', fn=place)
    e.stub(0x4e04f0, spec='')
    return e, case, ctl


def run(probe, seed, verbose=False):
    t = Trace()
    e, case, ctl = make(seed, t)
    COVER['node' if case['node'] else 'no node'] += 1
    COVER['record' if case['record'] else 'no record'] += 1
    mem = e.alloc(0x104)
    snap = e.snapshot()
    rep = Replay(t, e, snap)
    probe.probe_app_set_callback(rep.callback())
    name = f'seed {seed} {case}'
    e.cpu.fpu_control(0x007f)
    v = e.call(0x4a4de0, ecx=mem, stack=(ctl,))
    out = (U * 4)()
    probe.probe_stealth_create(mem, ctl, case['mode'], case['state'], out)
    if not compare(name, t.log, rep.log):
        return False
    a = (e.r32(v + 8) >> 8 & 0xff, e.r32(v + 0xfc), e.r32(ctl + 0x764), e.r32(ctl + 0x38))
    b = tuple(out)
    if a != b:
        print(f'{name}: (+0x09, overlay, mode, state) original {a} rebuild {b}')
        return False
    if verbose:
        print(name, len(t.log), 'calls')
    return True


def layer(probe):
    t = Trace()
    e = Emu(t)
    e.heap_stubs()
    e.stub(0x59bb10, spec='zs0')
    owner = e.world_alloc(0x110)
    mem = e.alloc(0x108)
    e.cpu.fpu_control(0x007f)
    e.call(0x4a55c0, ecx=mem, stack=(owner,))
    img = mem + 0x14
    a = [e.r32(mem + 4), e.r32(img + 0x0c), e.r32(img + 0x10), e.r32(img + 0x14), e.r32(img + 0x18),
         e.r32(img + 0x40) & 0xffff, e.r32(img + 0x48),
         int(e.r32(mem + 8) == img and e.r32(mem + 0xc) == img and e.r32(img + 8) == mem and e.r32(img + 4) == 0),
         int(e.r32(mem + 0x10) == owner)]
    t2 = Trace()
    rep = Replay(t2, e, e.snapshot())
    probe.probe_app_set_callback(rep.callback())
    out = (U * 9)()
    probe.probe_stealth_layer(out)
    b = list(out)
    if a != b:
        print(f'the overlay: original {a} rebuild {b}')
        return False
    if [k for k, _ in t.log] != ['0x0059bb10'] or t.log[0][1] != ('ret_off',):
        print(f'the overlay: its calls {t.log}')
        return False
    return True


def frame(probe, seed):
    """0x4a5280 on stealth controls (their IControls 0x5c85f8 at +0xa08, the
    shared accessors' own), the commands and the mouse stubbed."""
    r = rnd(seed, 'frame')
    t = Trace()
    e = Emu(t)
    e.heap_stubs()
    e.cpu.fpu_control(0x007f)
    base = e.world_alloc(0xa40)
    e.cpu.write(base, b'\0' * 0xa40)
    ictl = base + 0xa08
    vbt = e.world_alloc(0x10)
    e.w32(vbt + 4, 0xa04)
    e.w32(base + 4, vbt)
    e.w32(ictl, 0x5c85f8)
    # The CRT's _CIpow (0x5ac386) reaches tables the emulator lacks; the
    # curve's exponent is always 2: the base squared on the x87 (fstp st0,
    # fmul st0, st0, ret).
    e.cpu.write(0x5ac386, bytes([0xdd, 0xd8, 0xd8, 0xc8, 0xc3]))

    def val(lo, hi):
        x = r.random()
        return float('nan') if x < 0.03 else r.choice([0.0, 1.0, -1.0]) if x < 0.25 else r.uniform(lo, hi)
    case = dict(throttle=val(-1.6, 1.6), turn=val(-1, 1), ramp=int(r.random() < 0.4), in40=val(-1.8, 1.8),
                pitch_in=val(-1, 1), pitch=val(-0.6, 0.6), fx=r.uniform(-1, 1), fy=r.uniform(-1, 1),
                dt=r.choice([0.033, 0.016, 0.1]), err=r.random() < 0.12,
                held={c: r.choice([0, 0, 1, 3, 4, 7]) for c in (0x18, 0x19, 2, 3, 4, 5, 6)})
    for off, k in ((8, 'throttle'), (0xc, 'turn'), (0x40, 'in40'), (0x44, 'pitch_in'), (0x32c, 'pitch')):
        e.wf(base + off, case[k])
    e.w32(base + 0x10, case['ramp'])
    e.wf(0x72c52c, case['dt'])

    def mouse(em, key, a, pop):
        em.wf(a[0] + 0xc, case['fx'])
        em.wf(a[0] + 0x10, case['fy'])
        v = 0x8007001e if case['err'] else 0
        em.t.handle(key, ())
        em.t.override(key, v)
        em._ret(v, pop)
    e.stub(0x51ba60, spec='c', fn=mouse)

    def command(em, key, a, pop):
        v = case['held'][a[0]]
        em.t.handle(key, (a[0],))
        em.t.override(key, v)
        em._ret(v, pop)
    e.stub(0x51d300, spec='c', fn=command)
    rep = Replay(t, e, e.snapshot())
    probe.probe_app_set_callback(rep.callback())
    want = e.call(0x4a5280, ecx=ictl, stack=(0,))
    out = (U * 9)()
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]  # noqa: E731
    probe.probe_stealth_frame(f32(case['throttle']), f32(case['turn']), case['ramp'], f32(case['in40']), f32(case['pitch_in']),
                              f32(case['pitch']), f32(case['fx']), f32(case['fy']), out)
    name = f'frame seed {seed}'
    if not compare(name, t.log, rep.log):
        return False
    a = [e.r32(base + o) for o in (8, 0xc, 0x40, 0x44, 0x48, 0x4c, 0xa4, 0xa8)] + [want]
    b = list(out)
    # The curve's four outputs within an ulp: its powers are the x87's 80
    # bits there, a double here (arm64 has no longer one).
    near = {0, 1, 4, 5}
    bad = [i for i in range(9) if a[i] != b[i] and not (i in near and abs(a[i] - b[i]) <= 1 and (a[i] ^ b[i]) >> 31 == 0)]
    if bad:
        names = ['throttle', 'turn', 'in40', 'pitch_in', 'out48', 'pitch_out', 'mouse x', 'mouse y', 'answer']
        k = bad[0]
        print(f'{name} {case}: {names[k]} original {a[k]:#x} rebuild {b[k]:#x}')
        return False
    COVER['mouse lost' if case['err'] else 'mouse read'] += 1
    COVER['ramp' if case['ramp'] else 'no ramp'] += 1
    return True


def step(probe, seed):
    """0x4a5050, the stealth's frame (its vt+0x34): the controls' frame
    (IControls vt+0x0c) stubbed, the reticle 0x4a5a00 run for real with
    the image's place 0x59bab0 and the pick 0x4a58d0 stubbed, the move
    0x4a1240 and the matrix 0x5a85c0 stubbed."""
    r = rnd(seed, 'step')
    t = Trace()
    e = Emu(t)
    e.heap_stubs()
    e.cpu.fpu_control(0x007f)
    s = e.world_alloc(0x110)
    ctl = e.world_alloc(0xa40)
    ov = e.world_alloc(0x110)
    vbt = e.world_alloc(0x10)
    e.w32(vbt + 4, 0xa04)
    e.w32(ctl + 4, vbt)
    ivt = e.world_alloc(0x100)
    e.w32(ctl + 0xa08, ivt)
    e.w32(ivt + 0xc, e.new_stub('ictl.vt+0x0c', 'c,s0', 4))
    e.w32(s + 0xe4, ctl)
    e.w32(s + 0xfc, ov)
    x = r.random()
    mx = r.choice([0.0, 1.0, -1.0, 0.5, -0.5]) if x < 0.3 else r.uniform(-1, 1)
    my = r.choice([0.0, 1.0, -1.0, 0.25]) if r.random() < 0.3 else r.uniform(-1, 1)
    if r.random() < 0.15:
        mx = my = 0.0  # the mouse lost: the controls leave it at the centre
    dt = r.choice([0.033, 0.016, 0.1])
    e.wf(ctl + 0xa4, mx)
    e.wf(ctl + 0xa8, my)
    placed = []

    def place(em, key, a, pop):
        placed.append((a[0], a[1] - (1 << 32) if a[1] >= 1 << 31 else a[1], a[2] - (1 << 32) if a[2] >= 1 << 31 else a[2]))
        em._ret(0, pop)
    e.stub(0x59bab0, spec='c,s0,s1', fn=place)
    e.stub(0x4a58d0, spec='c')
    e.stub(0x4a1240, spec='c')
    e.stub(0x5a85c0, spec='c')
    rep = Replay(t, e, e.snapshot())
    probe.probe_app_set_callback(rep.callback())
    f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]  # noqa: E731
    e.call(0x4a5050, ecx=s, stack=(f2b(dt),))
    out = (I * 2)()
    probe.probe_stealth_step(s, ctl, ov + 0x14, f32(mx), f32(my), f32(dt), out)
    name = f'step seed {seed} (mouse {mx:.3f}, {my:.3f})'
    if not compare(name, t.log, rep.log):
        return False
    if len(placed) != 1 or placed[0][0] != ov + 0x14 or list(placed[0][1:]) != list(out):
        print(f'{name}: the reticle original {placed} rebuild {list(out)}')
        return False
    COVER['reticle off centre' if (mx, my) != (0.0, 0.0) else 'reticle centred'] += 1
    return True


def controls(probe):
    e = Emu(Trace())
    c = e.world_alloc(0xa40)
    a = [e.call(0x4a5260, ecx=c), e.call(0x4d72a0, ecx=c), e.call(0x4a5270, ecx=c)]
    a = [x - (1 << 32) if x >= 1 << 31 else x for x in a]
    out = (I * 3)()
    probe.probe_stealth_controls(out)
    if a != list(out):
        print(f'the controls: original {a} rebuild {list(out)}')
        return False
    return True


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    probe = load_probe()
    probe.probe_stealth_create.argtypes = [U, U, I, I, ctypes.POINTER(U)]
    probe.probe_stealth_layer.argtypes = [ctypes.POINTER(U)]
    probe.probe_stealth_controls.argtypes = [ctypes.POINTER(I)]
    ok = all(run(probe, s) for s in range(runs))
    probe.probe_stealth_frame.argtypes = [ctypes.c_float, ctypes.c_float, U, ctypes.c_float, ctypes.c_float, ctypes.c_float,
                                          ctypes.c_float, ctypes.c_float, ctypes.POINTER(U)]
    ok = all(frame(probe, s) for s in range(runs * 2)) and ok
    probe.probe_stealth_step.argtypes = [U, U, U, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.POINTER(I)]
    ok = all(step(probe, s) for s in range(runs)) and ok
    ok = layer(probe) and ok
    ok = controls(probe) and ok
    missing = [k for k, n in COVER.items() if n == 0]
    if missing:
        print('not reached:', missing)
        ok = False
    print(f'stealth: {runs} constructions, {runs * 2} controls\' frames, {runs} frames, the overlay, the controls: '
          f'{"ok" if ok else "FAILED"}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
