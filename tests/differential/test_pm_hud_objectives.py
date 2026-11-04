"""Pirate's Moon's objectives box (HudObjectives::fill, src/mech3/hud.cpp on the
pm branch; docs/pm-map.md step 7) against PM's exe: the fill 0x4486e0 with its
per-objective lines 0x448520 and 0x448680, PM's own, in Unicorn.

    cmake --build build && .venv/bin/python tests/differential/test_pm_hud_objectives.py [runs]

Each run makes up a mission: the game type (1 to 5), its name and mission, a
description or none, the primary, secondary, final and failed groups with
their texts or none, and objectives done, failed (+0x09), visible or not,
with one to three lines of text; lost or not, a network game or not, with its
score lines. The exe's box and its scores column are zTwoD text boxes, stubbed
here by a model of what fill asks of them (a line added with the box's font,
the box's font set on every line, the last line's font set, cleared); the
port's are its real text boxes (twod_textbox.cpp, checked against the exe in
test_twod.py), read back after the fill. Compared: every line's text and font
in the box and in the scores column, and the panel's size in lines.

The texts are PM's Mech3Msg.dll's. One deliberate difference: PM formats
0x236 ("Mission %2!d! P R I M A R Y ...") with the mission alone, so its %2
reads what follows on the stack; the stub gives the mission there, the port's
FIX (bug).
"""
import ctypes
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from original import ROOT, Original  # noqa: E402
import msgtable  # noqa: E402

MSGS = msgtable.messages((ROOT / 'game' / 'pm' / 'Mech3Msg.dll').read_bytes())
probe = ctypes.CDLL(str(next((ROOT / 'build').glob('libpmprobe.*'))))
probe.probe_pm_obj_fill.restype = ctypes.c_char_p
probe.probe_pm_obj_fill.argtypes = [ctypes.c_char_p]
probe.probe_pm_obj_msg.argtypes = [ctypes.c_int, ctypes.c_char_p]
for i, t in MSGS.items():
    probe.probe_pm_obj_msg(i, t.encode('latin-1', 'replace'))

FILL = 0x4486e0
G_DESC, G_PRIMARY, G_SECONDARY, G_ABORT, G_GAME = 0x62dab8, 0x62dabc, 0x62dac0, 0x62da9a, 0x62c40c
STRCHR, STRDUP, FREE, STRTOK = 0x5de63c, 0x5de678, 0x5de65c, 0x5de608
FONT = {'title': 0x100, 'incomplete': 0x101, 'complete': 0x102, 'scores': 0x103}  # the port's font_find order
HEIGHT = 14


class PmCpu(Original):
    """PM's exe at its own addresses: no translation but for the import slots."""

    def __init__(self):
        super().__init__(exe='pm')
        self.tr = lambda a, size=1: a

    def r32(self, a):
        return struct.unpack('<I', self.read(a, 4))[0]

    def cstr(self, a):
        out = b''
        while True:
            c = self.read(a, 1)
            if c == b'\0':
                return out.decode('latin-1')
            out += c
            a += 1

    def put_str(self, s):
        b = s.encode('latin-1') + b'\0'
        a = self.alloc(len(b))
        self.write(a, b)
        return a


def fmt(text, args):
    """FormatMessageA's %N, %N!d! and %N!s! over the stub's arguments."""
    out, i = '', 0
    while i < len(text):
        if text[i] == '%' and i + 1 < len(text) and text[i + 1].isdigit():
            n = int(text[i + 1]) - 1
            i += 2
            kind = 's'
            if text[i:i + 1] == '!':
                kind = text[i + 1]
                i += 3
            v = args[n] if n < len(args) else args[0]  # 0x236's %2: the mission (see the docstring)
            out += str(v) if kind == 'd' else v
        else:
            out += text[i]
            i += 1
    return out


class Box:
    def __init__(self, font):
        self.font, self.lines = font, []

    def put(self, text):
        self.lines.append([text, self.font])

    def set_font(self, f):
        self.font = f
        for line in self.lines:
            line[1] = f


