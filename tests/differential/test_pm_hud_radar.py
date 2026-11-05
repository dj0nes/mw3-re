"""Pirate's Moon's radar (HudRadar::update, src/mech3/hud.cpp on the pm branch;
docs/hud.md section 15, Pirate's Moon) against PM's exe: the whole of 0x43f220
in Unicorn at the game's 24-bit x87, its radar at PM's offsets.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_radar.py [runs]

Each run has no shots or mines (MW3's code in both). Each run makes up the radar's state (changed or not,
its sizes by zoom and where it is stepping to, the radius and place now, the
mech's sensor range and light flags, the light shown, the static, the view
direction, the HUD on or off, the radar hidden or not, on or off the mission
map, the mech's place and turn, its C3, up to 8 contacts, a selected target, the
network marker, the static) and one frame's dt. The
exe's zTwoD objects are recorded by small vtables (a place, shown or hidden, a
text, an update); the port's are its real objects, read back. Compared: the
radius and its square, the radar's place and shown flag, both texts' places
(and TEXT2's shown flag), the range text, the light's place and image, the
cone's two lines, the changed flag after, and in order every blip placed and
drawn, the C3 ring (place, radius, colour), the target's triangle and the
marker's text filled.

Not caught, by design: the ring's hide at the function's start (0x0043f28c).
Every contact hides it again before it can be drawn, so it only shows in the
ring's flag between frames, which nothing reads.
"""
import ctypes
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from original import FTOL, ROOT, Original  # noqa: E402
from test_mechdef import FLOOR  # noqa: E402

CEIL = FLOOR.replace(bytes.fromhex('660d0004'), bytes.fromhex('660d0008'))  # the CRT's ceil (test_mechsave's)

probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))


class Contact(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in ('team', 'on_radar', 'blip', 'has_mech', 'slave')] + [
        ('sensor', ctypes.c_float), ('pos', ctypes.c_float * 3), ('dist', ctypes.c_float)]


class RadarIn(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in ('changed', 'zoomed')] + [
        ('cx', ctypes.c_int32 * 2), ('cy', ctypes.c_int32 * 2), ('r', ctypes.c_int32 * 2)] + [
        (n, ctypes.c_int32) for n in ('to_r', 'to_x', 'to_y', 'r_steps', 'x_steps', 'y_steps', 'radius', 'x', 'y')] + [
        ('range', ctypes.c_float)] + [(n, ctypes.c_int32) for n in ('bc', 'light_current', 'hud_active', 'hidden')] + [
        ('noise', ctypes.c_float), ('dir', ctypes.c_float * 3), ('cos_half', ctypes.c_float),
        ('sin_half', ctypes.c_float), ('dt', ctypes.c_float), ('start', (ctypes.c_int32 * 2) * 3)] + [
        ('active', ctypes.c_int32), ('map_h', ctypes.c_int32), ('map_rect', ctypes.c_int32 * 4),
        ('map_scale', ctypes.c_float), ('me', ctypes.c_float * 3), ('q', ctypes.c_float * 4)] + [
        (n, ctypes.c_int32) for n in ('c3', 'text2_hidden', 'seed', 'target', 'marker', 'n')] + [
        ('marker_pos', ctypes.c_float * 3), ('contacts', Contact * 8)]


class RadarOut(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in (
        'radius', 'radius_sq', 'x', 'y', 'text1_x', 'text1_y', 'text2_x', 'text2_y', 'text2_shown', 'light_x',
        'light_y', 'light_current')] + [('lines', (ctypes.c_int32 * 4) * 2), ('changed', ctypes.c_int32),
                                         ('hidden', ctypes.c_int32), ('text1', ctypes.c_char * 32),
                                         ('n_events', ctypes.c_int32), ('events', (ctypes.c_int32 * 5) * 64)]


probe.probe_pm_radar_run.argtypes = [ctypes.POINTER(RadarIn), ctypes.POINTER(RadarOut)]
RADAR = 0x43f220
RING_GUARD, RING = 0x62957c, 0x629528
HIDDEN = 0x10


