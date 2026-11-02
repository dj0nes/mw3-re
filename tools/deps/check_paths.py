#!/usr/bin/env python3
# Fails when any file under a package folder carries a local path or the
# building user's name (release.md, Privacy): every byte of every file, as
# ASCII and as UTF-16, so a library's built-in strings count too (FFmpeg's
# configure line once carried the build folder). The public GitHub address
# (github.com/<user>) is the one allowed use of the name.
# Usage: check_paths.py <folder> [extra name ...]   (make_app.sh, make-appimage.sh)
import getpass, os, re, sys

root = sys.argv[1]
names = {n for n in [getpass.getuser(), os.environ.get('SUDO_USER', ''),
                     os.path.basename(os.path.expanduser('~'))] + sys.argv[2:] if len(n) >= 4}
words = [b'/Volumes/', b'/Users/', b'/home/', b'C:\\Users\\', b'C:/Users/'] + [n.encode() for n in sorted(names)]
pat = re.compile(b'|'.join(re.escape(w) for w in words))
pat16 = re.compile(b'|'.join(re.escape(w.decode().encode('utf-16-le')) for w in words))
allowed = re.compile(rb'github\.com/$', re.I)

bad = []
for d, _, files in os.walk(root):
    for f in files:
        p = os.path.join(d, f)
        if os.path.islink(p):
            continue
        data = open(p, 'rb').read()
        for m in pat.finditer(data):
            if allowed.search(data[max(0, m.start() - 11):m.start()]):
                continue
            ctx = data[max(0, m.start() - 30):m.end() + 50]
            bad.append(f"{os.path.relpath(p, root)}: {ctx.decode('latin-1')!r}")
            break
        else:
            if pat16.search(data):
                bad.append(f"{os.path.relpath(p, root)}: (UTF-16)")
for b in bad:
    print('local path or user name in the package:', b)
if bad:
    sys.exit(1)
print(f"check_paths: no local path or user name in {root}")