def run_exe(m):
    cpu = PmCpu()
    box, scores = Box(FONT['title']), Box(FONT['title'])
    this = cpu.alloc(0xb00)
    sc = this + 0x5dc
    cpu.u32(this + 0xab0, FONT['complete'])
    cpu.u32(this + 0xab4, FONT['incomplete'])
    cpu.u32(this + 0xab8, FONT['scores'])
    which = {this: box, sc: scores}
    lines = {}  # a box's line() object -> the box
    panel = []

    def vtable(entries):
        vt = cpu.alloc(0x100)
        for off, (fn, pop) in entries.items():
            code = cpu.code(b'\xc3')
            cpu.stub_function(code, fn, pop)
            cpu.u32(vt + off, code)
        return vt

    def set_text(c):  # vt+0x7c (cdecl, the box first): a line and a blank score (0x446930's twin)
        box.put(c.cstr(c.arg(1)))
        scores.put('')
        return 0

    def set_font(c):  # vt+0x90 (thiscall)
        which[c.ecx].set_font(c.arg(0))
        return 0

    def scores_msg(c):  # vt+0x98 (cdecl, the scores first): a line by its message id
        scores.put(MSGS.get(c.arg(1), ''))
        return 0
    box_vt = vtable({0x7c: (set_text, 0), 0x90: (set_font, 4)})
    sc_vt = vtable({0x90: (set_font, 4), 0x98: (scores_msg, 0)})
    cpu.u32(this, box_vt)
    cpu.u32(sc, sc_vt)

    def line_font(c):
        b = lines[c.ecx]
        if b.lines:
            b.lines[-1][1] = c.arg(0)
        return 0
    line_vt = vtable({0x90: (line_font, 4)})
    for b in (box, scores):
        t = cpu.alloc(0x10)
        cpu.u32(t, line_vt)
        lines[t] = b
    line_of = {id(b): t for t, b in lines.items()}

    def line(c):
        return line_of[id(which[c.ecx])]

    def clear(c):
        which[c.ecx].lines = []
        return 0

    def base_text(c):  # 0x5c2ac0 (cdecl): TextBox's set_text, the text as its format
        which[c.arg(0)].put(c.cstr(c.arg(1)))
        return 0
    cpu.stub_function(0x5c2fc0, line, 0)
    cpu.stub_function(0x5c3210, clear, 0)
    cpu.stub_function(0x5c2020, lambda c: HEIGHT, 0)
    cpu.stub_function(0x5c2ac0, base_text, 0)

    def panel_size(c):
        panel.append((c.arg(0), c.arg(1)))
        return 0
    cpu.stub_function(0x43bb70, panel_size, 8)

    # The objectives.
    texts = {}

    def objective(done, failed, visible, text):
        o = cpu.alloc(0x60)  # a group's list at +0x48, its count +0x4c
        cpu.write(o + 8, bytes([done, failed]))
        cpu.write(o + 0x34, bytes([visible]))
        texts[o] = cpu.put_str(text) if text else 0
        return o

    def group(g):
        if g is None:
            return 0
        o = objective(g['done'], 0, 1, g['text'])
        head = cpu.alloc(12)
        nodes = [head]
        for it in g['items']:
            n = cpu.alloc(12)
            cpu.u32(n + 8, objective(*it))
            nodes.append(n)
        for i, n in enumerate(nodes):
            cpu.u32(n, nodes[(i + 1) % len(nodes)])
            cpu.u32(n + 4, nodes[i - 1])
        cpu.u32(o + 0x48, head)
        cpu.u32(o + 0x4c, len(g['items']))
        return o
    groups = {k: group(m[k]) for k in ('primary', 'secondary', 'final', 'fail')}
    cpu.stub_function(0x45fc60, lambda c: texts.get(c.ecx, 0), 4)
    final = groups['final'] if m['primary'] and m['primary']['done'] else 0  # 0x004927f0, as the port's source
    fail = groups['fail'] if m['fail'] and m['fail']['done'] else 0           # 0x00492810
    cpu.stub_function(0x4927f0, lambda c: final, 0)
    cpu.stub_function(0x492810, lambda c: fail, 0)
    cpu.u32(G_DESC, cpu.put_str(m['desc']) if m['desc'] is not None else 0)
    cpu.u32(G_PRIMARY, groups['primary'])
    cpu.u32(G_SECONDARY, groups['secondary'])
    cpu.write(G_ABORT, bytes([m['aborting']]))
    name = cpu.put_str(m['name'])
    gvt = vtable({0x28: (lambda c: m['type'], 0), 0x08: (lambda c: name, 0), 0x48: (lambda c: m['mission'], 0)})
    gobj = cpu.alloc(0x10)
    cpu.u32(gobj, gvt)
    game = cpu.alloc(0x80)
    cpu.u32(game + 0x6c, gobj)
    cpu.u32(G_GAME, game)
    net = 0
    if m['multiplayer']:
        net = cpu.alloc(0x50)
        items = [cpu.alloc(4) for _ in m['scores']]
        for it, s in zip(items, m['scores']):
            cpu.u32(it, cpu.put_str(s))
        arr = cpu.alloc(4 * max(1, len(items)))
        for i, it in enumerate(items):
            cpu.u32(arr + 4 * i, it)
        cpu.u32(net + 0x3c, arr)
        cpu.u32(net + 0x40, arr + 4 * len(items))
    cpu.stub_function(0x47b710, lambda c: net, 4)
    cpu.stub_function(0x4374f0, lambda c: 0, 4)
    cpu.stub_function(0x436db0, lambda c: 0, 8)

    def message(c):  # 0x596de0 (cdecl: buffer, size, id, its arguments)
        buf, size, ident = c.arg(0), c.arg(1), c.arg(2)
        text = MSGS.get(ident, '')
        n = text.count('%')
        raw = [c.arg(3 + i) for i in range(max(n, 1))]
        args = [c.cstr(a) if ident in (0x233, 0x23a, 0x23c) else a for a in raw]
        if ident == 0x236:
            args = [raw[0], raw[0]]
        s = fmt(text, args).encode('latin-1')[:size - 1] + b'\0'
        c.write(buf, s)
        return len(s) - 1
    cpu.stub_function(0x596de0, message, 0)
    cpu.stub_function(0x536a80, lambda c: c.arg(0), 0)  # a command's key: the command itself

    def key_name(c):  # 0x536c90 (key, buffer, size)
        c.write(c.arg(1), f'K{c.arg(0)}'.encode() + b'\0')
        return c.arg(1)
    cpu.stub_function(0x536c90, key_name, 0)

    # The C library.
    heap = {}

    def strchr(c):
        s, ch = c.arg(0), c.arg(1) & 0xff
        t = c.cstr(s)
        i = t.find(chr(ch))
        return s + i if i >= 0 else 0

    def strdup(c):
        a = c.put_str(c.cstr(c.arg(0)))
        heap[a] = True
        return a
    tok = {'p': 0}

    def strtok(c):
        s, delim = c.arg(0), c.cstr(c.arg(1))
        p = s or tok['p']
        if not p:
            return 0
        while c.read(p, 1) != b'\0' and c.read(p, 1).decode('latin-1') in delim:
            p += 1
        if c.read(p, 1) == b'\0':
            tok['p'] = 0
            return 0
        start = p
        while c.read(p, 1) != b'\0' and c.read(p, 1).decode('latin-1') not in delim:
            p += 1
        if c.read(p, 1) == b'\0':
            tok['p'] = 0
        else:
            c.write(p, b'\0')
            tok['p'] = p + 1
        return start
    cpu.stub_import(STRCHR, strchr)
    cpu.stub_import(STRDUP, strdup)
    cpu.stub_import(FREE, lambda c: 0)
    cpu.stub_import(STRTOK, strtok)
    cpu.call(FILL, ecx=this)
    return box.lines, scores.lines, panel


