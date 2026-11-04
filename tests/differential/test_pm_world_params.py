"""PM's world parameters (mech3/mission_world, 0x495ba0, 0x495250, 0x4951e0) against PM's exe, on every
block of the four worlds' world.zrd and on crafted blocks.

    cmake --build build --target pmprobe
    .venv/bin/python -P tests/differential/test_pm_world_params.py

A small scene in both: the list's nodes (the skies a block names, under a horizon), the sun, a
world and a camera. The exe's reader, list find, set active, fog, clip, light and cloud setters run
for real; the wind's setters, the weather objects and the puffers' reset are logged on both sides, in
order. Compared after the read: the world's fog words, the camera's clip, the sun's bytes, every
node's flags, the clouds, whether the bake went stale, the jet ceiling and temperature, all the reader's
fields, the weather's and the log. The heading goes through x87 fsin/fcos, so it is within 1e-6; the
rest bit for bit.
"""
import ctypes
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import ROOT  # noqa: E402
import test_pm_gamez as g  # noqa: E402
import test_pm_zar  # noqa: E402
import test_pm_zreader as zr  # noqa: E402

sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import zrd  # noqa: E402

READ, ZERR, STALE, POINT_COLOUR, SET_FORMAT = 0x495ba0, 0x406cb0, 0x4ff750, 0x5898c0, 0x598330
HW, VIEW, WORLD, SUN_OPTION, LISTS, LIST_DIRTY = 0x8c4b70, 0x61874c, 0x62ca90, 0x61878c, 0x60db00, 0x60db84
CLOUDS = 0x612e18
PUFFER_RESET = 0x564ac0
SNOW, RAIN, WEATHER_ADD = 0x5c6010, 0x5c65e0, 0x5bfbe0
PM = ROOT / 'game' / 'pm'
JET, TEMPERATURE, INV_TEMPERATURE = 0x607ed0, 0x607ec8, 0x607ecc
WIND_VELOCITY, WIND_MAX, WIND_ACCEL, WIND_ANGVEL = 0x566720, 0x566740, 0x566750, 0x566760
HEADING_WORDS = (-3, -2)  # from the end of the first part, before the stale flag
STATIC = 0x200


