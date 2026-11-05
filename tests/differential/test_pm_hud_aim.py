"""Pirate's Moon's reticle ray's contact pass (HudReticle::contacts_by_mesh,
src/mech3/hud.cpp on the pm branch; docs/hud.md section 10) against PM's exe
0x445c60 in Unicorn: the mech's contacts nearer than the reach (nearest first),
each active one's node set to be hit by its mesh, or back.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_aim.py [runs]

Not here: the ray itself (0x445cf0), whose one PM change beside this is the far
point at the farther of the far clip and the group's range.
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT, Original  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))


class AimContact(ctypes.Structure):
    _fields_ = [('dist_sq', ctypes.c_float), ('vehicle', ctypes.c_int32), ('active', ctypes.c_int32)]


probe.probe_pm_contacts_mesh.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_float,
                                         ctypes.c_void_p, ctypes.c_void_p]
probe.probe_pm_contacts_mesh.restype = ctypes.c_uint32
PASS = 0x445c60


def run_exe(cs, on, reach_sq):
    cpu = Original(exe='pm')
    cpu.tr = lambda a, size=1: a
    cpu.fpu_control(0x007f)
    w32 = lambda a, v: cpu.write(a, struct.pack('<I', v & 0xffffffff))  # noqa: E731
    M = cpu.alloc(0x200)
    vec = cpu.alloc(4 * max(len(cs), 1))
    vehicles, nodes = {}, {}
    vt = cpu.alloc(0x100)
    code = cpu.code(b'\xc3')
    cpu.stub_function(code, lambda c: c.ecx, 0)  # vt+0x48: the entity itself
    w32(vt + 0x48, code)
    for i, (d, has_v, active) in enumerate(cs):
        c, v = cpu.alloc(0x40), cpu.alloc(0x40)
        cpu.write(c + 0xc, struct.pack('<f', d))
        w32(v, vt)
        w32(v + 4, 0x9000 + i)
        cpu.write(v + 8, bytes([active]))
        vehicles[c] = v if has_v else 0
        w32(vec + 4 * i, c)
    w32(M + 0xac, vec if cs else 0)
    w32(M + 0xb0, vec + 4 * len(cs) if cs else 0)
    cpu.stub_function(0x4f8d70, lambda c: vehicles[c.ecx], 0)
    log = []
    cpu.stub_function(0x4fd2c0, lambda c: log.append((c.arg(0) - 0x9000) * 2 + (1 if c.arg(1) else 0)) or 0, 0)
    cpu.call(PASS, stack_args=(M, on, struct.unpack('<I', struct.pack('<f', reach_sq))[0]))
    return cpu._uc.reg_read(UC_X86_REG_EAX), log


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    r = random.Random(1999)
    for k in range(runs):
        n = r.choice([0, 1, 2, 5, 8, 12])
        ds = sorted(struct.unpack('<f', struct.pack('<f', r.uniform(0, 2e6)))[0] for _ in range(n))
        cs = [(d, int(r.random() < 0.9), int(r.random() < 0.75)) for d in ds]
        on = r.choice([0, 1])
        reach = struct.unpack('<f', struct.pack('<f', r.choice([0.0, 640000.0, r.uniform(0, 2e6)])))[0]
        if cs and r.random() < 0.2:
            reach = cs[r.randrange(n)][0]  # a contact exactly at the reach
        e = run_exe(cs, on, reach)
        arr = (AimContact * max(n, 1))(*[AimContact(*c) for c in cs])
        log, logged = (ctypes.c_int32 * 16)(), ctypes.c_int32()
        seen = probe.probe_pm_contacts_mesh(arr, n, on, reach, log, ctypes.byref(logged))
        p = (seen, list(log)[:logged.value])
        if e != p:
            print(f'run {k}: contacts {cs} on {on} reach {reach}: exe {e} port {p}')
            sys.exit(1)
    print(f'ok: {runs} passes: the count and every node set, in order, identical')


if __name__ == '__main__':
    main()
