# Apple TV (tvOS)

A personal build of the rebuild for one Apple TV: the `mw3` program as a
tvOS app, sideloaded from Xcode and never distributed. Branch `tvos`, not
for upstream. The design is
`docs/superpowers/specs/2026-11-05-tvos-design.md`. Checked with Xcode
26.6, the tvOS 26.5 SDK, and an Apple TV 4K (2nd generation, A12).

## What is in the app

- `mw3` only, built with `-DMW3_TVOS=ON` (CMakeLists.txt; off by default,
  so no other build changes). No tests, launcher, importer or tools.
- No child processes: tvOS cannot start one, so `sdl_child_tvos.cpp`
  stands in for `sdl_child.cpp` and every child fails to start (a movie
  helper or the importer reads as not there).
- No libarchive: the app has no SETUP.
- SDL3, SDL3_ttf (with FreeType) and SDL3_sound, static, from
  `tools/deps/build_deps.sh tvos` or `tvos-sim` (docs/deps.md, tvOS).
- `tools/tvos/Info.plist.in`: bundle ID `org.mw3rebuild.tv`, the name
  "MechWarrior 3", arm64, an extended gamepad as the supported controller.
- `mw3_main.cpp` under `SDL_PLATFORM_TVOS`: `SDL_main.h` (SDL starts
  `main` from UIKit), and `SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS` set to
  "0" before `SDL_Init`, so the pad's Menu button reaches the game as a
  button instead of taking the player to the home screen.

The write dir (saves, `mw3.cfg`, `mw3.log`) is where `SDL_GetPrefPath`
puts it on tvOS: the app's `Library/Caches/MicroProse/MechWarrior 3/`
(seen in the Simulator, 2026-11-05). tvOS may clear Caches when it runs
short of space; that risk is accepted. Deleting the app deletes the
pilots.

## Building

```sh
tools/tvos/build.sh --sim              # build-tvos-sim/Release-appletvsimulator/mw3.app
tools/tvos/build.sh --device           # build-tvos/Release-appletvos/mw3.app
tools/tvos/build.sh --device --open    # the same project, opened in Xcode
```

The script builds the libraries first if their prefix is missing (about 20
seconds each), configures an Xcode project (`-G Xcode
-DCMAKE_SYSTEM_NAME=tvOS -DMW3_TVOS=ON -DMW3_PACKAGE=ON`), then builds the
`mw3` target in Release or opens the project. CMake searches only under its
find roots when it builds for tvOS, so the libraries' prefix is passed as
`CMAKE_FIND_ROOT_PATH` as well as `CMAKE_PREFIX_PATH`.

`--device` without `MW3_TVOS_TEAM` builds unsigned: it proves the app
compiles and links for the device, but it cannot be installed.

## Signing and installing on the Apple TV

1. Pair the Apple TV with the Mac once. Both on the same network; on the
   Apple TV open Settings, Remotes and Devices, Remote App and Devices. In
   Xcode open Window, Devices and Simulators: the Apple TV shows under
   Discovered. Pair it and type the code the TV shows.
2. Add your Apple ID in Xcode (Settings, Accounts) if it is not there.
   Its team is the signing team.
3. Find the team ID: Xcode's Settings, Accounts, the team's row; or, after
   step 4, the project's Signing & Capabilities tab. Then:

   ```sh
   export MW3_TVOS_TEAM=<team ID>
   tools/tvos/build.sh --device --open
   ```

   The team goes into the project (`DEVELOPMENT_TEAM`) with automatic
   signing. A team picked by hand in Xcode is lost the next time the
   script configures, so set the variable.
4. In Xcode pick the scheme `mw3` and the Apple TV as the destination, and
   press Run. Xcode registers the bundle ID, makes the provisioning
   profile and installs the app.
5. If tvOS asks for Developer Mode, turn it on in the Apple TV's Settings,
   Privacy & Security, Developer Mode, and let it restart.

A free Apple ID signs for 7 days: after that the app will not open until it
is installed again (steps 3 and 4). Installing over the old app keeps its
data, since the bundle ID does not change.
