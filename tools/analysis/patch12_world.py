"""Writes src/mech3/patch12_world.inc: what the 1.2 patch changed in c3's
gamez.zbd, as edits to 1.0's file (docs/patch12.md, sitting 6).

  patch12_world.py [game dir]

1.2's file was saved again in another model order with stale pointers, so
the edits keep 1.0's order and say only what the game gets differently:
models whose geometry changed (1.2's model in 1.0's slot, its polygons'
materials mapped to 1.0's by texture, colour and soil), materials, and the
node records' and data's words (pointers left out).
"""
import struct
import sys
from pathlib import Path

GAME = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / 'game'
U = lambda b, o, f='<i': struct.unpack_from(f, b, o)[0]
NODE_DATA = {1: 0x1e8, 2: 0xbc, 3: 0xf8, 4: 0x1c, 5: 0x90, 6: 0x50, 9: 0xd0, 10: 0x94}


def heap(v):
    """A stale pointer of the authoring tool's heap."""
    return 0x00800000 <= v < 0x08000000


class World:
    def __init__(self, path):
        b = self.b = path.read_bytes()
        _, _, tc, to, mo, mdo, nas, nlf, no = struct.unpack_from('<9i', b, 0)
        self.tex = [b[to + i * 0x28 + 8:to + i * 0x28 + 0x1c].split(b'\0')[0].decode('latin-1') for i in range(tc)]
        self.mo, self.mdo, self.no = mo, mdo, no
        asz = U(b, mo)
        self.mats = [b[mo + 16 + i * 0x2c:mo + 16 + (i + 1) * 0x2c] for i in range(asz)]
        textured = [m[1] & 1 for m in self.mats]
        o = mdo
        masz = U(b, o)
        o += 12
        self.mhdr = [b[o + i * 0x60:o + (i + 1) * 0x60] for i in range(masz)]
        o += masz * 0x60
        self.models = []  # (payload start, end, [(poly record offset in payload, material index)])
        for h in self.mhdr:
            st = o
            _, poly, vert, nrm, morph, light = struct.unpack_from('<6i', h, 0xc)
            o += 12 * vert + 12 * nrm + 12 * morph
            if light > 0:
                lights = [b[o + j * 0x4c:o + (j + 1) * 0x4c] for j in range(light)]
                o += 0x4c * light
                for li in lights:
                    o += 12 * U(li, 0xc)
            polys = []
            if poly > 0:
                recs = [(o + j * 0x24, b[o + j * 0x24:o + (j + 1) * 0x24]) for j in range(poly)]
                o += 0x24 * poly
                for at, p in recs:
                    fl, k, mi = U(p, 0, '<I'), U(p, 0, '<I') & 0xff, U(p, 0x1c)
                    polys.append((at - st, mi, o - st, k, fl))
                    if k:
                        o += 4 * k
                        if fl & 0x200:
                            o += 4 * k
                    if textured[mi]:
                        o += 8 * k
                    o += 12 * k
            self.models.append((st, o, polys))
        assert o == no
        self.nodes = []  # (record, data start, data end)
        o = no + nas * 0xd4
        for i in range(nas):
            n = b[no + i * 0xd4:no + (i + 1) * 0xd4]
            t, pc, cc = U(n, 0x34), U(n, 0x54), U(n, 0x5c)
            st = o
            if t:
                d = b[o:o + NODE_DATA[t]]
                o += NODE_DATA[t]
                if t == 2:
                    lc, sc, xc, zc = U(d, 0xa0), U(d, 0xac), U(d, 0x88), U(d, 0x8c)
                    o += 4 * max(lc, 0) + 4 * max(sc, 0)
                    for _ in range(zc * xc):
                        o += 0x48
                        nc = struct.unpack_from('<h', b, o - 0x48 + 0x3a)[0]
                        if nc > 0:
                            o += 4 * nc
                if t == 9 and U(d, 200) > 0:
                    o += 4 * U(d, 200)
                if t == 10 and U(d, 0x8c) > 0:
                    o += 4 * U(d, 0x8c)
            if pc > 0:
                o += 4 * pc
            if cc > 0:
                o += 4 * cc
            self.nodes.append((n, st, o))
        assert o == len(b)

    def mat_id(self, i):
        m = self.mats[i]
        fl = m[1]
        if fl & 1:
            return ('tex', m[0], fl, self.tex[U(m, 0x10)], U(m, 0x20))
        return ('rgb', m[0], fl, m[4:0x10], U(m, 0x20))

    def geometry(self, mi):
        """A model as the game draws it: points and faces, materials by identity."""
        st, end, polys = self.models[mi]
        pay = bytearray(self.b[st:end])
        mats = []
        for at, m, _, _, _ in polys:
            pay[at + 4:at + 0x20] = bytes(0x1c)  # pointers and the material index
            mats.append(self.mat_id(m) if 0 <= m < len(self.mats) else m)
        return bytes(self.mhdr[mi][0xc:0x24]) + bytes(pay), tuple(mats)


