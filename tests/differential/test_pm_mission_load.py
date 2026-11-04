"""PM's mission load (mech3/mission, 0x492830 with its budget 0x4930c0) and setup (0x493ca0 with its begin
0x492130 and cheats' check 0x495110) against PM's exe, by call traces
as test_mission_load.py does for MW3: every call out is stubbed in the emulator and a hook in the port,
both answered from one script; the traces must be equal, then the memory written. Cases: the worlds,
hardware and software, card memory from none to 64 MB, every TextureMemory setting, InstallOptions, every
object detail, the clutter on and off, a script failing, allocations failing, a mission already up.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_mission_load.py [cases]
"""
import ctypes
import random
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from calltrace import Trace, compare  # noqa: E402
import test_pm_zar  # noqa: E402

LOAD_WORLD, SETUP = 0x492830, 0x493ca0
CURRENT = 0x9c
NEW_BASE = 0x2c200000
# PM's address, the port's key (MW3's address where the function has a twin), arguments, bytes popped.
# In PM's build these are plain stack calls where MW3's passed registers.
CALLS = [
    (0x404c70, '0x004050d0', 's0', 0), (0x404c90, '0x00405120', 'zs0', 0), (0x532860, '0x00518f50', 's0', 0),
    (0x5b7d00, '0x00595bb0', 'zs0', 4), (0x4863b0, '0x00484270', 's0', 0), (0x521ff0, '0x00509150', 's0', 0),
    (0x40b360, '0x0040d4d0', '', 0), (0x40b250, '0x0040d410', '', 0), (0x5b90d0, '0x00597d80', 's0', 0),
    (0x540330, '0x005269e0', 's0', 0), (0x597090, '0x00576270', 's0', 0), (0x5307f0, '0x00516f20', 's0', 0),
    (0x531a70, '0x005182a0', 'zs0,s1,s2', 0), (0x5b7ad0, '0x00595990', 'zs0', 4), (0x50caa0, '0x004f8f70', '', 0),
    (0x5400e0, '0x00526800', '', 0), (0x54fe70, '0x00535ed0', 's0', 0), (0x406cb0, '0x0049bfd0', 's0,zs1,s2,zs3,zs4', 0),
    (0x494b80, '0x00491780', 's0', 4), (0x40b1e0, '0x0040d3b0', '', 0), (0x50b920, '0x004f7fe0', 's0,zs1', 0),
    (0x493280, '0x00493280', 'zs0', 0), (0x4932c0, '0x004932c0', '', 0), (0x530840, '0x00516f50', 'zs0', 0),
    (0x5329e0, '0x005190d0', '', 0), (0x4817b0, '0x0047f2b0', 'c,s0', 4), (0x4b9740, '0x004b52e0', 'c,s0', 4),
    (0x4087f0, '0x0040aca0', 'c,s0,s1,s2', 0xc), (0x50b880, '0x004f7f70', '', 0), (0x50b4d0, '0x004f7bd0', '', 0),
    (0x40b540, '0x0040d670', 's0', 0), (0x40b590, '0x0040d6b0', 's0', 0), (0x40b430, '0x0040d590', 's0', 0),
    (0x50dcb0, '0x004f9fa0', 's0,s1,s2', 0), (0x50ddc0, '0x004fa0b0', 's0,s1,s2', 0), (0x4f98d0, '0x004e7b00', '', 0),
    (0x526a40, '0x0050d2c0', 's0,s1,zs2', 0), (0x564ac0, '0x00564ac0', 's0', 0), (0x521f80, '0x005090e0', 's0', 0),
    (0x524480, '0x0050ae60', 'zs0', 0), (0x519eb0, '0x00501ab0', 's0,s1', 0), (0x5a52c0, '0x005a52c0', 's0', 0),
    (0x5a52d0, '0x005a52d0', 's0', 0), (0x5a52e0, '0x00583880', 's0,zs1,s2,s3', 0), (0x5116a0, '0x005116a0', 's0', 0),
    (0x513030, '0x00513030', 'zs0', 0), (0x5139d0, '0x005139d0', '', 0), (0x513a30, '0x00513a30', 's0', 0),
    (0x40b6c0, '0x0040d7b0', '', 0), (0x40b660, '0x0040d750', '', 0), (0x410110, '0x0040f7f0', 's0,s1', 0),
    (0x40b9f0, '0x0040db60', '', 0), (0x4b01f0, '0x004ac860', 's0', 0), (0x4a7a00, '0x004a51b0', 'c,s0', 4),
    (0x4a7630, '0x004a4de0', 'c,s0', 4),
]
STRINGS = [(0x4b31c0, '0x004af290', 'ver', ''), (0x598b10, '0x005799f0', 'drv', ''), (0x59ac60, '0x00579040', 'mode', ''),
           (0x596e50, '0x00576040', 'msg', 's0')]
