"""Pirate's Moon's missile lock and reticle image (HudReticle::aim_state,
src/mech3/hud.cpp on the pm branch; docs/hud.md section 12) against PM's exe
0x4455e0 in Unicorn, at the game's 24-bit x87.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_lock.py [runs]

Each run makes up one frame: over the target or not, the group's range and
whether it locks, the target (none, a mech or not, PM's roster index with the
Elemental's 11, powered down, ECM), its distance, the aim distance, the lock's
state and timer, the image shown and whether it is kept, the aimed node (none,
with an object or its +0xbc), the two tones playing. Compared: the lock and its
timer after, the image state and the image set, the group's lock flag, and the
tones started and stopped in order.
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))
IN_I1 = ('over', 'locks', 'b20', 'has_contact', 'has_mech', 'roster', 'down', 'ecm')
IN_I2 = ('state', 'locked', 'has_node', 'node_obj', 'obj_b8', 'node_bc', 'playing_c', 'playing_0')


class LockIn(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in IN_I1] + [(n, ctypes.c_float) for n in ('range', 'dist', 'aim')] + [
        ('lock', ctypes.c_int32), ('timer', ctypes.c_float)] + [(n, ctypes.c_int32) for n in IN_I2]


class LockOut(ctypes.Structure):
    _fields_ = [('lock', ctypes.c_int32), ('timer', ctypes.c_float)] + [
        (n, ctypes.c_int32) for n in ('state', 'image', 'lock_shown', 'n')] + [('events', (ctypes.c_int32 * 2) * 8)]


probe.probe_pm_lock.argtypes = [ctypes.POINTER(LockIn), ctypes.POINTER(LockOut)]
LOCK = 0x4455e0


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def run_exe(s):
    cpu = Original(exe='pm')
    cpu.tr = lambda a, size=1: a
    cpu.fpu_control(0x007f)
    w32 = lambda a, v: cpu.write(a, struct.pack('<I', v & 0xffffffff))  # noqa: E731
    wf = lambda a, v: cpu.write(a, struct.pack('<f', v))  # noqa: E731
    R, H, M, WM, cell, C = (cpu.alloc(n) for n in (0x1000, 0x7000, 0x800, 0x100, 8, 0x40))
    w32(R + 0x964, H)
    w32(H + 0x10, M)
    w32(M + 0xd4, WM)
    w32(H + 0x657c, cell)
    w32(cell, C if s.has_contact else 0)
    imgs = [0x7100 + i for i in range(4)]
    for i, a in enumerate(imgs):
        w32(R + 0x918 + 4 * i, a)
    w32(R + 0x910, s.state)
    cpu.write(R + 0x914, bytes([s.locked]))
    wf(R + 0x92c, s.aim)
    w32(R + 0x95c, s.lock)
    wf(R + 0x960, s.timer)
    node = cpu.alloc(0x100)
    w32(R + 0x954, node if s.has_node else 0)
    w32(node + 0xbc, s.node_bc)
    obj = cpu.alloc(0x20)
    cpu.write(obj + 8, bytes([s.obj_b8]))
    # The target's mech: its pilot's roster index, ECM, and vt+0xac (powered down).
    tm, pilot, vt = cpu.alloc(0x800), cpu.alloc(0x40), cpu.alloc(0x100)
    w32(tm + 0x6a8, pilot)
    w32(pilot + 0xc, s.roster)
    w32(tm + 0x3dc, s.ecm)
    code = cpu.code(b'\xc3')
    cpu.stub_function(code, lambda c: s.down, 0)
    w32(vt + 0xac, code)
    w32(tm, vt)
    fvals = cpu.alloc(16)
    wf(fvals, s.range)
    wf(fvals + 4, s.dist)
    cpu.write(0x4b5a20, b'\xd9\x05' + struct.pack('<I', fvals) + b'\xc3')  # fld [range]; ret
    cpu.write(0x4f8dc0, b'\xd9\x05' + struct.pack('<I', fvals + 4) + b'\xc3')
    cpu.stub_function(0x4b5ac0, lambda c: s.locks, 0)
    cpu.stub_function(0x4b5b20, lambda c: s.b20, 0)
    cpu.stub_function(0x4f8d80, lambda c: tm if s.has_mech else 0, 0)
    cpu.stub_function(0x40f700, lambda c: obj if s.node_obj else 0, 0)
    events = []
    cpu.stub_function(0x41f3c0, lambda c: (s.playing_c if c.arg(0) == 0xc else s.playing_0), 0)
    cpu.stub_function(0x41f310, lambda c: events.append([1, c.arg(0)]) or 0, 0)
    cpu.stub_function(0x41f340, lambda c: events.append([2, c.arg(0)]) or 0, 0)
    shown = []
    cpu.stub_function(0x5bba50, lambda c: shown.append(c.arg(0)) or 0, 4)
    w32(WM + 0x8c, 0xff)
    cpu.call(LOCK, ecx=R, stack_args=(s.over,))
    r32 = lambda a: struct.unpack('<I', cpu.read(a, 4))[0]  # noqa: E731
    lock_shown = cpu.read(WM + 0x8c, 1)[0]
    return dict(lock=r32(R + 0x95c), timer=struct.unpack('<f', cpu.read(R + 0x960, 4))[0], state=r32(R + 0x910),
                image=imgs.index(shown[-1]) if shown else -1, lock_shown=-1 if lock_shown == 0xff else lock_shown,
                events=events)


def run_port(s):
    o = LockOut()
    probe.probe_pm_lock(ctypes.byref(s), ctypes.byref(o))
    return dict(lock=o.lock, timer=o.timer, state=o.state, image=o.image, lock_shown=o.lock_shown,
                events=[list(o.events[i]) for i in range(o.n)])


def made_up(r):
    s = LockIn()
    s.over, s.locks, s.b20 = (int(r.random() < 0.8) for _ in range(3))
    s.has_contact = int(r.random() < 0.85)
    s.has_mech = int(r.random() < 0.8)
    s.roster = r.choice([11, 11, 7, 3, 17, 0])
    s.down = int(r.random() < 0.2)
    s.ecm = r.choice([0, 0, 1])
    s.range = f32(r.choice([300.0, 600.0, r.uniform(100, 1000)]))
    s.dist = f32(r.uniform(0, 1200))
    s.aim = f32(r.uniform(0, 1200))
    s.lock = r.randrange(5)
    s.timer = f32(r.choice([0.0, 0.0, r.uniform(0, 4)]))
    s.state = r.randrange(4)
    s.locked = int(r.random() < 0.15)
    s.has_node = int(r.random() < 0.6)
    s.node_obj = int(r.random() < 0.5)
    s.obj_b8 = int(r.random() < 0.5)
    s.node_bc = r.choice([0, 1, 2, 3])
    s.playing_c, s.playing_0 = int(r.random() < 0.5), int(r.random() < 0.5)
    return s


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    r = random.Random(1999)
    seen = {}
    for k in range(runs):
        s = made_up(r)
        e, p = run_exe(s), run_port(s)
        same = all(e[n] == p[n] for n in e if n != 'timer') and abs(e['timer'] - p['timer']) <= 1e-6
        if not same:
            print(f'run {k}: ' + ', '.join(f'{n}={getattr(s, n)}' for n, _ in s._fields_))
            for n in e:
                if e[n] != p[n]:
                    print(f'  {n}: exe {e[n]} port {p[n]}')
            sys.exit(1)
        seen[(e['lock'], e['state'])] = seen.get((e['lock'], e['state']), 0) + 1
    print(f'ok: {runs} frames identical; (lock, image state) reached: {dict(sorted(seen.items()))}')


if __name__ == '__main__':
    main()
