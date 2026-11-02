"""The game's app and states against the original (docs/game-loop.md).

Each case runs one function of the app (0x0047f7a0.., InitInstance 0x00480000,
the start and stop of the game, the main window and the command line) or of a
game state (Attract, Intro, Exit, Pre Lab, Core with its frame and three
drawing paths, the key list, the menus' shell and the Main, Pause, Load, Win
and Salvage states) in the emulator, with every call out stubbed (app_world.py),
then the rebuild's port with the original's answers replayed (calltrace.Replay).
Compared: the call traces, the state changes queued, the result, the ported
fields, and every byte either side wrote into the world.

    cmake --build build && .venv/bin/python tests/differential/test_app_states.py [filter]
"""
import ctypes
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from app_world import APP, RENDERERS, STATES, VTABLES, World, fbits  # noqa: E402
from calltrace import Replay, Trace, compare, load_probe, rnd  # noqa: E402

# Keys answered with booleans (biased) or small numbers; the rest get 0..3.
BOOLS = {'0x00599710', '0x0051dc10', '0x0051ddb0', '0x00586ce0', '0x005899b0', '0x00480340',
         '0x00575e30', '0x00589a50', 'AfxRegisterClass', '0x0040c420', '0x00597d80',
         '0x0040a820', '0x0040a830', '0x00575f30', '[0x0072f390]', '[0x0072f38c]',
         '0x00579c60', '0x00564370', '0x004ac150', '0x0040d4b0', '0x0040d4d0'}


def make_script(seed, w):
    def answer(key):
        def f(n, a):
            r = rnd(seed, f'{key}#{n}')
            if key in w.answers:
                v = w.answers[key]
                return v(r, n, a) if callable(v) else v
            if key in BOOLS:
                return 1 if r.random() < 0.7 else 0
            if key.startswith('0x005') and key in ('0x005700f0',):
                return r.choice([0, 1, 2])
            return r.randrange(4)
        return f
    return answer


class Script(dict):
    """A script that answers every key (calltrace.Trace looks keys up)."""

    def __init__(self, f):
        super().__init__()
        self.f = f

    def get(self, key, default=None):
        return self.f(key)

    def items(self):
        return []


# The heap menu objects (menu_states.h): made first in every world, so at the
# same addresses each time; their vtables, so internal calls run.
MENU_VTABLES = {'options': 0x5bf7a8, 'gfx': 0x5c0040, 'controls': 0x5be3d8, 'credits': 0x5bd938}
MENU = {}