def data_end(polys, k, size):
    """Where polygon k's data ends: the next polygon's data, or the model's end."""
    return polys[k + 1][2] if k + 1 < len(polys) else size


def faces(w, mi, mat=None):
    """The model's faces, order-free: each its corners (point, normal, UV,
    colour) from the lowest point round, its flags and its material (by
    `mat`, else by identity). The test compares the same way."""
    st, end, polys = w.models[mi]
    _, _, vert, nrm, _, _ = struct.unpack_from('<6i', w.mhdr[mi], 0xc)
    f3 = lambda at: tuple(round(c, 3) for c in struct.unpack_from('<3f', w.b, at))
    out = set()
    for k_, (_, m, data, k, fl) in enumerate(polys):
        tex = 0 <= m < len(w.mats) and w.mats[m][1] & 1
        vi = struct.unpack_from(f'<{k}i', w.b, st + data)
        at = st + data + 4 * k
        ni = struct.unpack_from(f'<{k}i', w.b, at) if fl & 0x200 else None
        if fl & 0x200:
            at += 4 * k
        uv = [tuple(round(c, 4) for c in struct.unpack_from('<2f', w.b, at + 8 * c)) for c in range(k)] if tex else None
        if tex:
            at += 8 * k
        col = [f3(at + 12 * c) for c in range(k)]
        corners = []
        for c in range(k):
            v = vi[c] if 0 <= vi[c] < vert else 0
            corner = (f3(st + 12 * v),)
            if ni:
                n = ni[c] if 0 <= ni[c] < nrm else 0
                corner += (f3(st + 12 * vert + 12 * n),)
            if uv:
                corner += (uv[c],)
            corners.append(corner + (col[c],))
        if corners:
            r = min(range(len(corners)), key=lambda c: corners[c][0])
            out.add((tuple(corners[r:] + corners[:r]), fl, mat(m) if mat else w.mat_id(m) if 0 <= m < len(w.mats) else m))
    return frozenset(out)


