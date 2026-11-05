#!/bin/bash
# Ours: the Apple TV app (docs/tvos.md). Configures an Xcode project for mw3
# as a tvOS app, then builds it or opens it in Xcode.
#
#   tools/tvos/build.sh --sim             build-tvos-sim/, for the tvOS Simulator
#   tools/tvos/build.sh --device          build-tvos/, for the Apple TV
#   tools/tvos/build.sh --device --open   the same, opened in Xcode (pick the
#                                         Apple TV, press Run)
#
# The libraries come from tools/deps/build_deps.sh tvos (or tvos-sim), built
# first if their prefix is not there. From the environment: MW3_TVOS_TEAM,
# the team that signs a device build (without it --device builds unsigned,
# which proves it compiles but cannot be installed), and MW3_TVOS_BUNDLE_ID
# (default org.mw3rebuild.tv.<team>). docs/tvos.md.
set -euo pipefail
cd "$(dirname "$0")/../.."

TARGET=sim
OPEN=0
for arg in "$@"; do
  case "$arg" in
    --sim) TARGET=sim ;;
    --device) TARGET=device ;;
    --open) OPEN=1 ;;
    *) echo "usage: $0 [--sim|--device] [--open]" >&2; exit 2 ;;
  esac
done

if [ "$TARGET" = sim ]; then
  DEPS=tvos-sim BUILD=build-tvos-sim SYSROOT=appletvsimulator
else
  DEPS=tvos BUILD=build-tvos SYSROOT=appletvos
fi
PREFIX="$PWD/build-deps/$DEPS/prefix"
[ -f "$PREFIX/lib/libSDL3.a" ] || tools/deps/build_deps.sh "$DEPS"

# CMake searches only under its find roots when it builds for tvOS: the
# prefix is made one (docs/deps.md, tvOS).
cmake -S . -B "$BUILD" -G Xcode \
  -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT="$SYSROOT" -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_FIND_ROOT_PATH="$PREFIX" \
  -DMW3_TVOS=ON -DMW3_PACKAGE=ON -DMW3_TVOS_TEAM="${MW3_TVOS_TEAM:-}" -DMW3_TVOS_BUNDLE_ID="${MW3_TVOS_BUNDLE_ID:-}"

if [ "$OPEN" = 1 ]; then
  open "$BUILD/mw3re.xcodeproj"
  echo "Xcode: pick the scheme mw3 and the Apple TV (or a Simulator), then Run."
  exit 0
fi

EXTRA=()
if [ "$TARGET" = device ]; then
  if [ -n "${MW3_TVOS_TEAM:-}" ]; then
    EXTRA=(-allowProvisioningUpdates)
  else
    echo "MW3_TVOS_TEAM not set: building unsigned (it cannot be installed; docs/tvos.md)"
    EXTRA=(CODE_SIGNING_ALLOWED=NO)
  fi
fi
cmake --build "$BUILD" --config Release --target mw3 -- ${EXTRA[@]+"${EXTRA[@]}"}
APP="$BUILD/Release-$SYSROOT/mw3.app"
echo "app: $APP, bundle ID $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Info.plist")"
