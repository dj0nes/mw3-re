"""What Pirate's Moon's Direct3D strip conversion (0x00570b70, left out of
the port: docs/pm-map.md, the models' form) does to the shipped world models.

    .venv/bin/python -P tests/differential/test_pm_strips.py

Each world's models are read in PM's exe (0x00558ba0) with the conversion
stubbed, as test_pm_gamez.py does; then PM's own 0x00570b70 runs, as the
reader would have called it under Direct3D (model, 0x7fffffff, 0x7fffffff),
on every model it was given that has polygons. Each model is dumped before
and after (header, vertices, normals, lights, every polygon's flags,
indices, materials, UVs and colours, the material refs): the port keeps the
file's form, so it holds only while the conversion changes nothing the
renderer or the game reads. A model it changes is listed with what changed.
It strips only a model of type 0 (+0x00) with polygons; every shipped model
without 0x80 is a facade (type 1), so it returns at once. A type-0 one
reaching it fails the test (its strip path also loads the textures' images,
0x005329e0, which this bench has not set up).
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_pm_gamez as g  # noqa: E402


def slot_dump(emu, s):
    """One model at `s`, its pointers followed (test_pm_gamez's dump, per slot)."""
    ptr = lambda a: emu.i32(a) & 0xffffffff
    h = emu.read(s, 0x68)
    cnt = lambda off: struct.unpack_from('<i', h, off)[0]
    out = {'header': h[0:0x34] + h[0x48:0x60]}
    for name, off, at in (('vertices', 0x14, 0x38), ('normals', 0x18, 0x3c), ('morphs', 0x1c, 0x44)):
        out[name] = emu.read(ptr(s + at), 12 * cnt(off)) if cnt(off) > 0 else b''
    lights = b''
    for k in range(max(cnt(0x20), 0)):
        l = emu.read(ptr(s + 0x40) + 0x4c * k, 0x4c)
        lights += l[0:0x2a] + l[0x30:0x4c]
    out['lights'] = lights
    polys, plist = ptr(s + 0x34), []
    for k in range(max(cnt(0x10), 0)):
        q = emu.read(polys + 0x28 * k, 0x28)
        flags, prio = struct.unpack_from('<Ii', q, 0)
        mc, uvs = struct.unpack_from('<i', q, 0x10)[0], struct.unpack_from('<I', q, 0x18)[0]
        n = flags & 0x3ff
        p = struct.pack('<IiiiI', flags, prio, mc, 1 if uvs else 0, struct.unpack_from('<I', q, 0x24)[0])
        p += emu.read(struct.unpack_from('<I', q, 8)[0], 4 * n) if n else b''
        if n and flags & 0x1000:
            p += emu.read(struct.unpack_from('<I', q, 0xc)[0], 4 * n)
        mats = struct.unpack_from('<I', q, 0x14)[0]
        for j in range(max(mc, 0)):
            p += struct.pack('<i', emu.mat_index(emu.i32(mats + 4 * j)))
            if uvs:
                p += emu.read(ptr(uvs + 4 * j), 8 * n)
        p += emu.read(struct.unpack_from('<I', q, 0x1c)[0], 12 * n)
        plist.append(p)
    out['polygons'] = plist
    refs = b''
    for k in range(max(cnt(0x5c), 0)):
        m, use, poly = struct.unpack('<IiI', emu.read(ptr(s + 0x60) + 12 * k, 12))
        refs += struct.pack('<iii', emu.mat_index(m), use, (poly - polys) // 0x28 if cnt(0x10) > 0 else -1)
    out['refs'] = refs
    return out


def model_slots(emu):
    blocks = emu.i32(g.M_BLOCKS) & 0xffffffff
    for i in range(emu.i32(g.M_CAPACITY)):
        yield (emu.i32(blocks + 4 * (i // 750)) & 0xffffffff) + 0x68 * (i % 750)


def main():
    stripped = 0
    emu = g.Emulated()
    total = changed = 0
    for w in g.WORLDS:
        emu.stack([g.ZBD / w / 'gamez.zbd'], 5, 6, 5, models=True)
        given = [p for kind, p in emu.converted if kind == 'strips']
        stub = emu.cpu.stubs.pop(g.STRIPS)  # PM's own conversion from here
        with_polys = 0
        for p in given:
            if emu.i32(p + 0x10) <= 0:
                continue
            with_polys += 1
            before = slot_dump(emu, p)
            emu.call(g.STRIPS, args=[p, 0x7fffffff, 0x7fffffff])
            after = slot_dump(emu, p)
            if before != after:
                changed += 1
                what = [k for k in before if before[k] != after[k]]
                print(f'{w}: model at {p:#x} changed: {what}; polygons {len(before["polygons"])} -> '
                      f'{len(after["polygons"])}, flags {before["header"][8]:#x} -> {after["header"][8]:#x}')
        # It strips a model of type 0 only (+0x00; 1 facades, 2 points): one
        # reaching it would come out changed, and the port's form would differ.
        types = sorted({emu.i32(p) for p in given if emu.i32(p + 0x10) > 0})
        typed0 = sum(1 for p in given if emu.i32(p + 0x10) > 0 and emu.i32(p) == 0)
        stripped += typed0
        print(f'{w}: the types given with polygons {types}; {typed0} of type 0')
        emu.cpu.stubs[g.STRIPS] = stub  # the next world's read records again
        total += with_polys
        print(f'{w}: {len(given)} models given to the conversion, {with_polys} with polygons')
    print(f'{"ok" if changed == 0 else "CHANGED"}: PM\'s strip conversion ran on {total} shipped models with polygons; '
          f'{changed} changed')
    if stripped:
        print(f'{stripped} models of type 0 reach the conversion: the file form is not what PM drew')
    sys.exit(1 if changed or stripped else 0)


if __name__ == '__main__':
    main()
