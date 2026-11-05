# MechWarrior 3 on your own Apple TV

This builds the rebuild as an Apple TV app and installs it from Xcode onto
your own Apple TV. It is for playing on your own TV with a game controller.
It is not on the App Store, and the app carries your own copy of the game,
so do not share the app you build.

## What you need

- A Mac with Xcode 26 or later (checked with Xcode 26.6 and the tvOS 26.5
  SDK). In Xcode, Settings, Components, install the tvOS platform.
- CMake 3.20 or later and Ninja (`brew install cmake ninja`).
- An Apple TV 4K or Apple TV HD with tvOS 17 or later, on the same network
  as the Mac.
- An Apple ID. A free one works; the app then stops opening after 7 days
  until you install it again (see Re-signing). A paid Apple Developer
  account signs for a year.
- A game controller paired with the Apple TV (an Xbox or PlayStation
  controller, or any MFi gamepad).
- MechWarrior 3 set up on the Mac: start the Mac rebuild once, point it at
  your disc or install, and let it convert the movies. The Apple TV build
  copies the game from there (see Where the game files come from).

## Build

From the repository's folder:

```sh
tools/deps/build_deps.sh tvos       # the libraries, once (about 20 seconds)
tools/tvos/build.sh --device --data --open
```

The first command builds SDL3, SDL3_ttf and SDL3_sound for tvOS from their
pinned sources (docs/deps.md). `build.sh` builds them itself if you skip it.
The second makes the Xcode project in `build-tvos/`, sets it to copy your
game files into the app on every build, and opens it in Xcode.

## Run it on the Apple TV

1. Pair the Apple TV with Xcode, once. On the Apple TV open Settings,
   Remotes and Devices, Remote App and Devices. On the Mac, in Xcode, open
   Window, Devices and Simulators. The Apple TV shows under Discovered:
   choose Pair and type the code the TV shows.
2. Add your Apple ID to Xcode, once: Xcode, Settings, Accounts, the plus
   button.
3. In the Xcode window `build.sh` opened, pick the scheme `mw3` at the top,
   and your Apple TV as the destination next to it.
4. Pick your team: select the project `mw3re` in the left column, then the
   target `mw3`, the tab Signing & Capabilities, and your name under Team
   ("Your Name (Personal Team)" for a free Apple ID).
5. Press Run (the triangle, or Command-R). Xcode signs the app, installs it
   and starts it.
6. The first time, the Apple TV may ask you to turn on Developer Mode:
   Settings, Privacy & Security, Developer Mode. It restarts; then press Run
   again.

Choosing the team in Xcode lasts until `build.sh` runs again. To keep it,
give the team's ID to the script instead:

```sh
export MW3_TVOS_TEAM=ABCDE12345     # your team's ID
tools/tvos/build.sh --device --data --open
```

The team's ID is the 10 letters and digits Xcode shows for the team in
Settings, Accounts (or in the build settings as `DEVELOPMENT_TEAM` after
you pick it in step 4).

The app's bundle ID is `org.mw3rebuild.tv.<your team ID>`, or
`org.mw3rebuild.tv` before a team is set. Set `MW3_TVOS_BUNDLE_ID` to use
another one. Keep it the same from one build to the next: the app's saves
belong to its bundle ID.

## Where the game files come from