def build_world(seed, t, case):
    w = World(t, seed)
    e = w.emu
    for k, vt in MENU_VTABLES.items():
        o = e.world_alloc(0x10)
        e.w32(o, vt)
        assert MENU.setdefault(k, o) == o
    r = rnd(seed, 'setup')
    w.answers = {}
    w.command_line = r.choice(['mech3.exe', 'mech3.exe -nofmv', 'mech3.exe -cd7 -seasick',
                               'mech3.exe -cd', 'mech3.exe   -nofmvx -cd12abc', 'x -seasic -cd3',
                               'mech3.exe -cd2 -cam', 'mech3.exe -c9'])
    # The app and the states: their real vtables, so internal virtual calls run.
    e.w32(APP, 0x5c3f80)
    for addr, vt in VTABLES.items():
        e.w32(addr, vt)
    # Globals of other modules.
    e.wf(0x72c52c, r.choice([0.0, 0.016, 0.05, 0.125, 0.3]))
    e.w32(0x72f348, r.randrange(2))
    e.w32(0x608d5c, 1 if r.random() < 0.2 else 0)
    for a in (0x609926, 0x609994, 0x60b56c):
        e.w8(a, 1 if r.random() < 0.3 else 0)
    w.dv = w.obj({0x0: 1, 0x4: 0})
    menu = {0x0: 1}
    for g in (0x609858, 0x60985c, 0x609860, 0x609864, 0x609868, 0x60986c, 0x609870, 0x609874,
              0x609878):
        e.w32(g, w.obj(menu) if r.random() < 0.8 else 0)
    e.w32(0x609854, w.obj())
    e.w32(0x5f4c80, w.obj({0x8: 1}) if r.random() < 0.7 else 0)
    e.w32(0x608620, w.obj() if r.random() < 0.5 else 0)
    for g, v in ((0x5f4d10, r.randrange(3, 10)), (0x5f4d2c, fbits(r.uniform(0.5, 2))),
                 (0x5f4d30, fbits(r.uniform(0.5, 2)))):
        ent = w.obj()
        e.w32(ent, v)
        e.w32(g, ent)
    # HUD text lines (cdecl vt+0x7c).
    vt = e.world_alloc(0x100)
    e.w32(vt + 0x7c, e.new_stub('vt+0x7c', 's0,zs1', 0))
    for i in range(0x19):
        e.w32(0x5fc848 + i * 0x4b0, vt)
    # Pilot, game, mech, view, vehicle.
    game = w.obj({0x2c: 0, 0x34: 1, 0x38: 0, 0x3c: 0, 0x48: 0, 0x4c: 0, 0x50: 1})
    cfg = w.obj(fields={0x4: r.randrange(3), 0x8: 0x77})
    torso = w.obj()
    vdata = w.obj(fields={0x228: w.obj(fields={0x4: torso}), 0xc: r.randrange(3)})
    mech = w.obj(fields={0x684: vdata, 0xd4: w.obj()})
    pilot = w.obj(fields={0x14: mech if case not in ('core enter', 'stop_game') or r.random() < 0.8 else 0,
                          0x6c: game})
    name = r.choice(['DefaultPlayer', 'Default Player', 'Lance'])
    e.cpu.write(pilot + 0x18, name.encode() + b'\0')
    e.w8(pilot + 0x59, 1 if r.random() < 0.3 else 0)
    no_pilot_ok = case in ('pick_post_intro', 'stop_game', 'init_instance')
    e.w32(0x60861c, pilot if not no_pilot_ok or r.random() < 0.8 else 0)
    w.answers['@pilot_is_default'] = 1 if name == 'DefaultPlayer' else 0
    veh = w.obj(fields={0x4: w.obj(), 0x684: vdata, 0x394: w.obj(fields={0x8: w.obj()}),
                        0x398: w.obj(fields={0x8: w.obj()})})
    target = w.obj({0x50: 0, 0xe4: 1})
    camobj = w.obj(fields={0x4: r.randrange(2)})
    wdata = w.obj(fields={0x4: w.obj()})
    view = w.obj({0x4: 0, 0x8: 1}, fields={0x4: w.obj(fields={0x38: w.obj()}), 0x8: wdata,
                                           0x14: w.obj(), 0x58: camobj if r.random() < 0.8 else 0,
                                           0x64: target})
    e.w32(0x5f4c8c, view)
    ai = w.obj()
    e.w32(ai + 0x144c, w.vtable({0x28: 1}))
    rects = [w.obj(fields={0: r.randrange(50), 4: r.randrange(50), 8: r.randrange(300, 700),
                           0xc: r.randrange(200, 500)}) for _ in range(3)]
    w.answers.update({
        'vt+0x50': lambda rr, n, a: veh if rr.random() < 0.8 else 0,
        'vt+0x34': cfg, 'vt+0x2c': lambda rr, n, a: rr.choice([0, 1, 1, 2, 3, 4]),
        'vt+0x38': lambda rr, n, a: rr.randrange(4), 'vt+0x48': lambda rr, n, a: rr.randrange(1, 5),
        'vt+0x4c': lambda rr, n, a: rr.randrange(1, 7), 'vt+0x18': lambda rr, n, a: rr.randrange(2),
        '0x00430aa0': ai, '0x004788b0': pilot, '0x0040a0f0': game, '0x00479440': w.obj(),
        '0x0040d6a0': rects[0], '0x0040d750': rects[1], '0x0040d7b0': rects[2],
        '0x0051a9b0': lambda rr, n, a: rr.choice([0, 0, 0, 1, 2, 3]),
        '0x00573040': lambda rr, n, a: rr.choice([0, 1, 2, 4, 5, 6, 12]),
        'rand': lambda rr, n, a: rr.randrange(0x8000),
        '0x0040d9d0': lambda rr, n, a: rr.choice([3, 4, 5, 7]),
        '0x0040cc10': lambda rr, n, a: rr.choice([0, 1, 999, -1 & 0xffffffff]),
        '0x004eaab0': lambda rr, n, a: rr.choice([2, -1 & 0xffffffff]),
        '@video_total': lambda rr, n, a: rr.choice([2, 3, 8]) << 20,
        '0x00598d80': lambda rr, n, a: rr.choice([0, -1 & 0xffffffff]),
        '0x005700f0': lambda rr, n, a: rr.choice([0, 1, 2]),
        '0x0056f6f0': lambda rr, n, a: rr.choice([0, w.obj()]),
        '@cursor_x': 17, '@cursor_y': 23,
        '0x0040ce90': lambda rr, n, a: rr.choice([0, 1]),
        '0x00575f30': 1,  # Mech3Msg.dll loads (the rebuild has no other path, FIX (A))
    })
    if case in ('screen mode', 'vmode toggle', 'cockpit refit'):
        # zEntity's list: each entry's object (vt+0x2c) and its mech (vt+0x50).
        n = r.randrange(4)
        ents = [w.obj({0x2c: 0}) for _ in range(n)]
        begin = e.world_alloc(4 * max(n, 1))
        for i, o in enumerate(ents):
            e.w32(begin + 4 * i, o)
        e.w32(0x73655c, begin)
        e.w32(0x736560, begin + 4 * n)
        seen = w.obj({0x50: 0})
        w.answers['vt+0x2c'] = seen
        w.answers['0x00576c80'] = lambda rr, n_, a: rr.choice([0, 0, 0, 1])
        w.answers['0x00579500'] = lambda rr, n_, a: rr.choice([w.obj(), 0])
        w.answers['0x00430aa0'] = lambda rr, n_, a: rr.choice([ai, ai, 0])
        w.answers['0x0040d9d0'] = lambda rr, n_, a: rr.choice([3, 4, 5, 6, 7, 11])
        w.mech_arg = r.choice([target, target, veh, 0x77000])
    w.renderers = []
    for vt_ in RENDERERS:
        o = e.world_alloc(16)
        e.w32(o, vt_)
        w.renderers.append(o)
    return w