# The setup 0x493ca0 and its begin 0x492130 (game object slots apart).
SETUP_CALLS = [
    (0x579310, '0x00557320', 'zs0,s1', 0), (0x578070, '0x005561a0', 'zs0', 0), (0x578090, '0x005561e0', 'zs0', 0),
    (0x414430, '0x00413e60', '', 0), (0x431d70, '0x004319a0', '', 0), (0x45af70, '0x00459e80', 'c', 0),
    (0x495b30, '0x00491de0', 'zs0,zs1', 8), (0x4f9900, '0x004e7b40', '', 0), (0x5cb140, '0x005a84c0', 'c,zs0', 4),
    (0x4ce410, '0x004c3240', '', 0), (0x413e60, '0x004135c0', 'c,zs0,zs1', 8), (0x4b8c20, '0x004b4ab0', 'c', 0),
    (0x4a19c0, '0x0049e810', 'sb0', 0), (0x4aabe0, '0x004a76b0', 'zs0,s1', 0), (0x524b50, '0x0050b450', 'zs0', 0),
    (0x47b6e0, '0x0047b6e0', 's0', 4), (0x47b710, '0x0047b710', 's0', 4), (0x590a70, '0x0056f6f0', 'zs0', 0),
    (0x4e3250, '0x004d2e20', '', 0), (0x4e3620, '0x004d31d0', '', 0), (0x4f35c0, '0x004e2120', '', 0),
    (0x4e8ae0, '0x004d8610', 'c', 0), (0x533b90, '0x0051a300', '', 0), (0x40f900, '0x0040f040', '', 0),
    (0x4101c0, '0x0040f890', '', 0), (0x4e4f60, '0x004d4b30', '', 0), (0x521870, '0x00508c10', 's0,s1,s2,s3,s4', 0),
    (0x5b8570, '0x005964c0', 's0', 4), (0x4f9930, '0x004f9930', '', 0), (0x536c30, '0x0051d2f0', 's0,s1', 0),
    (0x40aca0, '0x0040cf20', '', 0), (0x40ad00, '0x0040cf80', '', 0), (0x40ad40, '0x0040cfc0', '', 0),
]
# PM's globals and handler to MW3's, as the port passes them. The command ids too: PM's table is
# renumbered (CMD_MISSION_COMPLETE 0x79 against 0x71) and the port's is still MW3's (docs/pm-map.md).
TO_MW3 = {0x6297b0: 0x605ae0, 0x628b50: 0x604e48, 0x62f828: 0x60b228, 0x627ca8: 0x604048, 0x627cd8: 0x604078,
          0x627bf8: 0x603f90, 0x492170: 0x48f440, 0x79: 0x71, 0x7a: 0x72}
HUD, HUD_STEP = 0x6203f0, 0x4b8
TURRETS, NETWORK = 0x62cc46, 0x62cc51

# PM's global, the probe's name for it, size.
GLOBALS = [(0x629c50, 0x6061cc, 1), (0x617e80, 0x5f4244, 1), (0xb0e440, 0x9a17c0, 4), (0x61874c, 0x5f4c8c, 4),
           (0x62ca90, 0x608c84, 4), (0x62db98, 0x609a2c, 4)]
CSTRING_CTOR, CSTRING_FORMAT, CSTRING_DTOR, OP_NEW = 0x5ce8e4, 0x5ce8de, 0x5ce8d8, 0x5ce8c6
TEXMEM, FN390, RECT, RGB, NO_CLUTTER, PILOT = 0x8c4bb4, 0x8c4bb8, 0x40b580, 0x62cc4d, 0x62cc58, 0x62c40c


def c_format(fmt, args):
    out, i = [], 0
    for m in re.finditer(r'%(\.\d+)?(\d*)([sdf])', fmt):
        out.append(fmt[i:m.start()])
        i = m.end()
        v = args.pop(0)
        if m.group(3) == 's':
            out.append(v)
        elif m.group(3) == 'd':
            out.append(str(v - (1 << 32) if v & 0x80000000 else v))
        else:
            out.append(('%' + (m.group(1) or '') + 'f') % v)
    return ''.join(out) + fmt[i:]


