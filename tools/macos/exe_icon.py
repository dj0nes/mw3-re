#!/usr/bin/env python3
"""The player's own Mech3.exe's icon as a macOS iconset (install_app.sh).

    tools/macos/exe_icon.py <Mech3.exe> <out.iconset>

Reads the PE's RT_GROUP_ICON / RT_ICON resources, takes the largest, deepest
image of the first group, and writes icon_<n>x<n>[@2x].png scaled by nearest
neighbour (the original is 32x32 pixel art; smoothing would blur it). Nothing
of the game is kept in the repo: the icon is made from the player's copy, on
their machine. Standard library only. Exit 1 if the file has no icon.
"""
import pathlib
import struct
import sys
import zlib

RT_ICON, RT_GROUP_ICON = 3, 14


def sections(pe):
    off = struct.unpack_from('<I', pe, 0x3C)[0]
    if pe[off:off + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    nsec = struct.unpack_from('<H', pe, off + 6)[0]
    optsize = struct.unpack_from('<H', pe, off + 20)[0]
    opt = off + 24
    magic = struct.unpack_from('<H', pe, opt)[0]
    dd = opt + (96 if magic == 0x10B else 112)
    rsrc_rva = struct.unpack_from('<I', pe, dd + 2 * 8)[0]
    secs = []
    for i in range(nsec):
        s = opt + optsize + 40 * i
        vsize, va, rawsize, raw = struct.unpack_from('<IIII', pe, s + 8)
        secs.append((va, max(vsize, rawsize), raw))
    return rsrc_rva, secs


def rva_to_off(rva, secs):
    for va, size, raw in secs:
        if va <= rva < va + size:
            return rva - va + raw
    raise ValueError(f'rva {rva:#x} outside the sections')


def resources(pe):
    """{type: {id: bytes}}, the first language of each."""
    rsrc_rva, secs = sections(pe)
    if not rsrc_rva:
        return {}
    base = rva_to_off(rsrc_rva, secs)

    def entries(d):
        named, ids = struct.unpack_from('<HH', pe, base + d + 12)
        for i in range(named + ids):
            name, target = struct.unpack_from('<II', pe, base + d + 16 + 8 * i)
            yield name, target

    out = {}
    for tid, t in entries(0):
        if tid & 0x80000000 or tid not in (RT_ICON, RT_GROUP_ICON) or not t & 0x80000000:
            continue
        for rid, r in entries(t & 0x7FFFFFFF):
            if not r & 0x80000000:
                continue
            for _, leaf in entries(r & 0x7FFFFFFF):
                rva, size = struct.unpack_from('<II', pe, base + leaf)
                o = rva_to_off(rva, secs)
                out.setdefault(tid, {})[rid] = pe[o:o + size]
                break
    return out


def decode_dib(dib):
    """A BITMAPINFOHEADER icon image (XOR + AND mask) as (w, h, rgba rows)."""
    if dib[:8] == b'\x89PNG\r\n\x1a\n':
        raise ValueError('PNG icon images are not handled (none in 1999 executables)')
    hsize, w, h2, _, bpp = struct.unpack_from('<IiiHH', dib, 0)
    h = h2 // 2
    ncol = struct.unpack_from('<I', dib, 32)[0] or (1 << bpp if bpp <= 8 else 0)
    pal = [dib[hsize + 4 * i:hsize + 4 * i + 4] for i in range(ncol)]
    xor = hsize + 4 * ncol
    xstride = ((w * bpp + 31) // 32) * 4
    astride = ((w + 31) // 32) * 4
    andm = xor + xstride * h
    rows = []
    for y in range(h):
        src = h - 1 - y  # bottom-up
        row = bytearray()
        for x in range(w):
            if bpp <= 8:
                bit = x * bpp
                byte = dib[xor + src * xstride + bit // 8]
                idx = (byte >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1)
                b, g, r, _ = pal[idx]
                a = 255
            elif bpp == 24:
                b, g, r = dib[xor + src * xstride + 3 * x:xor + src * xstride + 3 * x + 3]
                a = 255
            elif bpp == 32:
                b, g, r, a = dib[xor + src * xstride + 4 * x:xor + src * xstride + 4 * x + 4]
            else:
                raise ValueError(f'{bpp}-bit icon images are not handled')
            if bpp != 32 and andm + src * astride < len(dib):
                if dib[andm + src * astride + x // 8] >> (7 - x % 8) & 1:
                    a = 0
            row += bytes((r, g, b, a))
        rows.append(bytes(row))
    return w, h, rows


def png(w, h, rows, n):
    """Nearest-neighbour scale to n x n, as PNG bytes."""
    raw = bytearray()
    for y in range(n):
        src = rows[y * h // n]
        raw.append(0)
        for x in range(n):
            sx = x * w // n
            raw += src[4 * sx:4 * sx + 4]

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', n, n, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def best_icon(pe):
    res = resources(pe)
    groups, icons = res.get(RT_GROUP_ICON, {}), res.get(RT_ICON, {})
    if not groups:
        return None
    grp = groups[min(groups)]
    count = struct.unpack_from('<H', grp, 4)[0]
    best = None
    for i in range(count):
        w, h, _, _, _, bpp, _, iid = struct.unpack_from('<BBBBHHIH', grp, 6 + 14 * i)
        key = ((w or 256) * (h or 256), bpp)
        if iid in icons and (best is None or key > best[0]):
            best = (key, icons[iid])
    return best and best[1]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    dib = best_icon(pathlib.Path(sys.argv[1]).read_bytes())
    if dib is None:
        print(f'{sys.argv[1]}: no icon', file=sys.stderr)
        sys.exit(1)
    w, h, rows = decode_dib(dib)
    out = pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for n in (16, 32, 128, 256, 512):
        (out / f'icon_{n}x{n}.png').write_bytes(png(w, h, rows, n))
        (out / f'icon_{n}x{n}@2x.png').write_bytes(png(w, h, rows, 2 * n))
    print(f'icon: {w}x{h} from {sys.argv[1]}')


if __name__ == '__main__':
    main()