def fbits(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


class Emu(zr.Emulated):
    def __init__(self, path, rel):
        super().__init__()
        self.errors = []
        self.files[test_pm_zar.key(rel)] = bytearray(path.read_bytes())
        if not self.call(zr.OPEN_ARCHIVE, args=[self.string(rel.replace('/', '\\')), 1]):
            raise SystemExit(f'{rel}: PM does not open it')
        self.cpu.fpu_control(0x7f)
        self.cpu.call(self.thunk(SET_FORMAT), stack_args=(5, 6, 5, 0, 0, 0))
        self.stale = 0
        self.native_stub(STALE, self.mark_stale)
        self.native_stub(POINT_COLOUR, lambda c: 0)
        self.cpu.stubs[ZERR] = (lambda c: self.errors.append(c.arg(2)) or 0, 0)  # the bench hooks it already
        self.log = []
        self.weathers = []
        self.native_stub(PUFFER_RESET, lambda c: self.log.append(6) or 0)

        def word(c, i):
            return c.arg(i)
        self.native_stub(WIND_VELOCITY, lambda c: self.log.extend(
            [13] + [self.r32(word(c, 0) + o) for o in (0, 4, 8)]) or 0)
        self.native_stub(WIND_MAX, lambda c: self.log.extend([10, word(c, 0)]) or 0)
        self.native_stub(WIND_ACCEL, lambda c: self.log.extend([11, word(c, 0)]) or 0)
        self.native_stub(WIND_ANGVEL, lambda c: self.log.extend([12, word(c, 0)]) or 0)
        weather_vt = self.alloc(0x80)
        ret0 = self.cpu.code(b'\xc3')
        for o in range(0, 0x80, 4):
            self.w32(weather_vt + o, ret0)
        destroy = self.cpu.code(b'\xc3' * 16)
        start = self.cpu.code(b'\xc3' * 16)
        self.w32(weather_vt, destroy)
        self.w32(weather_vt + 0x60, start)
        self.native_stub(destroy, lambda c: self.log.extend([4, self.weathers.index(c.ecx)]) or 0, 4)
        self.native_stub(start, lambda c: self.log.extend([5, self.weathers.index(c.ecx), word(c, 0)]) or 0, 4)

        def made(kind):
            def f(c):
                self.log.extend([kind, word(c, 0)])
                self.cpu._uc.mem_write(c.ecx, bytes(0xc0))
                self.w32(c.ecx, weather_vt)
                self.weathers.append(c.ecx)
                return c.ecx
            return f
        self.native_stub(SNOW, made(1), 4)
        self.native_stub(RAIN, made(2), 4)
        self.native_stub(WEATHER_ADD, lambda c: self.log.extend([3, self.weathers.index(word(c, 0))]) or 0, 4)
        self.w32(HW, 1)
        self.w32(LIST_DIRTY, 0)
        self.root = self.call(zr.LOAD, args=[self.string('world.zrd'), 0, 0])

    def w32(self, a, v):
        self.cpu._uc.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def thunk(self, addr):
        if addr not in self.thunks:
            self.thunks[addr] = self.cpu.code(b'\xb8' + struct.pack('<I', addr) + b'\xff\xe0')
        return self.thunks[addr]

    def mark_stale(self, c):
        self.stale = 1
        return 0

    def node(self, name, cls, data=0, flags=0x10):
        n = self.alloc(0xd4)
        self.cpu._uc.mem_write(n, name.encode() + b'\0')
        self.w32(n + 0x34, cls)
        self.w32(n + 0x24, flags)
        self.w32(n + 0x38, data)
        return n

    def run(self, key, names, cfg):
        uc, w = self.cpu._uc, self.w32
        kids = []
        for i, name in enumerate(names):
            kids.append(self.node(name, 5, flags=(4 if i % 2 else 0x10) if cfg[2] else 0x10))
        horizon = self.node('horizon', 5)
        table = self.alloc(4 * max(len(kids), 1))
        for i, k in enumerate(kids):
            w(table + 4 * i, k)
        uc.mem_write(horizon + 0x56, struct.pack('<h', len(kids)))
        w(horizon + 0x5c, table)
        wd = self.alloc(0xbc)
        for o in range(4, 0x34, 4):
            w(wd + o, 0x3f800000 + o * 0x1000)
        world = self.node('world1', 13, wd)
        cd = self.alloc(0x1e8)
        w(cd + 0xb0, fbits(0.5)), w(cd + 0xb4, fbits(777.0))
        w(cd + 8, horizon if cfg[2] else 0)
        camera = self.node('camera1', 1, cd)
        ld = self.alloc(0x100)
        for i in range(0, 0x100, 4):
            w(ld + i, fbits(0.125 + 0.0625 * (i // 4 % 16)))
        w(ld + 0xe0, cfg[3])
        sun = self.node('sunlight', 9, ld, 0x10 if cfg[1] == 2 else 0x14)  # 2: starts inactive
        listed = kids + ([sun] if cfg[1] else [])
        items = [self.alloc(0x10) for _ in listed]
        for k, (item, n) in enumerate(zip(items, listed)):
            uc.mem_write(item, struct.pack('<IIII', n, items[k - 1] if k else 0,
                                           items[k + 1] if k + 1 < len(items) else 0, 1))
        head = self.alloc(12)
        uc.mem_write(head, struct.pack('<III', items[0] if items else 0, items[-1] if items else 0, 0))
        w(LISTS + 4 * 6, head)
        for addr, node in ((VIEW, camera), (WORLD, world)):
            obj = self.alloc(8)
            w(obj + 4, node)
            w(addr, obj)
        option = self.alloc(4)
        w(option, cfg[0])
        w(SUN_OPTION, option)
        uc.mem_write(CLOUDS, struct.pack('<I6f', 7, 1.0, 2.0, 0.25, 0.125, 0.5, 0.75))
        self.stale = 0
        self.log.clear(), self.weathers.clear()
        for addr, v in ((JET, 1.0), (TEMPERATURE, 2.0), (INV_TEMPERATURE, 3.0)):
            w(addr, fbits(v))
        this = self.alloc(0x100)
        uc.mem_write(this, bytes(0x100))
        for o in (8, 9, 0x30, 0x50):  # the reset's own: not what the mission's memory held
            uc.mem_write(this + o, b'\x77')
        defaults = self.call(zr.FIND, args=[self.root, self.string('defaults')])
        block = self.call(zr.FIND, args=[self.root, self.string(key)]) if key else 0
        self.call(READ, ecx=this, args=[defaults, block])
        out = [self.r32(wd + o) for o in range(0, 0x34, 4)]
        out += [self.r32(cd + 0xb0), self.r32(cd + 0xb4), self.r32(cd + 0xf8)]
        out += [self.r32(ld + o) for o in range(0, 0x100, 4)]
        out += [self.r32(n + 0x24) for n in kids] + [self.r32(sun + 0x24)]
        out += [self.r32(CLOUDS + o) for o in range(0, 0x1c, 4)]
        out.append(self.stale)
        byte = lambda o: self.r32(this + o) & 0xff
        fields = lambda o, n: [self.r32(this + o + 4 * i) for i in range(n)]
        out += [self.r32(a) for a in (JET, TEMPERATURE, INV_TEMPERATURE)]
        sky = self.r32(this + 0x28)
        out += [byte(8), byte(9)] + fields(0xc, 7) + [kids.index(sky) + 1 if sky else 0] + fields(0x2c, 1)
        out += [byte(0x30)] + fields(0x34, 7) + [byte(0x50)] + fields(0x54, 3) + [self.r32(this + 0x60) & 0xffff]
        out += fields(0x64, 2) + fields(0x70, 1) + [byte(0x74)] + fields(0x78, 2) + fields(0x80, 1) + fields(0x84, 1)
        weather = self.r32(this)
        out.append(self.weathers.index(weather) + 1 if weather else 0)
        if weather:
            out += [self.r32(weather + 0x44) & 0xffff] + [self.r32(weather + o) for o in (0x48, 0x4c, 0x68, 0x6c, 0x70)]
            out.append(int(self.r32(weather + 0x50) == camera))
        return out + [len(self.log)] + self.log


def compare(label, emu, probe, base, rel, key, names, cfg):
    want = emu.run(key, names, cfg)
    HEADING_END = 13 + 3 + 0x40 + len(names) + 1 + 7
    if emu.errors:
        raise SystemExit(f'{label}: the exe reported errors at {emu.errors}')
    out = (ctypes.c_uint32 * 512)()
    n = probe.probe_pm_world_params(str(base).encode(), rel.encode(), key.encode(), '\n'.join(names).encode(),
                                    (ctypes.c_int32 * 4)(*cfg), out)
    if n < 0:
        raise SystemExit(f'{label}: the port cannot open it')
    got = list(out[:n])
    if len(got) != len(want):
        raise SystemExit(f'{label}: {len(want)} words against {len(got)}')
    for i, (a, b) in enumerate(zip(want, got)):
        if a == b:
            continue
        if i - HEADING_END in HEADING_WORDS and abs(struct.unpack('<f', struct.pack('<I', a))[0] -
                                                 struct.unpack('<f', struct.pack('<I', b))[0]) < 1e-6:
            continue
        raise SystemExit(f'{label}: word {i} of {len(want)} (world 0..12, clip 13..15, light 16..79, then nodes, '
                         f'sun, clouds 7, stale at {HEADING_END}, then env 3, fields 40ish, weather, log): exe {a:08x}, port {b:08x}')


def tree(v):
    if isinstance(v, int):
        return struct.pack('<ii', 1, v)
    if isinstance(v, float):
        return struct.pack('<if', 2, v)
    if isinstance(v, str):
        b = v.encode('latin-1')
        return struct.pack('<iI', 3, len(b)) + b
    return struct.pack('<iI', 4, len(v) + 1) + b''.join(tree(x) for x in v)


def crafted_archive(blocks):
    body = tree([[x for k, v in blocks.items() for x in (k, v)]])
    entry = struct.pack('<II', 0, len(body)) + b'world.zrd'.ljust(64, b'\0')
    table = entry.ljust(test_pm_zar.ENTRY, b'\0')
    return body + table + struct.pack('<III', 2, 1, test_pm_zar.crc32(body))


FLUCT = ['period', [8.0], 'wavelength', [600], 'base', [0.6], 'amplitude', [0.4], 'heading', [33.0]]
SUN = ['orientation', [-20.0, 22.0, 0.5], 'diffuse', [0.7], 'ambient', [0.3]]
CRAFTED = {
    'defaults': ['camera', ['nearfar_clip', [1.0, 900.0]],
                 'weather', ['type', ['SNOW'], 'particles', [9]], 'jet_ceiling', [111.0], 'ambient_temperature', [40.0]],
    'empty': [],
    'full': ['fog', ['node', ['hormain'], 'color', [0.5, 0.25, 0.125], 'altitude', [10.0, 20.0], 'range', [100.0, 200.0],
                     'hwrange', [30, 400], 'state', [7]],
             'camera', ['nearfar_clip', [0.25, 600.0]], 'sun', SUN + ['color', [0.1, 0.2, 0.3], 'ambient_color', [0.4, 0.5, 0.6]],
             'fluctuate', FLUCT, 'global_wind', ['max_speed', [13]], 'jet_ceiling', [130.0]],
    'fog_bare': ['fog', ['node', ['nosuch']]],
    'fog_no_node': ['fog', ['range', [1.0, 2.0]]],
    'sun_bare': ['sun', []],
    'sun_ambient_colour_only': ['sun', ['ambient_color', [0.4, 0.5, 0.6]]],
    'sun_diffuse_colour': ['sun', ['diffuse_color', [0.9, 0.8, 0.7]]],
    'fluct_bare': ['fluctuate', []],
    'fluct_flat': ['fluctuate', ['base', [0.5], 'amplitude', [0.0]]],
    'fluct_partial': ['fluctuate', ['base', [1.5], 'heading', [-90.0]]],
    'fluct_mid': ['fluctuate', ['base', [0.5]]],
    'weather': ['weather', ['type', ['SNOW'], 'particles', [77], 'gravity', [0.2]]],
    'rain_full': ['weather', ['type', ['RAIN'], 'particles', [5], 'color', [200, 100, 50], 'wind', ['dir', [1.5], 'vel', [2.5]],
                              'gravity', [3.5], 'alpha_gradient', [0.25, 0.75]]],
    'snow_default': ['weather', ['type', ['SNOW']]],
    'weather_no_type': ['weather', ['particles', [5]]],
    'weather_other': ['weather', ['type', ['HAIL']]],
    'wind_full': ['global_wind', ['max_speed', [4.5], 'random_accel', [1.5], 'random_angvel', [90.0], 'velocity', [1.0, 2.0, 3.0]]],
    'cloud': ['cloud_cover', ['top', [125.0], 'bottom', [85.0], 'depth', [10.0], 'death', [1.0, 2.0], 'color', [0.5, 0.25, 0.75]]],
    'cloud_deep': ['cloud_cover', ['top', [125.0], 'bottom', [85.0], 'depth', [99.0]]],
    'cloud_negative': ['cloud_cover', ['top', [125.0], 'bottom', [85.0], 'depth', [-5.0]]],
    'cloud_bare': ['cloud_cover', []],
    'heat': ['ambient_temperature', [35.0], 'jet_ceiling', [200.0]],
    'heat_zero': ['ambient_temperature', [0.0]],
    'heat_negative': ['ambient_temperature', [-3.0]],
    'lights_on': ['ai_headlights', ['on']],
    'lights_off': ['ai_headlights', ['off']],
    'water': ['underwater', ['range', [1.0, 2.0], 'hwrange', [3.0, 4.0], 'color', [5.0, 6.0, 7.0], 'altitude', [8.0, 9.0]]],
    'water_bare': ['underwater', []],
}
NAMES = ['decoy', 'horscroll', 'hormain', 'horblack']
CONFIGS = [(1, 1, 1, 0), (0, 1, 1, 0), (1, 1, 1, STATIC), (0, 1, 1, STATIC), (1, 2, 1, STATIC), (0, 2, 1, STATIC),
           (1, 0, 1, 0), (1, 1, 0, 0)]


def main():
    probe = test_pm_zar.load_pm_probe()
    probe.probe_pm_world_params.argtypes = [ctypes.c_char_p] * 4 + [ctypes.c_void_p, ctypes.c_void_p]
    runs = blocks = 0
    for world in ('c1', 'c2', 'c3', 'c4'):
        rel = f'zbd/{world}/reader.zbd'
        data = (PM / rel).read_bytes()
        top = next(zrd.node(data[s:s + n], 0)[0] for name, s, n in zrd.entries(data) if name == 'world.zrd')
        keys = top[0][::2]
        names = list(NAMES)

        def sky(block):
            for a, b in zip(block[::2], block[1::2]):
                if a == 'fog':
                    for c, d in zip(b[::2], b[1::2]):
                        if c == 'node' and d[0] not in names:
                            names.append(d[0])
        for b in top[0][1::2]:
            sky(b)
        emu = Emu(PM / rel, rel)
        for key in [''] + [k for k in keys if k != 'defaults']:
            for cfg in CONFIGS:
                compare(f'{world}/{key or "(none)"} {cfg}', emu, probe, PM, rel, key, names, cfg)
                runs += 1
            blocks += 1
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / 'crafted.zbd'
        path.write_bytes(crafted_archive(CRAFTED))
        emu = Emu(path, 'crafted.zbd')
        for key in [k for k in CRAFTED if k != 'defaults'] + ['nosuch']:
            for cfg in CONFIGS:
                compare(f'crafted {key} {cfg}', emu, probe, tmp, 'crafted.zbd', key, NAMES, cfg)
                runs += 1
    print(f'ok: {runs} runs identical ({blocks} world blocks on 4 worlds, {len(CRAFTED)} crafted blocks)')


if __name__ == '__main__':
    main()