def spec_of(m):
    out = [f"G|{m['type']}|{m['mission']}|{m['name']}|{m['aborting']}|{int(m['multiplayer'])}"]
    if m['desc'] is not None:
        out.append(f"D|{m['desc']}")
    for s in m['scores']:
        out.append(f'S|{s}')
    for tag, k in (('P', 'primary'), ('Q', 'secondary'), ('F', 'final'), ('X', 'fail')):
        g = m[k]
        if g is None:
            continue
        out.append(f"{tag}|{g['text'] or ''}|{g['done']}")
        for done, failed, visible, text in g['items']:
            out.append(f"I|{done}|{failed}|{visible}|{(text or '').replace(chr(10), chr(92) + 'n')}")
    return ('\n'.join(out) + '\n').encode('latin-1')


def run_port(m):
    box, scores, panel = [], [], None
    for line in probe.probe_pm_obj_fill(spec_of(m)).decode('latin-1').split('\n'):
        if line.startswith('box ') or line.startswith('scores '):
            tag, font, text = (line.split(' ', 2) + [''])[:3]
            (box if tag == 'box' else scores).append([text, int(font)])
        elif line.startswith('panel '):
            _, w, n = line.split()
            panel = (int(w), int(n))
    return box, scores, panel


WORDS = ['Destroy', 'the', 'convoy', 'Escort', 'Dummy', 'Reach', 'nav', 'Alpha', 'base', 'bridge', 'Protect']


def made_up(r):
    def text(lines=None):
        n = lines or r.choice([1, 1, 1, 2, 3])
        return '\n'.join(' '.join(r.choice(WORDS) for _ in range(r.randint(1, 4))) for _ in range(n))

    def grp(p_none=0.25):
        if r.random() < p_none:
            return None
        items = []
        for _ in range(r.randint(0, 4)):
            done = int(r.random() < 0.5)
            items.append((done, int(done and r.random() < 0.3), int(r.random() < 0.85),
                          text() if r.random() < 0.9 else None))
        return dict(text=text(1) if r.random() < 0.4 else None, done=int(r.random() < 0.5), items=items)
    mp = r.random() < 0.15
    return dict(type=r.randint(1, 5), mission=r.randint(1, 9), name=text(1), desc=text(1) if r.random() < 0.3 else None,
                aborting=int(r.random() < 0.2), multiplayer=mp,
                scores=[text(1) for _ in range(r.randint(0, 3))] if mp else [],
                primary=grp(0.1), secondary=grp(), final=grp(), fail=grp())


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    r = random.Random(1999)
    lines = 0
    for k in range(runs):
        m = made_up(r)
        exe, port = run_exe(m), run_port(m)
        eb, es, ep = exe
        pb, ps, pp = port
        ep_lines = (ep[-1][0], ep[-1][1] // HEIGHT) if ep else None
        if eb != pb or es != ps or ep_lines != pp:
            print(f'run {k}: differs')
            print('  mission', m)
            for name, a, b in (('box', eb, pb), ('scores', es, ps)):
                for i in range(max(len(a), len(b))):
                    x = a[i] if i < len(a) else None
                    y = b[i] if i < len(b) else None
                    print(f"  {name} {i}: exe {x}  port {y}{'   <--' if x != y else ''}")
            print('  panel exe', ep_lines, 'port', pp)
            sys.exit(1)
        lines += len(eb)
    print(f'ok: {runs} made-up missions, {lines} lines: the box, its scores column and the panel identical')


if __name__ == '__main__':
    main()