def make_script(case):
    return {
        '0x0040d4d0': case['hw'], '[0x0072f390]': case['fn390'], '[0x0072f38c]': case['texmem_ok'],
        '0x0040d410': case['texopt'], '0x0040d3b0': case['lod'],
        '0x00597d80': lambda n, a: case['install'] & a[0],
        '0x00595990': lambda n, a: case['gs_ok'] if a[0].endswith('.gs') else 1,
        '0x004f7fe0': lambda n, a: case['nodes'].get(a[1], 0),
        'new': lambda n, a: case['new'][n] if n < len(case['new']) else NEW_BASE + 0x1000 * (n + 1),
        '0x0047f2b0': lambda n, a: a[0], '0x004b52e0': lambda n, a: a[0], '0x0040aca0': lambda n, a: a[0],
        '0x004ac860': case['stealth_obj'], '0x004a51b0': 0x7700, '0x004a4de0': 0x7800,
        'game.vt+0x2c': case['type'], '0x0040d7b0': 3, '0x0040d750': 2,
        'game.vt+0x54': case['index'], 'game.vt+0x4c': case['number'], 'game.vt+0x28': case['saved'],
        'game.vt+0x54dir': 1, 'game.vt+0x58': lambda n, a: 2 + n, '0x0050b450': case['anims'],
        '0x0047b710': NEW_BASE + 0x9000, '0x0056f6f0': lambda n, a: 0x9000 + n,
        '0x0040cf20': case['cheats'][0], '0x0040cf80': case['cheats'][1], '0x0040cfc0': case['cheats'][2],
        '0x004af290': 7, '0x005799f0': 8, '0x00579040': 9, '0x00576040': 10,
    }