def field_values(probe, seed):
    """Random values for the ported fields, by what each holds."""
    r = rnd(seed, 'fields')
    n = probe.probe_states_field_count()
    out = []
    for i in range(n):
        a, s = ctypes.c_uint32(), ctypes.c_int32()
        probe.probe_states_field_info(i, ctypes.byref(a), ctypes.byref(s))
        addr, size = a.value, s.value
        if addr == APP + 0xc4:  # the first state: the queue calls into it
            v = r.choice(list(STATES.values()))
        elif addr == APP + 0x1dc:
            v = r.choice(list(STATES.values()) + [0])
        elif addr == 0x608ec0 + 0x1c:
            v = r.choice(list(RENDERERS))
        elif addr == 0x608ec0 + 0x20 or addr in (0x608f00 + 0xc, 0x608f48 + 0xc, 0x608f18 + 0xc,
                                                  0x608e70 + 0xc) or addr - 0xc in MENU.values():
            v = 0x1234  # a mission's message box, the menus' screens: always there
        elif addr in (0x608ec0 + 0x14, 0x608f00 + 0x14, APP + 0x1cc, APP + 0x1d0, APP + 0x1d4):
            v = fbits(r.choice([0.0, 1.0, 0.04, 30.0, -0.5]))
        elif addr == APP + 0x190:  # the input mode: 8 in 0xc makes the cursor follow
            v = r.choice([0, 1, 5, 8, 9, 0xc])
        elif LAB <= addr < LAB + 0x94:
            v = lab_field(r, addr - LAB, size)
        elif size == 1:
            v = 1 if r.random() < 0.4 else 0
        else:
            plain = addr in (APP + 0x6c, APP + 0x190, 0x608f78, 0x608f80, 0x608f00 + 0x10) or \
                (addr - 0x8) in STATES.values() or addr in (0x608ec0 + 0x8, 0x608ec0 + 0xc, 0x608ec0 + 0x10)
            v = r.choice([0, 0, 1, 5, 0x1234] if plain else [0, 0x1234, 0x1234])
        out.append((i, addr, size, v))
    return out


