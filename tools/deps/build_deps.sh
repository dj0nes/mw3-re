#!/bin/bash
# The libraries the release bundles, built from their official release
# sources (Owen, 2026-10-30; docs/deps.md): SDL3, SDL3_ttf (with FreeType,
# built in), SDL3_sound and a minimal LGPL FFmpeg for mw3import. Each
# tarball is pinned by its sha256 (checked against the release's own
# signature when the pin was made, docs/deps.md); a download that does not
# match is refused.
#
#   tools/deps/build_deps.sh mac     universal (arm64, x86_64), macOS 12 and later
#   tools/deps/build_deps.sh linux   this machine's architecture
#
# Out: build-deps/<platform>/prefix (include, lib, lib/cmake, lib/pkgconfig),
# which tools/macos/make_app.sh and the Linux build take as CMAKE_PREFIX_PATH.
set -euo pipefail
cd "$(dirname "$0")/../.."
PLATFORM="${1:?mac or linux}"
ROOT="$PWD/build-deps"
DL="$ROOT/downloads"
PREFIX="$ROOT/$PLATFORM/prefix"
WORK="$ROOT/$PLATFORM/work"
JOBS="${JOBS:-$( (sysctl -n hw.ncpu 2>/dev/null || nproc) )}"  # JOBS=4 to share the machine

# name | url | sha256
PINS=(
  "SDL3-3.4.12.tar.gz|https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz|f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7"
  "freetype-2.13.2.tar.xz|https://download.savannah.gnu.org/releases/freetype/freetype-2.13.2.tar.xz|12991c4e55c506dd7f9b765933e62fd2be2e06d421505d7950a132e4f1bb484d"
  "SDL3_ttf-3.2.2.tar.gz|https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz|63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d"
  "SDL3_sound-3.2.0.tar.gz|https://github.com/icculus/SDL_sound/releases/download/v3.2.0/SDL3_sound-3.2.0.tar.gz|3348f3ecc653e21f2b7ed8e409f07c67b179fbbfba3b99c85698e6e5d7b5924c"
  "ffmpeg-8.1.2.tar.xz|https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz|464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c"
)

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

mkdir -p "$DL"
for p in "${PINS[@]}"; do
  IFS='|' read -r name url sum <<< "$p"
  [ -f "$DL/$name" ] || curl -sSfL -o "$DL/$name" "$url"
  got="$(sha256 "$DL/$name")"
  if [ "$got" != "$sum" ]; then echo "$name: sha256 $got, pinned $sum: refused" >&2; exit 1; fi
done
echo "sources: all five match their pins"

rm -rf "$WORK" "$PREFIX"
mkdir -p "$WORK" "$PREFIX"
unpack() { mkdir -p "$WORK/src" && tar -xf "$DL/$1" -C "$WORK/src"; }

CMAKE_COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX"
              -DCMAKE_INSTALL_LIBDIR=lib)
if [ "$PLATFORM" = mac ]; then
  CMAKE_COMMON+=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
                 -DCMAKE_INSTALL_NAME_DIR=@rpath)
else
  CMAKE_COMMON+=(-DCMAKE_INSTALL_RPATH='$ORIGIN')
fi
cmake_build() {  # source dir, then its options
  local src="$1"; shift
  cmake -S "$src" -B "$src/_build" "${CMAKE_COMMON[@]}" "$@" >/dev/null
  cmake --build "$src/_build" -j "$JOBS" >/dev/null
  cmake --install "$src/_build" >/dev/null
}

unpack SDL3-3.4.12.tar.gz
# Linux: XTest only feeds synthetic input; without its headers SDL refuses
# to configure X11 (docs/linux.md).
SDL_EXTRA=()
[ "$PLATFORM" = linux ] && SDL_EXTRA+=(-DSDL_X11_XTEST=OFF)
cmake_build "$WORK/src/SDL3-3.4.12" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
  ${SDL_EXTRA[@]+"${SDL_EXTRA[@]}"}
echo "built SDL3"

# FreeType inside SDL3_ttf (static, position-independent), with none of its
# optional libraries: the game's TrueType fonts need none of them.
unpack freetype-2.13.2.tar.xz
# Its cmake_minimum_required predates 3.5, which CMake 4 refuses: the policy floor raised.
cmake_build "$WORK/src/freetype-2.13.2" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
echo "built FreeType"