class Emu(test_pm_zar.Emulated):
    def __init__(self, t, case):
        super().__init__()
        self.t = t
        self.case = case
        self.cpu.fpu_control(0x27f)
        self.this = self.alloc(0x400)
        self.cpu._uc.mem_map(NEW_BASE, 0x10000)
        self.strings = {}
        for addr, key, spec, pop in CALLS:
            if addr == 0x406cb0:
                self.cpu.stubs[addr] = (self.stub(key, spec), pop)  # the bench hooks it already
            else:
                self.native_stub(addr, self.stub(key, spec), pop)
        for addr, key, tag, spec in STRINGS:
            self.native_stub(addr, self.string_stub(key, tag, spec))
        for addr, key, spec, pop in SETUP_CALLS:
            self.native_stub(addr, self.stub(key, spec), pop)
        self.native_stub(0x5ceabe, self.cs_ctor_str, 4)
        self.native_stub(0x4936f0, self.unload)
        self.native_stub(CSTRING_CTOR, self.cs_ctor)
        self.native_stub(CSTRING_DTOR, lambda c: 0)
        self.native_stub(CSTRING_FORMAT, self.cs_format)
        self.native_stub(OP_NEW, lambda c: self.t.handle('new', [c.arg(0)]))
        rect = self.alloc(16)
        self.cpu._uc.mem_write(rect, struct.pack('<4i', *case['rect']))
        self.native_stub(RECT, lambda c: t.handle('0x0040d6a0', []) and 0 or rect)
        self.w32(TEXMEM, self.fn_ptr(self.texmem))
        self.w32(FN390, self.fn_ptr(lambda c: t.handle('[0x0072f390]', [c.arg(0)])))
        game = self.alloc(8)
        vt = self.alloc(0x100)
        self.w32(game, vt)
        self.w32(vt + 0x28, self.fn_ptr(lambda c: t.handle('game.vt+0x2c', [])))
        self.w32(vt + 0x50, self.fn_ptr(lambda c: t.handle('game.vt+0x54', [])))
        self.w32(vt + 0x48, self.fn_ptr(lambda c: t.handle('game.vt+0x4c', [])))
        self.w32(vt + 0x24, self.fn_ptr(lambda c: t.handle('game.vt+0x28', [])))
        self.w32(vt + 0x54, self.fn_ptr(lambda c: self.keep(f"dir{t.handle('game.vt+0x54dir', [])}")))
        self.w32(vt + 0x58, self.fn_ptr(lambda c: self.keep(f"key{t.handle('game.vt+0x58', [])}")))
        self.w32(vt + 0x3c, self.fn_ptr(lambda c: t.handle('game.vt+0x3c', [
            c.arg(i) - self.this - 0xb0 + 0x6098d0 + 0x78 for i in range(5)]), 0x14))
        hud_vt = self.alloc(0x100)
        self.w32(hud_vt + 0x7c, self.fn_ptr(lambda c: t.handle('hudline.vt+0x7c', [(c.arg(0) - HUD) // HUD_STEP])))
        for i in range(25):
            self.w32(HUD + HUD_STEP * i, hud_vt)
        self.w8(TURRETS, case['turrets'])
        self.w8(NETWORK, case['network'])
        view = case['new'][2]
        self.w32(view + 4, NEW_BASE + 0x8000)
        self.w32(NEW_BASE + 0x8000 + 0x38, 0x2c310000)
        pilot = self.alloc(0x100)
        self.w32(pilot + 0x6c, game)
        self.w32(PILOT, pilot)
        self.w8(RGB, case['rgb'])
        self.w8(NO_CLUTTER, case['no_clutter'])
        self.w32(self.this + CURRENT, case['current'] & 0xffffffff)
        for a, _, n in GLOBALS:
            (self.w8 if n == 1 else self.w32)(a, 0)
        if case['current'] != -1:
            self.w32(0x61874c, case['new'][2])  # the mission up has its view

    def w32(self, a, v):
        self.cpu._uc.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def w8(self, a, v):
        self.cpu._uc.mem_write(a, bytes([v & 0xff]))

    def fn_ptr(self, fn, pop=0):
        at = self.cpu.code(b'\xc3' * 16)
        self.native_stub(at, fn, pop)
        return at

    def args(self, c, spec):
        out = []
        for tok in spec.split(',') if spec else []:
            if tok == 'c':
                out.append(c.ecx)
            elif tok.startswith('sb'):
                out.append(c.arg(int(tok[2:])) & 0xff)
            elif tok.startswith('zs'):
                p = c.arg(int(tok[2:]))
                out.append(self.cstr(p).decode('latin-1') if p else 0)
            else:
                out.append(c.arg(int(tok[1:])))
        return out

    def stub(self, key, spec):
        def f(c):
            a = self.args(c, spec)
            if spec in ('c', 's0,s1') or spec.startswith('c,'):  # an object or a bind: as MW3's
                a = [TO_MW3.get(v, v) if isinstance(v, int) else v for v in a]
            return self.t.handle(key, a)
        return f

    def cs_ctor_str(self, c):
        self.w32(c.ecx, self.keep(self.cstr(c.arg(0)).decode('latin-1')))
        return c.ecx

    def string_stub(self, key, tag, spec):
        def f(c):
            v = self.t.handle(key, self.args(c, spec))
            return self.keep(f'{tag}{v}')
        return f

    def keep(self, s):
        p = self.alloc(len(s) + 1)
        self.cpu._uc.mem_write(p, s.encode('latin-1') + b'\0')
        return p

    def texmem(self, c):
        r = self.t.handle('[0x0072f38c]', [c.arg(0)])
        if r:
            self.w32(c.arg(1), self.case['texmem'][0])
            self.w32(c.arg(2), self.case['texmem'][1])
        return r

    def unload(self, c):
        self.t.handle('0x00490480', [])
        self.w32(self.this + CURRENT, 0xffffffff)
        return 0

    def cs_ctor(self, c):
        self.w32(c.ecx, self.keep(''))
        return c.ecx

    def cs_format(self, c):
        this, fmt = c.arg(0), self.cstr(c.arg(1)).decode('latin-1')
        vals, k = [], 2
        for m in re.finditer(r'%(\.\d+)?(\d*)([sdf])', fmt):
            if m.group(3) == 'f':
                vals.append(struct.unpack('<d', struct.pack('<II', c.arg(k), c.arg(k + 1)))[0])
                k += 2
            else:
                v = c.arg(k)
                vals.append(self.cstr(v).decode('latin-1') if m.group(3) == 's' else v)
                k += 1
        self.w32(this, self.keep(c_format(fmt, vals)))
        return 0


def run_exe(case, entry):
    t = Trace(make_script(case))
    e = Emu(t, case)
    if entry == 'world':
        r = e.call(LOAD_WORLD, ecx=e.this, args=[case['index']]) & 0xff
    else:
        r = e.call(SETUP, ecx=e.this) & 0xff
    mem = {}
    for a, name, n in GLOBALS:
        mem[name] = e.r32(a) & (0xff if n == 1 else 0xffffffff)
    vw = case['new'][1]
    for off in range(8, 0x28, 4):
        mem[vw + off] = e.r32(vw + off)
    mem[vw + 0x28] = e.r32(vw + 0x28) & 0xff
    mem['current'] = e.r32(e.this + CURRENT)
    return r, t.log, mem


def run_port(probe, case, entry):
    t = Trace(make_script(case))
    cb = t.callback()
    probe.probe_app_set_callback(cb)
    probe.probe_mission_reset()
    probe.probe_mission_poke(0x608e41, case['rgb'])
    probe.probe_mission_poke(0x62cc58, case['no_clutter'])
    probe.probe_mission_set((ctypes.c_int32 * 4)(*case['rect']), case['texmem'][0], case['texmem'][1], -1, case['current'])
    probe.probe_mission_poke(0x608e3a, case['turrets'])
    probe.probe_mission_poke(0x608e45, case['network'])
    view = case['new'][2]
    probe.probe_mission_poke(view + 4, NEW_BASE + 0x8000)
    probe.probe_mission_poke(NEW_BASE + 0x8000 + 0x38, 0x2c310000)
    if case['current'] != -1:
        probe.probe_mission_poke(0x5f4c8c, view)
    r = probe.probe_mission_load_world(case['index']) if entry == 'world' else probe.probe_mission_setup()
    mem = {}
    for _, name, n in GLOBALS:
        mem[name] = probe.probe_mission_peek(name) & (0xff if n == 1 else 0xffffffff)
    vw = case['new'][1]
    for off in range(8, 0x28, 4):
        mem[vw + off] = probe.probe_mission_peek(vw + off)
    mem[vw + 0x28] = probe.probe_mission_peek(vw + 0x28) & 0xff
    mem['current'] = probe.probe_mission_field(0x60) & 0xffffffff
    return r, t.log, mem


def make_case(seed):
    r = random.Random(seed)
    new = [NEW_BASE + 0x1000 * (i + 1) for i in range(5)]
    if r.random() < 0.08:
        new[r.choice([0, 3])] = 0  # the view and its window the original writes through
    index = seed % 6 if r.random() < 0.9 else r.choice([6, 7, 8, 9, -1])
    case = {
        'index': index, 'type': r.choice([-1, 0, 1, 1, 2, 3, 4, 5, 5, 6]), 'hw': r.randrange(2), 'fn390': r.choice([0, 0, 1]),
        'texmem_ok': [r.choice([0, 1, 1]), r.choice([0, 1])],
        'texmem': [r.choice([0, 2 << 20, 3 << 20, 4 << 20, 6 << 20, 8 << 20, 12 << 20, 16 << 20, 64 << 20,
                             r.randrange(1 << 30)]), r.randrange(1 << 28)],
        'texopt': r.randrange(0, 8), 'install': r.choice([0, 1, 2, 4, 6, 0x101, 0x103, 0xff]),
        'lod': r.choice([0, 1, 2, 2]), 'no_clutter': r.randrange(2), 'gs_ok': 0 if r.random() < 0.08 else 1,
        'nodes': {'world1': 0x7101, 'camera1': 0x7102, 'window1': 0x7103, 'display': 0x7104},
        'new': new, 'stealth_obj': r.choice([0, 0x7600]),
        'rect': [r.randrange(0, 50), r.randrange(0, 50), r.randrange(300, 1700), r.randrange(200, 1300)],
        'rgb': r.randrange(2), 'current': -1, 'number': r.randrange(0, 9), 'saved': r.randrange(2),
        'anims': [r.choice([0, 0x8800]), r.choice([0, 0x8900])], 'turrets': r.randrange(2),
        'network': r.randrange(2), 'cheats': [r.randrange(2), r.randrange(2), r.randrange(2)],
    }
    if r.random() < 0.08:
        case['current'] = r.choice([index, (index + 1) % 6])
    return case


def main():
    probe = test_pm_zar.load_pm_probe()
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    calls = 0
    for seed in range(count):
        case = make_case(seed)
        for entry in ('world', 'setup'):
            a = run_exe(case, entry)
            b = run_port(probe, case, entry)
            name = f'seed {seed} {entry} (world {case["index"]}, type {case["type"]}, hw {case["hw"]}, lod {case["lod"]})'
            if not compare(name, a[1], b[1]):
                raise SystemExit(1)
            if a[0] != b[0]:
                raise SystemExit(f'{name}: returned {a[0]} (exe), {b[0]} (port)')
            if a[2] != b[2]:
                diff = {str(k): (hex(a[2][k]), hex(b[2].get(k, 0))) for k in a[2] if a[2][k] != b[2].get(k)}
                raise SystemExit(f'{name}: memory differs (exe, port): {diff}')
            calls += len(a[1])
    print(f'ok: {count} cases x 2 entries, {calls} calls identical')


if __name__ == '__main__':
    main()