def place_fields(w, probe, values, with_handles):
    """Writes the values into both; handle fields get world objects."""
    e = w.emu
    frame = w.obj(fields={0x20: 0x99})
    for i, addr, size, v in values:
        if addr == APP + 0x20 and with_handles:
            v = frame
        if addr == 0x608f74 and v:
            v = w.dv
        if addr == LAB + 0x84 and v == PILOT:
            v = w.emu.r32(0x60861c)
        if v == 0x1234 and size == 4 and with_handles:
            v = w.make('handle')
        if addr == 0x608ec0 + 0x1c:
            ev = w.renderers[RENDERERS.index(v)] if v else 0
            e.w32(addr, ev)
        elif size == 1:
            e.w8(addr, v)
        else:
            e.w32(addr, v)
        probe.probe_states_field_set(i, v)


def read_fields(w, probe, values):
    e = w.emu
    a, b = [], []
    for i, addr, size, _ in values:
        if addr == 0x608ec0 + 0x1c:
            p = e.r32(addr)
            ev = e.r32(p) if p else 0
        else:
            ev = e.r8(addr) if size == 1 else e.r32(addr)
        a.append((addr, ev))
        b.append((addr, probe.probe_states_field_get(i)))
    return a, b


# The Lab State (0x006059f8, mech_lab.cpp): its fields by offset; PILOT stands
# for the world's pilot.
LAB = 0x6059f8
PILOT = 0x5151


def lab_field(r, off, size):
    if size == 1:
        return 1 if r.random() < 0.4 else 0
    if off == 0x30:
        return r.choice([-1, 0, 1, 2, 3]) & 0xffffffff
    if off == 0x34:
        return r.randrange(5)
    if off in (0x60, 0x08, 0x0c):
        return 0x1234  # the screen: the update and draw use it unchecked
    if off == 0x64:
        return r.randrange(3)
    if off == 0x84:
        return r.choice([0, PILOT, PILOT])
    if off in (0x6c, 0x90):
        return r.choice([0, 0, 1])
    return r.choice([0, 0x1234])