# SDL3_ttf without HarfBuzz (complex-script shaping) or PlutoSVG (SVG fonts):
# the game draws Latin text in its own TrueType fonts.
unpack SDL3_ttf-3.2.2.tar.gz
cmake_build "$WORK/src/SDL3_ttf-3.2.2" -DBUILD_SHARED_LIBS=ON -DSDLTTF_VENDORED=OFF -DSDLTTF_HARFBUZZ=OFF \
  -DSDLTTF_PLUTOSVG=OFF -DSDLTTF_SAMPLES=OFF
echo "built SDL3_ttf"

unpack SDL3_sound-3.2.0.tar.gz
cmake_build "$WORK/src/SDL3_sound-3.2.0" -DSDLSOUND_BUILD_SHARED=ON -DSDLSOUND_BUILD_STATIC=OFF \
  -DSDLSOUND_BUILD_TEST=OFF -DSDLSOUND_BUILD_DOCS=OFF
echo "built SDL3_sound"

# FFmpeg: only what mw3import uses (the AVI demuxer, the Indeo 5 decoder,
# the MPEG-1 video encoder and muxer, swscale, the file protocol), LGPL 2.1,
# shared. FFmpeg configures one architecture at a time: on the Mac each is
# built and installed apart, then the dylibs are joined with lipo.
FFMPEG_OPTS=(--enable-shared --disable-static --disable-everything --disable-autodetect --disable-programs
             --disable-doc --disable-network --disable-avdevice --disable-avfilter --disable-swresample
             --enable-protocol=file --enable-demuxer=avi --enable-decoder=indeo5 --enable-encoder=mpeg1video
             --enable-muxer=mpeg1video --enable-swscale)
unpack ffmpeg-8.1.2.tar.xz
ff_build() {  # install dir, then extra configure options
  local dst="$1"; shift
  local b="$WORK/ffmpeg-build-$(basename "$dst")"
  rm -rf "$b" && mkdir -p "$b"
  (cd "$b" && "$WORK/src/ffmpeg-8.1.2/configure" --prefix="$dst" "${FFMPEG_OPTS[@]}" "$@" >/dev/null &&
     make -j "$JOBS" >/dev/null && make install >/dev/null)
}
if [ "$PLATFORM" = mac ]; then
  for arch in arm64 x86_64; do
    extra=(--arch="$arch" --cc="clang -arch $arch" --extra-cflags="-mmacosx-version-min=12.0"
           --extra-ldflags="-arch $arch -mmacosx-version-min=12.0" --install-name-dir=@rpath)
    [ "$arch" = "$(uname -m)" ] || extra+=(--enable-cross-compile --target-os=darwin --disable-x86asm)
    ff_build "$WORK/ffmpeg-$arch" "${extra[@]}"
  done
  # One tree (headers, pkg-config) with each dylib joined.
  cp -R "$WORK/ffmpeg-arm64/include/." "$PREFIX/include/"
  mkdir -p "$PREFIX/lib/pkgconfig"
  for f in "$WORK"/ffmpeg-arm64/lib/*.dylib; do
    n="$(basename "$f")"
    if [ -L "$f" ]; then cp -P "$f" "$PREFIX/lib/$n"; else lipo -create "$f" "$WORK/ffmpeg-x86_64/lib/$n" -output "$PREFIX/lib/$n"; fi
  done
  for f in "$WORK"/ffmpeg-arm64/lib/pkgconfig/*.pc; do
    sed "s|$WORK/ffmpeg-arm64|$PREFIX|g" "$f" > "$PREFIX/lib/pkgconfig/$(basename "$f")"
  done
else
  # Without nasm FFmpeg's x86 assembly is left out (the Indeo decoder a little slower).
  asm=(); command -v nasm >/dev/null || asm=(--disable-x86asm)
  ff_build "$PREFIX" --enable-pic --extra-ldflags='-Wl,-rpath,$$ORIGIN' ${asm[@]+"${asm[@]}"}
fi
echo "built FFmpeg"
echo "prefix: $PREFIX"
