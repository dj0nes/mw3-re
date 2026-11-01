# Cutting a release

A checklist for the three platforms' packages. Owen approves every tag and
every GitHub release; nothing here publishes anything by itself. The
packages are built from one commit, so every platform's version is the same.

## MW3 v1.0: "done"

Owen, 2026-10-31: MechWarrior 3 is declared done with the tag `v1.0`
before any Pirate's Moon work reaches `main`. Until then Pirate's Moon
lives on its own branch (`mac5/pm`); after `v1.0` it merges in, and later
releases carry both games through the launcher. `v1.0` is tagged when
every box here is ticked:

- [ ] Multiplayer ported (`docs/multiplayer-port.md`, every piece), with
      the two-process and soak tests passing.
- [x] The 1.2 patch rebuilt from 1.0 data (`docs/patch12.md`), and SETUP's
      patch button gone (shown only for data of an unknown version).
- [ ] The stealth object and BASIC ported as the original has them, its
      fly camera on F11 under --debug-camera (docs/editor.md S1-S4). The
      Battlesport editor's menus (the AI net editor, the vehicle editor) come
      after v1.0, with Pirate's Moon, whose net editor changed (Owen,
      2026-11-01).
- [x] SETUP takes any source foolproof: an install, a disc, .iso, .cue/.bin,
      .zip (nested too), .7z; maps it and installs without the player
      unpacking anything (`docs/setup-sources.md`; the packages carry
      libarchive, zlib and liblzma with their licences).
- [x] The software renderer no longer offered anywhere in the menus.
- [x] Every row in `docs/playtest.md` closed: fixed, by design or the
      original's, none open.
- [x] The community-reported problems (`docs/community-issues.md`) each
      have a verdict (Owen, 2026-10-31: items 9 and 12 kept as the original).
- The whole campaign on the release build: the automated 20-mission run
  (`sdl_program_campaign_by_objectives`) counts; Owen's own playthrough is a
  later playtest, not a gate (Owen, 2026-10-31).
- [ ] Every check in sections 1-4 below passes on all three platforms.

## 1. Before

- [ ] `main` is the commit to release, with nothing uncommitted.
- [ ] The tests pass on that commit: `mw3tests` and `mw3sdltests` on the Mac
      and Windows, the differential tests on the Mac
      (`tests/differential/run_all.sh`), and on Linux `mw3tests` and
      `mw3sdltests` one test at a time under `run_capped.py` (WSL: docs/linux.md,
      Checking).
- [ ] `CHANGES.md` has a line for every change since the last release.
- [ ] The release notes are drafted (`docs/release-notes-<version>.md`) and
      Owen has read them.
- [ ] The players' read-mes match the build: `tools/windows/dist/README.txt`,
      `tools/macos/README.txt`, the top-level `README.md`.
- [ ] Open since 2026-11-01: the Mac `--deps` release form
      (`tools/macos/make_app.sh --deps`) not built since libarchive, zlib
      and liblzma arrived: rebuild build-deps/mac (`build_deps.sh mac`),
      build the app, and check it is universal, links them in, and carries
      their licences in Contents/Resources/licenses.

## 2. The version

The version is git's: `git describe --tags --always --dirty` (CMake reads it
into `MW3_VERSION`, which `mw3 --version`, the Windows version resource, the
Mac's Info.plist and every package's file name carry), and the commit count
into `MW3_BUILD_NUMBER`.

- [ ] Owen tags the commit (`git tag -a v0.1 -m ...`), or approves the tag.
      Until then the version is the commit's short hash.
- [ ] The version does not end in `-dirty`: every package script builds from
      scratch, and a dirty tree names every file.
- [ ] Each platform builds from the same tagged commit.

## 3. The packages

All three are configured with `-DMW3_PACKAGE=ON`, so no development copy of
the game's path is built into the programs.

### Windows (on the PC)