def lab_world(w, probe, name, values, seed):
    """The lab's 3D mechs (+0x20, each +0x08 its mech), the mech to show
    (+0x38), the game's slot and the answers its developer keys get."""
    r = rnd(seed, 'lab')
    e = w.emu
    n = r.randrange(4)
    parts = [w.obj(fields={0x8: r.randrange(19)}) for _ in range(n)]
    begin = e.world_alloc(4 * n) if n else 0
    for i, p in enumerate(parts):
        e.w32(begin + 4 * i, p)
    for off, v in ((0x20, begin), (0x24, begin + 4 * n), (0x28, begin + 4 * n)):
        e.w32(LAB + off, v)
    probe.probe_states_lab_parts((ctypes.c_uint32 * max(n, 1))(*parts), n)
    node = r.choice([LabNoMech, LabNoMech, r.randrange(19)] + [e.r32(p + 8) for p in parts])
    for i, addr, size, _ in values:
        if addr == LAB + 0x38:
            e.w32(addr, node)
            probe.probe_states_field_set(i, node)
        if addr == LAB + 0x84 and name not in ('lab open', 'lab enter') and not e.r32(addr):
            e.w32(addr, e.r32(0x60861c))  # the update, leave and ACCEPT run with one
            probe.probe_states_field_set(i, e.r32(0x60861c))
    # The slot (vt+0x34): always one, its loadout (+0x08) or none; the loadout's
    # mech (+0x14). The read loadout or its mech's list stays empty: the add
    # after it (0x00448c1d..) is not ported yet (mechlab.md section 5).
    config = r.choice([0, w.obj(fields={0x14: r.randrange(19)})])
    slot = w.obj(fields={0x8: config, 0xc: 5})
    w.answers['vt+0x34'] = slot
    w.answers['vt+0x3c'] = lambda rr, n_, a: rr.randrange(5)
    mode = r.randrange(3)
    w.answers['0x00426300'] = 0 if mode == 0 else w.obj()
    w.answers['0x004846a0'] = 0 if mode == 1 else w.obj(fields={0x20: 0 if mode == 2 else w.obj()})
    w.answers['0x0051a9b0'] = lambda rr, n_, a: rr.choice([0, 0, 1, 1, 2])


LabNoMech = 0x13


# (name, probe address, emulator address, ecx, args builder) — args builder
# returns (probe arg, probe arg2, emulator stack args, edx).
def stack1(r):
    v = r.randrange(8)
    return v, 0, (v,), 0


def none(r):
    return 0, 0, (), 0


