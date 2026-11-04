#!/bin/sh
# On the branch `pm`: builds the Linux package (docs/linux.md, Packaging): an AppImage with mw3pm, the
# read-me and the notices, and the SDL3, SDL3_ttf and SDL3_sound libraries
# they load. Nothing from the game. Its own build tree, build-linux-pkg,
# configured with MW3_PACKAGE (no development copy of the game in the
# programs). Vulkan, X11, Wayland and the audio servers are the system's
# (loaded by SDL at run time), as an AppImage expects.
# Usage: tools/linux/make-appimage.sh [PREFIX] [TOOLS]
#   PREFIX: where SDL3, SDL3_ttf, SDL3_sound and FFmpeg are installed
#           (tools/deps/build_deps.sh linux's build-deps/linux/prefix;
#           default ../prefix); TOOLS: where linuxdeploy-x86_64.AppImage and
#           appimagetool-x86_64.AppImage are (default ../deps).
set -e
cd "$(dirname "$0")/../.."
PREFIX=$(cd "${1:-../prefix}" && pwd)
TOOLS=$(cd "${2:-../deps}" && pwd)
export APPIMAGE_EXTRACT_AND_RUN=1  # no FUSE needed (WSL, containers)
# FFmpeg's libraries from the prefix too (tools/deps/build_deps.sh linux:
# the minimal LGPL build mw3import needs), found through pkg-config.
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

rm -rf build-linux-pkg
cmake -S . -B build-linux-pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DMW3_PACKAGE=ON -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build build-linux-pkg --target mw3pm

VER=$(git describe --tags --always --dirty 2>/dev/null || echo dev)
APPDIR=build-linux-pkg/AppDir
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/doc/mw3pm/licenses"
# mw3pm is the AppImage's entry (mw3pm.desktop's Exec); no launcher in PM's package for now.
cp build-linux-pkg/mw3pm "$APPDIR/usr/bin/"
EXES="-e $APPDIR/usr/bin/mw3pm"
cp tools/windows/dist/README.txt tools/windows/dist/THIRD-PARTY.txt "$APPDIR/usr/share/doc/mw3pm/"
[ -f LICENSE ] && cp LICENSE "$APPDIR/usr/share/doc/mw3pm/LICENSE.txt"
# Each library's licence files: from its source tree when the prefix is
# build_deps.sh's (FFmpeg installs none), else what the prefix installed.
if [ -d "$PREFIX/../work/src" ]; then
  for d in "$PREFIX"/../work/src/*/; do
    n=$(basename "$d"); mkdir -p "$APPDIR/usr/share/doc/mw3pm/licenses/$n"
    for f in "$d"LICENSE* "$d"COPYING* "$d"LICENSE.txt "$d"docs/LICENSE.TXT "$d"docs/FTL.TXT "$d"docs/GPLv2.TXT; do
      [ -f "$f" ] && cp "$f" "$APPDIR/usr/share/doc/mw3pm/licenses/$n/"
    done
  done
else
  for lib in SDL3 SDL3_ttf SDL3_sound; do
    mkdir -p "$APPDIR/usr/share/doc/mw3pm/licenses/$lib"
    find "$PREFIX/share" -ipath "*$lib*" \( -iname "LICENSE*" -o -iname "COPYING*" \) -exec cp {} "$APPDIR/usr/share/doc/mw3pm/licenses/$lib/" \; 2>/dev/null || true
  done
fi

# libarchive, zlib and liblzma are linked in: build_deps.sh keeps their licences in the prefix.
[ -d "$PREFIX/share/licenses" ] && cp -R "$PREFIX/share/licenses/." "$APPDIR/usr/share/doc/mw3pm/licenses/"
# The libraries they load (SDL's from PREFIX), the desktop entry and icon.
LD_LIBRARY_PATH="$PREFIX/lib:$LD_LIBRARY_PATH" "$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" $EXES \
  -d tools/linux/mw3pm.desktop -i tools/linux/mw3.png
# The program as the entry (AppRun follows the desktop file's Exec).
for p in mw3pm; do [ -x "$APPDIR/usr/bin/$p" ] || { echo "no $p in the AppDir" >&2; exit 1; }; done
grep -qx 'Exec=mw3pm' "$APPDIR"/*.desktop || { echo "the AppImage does not start mw3pm" >&2; exit 1; }
# No local path or user name in anything shipped (release.md, Privacy).
python3 -I tools/deps/check_paths.py "$APPDIR" || exit 1
mkdir -p build-linux-pkg/dist
OUT="build-linux-pkg/dist/PM-Rebuild-$VER-x86_64.AppImage"
ARCH=x86_64 "$TOOLS/appimagetool-x86_64.AppImage" "$APPDIR" "$OUT"
echo "$OUT"