`tools\windows\package.cmd` (docs/windows.md, Packaging) makes
`build-win\dist\mw3-<version>-win64\`, its `.zip`, and
`MW3-Rebuild-Setup-<version>.exe` (Inno Setup).

- [ ] `mw3.exe --version` names the version.
- [ ] The folder has `mw3.exe`, `mw3indeo.exe`, `mw3import.exe`, the SDL and
      FFmpeg DLLs, the Visual C++ runtime, `README.txt`, `THIRD-PARTY.txt`,
      `licenses\`, `assets\launcher\`.
- [ ] On a clean user: the installer installs, the launcher finds the
      install (or SETUP does), a mission plays, the intro movie plays through
      `mw3indeo.exe`, and with the helper renamed `mw3import.exe` converts
      the movies instead.
- [ ] The uninstaller removes the program and keeps
      `%APPDATA%\MicroProse\MechWarrior 3\`.

### Mac

`tools/deps/build_deps.sh mac`, then `tools/macos/make_app.sh --deps`
(docs/macos.md) makes `build-mac/dist/MW3-Rebuild-<version>-mac.dmg`.

- [ ] Every binary in the bundle is x86_64 and arm64 (`lipo -archs`), with
      minimum macOS 12.0, and loads only system libraries and the bundle's
      own (`otool -L`: no `/opt/homebrew`).
- [ ] `codesign -dv` on the app: `Signature=adhoc`, `TeamIdentifier=not set`.
- [ ] Opened from the DMG on a Mac that has not seen it: Gatekeeper's
      first-open steps in the README work.
- [ ] It starts and plays a mission natively and under Rosetta
      (`arch -x86_64`).

### Linux

`tools/deps/build_deps.sh linux` (JOBS=6 under WSL), then
`tools/linux/make-appimage.sh build-deps/linux/prefix <tools dir>`
(docs/linux.md) makes `build-linux-pkg/dist/MW3-Rebuild-<version>-x86_64.AppImage`.

- [ ] `AppDir/usr/lib` has SDL3, SDL3_ttf, SDL3_sound and the four FFmpeg
      libraries; `ldd` on `mw3` and `mw3import` finds them there, and nothing
      else from the build prefix.
- [ ] The launcher shows its text; with an install, a mission plays.
- [ ] `--import --movie INTRO` converts the movies and plays the intro.
- [ ] `AppDir/usr/share/doc/mw3/licenses` has every bundled library's
      licence files.

## 4. Privacy

Every file that leaves the machine (the packages, the read-mes, the release
notes, the README) is checked before it goes up.

- [ ] **No legal name.** The credits are "Owen Morgan" everywhere: LICENSE,
      the README, `THIRD-PARTY.txt`, the installer, the Info.plist, the
      release notes. Search each package's text and binaries for Owen's other
      name and for his email addresses (`strings -a`, `grep -ri`); there must
      be no match.
- [ ] **No team ID or signing identity.** The Mac app is signed ad hoc
      (above); the Windows files carry no certificate.
- [ ] **No local paths.** No machine's folders in the binaries or the text:
      `strings -a <each binary> | grep -E '/Users/|/Volumes/|/home/|C:\\Users\\|E:\\'`
      must print nothing. (`MW3_PACKAGE=ON` keeps the game folder out; the
      compilers' own `__FILE__` strings must be relative or absent.)
- [ ] **No game files.** No `.zbd`, `.avi`, `.wav`, `.zrd` or other MicroProse
      data in any package; only our own launcher art (`assets/launcher`).
- [ ] **No logs or settings.** No `mw3.log`, `mw3.cfg`, `settings.reg`,
      pilots or saves in any package.
- [ ] The release notes and the README name no machine, folder or private
      service.

## 5. Publishing (Owen)

- [ ] The tag pushed, the GitHub release made with the notes, and the three
      platforms' files uploaded, with a `SHA256SUMS` of them.
- [ ] Each upload downloaded again and its checksum checked.