CASES = [
    ('constructor', 0x47f7a0, 0x47f7a0, APP, none),
    ('set_defaults', 0x47fb80, 0x47fb80, APP, none),
    ('pick_post_intro', 0x47fed0, 0x47fed0, APP, lambda r: (lambda v: (v, 0, (v,), 0))(r.randrange(2))),
    ('restore_video', 0x47fe10, 0x47fe10, APP,
     lambda r: (lambda x, y: (x, y, (x, y), 0))(r.randrange(2), r.randrange(2))),
    ('state picker', 0x482970, 0x482970, APP, lambda r: (lambda v: (v, 0, (v,), 0))(r.randrange(14))),
    ('confirm_quit', 0x482a20, 0x482a20, APP, none),
    ('restart_in_lab', 0x47ff60, 0x47ff60, APP, none),
    ('start_game', 0x480570, 0x480570, APP, lambda r: (0x55, 0, (0x55,), 0)),
    ('open_video', 0x480970, 0x480970, APP, lambda r: (0x55, 0, (0x55,), 0)),
    ('stop_game', 0x4808a0, 0x4808a0, APP, none),
    ('exit_instance', 0x47fa10, 0x47fa10, APP, none),
    ('main window', 0x482d10, 0x47ff90, APP, none),
    ('init_instance', 0x480000, 0x480000, APP, none),
    ('attract enter', 0x482330, 0x482330, STATES[0], none),
    ('attract update', 0x482430, 0x482430, STATES[0], none),
    ('attract leave', 0x482470, 0x482470, STATES[0], none),
    ('intro enter', 0x482070, 0x482070, STATES[1], none),
    ('intro update', 0x482170, 0x482170, STATES[1], none),
    ('intro leave', 0x4822d0, 0x4822d0, STATES[1], none),
    ('exit enter', 0x482060, 0x482060, STATES[4], none),
    ('pre lab enter', 0x480b30, 0x480b30, STATES[7], none),
    ('pre lab update', 0x480b60, 0x480b60, STATES[7], none),
    ('core activate', 0x481280, 0x481280, STATES[5], stack1),
    ('core enter', 0x480c70, 0x480c70, STATES[5], none),
    ('core update', 0x481f70, 0x481f70, STATES[5], none),
    ('core frame', 0x481e70, 0x481e70, STATES[5], none),
    ('core leave', 0x481fc0, 0x481fc0, STATES[5], none),
    ('core suspend', 0x481250, 0x481250, STATES[5], stack1),
    ('core resume', 0x481170, 0x481170, STATES[5], lambda r: (lambda v: (v, 0, (v,), 0))(r.choice([4, 7, 1]))),
    ('core pick renderer', 0x480bf0, 0x480bf0, STATES[5], none),
    ('mission start', 0x480df0, 0x480df0, STATES[5], none),
    ('key list update', 0x482520, 0x482520, STATES[10], none),
    ('key list leave', 0x482550, 0x482550, STATES[10], none),
    ('shell enter', 0x48ebf0, 0x48ebf0, STATES[8], none),
    ('shell update', 0x48ed30, 0x48ed30, STATES[8], none),
    ('shell leave', 0x48ec40, 0x48ec40, STATES[8], none),
    ('shell suspend', 0x48ec80, 0x48ec80, STATES[8], lambda r: (lambda v: (v, 0, (v,), 0))(r.randrange(2))),
    ('shell resume', 0x48ecc0, 0x48ecc0, STATES[8], lambda r: (lambda v: (v, 0, (v,), 0))(r.randrange(3))),
    ('main enter', 0x486990, 0x486990, STATES[2], none),
    ('main update', 0x486d40, 0x486d40, STATES[2], none),
    ('main leave', 0x486c50, 0x486c50, STATES[2], none),
    ('main suspend', 0x48e400, 0x48e400, STATES[2], stack1),
    ('main resume', 0x486e80, 0x486e80, STATES[2], stack1),
    ('main escape', 0x486ec0, 0x486ec0, STATES[2], none),
    ('pause enter', 0x493040, 0x493040, STATES[3], none),
    ('pause leave', 0x4931e0, 0x4931e0, STATES[3], none),
    ('pause escape', 0x493250, 0x493250, STATES[3], none),
    ('load enter', 0x4091e0, 0x4091e0, STATES[6], none),
    ('load update', 0x409440, 0x409440, STATES[6], none),
    ('load leave', 0x4093e0, 0x4093e0, STATES[6], none),
    ('load resume', 0x4092e0, 0x4092e0, STATES[6], lambda r: (lambda v: (v, 0, (v,), 0))(r.choice([4, 1]))),
    ('win enter', 0x47ac20, 0x47ac20, STATES[9], none),
    ('win update', 0x47aed0, 0x47aed0, STATES[9], none),
    ('win leave', 0x47af20, 0x47af20, STATES[9], none),
    ('salvage enter', 0x47d310, 0x47d310, STATES[8], none),
    ('salvage leave', 0x47d390, 0x47d390, STATES[8], none),
    ('options enter', 0x438c90, 0x438c90, 'options', none),
    ('gfx enter', 0x42da50, 0x42da50, 'gfx', none),
    ('controls enter', 0x42da50, 0x42da50, 'controls', none),
    ('options resume', 0x42da70, 0x42da70, 'options', lambda r: (lambda v: (v, 0, (v,), 0))(r.randrange(3))),
    ('options escape', 0x438c80, 0x438c80, 'gfx', none),
    ('credits enter', 0x429600, 0x429600, 'credits', none),
    ('credits leave', 0x429670, 0x429670, 'credits', none),
    ('lab open', 0x448830, 0x448830, 'pilot', lambda r: (lambda p, st, f: (p, st, (f,), st))(
        r.choice([0, 0x7000, 0x7100]), r.choice([0, 0x7200]), r.choice([0, 1, 0x101]))),
    ('lab enter', 0x448960, 0x448960, LAB, none),
    ('lab update', 0x448a80, 0x448a80, LAB, none),
    ('lab leave', 0x448a20, 0x448a20, LAB, none),
    ('lab accept', 0x448870, 0x448870, LAB, none),
    ('lab part_valid', 0x448e20, 0x448e20, LAB, lambda r: (lambda v: (v, 0, (v,), 0))(
        r.choice([-1 & 0xffffffff, 0, 1, 2, 3, 0x80000000]))),
    ('lab part_index', 0x4507c0, 0x4507c0, LAB, none),
    ('lab make_screen', 0x448e70, 0x448e70, LAB, none),
    ('lab delete_screen', 0x448ef0, 0x448ef0, LAB, none),
    ('lab draw', 0x448f10, 0x448f10, LAB, lambda r: (lambda v: (v, 0, (v,), 0))(
        fbits(r.choice([0.0, 0.033, 0.5])))),
    # The screen mode switch (ECX the mode), Shift+V, a mech's cockpit refit
    # (ECX the mech: the view's target, another, or none; resolution.md step 3).
    ('screen mode', 0x4ab6b0, 0x4ab6b0, 'mode', lambda r: (lambda v: (v, 0, (), 0))(r.choice([3, 4, 5, 5, 6, 7, 11]))),
    ('vmode toggle', 0x4ab910, 0x4ab910, APP, none),
    ('cockpit refit', 0x41cdc0, 0x41cdc0, 'mech', none),
]
# Results compared as booleans (functions returning bool / BOOL / a byte).
BOOL_RESULT = {0x438c90, 0x42da50, 0x429600, 0x482a20, 0x480570, 0x480970, 0x480000, 0x482330, 0x482430, 0x482070,
               0x482170, 0x482060, 0x480b30, 0x480b60, 0x480c70, 0x481f70, 0x481e70,
               0x482520, 0x48ebf0, 0x48ed30, 0x486990, 0x486d40, 0x493040, 0x4091e0,
               0x409440, 0x47ac20, 0x47aed0, 0x47d310, 0x448830, 0x448960, 0x448a80,
               0x448870, 0x448e20}
