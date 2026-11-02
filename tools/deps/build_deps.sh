#!/bin/bash
# The libraries the release bundles, built from their official release
# sources (Owen, 2026-10-30; docs/deps.md): SDL3, SDL3_ttf (with FreeType,
# built in), SDL3_sound, a minimal LGPL FFmpeg for mw3import, and libarchive
# with zlib and liblzma (static, for SETUP's "Point to your game"). Each
# tarball is pinned by its sha256 (checked against the release's own
# signature when the pin was made, docs/deps.md); a download that does not
# match is refused.
#
#   tools/deps/build_deps.sh mac     universal (arm64, x86_64), macOS 12 and later
#   tools/deps/build_deps.sh linux   this machine's architecture
#   tools/deps/build_deps.sh win     FFmpeg only, for Windows x64, cross-built
#                                    with mingw-w64 (the PC takes the rest
#                                    from their official Windows releases)
#
# Out: build-deps/<platform>/prefix (include, lib, lib/cmake, lib/pkgconfig),
# which tools/macos/make_app.sh and the Linux build take as CMAKE_PREFIX_PATH.
set -euo pipefail
cd "$(dirname "$0")/../.."
PLATFORM="${1:?mac, linux or win}"
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
  "zlib-1.3.1.tar.gz|https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz|9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"
  "xz-5.8.1.tar.xz|https://github.com/tukaani-project/xz/releases/download/v5.8.1/xz-5.8.1.tar.xz|0b54f79df85912504de0b14aec7971e3f964491af1812d83447005807513cd9e"
  "libarchive-3.7.9.tar.xz|https://www.libarchive.org/downloads/libarchive-3.7.9.tar.xz|ed8b5732e4cd6e30fae909fb945cad8ff9cb7be5c6cdaa3944ec96e4a200c04c"
)

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

mkdir -p "$DL"
for p in "${PINS[@]}"; do
  IFS='|' read -r name url sum <<< "$p"
  [ -f "$DL/$name" ] || curl -sSfL -o "$DL/$name" "$url"
  got="$(sha256 "$DL/$name")"
  if [ "$got" != "$sum" ]; then echo "$name: sha256 $got, pinned $sum: refused" >&2; exit 1; fi
done
echo "sources: all ${#PINS[@]} match their pins"

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

if [ "$PLATFORM" != win ]; then
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
fi

# FFmpeg: only what mw3import uses (the AVI demuxer, the Indeo 5 decoder,
# the MPEG-1 video encoder and muxer, swscale, the file protocol), LGPL 2.1,
# shared. FFmpeg configures one architecture at a time: on the Mac each is
# built and installed apart, then the dylibs are joined with lipo.
FFMPEG_OPTS=(--enable-shared --disable-static --disable-everything --disable-autodetect --disable-programs
             --disable-doc --disable-network --disable-avdevice --disable-avfilter --disable-swresample
             --enable-protocol=file --enable-demuxer=avi --enable-decoder=indeo5 --enable-encoder=mpeg1video
             --enable-muxer=mpeg1video --enable-swscale)
