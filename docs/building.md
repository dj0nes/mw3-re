# Building and running from source

For developers. Players: the downloads and the first start are in the
[README](../README.md).

## Build

Needs CMake 3.20+, a C++17 compiler and, for the program itself, SDL3,
SDL3_ttf and SDL3_sound (macOS: `brew install cmake ninja sdl3 sdl3_ttf
sdl3_sound`; the release builds them from their pinned sources,
[deps.md](deps.md)). `mw3import` also needs FFmpeg's libraries; without
them it is not built and the game is.

```bash
cmake -S . -B build -G Ninja && cmake --build build
./build/mw3tests          # the unit tests
./build/mw3sdltests       # the SDL front end's tests
```

Per platform: [windows.md](windows.md), [macos.md](macos.md),
[linux.md](linux.md). Cutting a release: [release.md](release.md).

## Run

A development build finds the game in `game/mw3-1.2/` (`MW3_GAME_DIR`), or
take any install:

```bash
./build/mw3                              # the launcher, then the game
./build/mw3 path/to/mw3-1.2              # that install, straight to the game
./build/mw3 --mission c1m1               # straight into a mission
./build/mw3 --fly c1                     # a free camera over a mission world
./build/mw3 --original                   # the original's behaviour (bug fixes off)
```

No install? Install it from the disc, with no Windows needed: give
`--install-from` the CD drive, a disc image (`.iso`) or a `.cue`, and the
1.2 patch if you have it. The game is copied into a folder of its own (or
`--install-to DIR`) and remembered; a `.cue`'s CD audio becomes the music.
More in [assets.md](assets.md), Installing from the disc.

```bash
./build/mw3 --install-from /Volumes/MW3 --patch MechWarrior-3_Patch.zip     # the disc, macOS
./build/mw3 --install-from "MechWarrior 3 (USA).cue"                       # a Redump image
```

Music: the original plays CD audio. Rip the disc's tracks to
`music/track02.ogg`, `track03.ogg` and so on (ogg, flac, wav or mp3),
numbered as on the disc. More in [program.md](program.md).

## The differential tests

They run the original x86 code in the Unicorn emulator beside the port.
They need your game files in `game/mw3-1.2/` and Python with Unicorn:

```bash
python3 -m venv .venv && .venv/bin/pip install unicorn
tests/differential/run_all.sh
```

## Layout

- `src/gamez/<module>/`, `src/mech3/`: the rebuild, laid out like the
  original source tree recovered from the executable's debug strings
  ([module-map.md](module-map.md)).
- `src/platform/`, `src/frontend/`: SDL3 in place of Win32, DirectX and MCI.
- `docs/`: how each system works, the evidence, and the open questions.
  [fixes.md](fixes.md) is the technical record of every change.
- `tests/`: unit tests, and the differential tests against the original.
- `tools/`: the analysis scripts, the importer and the package scripts.