IGNORE = [(0x608c88, 0x608f6c), (0x608f6c, 0x608fb0), (0x6040b0, 0x6040b4),
          (0x5f4c98, 0x5f4d50), (LAB, LAB + 0x94)]


def box_inside_2d(log, box):
    """The one departure, FIX (A) (playtest W44/W46, docs/fixes.md): the hardware
    path's frame updates the quit box (vt+0) inside its 2D pass with the HUD,
    before 0x00576d60 ends it; the exe's comes just after, when the port's 2D
    layer is already over the frame, so nothing drew it."""
    for i in range(len(log) - 1):
        if log[i] == ('0x00576d60', ()) and log[i + 1][0] == 'vt+0x0' and log[i + 1][1][:1] == (box,):
            log[i], log[i + 1] = log[i + 1], log[i]
            return


def run_case(probe, name, paddr, eaddr, ecx, args, seed, verbose=False):
    t = Trace()
    w = build_world(seed, t, name)
    t.script = Script(make_script(seed, w))
    probe.probe_states_reset()
    probe.probe_states_set_menus(*(MENU[k] for k in MENU_VTABLES))
    if ecx in ('pilot', 'mode', 'mech'):
        pass
    elif isinstance(ecx, str):
        ecx = MENU[ecx]
        probe.probe_states_this(ecx)
    probe.probe_states_set_command_line(w.command_line.encode())
    values = field_values(probe, seed)
    if paddr == 0x47f7a0:  # static objects start zeroed, before their constructors
        values = [(i, a, s, 0xffffffff if a == LAB + 0x30 else 0) for i, a, s, _ in values]  # (the lab's, 0x004488b0)
    place_fields(w, probe, values, paddr != 0x47f7a0)
    r = rnd(seed, 'args')
    parg, parg2, stack, edx = args(r)
    if name.startswith('lab'):
        lab_world(w, probe, name, values, seed)
    if paddr == 0x448830:  # ECX the pilot, EDX the store, the flag on the stack
        ecx = parg
        probe.probe_states_arg3(stack[0])
    if paddr == 0x4507c0:  # a mech of the entries, none (0x13) or another
        parg = w.emu.r32(LAB + 0x38)
        stack = (parg,)
    if paddr == 0x4803f0:
        pass
    if paddr == 0x4ab6b0:  # the port runs the switch: the emulator too
        del w.emu.slots[0x4ab6b0]
        ecx = parg
    if paddr == 0x41cdc0:
        del w.emu.slots[0x41cdc0]
        ecx = parg = w.mech_arg
    snap = w.emu.snapshot()
    w.emu.watch_writes([(w.emu.world_base, w.emu.world_base + w.emu.world_size),
                        (0x5e0000, 0x810000)], IGNORE + [(o, o + 0x10) for o in MENU.values()])
    if eaddr == 0x47f7a0:
        eret = w.call(eaddr, ecx=ecx)
    else:
        eret = w.call(eaddr, ecx=ecx, edx=edx, stack=stack)
    rep = Replay(t, w.emu, snap)
    rep.script = {k: make_script(seed, w)(k) for k in w.answers if k.startswith('@')}
    probe.probe_app_set_callback(rep.callback())
    pret = ctypes.c_uint32(0)
    exited = probe.probe_states_call(paddr, parg, parg2, ctypes.byref(pret))
    if paddr in (0x481f70, 0x481e70):
        box_inside_2d(t.log, w.emu.r32(STATES[5] + 0x20))
    ok = compare(f'{name} (seed {seed})', t.log, rep.log)
    q = (ctypes.c_uint32 * 48)()
    nq = probe.probe_states_queue(q, 16)
    pq = [(q[3 * i], q[3 * i + 1], q[3 * i + 2]) for i in range(nq)]
    if ok and pq != w.queue:
        print(f'{name} (seed {seed}): queued changes differ\n  original {w.queue}\n  rebuild  {pq}')
        ok = False
    if ok and bool(exited) != w.exited:
        print(f'{name} (seed {seed}): ExitProcess original {w.exited} rebuild {bool(exited)}')
        ok = False
    if ok and not w.exited:
        e = eret if eret is not None else 0
        p = pret.value
        if paddr in BOOL_RESULT:
            e, p = (e & 0xff) != 0, p != 0
        elif paddr in (0x482970, 0x4507c0, 0x4ab6b0):
            pass
        else:
            e = p = 0
        if e != p:
            print(f'{name} (seed {seed}): result original {e} rebuild {p}')
            ok = False
    if ok:
        fa, fb = read_fields(w, probe, values)
        diff = [(hex(x[0]), hex(x[1]), hex(y[1])) for x, y in zip(fa, fb) if x != y]
        if diff:
            print(f'{name} (seed {seed}): fields differ (address, original, rebuild): {diff}')
            ok = False
    if ok:
        e = w.emu
        bad = []
        for a in sorted(set(rep.shadow) | e.writes):
            ev = e.r8(a)
            if a not in rep.shadow:
                bad.append((hex(a), 'original only', ev))
            elif rep.shadow[a] != ev and a not in e.writes and ev == rep.read8(a):
                continue
            elif rep.shadow[a] != ev:
                bad.append((hex(a), ev, rep.shadow[a]))
        if bad:
            print(f'{name} (seed {seed}): world writes differ: {bad[:12]}')
            ok = False
    return ok, len(t.log)


def main():
    probe = load_probe()
    probe.probe_states_field_get.restype = ctypes.c_uint32
    probe.probe_states_call.restype = ctypes.c_int32
    flt = sys.argv[1] if len(sys.argv) > 1 else ''
    seeds = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    cases = calls = 0
    for name, paddr, eaddr, ecx, args in CASES:
        if flt and flt not in name:
            continue
        for seed in range(seeds):
            ok, n = run_case(probe, name, paddr, eaddr, ecx, args, seed)
            if not ok:
                raise SystemExit(1)
            cases += 1
            calls += n
    print(f'{cases} runs, {calls} calls: the rebuilt app and states match the original')


if __name__ == '__main__':
    main()
