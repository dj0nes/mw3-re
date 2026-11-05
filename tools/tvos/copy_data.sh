#!/bin/bash
# Ours: the player's game into the tvOS app (docs/tvos.md, Where the game
# files come from). Run by Xcode after each build of mw3 when build.sh was
# given --data (CMake's MW3_TVOS_DATA), before the app is signed.
#
#   tools/tvos/copy_data.sh <the Mac rebuild's folder> [mw3.app]
#
# Without the app, Xcode's own (TARGET_BUILD_DIR/WRAPPER_NAME, set in its
# build phases).
#
# The install (that folder's mw3.cfg install=, else its install/) goes into
# <app>/game without the original AVIs; the converted movies (video/) and
# the music (music/) into <app>/media. rsync copies only what changed.
set -euo pipefail
SRC="${1:?the folder of the Mac rebuild}"
APP="${2:-${TARGET_BUILD_DIR:?no app given and not run by Xcode}/${WRAPPER_NAME:?}}"
INSTALL=""
if [ -f "$SRC/mw3.cfg" ]; then
  INSTALL="$(sed -n 's/^install=//p' "$SRC/mw3.cfg" | tail -1 | tr -d '\r')"
fi
[ -n "$INSTALL" ] || INSTALL="$SRC/install"
if [ ! -f "$INSTALL/zbd/reader.zbd" ]; then
  echo "error: no MechWarrior 3 install at \"$INSTALL\" (no zbd/reader.zbd). Start the Mac rebuild once and point it at your game, or set MW3_TVOS_DATA (docs/tvos.md)." >&2
  exit 1
fi
[ -d "$SRC/video" ] || echo "warning: no converted movies in \"$SRC/video\": the app plays none (docs/tvos.md)" >&2
mkdir -p "$APP/game" "$APP/media"
changed="$(rsync -a --delete --itemize-changes --exclude '/video/*.[aA][vV][iI]' "$INSTALL/" "$APP/game/")"
for d in video music; do
  if [ -d "$SRC/$d" ]; then
    changed+="$(rsync -a --delete --itemize-changes --exclude 'import-failed' "$SRC/$d/" "$APP/media/$d/")"
  elif [ -d "$APP/media/$d" ]; then
    rm -rf "$APP/media/$d"
    changed+="removed $d"
  fi
done
# Xcode decides at the start of a build whether to sign, so a copy that
# changes an app it has already signed would leave the seal broken (the
# Apple TV refuses such an app). Signed again here with Xcode's identity,
# keeping the signature's entitlements; a first build Xcode signs itself.
if [ -n "$changed" ] && [ -d "$APP/_CodeSignature" ] && [ "${CODE_SIGNING_ALLOWED:-YES}" != NO ] &&
   [ -n "${EXPANDED_CODE_SIGN_IDENTITY:-}" ]; then
  /usr/bin/codesign --force --sign "$EXPANDED_CODE_SIGN_IDENTITY" --preserve-metadata=identifier,entitlements,flags \
    --timestamp=none --generate-entitlement-der "$APP"
fi
echo "copy_data: $INSTALL into game/, $SRC video/ and music/ into media/ ($(du -sh "$APP" | cut -f1) in all)"