unpack ffmpeg-8.1.2.tar.xz
# FFmpeg keeps its configure line in each library (avutil_configuration()),
# so the prefix is a neutral /ffbuild and the install goes through DESTDIR:
# the build folder (and the user name in it) stays out of the package.
ff_build() {  # staging dir (the tree lands in <dir>/ffbuild), then extra configure options
  local dst="$1"; shift
  local b="$WORK/ffmpeg-build-$(basename "$dst")"
  rm -rf "$b" "$dst" && mkdir -p "$b"
  (cd "$b" && "$WORK/src/ffmpeg-8.1.2/configure" --prefix=/ffbuild "${FFMPEG_OPTS[@]}" "$@" >/dev/null &&
     ff_config_edit && make -j "$JOBS" >/dev/null && make install DESTDIR="$dst" >/dev/null)
}
# mingw-w64 has clock_gettime and nanosleep only in libwinpthread-1.dll:
# without them avutil uses Windows' own clock and Sleep, and the DLLs load
# only system DLLs (docs/deps.md).
ff_config_edit() {
  [ "$PLATFORM" = win ] || return 0
  sed -i.orig -e 's/^#define HAVE_CLOCK_GETTIME 1$/#define HAVE_CLOCK_GETTIME 0/' \
    -e 's/^#define HAVE_NANOSLEEP 1$/#define HAVE_NANOSLEEP 0/' config.h
}
ff_pc() {  # the staged .pc files into PREFIX, pointed at it
  mkdir -p "$PREFIX/lib/pkgconfig"
  for f in "$1"/ffbuild/lib/pkgconfig/*.pc; do
    sed "s|/ffbuild|$PREFIX|g" "$f" > "$PREFIX/lib/pkgconfig/$(basename "$f")"
  done
}
if [ "$PLATFORM" = mac ]; then
  for arch in arm64 x86_64; do
    extra=(--arch="$arch" --cc="clang -arch $arch" --extra-cflags="-mmacosx-version-min=12.0"
           --extra-ldflags="-arch $arch -mmacosx-version-min=12.0" --install-name-dir=@rpath)
    [ "$arch" = "$(uname -m)" ] || extra+=(--enable-cross-compile --target-os=darwin --disable-x86asm)
    ff_build "$WORK/ffmpeg-$arch" "${extra[@]}"
  done
  # One tree (headers, pkg-config) with each dylib joined.
  cp -R "$WORK/ffmpeg-arm64/ffbuild/include/." "$PREFIX/include/"
  for f in "$WORK"/ffmpeg-arm64/ffbuild/lib/*.dylib; do
    n="$(basename "$f")"
    if [ -L "$f" ]; then cp -P "$f" "$PREFIX/lib/$n"; else lipo -create "$f" "$WORK/ffmpeg-x86_64/ffbuild/lib/$n" -output "$PREFIX/lib/$n"; fi
  done
  ff_pc "$WORK/ffmpeg-arm64"
elif [ "$PLATFORM" = win ]; then
  # Windows x64 with mingw-w64's GCC (its UCRT), Windows threads, libgcc
  # linked in, no x86 assembly (no nasm here). The layout is a shared build's
  # (include\, lib\*.lib, bin\*.dll), as CMake takes it from deps\ffmpeg-*shared*.
  ff_build "$WORK/ffmpeg-win64" --target-os=mingw32 --arch=x86_64 --cross-prefix=x86_64-w64-mingw32- \
    --enable-cross-compile --disable-x86asm --enable-w32threads --extra-ldflags=-static-libgcc
  st="$WORK/ffmpeg-win64/ffbuild"
  mkdir -p "$PREFIX/bin" "$PREFIX/lib"
  cp -R "$st/include" "$PREFIX/"
  cp "$st"/bin/*.dll "$PREFIX/bin/"
  cp "$st"/bin/*.lib "$PREFIX/lib/"   # dlltool's import libraries, which MSVC links
  cp "$WORK/src/ffmpeg-8.1.2/COPYING.LGPLv2.1" "$WORK/src/ffmpeg-8.1.2/LICENSE.md" "$PREFIX/"
  # Each DLL loads only Windows' own (KERNEL32, bcrypt, the UCRT's
  # api-ms-win-crt-*) and the other FFmpeg DLLs: no libgcc or winpthread.
  for f in "$PREFIX"/bin/*.dll; do
    other=$(x86_64-w64-mingw32-objdump -p "$f" | awk '/DLL Name:/{print $3}' |
            grep -viE '^(kernel32|bcrypt)\.dll$|^api-ms-win-crt-|^(avutil|avcodec|avformat|swscale)-[0-9]+\.dll$' || true)
    [ -z "$other" ] || { echo "$(basename "$f") loads $other: refused" >&2; exit 1; }
  done
else
  # Without nasm FFmpeg's x86 assembly is left out (the Indeo decoder a little slower).
  asm=(); command -v nasm >/dev/null || asm=(--disable-x86asm)
  ff_build "$WORK/ffmpeg-native" --enable-pic --extra-ldflags='-Wl,-rpath,$$ORIGIN' ${asm[@]+"${asm[@]}"}
  cp -R "$WORK/ffmpeg-native/ffbuild/include/." "$PREFIX/include/"
  cp -P "$WORK"/ffmpeg-native/ffbuild/lib/*.so* "$PREFIX/lib/"
  ff_pc "$WORK/ffmpeg-native"
fi
echo "built FFmpeg"
if [ "$PLATFORM" = win ]; then echo "prefix: $PREFIX"; exit 0; fi

# SETUP's archives (docs/setup-sources.md): libarchive reading zip, 7z, RAR4/5,
# tar and ISO 9660, with zlib (deflate) and liblzma (7z's LZMA/LZMA2, xz);
# all three static and position-independent, linked into the programs, so the
# packages carry no more libraries. No crypto, XML, bzip2, zstd, LZ4 or iconv:
# none of the game's sources needs them.
unpack zlib-1.3.1.tar.gz
cmake_build "$WORK/src/zlib-1.3.1" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DZLIB_BUILD_EXAMPLES=OFF
rm -f "$PREFIX"/lib/libz*.dylib "$PREFIX"/lib/libz.so*  # the static one only
echo "built zlib"
unpack xz-5.8.1.tar.xz
cmake_build "$WORK/src/xz-5.8.1" -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DXZ_TOOL_XZ=OFF \
  -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF -DXZ_DOC=OFF -DXZ_NLS=OFF
echo "built liblzma"
unpack libarchive-3.7.9.tar.xz
cmake_build "$WORK/src/libarchive-3.7.9" -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF -DENABLE_TEST=OFF -DENABLE_INSTALL=ON \
  -DENABLE_ZLIB=ON -DENABLE_LZMA=ON -DENABLE_BZip2=OFF -DENABLE_ZSTD=OFF -DENABLE_LZ4=OFF -DENABLE_LIBB2=OFF \
  -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF -DENABLE_NETTLE=OFF -DENABLE_CNG=OFF -DENABLE_LIBXML2=OFF \
  -DENABLE_EXPAT=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF \
  -DENABLE_ICONV=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_LZO=OFF
rm -f "$PREFIX"/lib/libarchive*.dylib "$PREFIX"/lib/libarchive.so*
echo "built libarchive"
# Their licences, for the packages (linked in, so no library file carries them).
for l in "libarchive-3.7.9 COPYING" "zlib-1.3.1 LICENSE" "xz-5.8.1 COPYING COPYING.0BSD"; do
  set -- $l; d=$1; shift
  mkdir -p "$PREFIX/share/licenses/$d"
  for f in "$@"; do cp "$WORK/src/$d/$f" "$PREFIX/share/licenses/$d/"; done
done
echo "prefix: $PREFIX"