`--data` copies your game into the app each time Xcode builds it. It reads
the Mac rebuild's folder, `~/Library/Application Support/MicroProse/
MechWarrior 3/`:

- the install that folder's `mw3.cfg` names (`install=`), or its
  `install/` folder, into the app's `game/` folder. The original AVI movies
  are left out; the app plays the converted ones.
- the converted movies (`video/`) and the CD music (`music/`) into the
  app's `media/` folder.

Set `MW3_TVOS_DATA` to read another folder laid out the same way. The app is
about 800 MB. Nothing from the game goes into this repository.

The app's saves and settings live on the Apple TV, in the app's own
storage. Installing the app again keeps them. Deleting the app deletes your
pilots, and tvOS may clear them if the Apple TV runs very short of space.

## Playing

The app starts the game straight away. Turn the controller on before or
after starting it: the first gamepad to connect is the game's. In the menus
the left stick moves the cursor and A clicks; in a mission the sticks,
triggers and buttons are the game's joystick, and holding LB gives the
keyboard's actions. The full list is in [pad-bridge.md](pad-bridge.md).

The controller's Menu (Start) button opens the game's pause menu. To leave
the app, press the TV button on the Siri Remote; the remote is not a game
controller here.

## Re-signing every 7 days (free Apple ID)

A free Apple ID's signature lasts 7 days. After that the app will not open.
Connect as before and press Run in Xcode again (or run `build.sh` again and
then Run). Your pilots stay, as long as the bundle ID has not changed.

## Trying it without an Apple TV

```sh
tools/deps/build_deps.sh tvos-sim
tools/tvos/build.sh --sim --data
```

builds `build-tvos-sim/Release-appletvsimulator/mw3.app` for the tvOS
Simulator. Install and start it with `xcrun simctl install booted <app>` and
`xcrun simctl launch booted <bundle ID>`, or pick a Simulator as the
destination in Xcode (`build.sh --sim --open`).

The Simulator's Metal device lacks what SDL's GPU API asks for on tvOS
(`MTLGPUFamilyApple3`, which every Apple TV 4K has), so
`build_deps.sh tvos-sim` skips that one check in its copy of SDL. The
Apple TV build uses SDL unchanged. A Simulator left idle for some minutes
starts apps behind its screen saver: shut it down and boot it again.

## Troubleshooting

- **"No account for team" or "Signing requires a development team"**: do
  step 4, or set `MW3_TVOS_TEAM`.
- **"Failed to register bundle identifier" or "is not available"**: another
  team owns that bundle ID. Leave `MW3_TVOS_BUNDLE_ID` unset (the default
  carries your team's ID) or pick another.
- **The Apple TV is not listed in Xcode**: both on the same network, the TV
  awake, and Remote App and Devices open on it while Xcode looks. Restarting
  the TV often helps.
- **"Developer Mode disabled"**: Settings, Privacy & Security, Developer
  Mode on the Apple TV.
- **The app opens and closes at once after a week**: the free signature ran
  out. See Re-signing.
- **The app shows "FIND THE GAME"**: it was built without the game. Build
  with `--data`, and check that the Mac rebuild finds and plays the game.
- **`build.sh` says the libraries' prefix is missing or CMake cannot find
  SDL3**: run `tools/deps/build_deps.sh tvos` (or `tvos-sim`) again.
- **`--device` without a team**: the script builds the app unsigned. That
  checks that it compiles, but it cannot be installed; pick a team.

## How it is built

For developers changing this build:

- CMake option `MW3_TVOS` (off by default; no other build changes) builds
  only `mw3`, as an app bundle, with the Xcode generator and
  `CMAKE_SYSTEM_NAME=tvOS`. No tests, launcher, importer or tools. Cache
  values `MW3_TVOS_TEAM` and `MW3_TVOS_BUNDLE_ID` (from the environment by
  default).
- tvOS cannot start a process, so `sdl_child_tvos.cpp` stands in for
  `sdl_child.cpp` and every child fails to start. There is no libarchive
  (no SETUP).
- SDL3, SDL3_ttf (with FreeType) and SDL3_sound are static, from
  `tools/deps/build_deps.sh tvos` and `tvos-sim` (docs/deps.md, tvOS). When
  CMake builds for tvOS it searches only under its find roots, so
  `build.sh` passes the libraries' prefix as `CMAKE_FIND_ROOT_PATH` too.
- `tools/tvos/Info.plist.in`: the app's name, arm64, and an extended
  gamepad as the supported controller.
- `mw3_main.cpp` under `SDL_PLATFORM_TVOS`: `SDL_main.h` (SDL starts
  `main` from UIKit), and `SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS` set to
  "0" before `SDL_Init`, so the controller's Menu button reaches the game
  instead of leaving the app.
- The write dir is where `SDL_GetPrefPath` puts it on tvOS, the app's
  `Library/Caches/MicroProse/MechWarrior 3/`.
- With no command line on tvOS, `main` takes `tvos_default_args`
  (`tvos_args.h`): the bundle's `game/` as the install, `--assets` the
  bundle's `media/` (where the movies and music are found through the
  game's mounts), and `--no-launcher`.
- `--data` adds `tools/tvos/copy_data.sh` as a build step after linking.
  Xcode decides whether to sign at the start of a build, so when the copy
  changes an app Xcode has already signed, the script signs it again with
  Xcode's identity, keeping its entitlements.
- The game asks for its fonts as `arial.ttf` and `lucon.ttf`; the install
  has `ARIAL.TTF` and `LUCON.TTF`. tvOS names are case-sensitive, so the
  upper-case name is tried next (`sdl_twod_host.cpp`).
- The launcher's 16-bit canvas is drawn through an ARGB8888 texture on
  every platform (`rgb565.h`): Metal in the tvOS Simulator refuses a
  B5G6R5 texture, and the pixels on screen are the same.
