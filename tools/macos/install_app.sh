#!/bin/bash
# Builds "MechWarrior 3.app" from source and puts it in Applications, for a
# player building on their own Mac (docs/macos.md, Building it yourself).
#
#   tools/macos/install_app.sh                  run from anywhere in the repo
#   tools/macos/install_app.sh --to DIR         somewhere else than /Applications
#   tools/macos/install_app.sh --exe Mech3.exe  the icon from that copy
#
# 1. Homebrew's build tools and libraries, if any are missing.
# 2. tools/macos/make_app.sh (Homebrew's libraries, this Mac only).
# 3. The app's icon from the player's own Mech3.exe (exe_icon.py), when one is
#    found: --exe, else the install in mw3.cfg. No game art is in the repo; the
#    placeholder stays when there is no Mech3.exe.
# 4. The app copied to /Applications (~/Applications when that is not
#    writable), replacing an earlier copy.
set -euo pipefail
cd "$(dirname "$0")/../.."
DEST="/Applications"
EXE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --to) DEST="${2:?--to needs a folder}"; shift 2 ;;
    --exe) EXE="${2:?--exe needs a path}"; shift 2 ;;
    -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done

command -v brew >/dev/null || { echo "Homebrew is needed: https://brew.sh" >&2; exit 1; }
missing=()
for f in cmake ninja sdl3 sdl3_ttf sdl3_sound libarchive; do
  brew list --versions "$f" >/dev/null 2>&1 || missing+=("$f")
done
if [ ${#missing[@]} -gt 0 ]; then
  echo "installing with Homebrew: ${missing[*]}"
  # brew can exit non-zero over a warning (a ninja already on PATH): what
  # counts is whether the formulae are there afterwards.
  brew install "${missing[@]}" || true
  for f in "${missing[@]}"; do
    brew list --versions "$f" >/dev/null 2>&1 || { echo "brew install $f failed" >&2; exit 1; }
  done
fi

tools/macos/make_app.sh
BUILT="build-mac/dist/MechWarrior 3.app"

# The player's own icon.
CFG="$HOME/Library/Application Support/MicroProse/MechWarrior 3/mw3.cfg"
if [ -z "$EXE" ] && [ -f "$CFG" ]; then
  dir="$(sed -n 's/^install=//p' "$CFG" | head -1)"
  for n in Mech3.exe MECH3.EXE mech3.exe; do
    [ -n "$dir" ] && [ -f "$dir/$n" ] && { EXE="$dir/$n"; break; }
  done
fi
ICON=""
if [ -n "$EXE" ]; then
  ICONSET="build-mac/mw3-own.iconset"
  rm -rf "$ICONSET"
  if python3 tools/macos/exe_icon.py "$EXE" "$ICONSET"; then
    ICON="build-mac/mw3-own.icns"
    iconutil -c icns "$ICONSET" -o "$ICON"
  fi
else
  echo "no Mech3.exe found (install the game first, or --exe): keeping the placeholder icon"
fi

if [ ! -w "$DEST" ]; then
  DEST="$HOME/Applications"
  mkdir -p "$DEST"
fi
APP="$DEST/MechWarrior 3.app"
rm -rf "$APP"
cp -R "$BUILT" "$APP"
if [ -n "$ICON" ]; then
  cp "$ICON" "$APP/Contents/Resources/mw3.icns"
  codesign --force -s - "$APP"  # the resources changed: seal the bundle again
  touch "$APP"                  # Finder picks the new icon up
fi
codesign --verify --strict "$APP"
echo "installed: $APP"
