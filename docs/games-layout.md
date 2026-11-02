# How the rebuild is organised: MW3, Pirate's Moon and the launcher

Decided with Owen, 2026-10-31. (The original exe's own structure is
`architecture.md`; this page is about the rebuild.)

## Three programs, one package

- **The launcher** (ours, not original code): the main icon. It finds the
  installs, runs SETUP (install from disc, the music, the movies) and
  starts the game the player picks. It is its own program, so either game
  can also be started without it.
- **`mw3`**: MechWarrior 3. Started directly, it goes straight into the
  game.
- **`mw3pm`**: Pirate's Moon (after MW3 `v1.0`). Started directly, it goes
  straight into PM.

A release has one download per platform (the Windows installer, the Mac
DMG, the Linux AppImage) holding all three. The installer's main shortcut
is the launcher; the two games get their own Start-menu shortcuts. Each
game runs without the other: removing `mw3pm` leaves MW3 working and the
other way round. Each game mounts its own install's data (the player's
own MW3 or PM files) and keeps its own settings and saves.

## The code

| Folder | What | Address comments |
|---|---|---|
| `src/gamez/` | the shared GameZ engine (render, zTwoD, zSound, readers, ...) | `// Mech3.exe 1.2 @ 0x...` |
| `src/mech3/` | MechWarrior 3's game code | `// Mech3.exe 1.2 @ 0x...` |
| `src/pm/` | only what Pirate's Moon adds or changes | `// Pirate's Moon 1.0 @ 0x...` |
| `src/frontend/sdl/` | the SDL3 platform layer and the launcher | ours |

- MW3's code never includes anything from `src/pm/`.
- Where PM changed a function MW3 also has, PM's version lives in
  `src/pm/` under the same name, with a one-line note on what changed
  (`docs/pm-map.md` lists every pair). An engine function PM changed is
  chosen per game at run time through that game's table, never by `#ifdef`
  scattered in shared code.
- Build targets: `mw3`, `mw3pm`, the launcher, and each game's tests. A
  switch (`-DMW3_BUILD_PM=OFF`) builds MW3 alone.
- Tests: each game has its own test executables and differential tests
  (`MW3_EXE=pm` runs a test against PM's exe), so a PM change cannot break
  MW3's suite unseen.

## The pieces on `mac5/pm` (2026-11-02)

- **The game table**: `src/gamez/zgame/zgame_table.h`, `zgame::Table`,
  one per game: MW3's is the default (`zgame::mw3_table()`), PM's is
  `pm::table()` (`src/pm/pm_game.cpp`), set by `zgame::set_game` before
  anything loads. Shared code calls a changed function through
  `zgame::game()`; each entry arrives with its port (`pm-map.md`, step 1
  onwards). For now it holds only the game's name and its exe's.
- **`pmcore`**: the library of `src/pm/`, on top of `mw3core`.
- **`mw3pm`** (`src/frontend/sdl/mw3pm_main.cpp`):
  - It takes `mw3pm [install-dir] [--write-dir DIR] [--hidden]`.
  - It uses PM's table and its own write dir: SDL's app data for "Pirate's
    Moon", apart from MW3's.
  - The install comes from the command line, else `mw3pm.cfg`'s
    `install=`, else (development builds) `game/pm`.
  - The install must be PM's: zbd/reader.zbd ends in a zArchive of version
    2. MW3's install, or a folder without reader.zbd, is refused, and the
    message says what the folder is.
  - It mounts the install and stops with "data not loadable yet" (exit 0).
- **`mw3pmtests`**: PM's own unit tests (`tests/pm/`).
- **The switch**: `-DMW3_BUILD_PM=OFF` drops all three, and `MW3PM_GAME_DIR`
  names the PM install the tests read.
- **Not yet**: the packages carry no `mw3pm`, and the launcher's entry is
  still greyed. Both come when PM plays.

## One repo, and when PM arrives

- Until MW3 is tagged `v1.0` (`release.md`, "MW3 v1.0"), `main` is MW3
  only and PM work stays on the branch `mac5/pm`.
- Before `v1.0` the launcher moves into its own program, so MW3's final
  layout is this one from the start. Done 2026-11-01: `mw3launcher`
  (launcher.md, The two programs); `mw3pm` has its greyed entry until then.
- After `v1.0`, `mac5/pm` merges into `main` (after the full MW3
  differential set passes on it, `pm-map.md`), and both games live on
  `main` from then on. One branch per game would mean merging every
  engine fix across by hand forever; one repo with separate folders and
  targets keeps them apart without that.