def fb(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


class PmCpu(Original):
    def __init__(self):
        super().__init__(exe='pm')
        self.tr = lambda a, size=1: a
        self.fpu_control(0x007f)
        self.import_code(0x5de644, FTOL)  # MSVCRT's _ftol (PM's import slot)
        self.stub_import(0x5de658, self.sprintf)  # sprintf: the range text's "%.1fkm"
        self.seed = 1
        self.stub_import(0x5de668, self.rand)
        self.import_code(0x5de618, CEIL)  # the CRT's ceil, as code (test_mechsave's)

    def rand(self, c):  # MSVCRT's
        self.seed = (self.seed * 214013 + 2531011) & 0xffffffff
        return (self.seed >> 16) & 0x7fff

    def sprintf(self, c):
        import re
        buf, fmt = c.arg(0), self.cstr(c.arg(1))
        args, k = [], 2
        for m in re.finditer(r'%[-+ 0#]*\d*(?:\.\d+)?([dfs])', fmt):
            if m.group(1) == 'f':
                args.append(struct.unpack('<d', struct.pack('<II', c.arg(k), c.arg(k + 1)))[0])
                k += 2
            elif m.group(1) == 's':
                args.append(self.cstr(c.arg(k)))
                k += 1
            else:
                args.append(struct.unpack('<i', struct.pack('<I', c.arg(k)))[0])
                k += 1
        out = (fmt % tuple(args)).encode('latin-1') + b'\0'
        self.write(buf, out)
        return len(out) - 1

    def r32(self, a):
        return struct.unpack('<I', self.read(a, 4))[0]

    def s32(self, a):
        return struct.unpack('<i', self.read(a, 4))[0]

    def w32(self, a, v):
        self.write(a, struct.pack('<I', v & 0xffffffff))

    def cstr(self, a):
        out = b''
        while self.read(a, 1) != b'\0':
            out += self.read(a, 1)
            a += 1
        return out.decode('latin-1')


def run_exe(s):
    cpu = PmCpu()
    cpu.seed = s.seed & 0xffffffff
    events = []
    rec = {'text1': None, 'updates': []}
    vt_cache = {}

    def vtable(kind):
        if kind in vt_cache:
            return vt_cache[kind]
        vt = cpu.alloc(0x100)

        def slot(off, fn, pop):
            code = cpu.code(b'\xc3')
            cpu.stub_function(code, fn, pop)
            cpu.w32(vt + off, code)

        def set_pos(c):
            cpu.w32(c.ecx + 0x14, c.arg(0))
            cpu.w32(c.ecx + 0x18, c.arg(1))
            return 0
        slot(0x0c, set_pos, 8)
        slot(0x68, lambda c: cpu.r32(c.ecx + 0x14), 0)
        slot(0x6c, lambda c: cpu.r32(c.ecx + 0x18), 0)
        slot(0x60, lambda c: cpu.w32(c.ecx + 0xc, cpu.r32(c.ecx + 0xc) & ~HIDDEN) or 0, 4)
        slot(0x64, lambda c: cpu.w32(c.ecx + 0xc, cpu.r32(c.ecx + 0xc) | HIDDEN) or 0, 0)
        slot(0x24, lambda c: rec['updates'].append((kind, c.ecx)) or 0, 4)
        slot(0x70, lambda c: 0, 4)

        def set_text(c):  # cdecl, the object first, the text as its format
            rec['text1'] = cpu.cstr(c.arg(1))
            return 0
        slot(0x7c, set_text, 0)
        slot(0x20, lambda c: 0, 0)

        def fill(c):  # cdecl: the object, the message
            events.append([2, c.arg(1), 0, 0, 0])
            return 0
        slot(0x98, fill, 0)
        vt_cache[kind] = vt
        return vt
    R = cpu.alloc(0xe00)
    H, M = cpu.alloc(0x23200), cpu.alloc(0x800)
    for off, kind in ((0, 'radar'), (0x1a4, 'text'), (0x65c, 'text'), (0xb14, 'light'), (0xb4, 'line'),
                      (0x104, 'line'), (0x154, 'line'), (0xbec, 'poly'), (0xcd8, 'poly')):
        cpu.w32(R + off, vtable(kind))
    cpu.w32(R + 0x0c, HIDDEN if s.hidden else 0)
    cpu.w32(R + 0x65c + 0xc, HIDDEN if s.text2_hidden else 0)
    for off, (px, py) in zip((0x1a4, 0x65c, 0xb14), s.start):
        cpu.w32(R + off + 0x14, px)
        cpu.w32(R + off + 0x18, py)
    cpu.write(R + 0x5a, struct.pack('<H', s.zoomed))
    cpu.write(R + 0x5c, bytes([s.active, s.changed]))
    for i in range(2):
        cpu.w32(R + 0x60 + 4 * i, s.cx[i])
        cpu.w32(R + 0x68 + 4 * i, s.cy[i])
        cpu.w32(R + 0x70 + 4 * i, s.r[i])
    for off, v in ((0x78, s.to_r), (0x7c, s.to_x), (0x80, s.to_y), (0x84, s.r_steps), (0x88, s.x_steps),
                   (0x8c, s.y_steps), (0x34, s.radius), (0x38, s.radius * s.radius), (0x14, s.x), (0x18, s.y)):
        cpu.w32(R + off, v)
    cpu.write(R + 0x54, struct.pack('<f', s.noise))
    cpu.write(R + 0x90, struct.pack('<3f', *s.dir))
    cpu.write(R + 0xac, struct.pack('<2f', s.cos_half, s.sin_half))
    imgs = cpu.alloc(16)
    for i in range(3):
        cpu.w32(imgs + 4 * i, 0x1000 + i)
    cpu.w32(R + 0xb14 + 0xbc, imgs)
    cpu.w32(R + 0xb14 + 0xc4, s.light_current & 0xffffffff)
    cpu.w32(R + 0xdcc, H)
    cpu.w32(H + 4, s.hud_active)
    cpu.w32(H + 0x10, M)
    cpu.write(M + 0x9c, struct.pack('<f', s.range))
    cpu.write(M + 0xbc, bytes([s.bc]))
    cpu.write(M + 0x48, struct.pack('<4f', *s.q))
    cpu.w32(M + 0xc4, 1)
    cpu.w32(M + 200, 2)
    cpu.w32(M + 0x3d4, s.c3)
    # The light's image, for its test against the map's rect (0 x 0).
    cpu.w32(R + 0xb14 + 0x3c, cpu.alloc(0x40))
    # The contacts: a pointer each at the mech's +0xac; each one's vehicle, team,
    # blip and mech. The distance is read by the real 0x4f8dc0.
    positions = {M: list(s.me)}
    vehicles = {}

    def blip_obj(index):
        b, vt = cpu.alloc(0x40), cpu.alloc(0x40)

        def place(c):
            events.append([3, index, struct.unpack('<i', struct.pack('<I', c.arg(0)))[0],
                           struct.unpack('<i', struct.pack('<I', c.arg(1)))[0], 0])
            return 0
        for off, fn, pop in ((0x0c, place, 8), (4, lambda c: events.append([4, index, 0, 0, 0]) or 0, 0)):
            code = cpu.code(b'\xc3')
            cpu.stub_function(code, fn, pop)
            cpu.w32(vt + off, code)
        cpu.w32(b, vt)
        return b
    vec = cpu.alloc(4 * max(s.n, 1))
    pointers = []
    for i in range(s.n):
        k = s.contacts[i]
        c, v, vt, team = cpu.alloc(0x40), cpu.alloc(0x40), cpu.alloc(0x80), cpu.alloc(0x100)
        cpu.w32(c, 0x7000 + i)
        cpu.w32(c + 8, 0x8000 + i)
        cpu.write(c + 0x2c, bytes([k.on_radar]))
        cpu.write(c + 0x1c, struct.pack('<f', k.dist))
        cpu.w32(team + 0xc0, k.team)
        blip = blip_obj(i) if k.blip else 0
        ally = 0
        if k.has_mech:
            ally = cpu.alloc(0x800)
            cpu.w32(ally + 0x3d8, k.slave)
            cpu.write(ally + 0x9c, struct.pack('<f', k.sensor))
        for off, val in ((0x28, team), (0x44, blip), (0x58, ally)):
            code = cpu.code(b'\xc3')
            cpu.stub_function(code, lambda c, val=val: val, 0)
            cpu.w32(vt + off, code)
        cpu.w32(v, vt)
        vehicles[c] = v
        positions[v] = list(k.pos)
        cpu.w32(vec + 4 * i, c)
        pointers.append(c)
    cpu.w32(M + 0xac, vec)
    cpu.w32(M + 0xb0, vec + 4 * s.n)
    cpu.stub_function(0x4f8d70, lambda c: vehicles[c.ecx], 0)
    if s.target >= 0:
        cell = cpu.alloc(8)
        cpu.w32(cell, pointers[s.target])
        cpu.w32(H + 0x657c, cell)
    # The selected target's triangle, made for the frame.
    tri = {}
    cpu.stub_function(0x5c4220, lambda c: 0, 0)
    screen = cpu.alloc(0x20)
    cpu.stub_function(0x598990, lambda c: screen, 0)

    def tri_pos(c):
        tri['at'] = [struct.unpack('<i', struct.pack('<I', c.arg(i)))[0] for i in range(2)]
        return 0
    cpu.stub_function(0x5c4580, tri_pos, 8)
    cpu.stub_function(0x5c45e0, lambda c: 0, 4)
    cpu.stub_function(0x5c4600, lambda c: events.append([1, *tri['at'], 0, cpu.s32(c.ecx + 0xe0)]) or 0, 4)
    cpu.stub_function(0x5983e0, lambda c: probe.probe_pm_pack(c.arg(0), c.arg(1), c.arg(2)) & 0xffffffff, 0)
    # The map.
    MAP = 0
    if s.active:
        MAP, mvt, mrect, mimg = cpu.alloc(0x100), cpu.alloc(0x40), cpu.alloc(16), cpu.alloc(0x20)
        code = cpu.code(b'\xc3')
        cpu.stub_function(code, lambda c: 0, 4)
        cpu.w32(mvt + 0x24, code)
        cpu.w32(MAP, mvt)
        cpu.write(mrect, struct.pack('<4i', *s.map_rect))
        cpu.w32(MAP + 0xd8, mrect)
        cpu.write(MAP + 0xd4, struct.pack('<f', s.map_scale))
        cpu.w32(MAP + 0x3c, mimg)
        cpu.write(mimg + 6, struct.pack('<h', s.map_h))

        def map_point(c):
            x, _, z = struct.unpack('<3f', cpu.read(c.arg(0), 12))
            cpu.write(c.arg(1), struct.pack('<2i', int(x * 0.1) + 100, int(z * 0.1) + 200))
            return 1
        cpu.stub_function(0x459870, map_point, 8)
    # The network player's marker.
    if s.marker:
        E, evt, epos, name = cpu.alloc(0x40), cpu.alloc(0x40), cpu.alloc(12), cpu.alloc(8)
        cpu.write(epos, struct.pack('<3f', *s.marker_pos))
        code = cpu.code(b'\xc3')
        cpu.stub_function(code, lambda c: epos, 0)
        cpu.w32(evt + 0x18, code)
        cpu.w32(E, evt)
        cpu.w32(E + 0x28, name)
        cpu.w32(0x62c99c, blip_obj(100))
    # The ring, made already (its guard set) with a recording vtable.
    cpu.w32(RING_GUARD, cpu.r32(RING_GUARD) | 1)
    cpu.w32(RING, vtable('ring'))
    lines = {}

    def line_set(c):  # 0x5c8410 (ECX line, x0, y0, x1, y1)
        lines[c.ecx] = [c.arg(0) - (1 << 32) if c.arg(0) >> 31 else c.arg(0) for _ in range(1)] and \
            [struct.unpack('<i', struct.pack('<I', c.arg(i)))[0] for i in range(4)]
        return 0
    cpu.stub_function(0x5c8410, line_set, 16)
    cpu.stub_function(0x495070, lambda c: MAP, 0)

    def place_of(c):
        a = cpu.alloc(12)
        cpu.write(a, struct.pack('<3f', *positions[c.ecx]))
        return a
    cpu.stub_function(0x5cb260, place_of, 0)
    cpu.stub_function(0x5c01c0, lambda c: cpu.w32(c.ecx + 0xc, cpu.r32(c.ecx + 0xc) | HIDDEN) or 0, 0)

    def base_update(c):
        if c.ecx == RING:
            events.append([0, cpu.s32(RING + 0x14), cpu.s32(RING + 0x18), cpu.s32(RING + 0x34), cpu.s32(RING + 0x3c)])
        return 0
    cpu.stub_function(0x5c0220, base_update, 4)
    cpu.stub_function(0x5bba50, lambda c: 0, 4)
    cpu.stub_function(0x5acad0, lambda c: E if s.marker else 0, 4)  # the network player's (the session's +0x20)
    cpu.w32(0x62daf0, cpu.alloc(0x40))
    shots = cpu.alloc(16)
    cpu.w32(shots, shots)
    cpu.w32(shots + 4, shots)
    cpu.w32(0x62f82c, shots)  # the shots' list, empty
    cpu.w32(0x628b54, 0)
    cpu.w32(0x628b58, 0)  # the mines, none
    cpu.call(RADAR, ecx=R, stack_args=(fb(s.dt),))
    sx = lambda a: struct.unpack('<i', cpu.read(a, 4))[0]  # noqa: E731
    out = dict(radius=sx(R + 0x34), radius_sq=sx(R + 0x38), x=sx(R + 0x14), y=sx(R + 0x18),
               text1_x=sx(R + 0x1a4 + 0x14), text1_y=sx(R + 0x1a4 + 0x18), text2_x=sx(R + 0x65c + 0x14),
               text2_y=sx(R + 0x65c + 0x18), text2_shown=0 if cpu.r32(R + 0x65c + 0xc) & HIDDEN else 1,
               light_x=sx(R + 0xb14 + 0x14), light_y=sx(R + 0xb14 + 0x18), light_current=sx(R + 0xb14 + 0xc4),
               lines=[lines.get(R + 0xb4, [0, 0, 0, 0]), lines.get(R + 0x104, [0, 0, 0, 0])], changed=cpu.read(R + 0x5d, 1)[0],
               hidden=1 if cpu.r32(R + 0xc) & HIDDEN else 0, text1=rec['text1'] or '', events=events)
    return out


def run_port(s):
    o = RadarOut()
    probe.probe_pm_radar_run(ctypes.byref(s), ctypes.byref(o))
    return dict(radius=o.radius, radius_sq=o.radius_sq, x=o.x, y=o.y, text1_x=o.text1_x, text1_y=o.text1_y,
                text2_x=o.text2_x, text2_y=o.text2_y, text2_shown=o.text2_shown, light_x=o.light_x,
                light_y=o.light_y, light_current=o.light_current, lines=[list(o.lines[0]), list(o.lines[1])],
                changed=o.changed, hidden=o.hidden, text1=o.text1.decode('latin-1'),
                events=[list(o.events[i]) for i in range(o.n_events)])


def made_up(r):
    s = RadarIn()
    s.changed = int(r.random() < 0.4)
    s.zoomed = r.choice([0, 1])
    for i in range(2):
        s.cx[i], s.cy[i], s.r[i] = r.randint(40, 600), r.randint(40, 440), r.randint(20, 120)
    s.radius = r.randint(0, 120) if r.random() < 0.9 else 0
    s.to_r = r.choice([s.radius, r.randint(0, 120)])
    s.x, s.y = r.randint(40, 600), r.randint(40, 440)
    s.to_x, s.to_y = r.choice([s.x, r.randint(40, 600)]), r.choice([s.y, r.randint(40, 440)])
    s.r_steps, s.x_steps, s.y_steps = r.randint(1, 20), r.randint(1, 30), r.randint(1, 30)
    s.range = r.choice([500.0, 1000.0, 2000.0, r.uniform(100, 3000)])
    s.bc = r.randrange(4)
    s.light_current = r.choice([-1, 0, 1, 2])
    s.hud_active = int(r.random() < 0.9)
    s.hidden = int(r.random() < 0.1)
    s.noise = 0.0
    a = r.uniform(-math.pi, math.pi)
    s.dir[:] = [math.sin(a), 0.0, -math.cos(a)]
    h = r.uniform(0.3, 0.8)
    s.cos_half, s.sin_half = math.cos(h), math.sin(h)
    s.dt = r.choice([1 / 30, 1 / 60, 0.1])
    for i in range(3):
        s.start[i][0], s.start[i][1] = r.randint(0, 640), r.randint(0, 480)
    s.active = int(r.random() < 0.5)
    s.map_h = r.randint(64, 256)
    x, y = r.randint(0, 200), r.randint(0, 200)
    s.map_rect[:] = [x, y, x + r.randint(100, 400), y + r.randint(100, 280)]
    s.map_scale = r.uniform(0.02, 0.3)
    s.me[:] = [r.uniform(-2000, 2000), r.uniform(0, 100), r.uniform(-2000, 2000)]
    a = r.uniform(-math.pi, math.pi) / 2
    s.q[:] = [math.cos(a), 0.0, math.sin(a), 0.0]
    s.c3 = r.choice([0, 1, 1, 2])
    s.text2_hidden = int(r.random() < 0.4)
    s.seed = r.getrandbits(31)
    s.noise = r.choice([0.0, 0.0, r.uniform(0, 0.5)])
    s.marker = int(r.random() < 0.4)
    s.marker_pos[:] = [r.uniform(-3000, 3000), 0.0, r.uniform(-3000, 3000)]
    s.n = r.randint(0, 8)
    for i in range(s.n):
        k = s.contacts[i]
        k.team = r.choice([1, 1, 2, 4, 3])
        k.on_radar = int(r.random() < 0.8)
        k.blip = int(r.random() < 0.9)
        k.has_mech = int(r.random() < 0.8)
        k.slave = r.choice([0, 1, 1])
        k.sensor = r.uniform(100, 1500)
        k.pos[:] = [r.uniform(-3000, 3000), r.uniform(0, 100), r.uniform(-3000, 3000)]
        k.dist = r.uniform(0, 3000)
    s.target = r.randrange(s.n) if s.n and r.random() < 0.5 else -1
    return s


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    r = random.Random(1999)
    seen = [0, 0, 0, 0]  # rings, triangles, texts filled, blips
    for k in range(runs):
        s = made_up(r)
        e, p = run_exe(s), run_port(s)
        for ev in e['events']:
            seen[min(ev[0], 3)] += 1
        if e != p:
            print(f'run {k}: differs')
            for key in e:
                if e[key] != p[key]:
                    print(f'  {key}: exe {e[key]}  port {p[key]}')
            sys.exit(1)
    print(f'ok: {runs} radars ({seen[0]} C3 rings, {seen[1]} triangles, {seen[2]} marker texts, {seen[3]} blip calls): '
          'every place, text, light, line, flag, blip, ring and triangle identical', flush=True)


if __name__ == '__main__':
    main()