def main():
    a, b = World(GAME / 'mw3-1.0/zbd/c3/gamez.zbd'), World(GAME / 'mw3-1.2/zbd/c3/gamez.zbd')
    lines = ['// Generated by tools/analysis/patch12_world.py from the 1.0 and 1.2 installs',
             '// (docs/patch12.md): c3 gamez.zbd 1.0 -> 1.2.']
    # Materials: 1.0's by identity; 1.2's that 1.0 lacks are fixed in place
    # (same texture, other soil) or added.
    ids10 = {a.mat_id(i): i for i in range(len(a.mats)) if not a.mats[i][1] & 0x20}
    mat_edits, added = [], []
    def map_mat(mb):
        idb = b.mat_id(mb)
        if idb in ids10:
            return ids10[idb]
        if idb[0] == 'tex':
            same = [i for i in ids10 if i[0] == 'tex' and i[3] == idb[3] and i[1:3] == idb[1:3]]
            if len(same) == 1:
                i = ids10.pop(same[0])
                mat_edits.append((i, 0x20, idb[4], same[0][4]))
                ids10[idb] = i
                return i
        added.append(b.mats[mb])
        i = len([m for m in a.mats if not m[1] & 0x20]) + len(added) - 1
        ids10[idb] = i
        return i
    # Models: by the nodes that use them (node indices match), 1.2's in
    # 1.0's slot where the faces changed.
    repl = {}
    for i, ((na, _, _), (nb, _, _)) in enumerate(zip(a.nodes, b.nodes)):
        if U(na, 0x34) != 5 or U(nb, 0x34) != 5:
            continue
        ma, mb = U(na, 0x3c), U(nb, 0x3c)
        if not (0 <= ma < len(a.models) and 0 <= mb < len(b.models)):
            continue
        if faces(a, ma, lambda m: m) == faces(b, mb, map_mat):
            continue
        assert repl.get(ma, mb) == mb, (i, ma, mb)
        repl[ma] = mb
    def poly_bytes(w, mi, mat=None):
        """Each polygon's record (pointers zeroed, material by `mat`) and data."""
        st, end, polys = w.models[mi]
        out = []
        for k, (at, m, data, _, _) in enumerate(polys):
            rec = bytearray(w.b[st + at:st + at + 0x24])
            rec[4:0x1c] = bytes(0x18)
            struct.pack_into('<i', rec, 0x1c, mat(m) if mat else m)
            out.append(bytes(rec) + w.b[st + data:st + data_end(polys, k, end - st)])
        return out

    models = []
    for ma, mb in sorted(repl.items()):
        sa, ea, pa = a.models[ma]
        sb, eb, pb = b.models[mb]
        pts_a = a.b[sa:sa + (pa[0][0] if pa else ea - sa)]
        pts_b = b.b[sb:sb + (pb[0][0] if pb else eb - sb)]
        old = {x: k for k, x in enumerate(poly_bytes(a, ma))}
        toks = [f'@{old[x]}' if x in old else '*' + x.hex() for x in poly_bytes(b, mb, map_mat)]
        models.append((ma, bytes(b.mhdr[mb][:0x5c]), None if pts_a == pts_b else pts_b, ' '.join(toks)))

    # Words of the node records and data: 1.2's where they differ, but not
    # the model index (1.0's order is kept) or stale pointers.
    words = []
    for i, ((ra, sa, ea), (rb, sb, eb)) in enumerate(zip(a.nodes, b.nodes)):
        for j in range(0, 0xd4, 4):
            x, y = U(ra, j, '<I'), U(rb, j, '<I')
            # the model index: 1.0's order is kept, but a model 1.2 takes away goes
            if j == 0x3c:
                if y == 0xffffffff and x != y:
                    words.append((i, 0, j, y))
                assert not (x == 0xffffffff and y != x and U(ra, 0x34) == 5), i
                continue
            if x != y and not (heap(x) and heap(y)):
                words.append((i, 0, j, y))
        da, db = a.b[sa:ea], b.b[sb:eb]
        assert len(da) == len(db), i
        for j in range(0, len(da), 4):
            x, y = U(da, j, '<I'), U(db, j, '<I')
            if x != y and not (heap(x) and heap(y)):
                words.append((i, 1, j, y))
    chunks = lambda t: ' '.join(f'"{t[k:k + 96]}"' for k in range(0, len(t), 96))
    hexs = lambda d: chunks(d.hex())
    lines.append("// A model: its 1.0 slot, 1.2's header, its points (null: 1.0's are the same),")
    lines.append("// its polygons: @k 1.0's polygon k, *hex a polygon record and its data.")
    lines.append('constexpr WorldModel kWorldModels[] = {')
    size = 0
    for ma, h, pts, toks in models:
        lines.append(f'    {{{ma}, {hexs(h)},')
        lines.append(f'     {hexs(pts) if pts else "nullptr"},')
        lines.append(f'     {chunks(toks)}}},')
        size += 0x5c + (len(pts) if pts else 0) + sum(len(t) // 2 for t in toks.split() if t[0] == '*')
    lines.append('};')
    lines.append('constexpr WorldMaterial kWorldMaterials[] = {')
    for i, off, new, old in mat_edits:
        lines.append(f'    {{{i}, 0x{off:x}, {old}, {new}}},')
    lines.append('};')
    lines.append(f'constexpr const char* kWorldAddedMaterials[] = {{{", ".join(hexs(m) for m in added) or "nullptr"}}};')
    lines.append('constexpr WorldWord kWorldWords[] = {')
    for i, part, j, v in words:
        lines.append(f'    {{{i}, {part}, 0x{j:x}, 0x{v:08x}}},')
    lines.append('};')
    sys.stdout.write('\n'.join(lines) + '\n')
    print(f'// {len(models)} models, {size} bytes from 1.2; {len(mat_edits)} material edits, '
          f'{len(added)} added; {len(words)} words', file=sys.stderr)


if __name__ == '__main__':
    main()
