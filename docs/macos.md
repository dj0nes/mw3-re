# The macOS app

`tools/macos/make_app.sh` makes the release for Mac players:
`build-mac/dist/MechWarrior 3 Rebuild.app` and
`build-mac/dist/MW3-Rebuild-<git describe>-mac.dmg`. Run from the repo root.
The release form is `tools/macos/make_app.sh --deps` (after
`tools/deps/build_deps.sh mac`, docs/deps.md): universal (arm64, x86_64),
macOS 12 and later, against the libraries built from their release sources
(about 70 s at `CMAKE_BUILD_PARALLEL_LEVEL=4`). Without `--deps` it takes
Homebrew's (this Mac's macOS, arm64 only) and says it is not for release:
THIRD-PARTY.txt describes the `--deps` build.

- A package build (`-DMW3_PACKAGE=ON`, Release, its own tree
  `build-mac-pkg/`): no development copy of the game compiled in.
- `Contents/MacOS/mw3`; every library outside the system, followed through
  the ones those load, copied into `Contents/Frameworks` and every
  reference rewritten to `@rpath/<name>`, with the rpath
  `@executable_path/../Frameworks`. The script stops if any reference is
  left outside the bundle. With `/opt/homebrew` unreadable
  (`sandbox-exec`), the app still starts c1m1; the development build does
  not even load.
- `Contents/Resources`: `mw3.icns` (our placeholder, `make_icon.py`'s
  drawing at each iconset size), the README, `LICENSE.txt`,
  `THIRD-PARTY.txt`, and each bundled library's licence files under
  `licenses/<library>/<source>/` (with `--deps`, from the source trees;
  FreeType's FTL.TXT too), or `licenses/<formula>/` from Homebrew's kegs.
- `Info.plist`: bundle id `com.owenmorgan.mw3rebuild`, the version from
  git, `LSMinimumSystemVersion` from the binary (see below), games
  category.
- Signed **ad hoc** (`codesign -s -`): the code is sealed, but no identity
  or name is in the signature (Owen, 2026-10-30: the releases are not
  Developer ID signed and not notarized, for his privacy; he publishes as
  Owen Morgan). `tools/macos/mw3.entitlements` grants nothing: the app needs
  no exception. The DMG is not signed.
- An explicit opt-in, `--identity "<IDENTITY>"`, signs with a keychain
  identity, the hardened runtime and a secure timestamp instead; never used
  for releases.

The game's files, settings and saves go to the write dir,
`~/Library/Application Support/MicroProse/MechWarrior 3/`, as for the
development build; with no console, nothing else changes on the Mac (the
terminal shows the output when it is started from one).

## The first open

An app without a Developer ID signature and notarization is blocked by
Gatekeeper on its first open. The player opens it once by right-click (or
Control-click) > Open > Open, or after a blocked attempt, System Settings >
Privacy & Security > "Open Anyway"; macOS remembers it after that. The
README says so.

## Which macOS it runs on, and Intel Macs

With `--deps` (2026-10-31): **macOS 12 or later, Apple silicon and Intel**.
SDL3 3.4.12, SDL3_ttf 3.2.2 (FreeType 2.13.2 built in, no HarfBuzz) and
SDL3_sound 3.2.0, three libraries in place of Homebrew's ten. Checked: every
binary in the bundle x86_64 + arm64 with minimum 12.0 (`LSMinimumSystemVersion`
12.0), only the bundle's rpath, the signature ad hoc with no team; with
`/opt/homebrew` unreadable (`sandbox-exec`) both slices start c1m1 and run
60 frames (`arch -arm64`, `arch -x86_64` under Rosetta), exit 0. Not yet
run on a real macOS 12 or an Intel Mac. (The bundling follows `@rpath`
names into the prefix; a universal binary's `otool -L` lists each arch under
its own header, and its rpath once per arch.)

## The movies

With `--deps` (2026-10-31) the app carries `mw3import` in `Contents/MacOS`
beside `mw3`, on the minimal FFmpeg 8.1.2 build_deps.sh builds (LGPL 2.1;
four dylibs in `Contents/Frameworks`, their licence files under
`licenses/`, THIRD-PARTY.txt's FFmpeg section with the configure line's
place). The first start converts the movies the way Windows does
(first_run_import.h): SDL's base path in an app is `Contents/Resources`, so
the importer is looked for in `../MacOS` there too. Without `--deps`
(Homebrew's FFmpeg would bring dozens of libraries) there is no importer.

Checked with `/opt/homebrew` unreadable, a fresh write dir and `--import`:
both slices convert all 19 movies (arm64 75 s, x86_64 under Rosetta 104 s)
and then start c1m1; c1m2 then plays its 475 frames in either. The arm64
slice's files are byte for byte the development importer's (Homebrew's
FFmpeg); the x86_64 slice's video differs by 0.1% in size (its FFmpeg has no
hand-written assembly), its sound and index identical.

A stray FFmpeg 7 in `/usr/local/include` (a root-owned install on this Mac)
won over the prefix's `-isystem` headers and gave mw3import another
`AVCodecContext` layout (the decoder's size read as 480x640, "no MPEG-1
encoder"); mw3import takes FFmpeg's headers by `-I`
(`NO_SYSTEM_FROM_IMPORTED`).

The launcher's art goes in `Contents/Resources/assets/launcher` (SDL's base
path). Package builds no longer compile in the source tree's `assets` path
(`MW3_ASSETS_DIR`, development builds only), which had put this Mac's path in
the release binary and found the art there instead of in the app.
