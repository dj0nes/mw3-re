# The launcher and the first-start setup (ours; design, 2026-10-30)

Not original code: the rebuild's own front door, so a player installs,
starts and plays with no command lines (Owen, 2026-10-30). The design
sketch's screens: `assets/launcher/` holds the art (with its prompts);
the mockups were handed over in the outbox (launcher-sketch).

## The flow

Since SETUP from any source (2026-10-31, docs/setup-sources.md and
games-layout.md): a plain `mw3` start goes straight into the game when an
install is found (mw3.cfg's, `<write dir>/install`, the finder's places),
with no launcher; with none, it opens the **find screen alone** (BACK is
QUIT there) and goes on into the game in the same window once one is
found or installed. The launcher below is its own program, `mw3launcher`
(the launcher split, 2026-11-01; The two programs, below):

1. SDL, the write dir, mw3.log (as now).
2. The window, made once and kept for the game: fullscreen at the desktop
   size, the 640x480 frame fitted as the menus are (sdl_fit), so the
   launcher looks the same in a window, at 4:3 and at 16:9.
3. The install, quietly: mw3.cfg's `install=`, then the finder's places
   (`sdlfe::install_candidates`), each through `setup::check_install`.
   No message box any more: the setup screen replaces the Browse dialog.
4. **The launcher screen** (mockup 1). Entries:
   - **MechWarrior 3**: "Ready: version 1.2, <folder>", or "Not found yet:
     click to find your game" (a click opens the find screen; v1.0, Owen
     2026-11-02: only then, the launcher always opens on the games). A
     folder too long for the plate is
     shortened from the middle by its drawn width ("Ready: version 1.2, "
     kept, more of the folder's end; `fit_middle`, text_fit.cpp): a long
     path ran past both sides on Owen's PC (lead, 2026-10-31).
     `launcher_long_path_fits_the_button`.
   - **Pirate's Moon**: greyed, "Coming later".
   - **MechWarrior (1989)**: only when the MW1 port is found beside ours
     (below).
   - **Game files** (Owen, 2026-11-02, after the rework): the find screen
     at any time, to repick or refresh the game's files (below), and
     **Quit** (for fullscreen; Escape too).
5. MechWarrior 3 chosen: the launcher's textures are freed and the start
   goes on exactly as today in the same window (mounts, data version,
   movies helper or import, `run()`), so the intro movie is the next
   thing seen.

## The find screen and the notes (v1.0, Owen and the lead, 2026-11-02)

Reached by choosing a game without its files (none found at the start, or
mw3.cfg's gone), or alone on a plain mw3 start with none. At the top, in
the launcher's text, `kFindWords`: "MechWarrior 3 needs your own copy of the
game. Show us your disc, a disc image or download (.iso, .cue, .zip), or a
folder where it's installed. We copy what the game needs into your user
folder; your disc and any installed copy are left untouched."

| plate | shown | what it does |
| --- | --- | --- |
| Find the game | always (one option, Owen 2026-11-02) | the finder (mw3.cfg's, the installer's places) and a disc in a drive (`find_discs`) first: either is used at once, no picker. With neither, "Not found on this computer or in a disc drive. Pick it: a disc image, a download, or Mech3.exe." and one file picker opens (.iso .cue .bin .zip .7z .rar .., Mech3.exe, any file). `pick_source`: an image or archive is the source; any other file stands for its folder (Mech3.exe or any file of an installed copy, zbd\\ and video\\ up one; a disc's folder), a .bin for its .cue. Then `source::map_source` on the worker ("LOOKING AT YOUR GAME", its bar, CANCEL): an installed copy on disk is used where it is, anything else INSTALL / BACK under its summary and `setup::install_from_source` into `<write dir>/install`, a cue's music into `<write dir>/music`. SDL3 has no file-or-folder dialog on the three systems (none on the Mac's panel either through SDL 3.4), so a folder is picked by one of its files |
| Back | always | the games (QUIT on a plain mw3 start) |

Once found or installed the game starts at once (`use_install`, `play`).
When the install lacks the movies (a CD-type install leaves them on the
disc: an empty video folder; dev 2's PC check) or the music, or its data is
of an unknown version, the notes come first ("BEFORE YOU PLAY", one OK that
starts the game; Escape as OK). They say what is missing (launcher_notes:
"Your install has no movies (they stayed on the CD) and no music ...").

| plate | shown | what it does |
| --- | --- | --- |
| Add from my disc | the notes (and GAME FILES' screen), when the movies or the music are missing | a disc in a drive first; else "No disc in a drive ..." and one picker (its image, or any file of the disc for its folder: `pick_source`); `setup::add_from_disc` on the worker ("ADDING FROM YOUR DISC"): the disc's VIDEO\*.AVI into `<write dir>/video` (`add_movies`, from a drive, the disc's folder, an .iso or a .cue's data track), the music from a .cue only (`add_music`; from a drive, a folder or an .iso it says the music needs a .cue). Back to the notes with what was added |

The game finds movies in the write dir before the install's (zutl::find),
so the Indeo helper plays the added AVIs on Windows. Elsewhere the movies
are converted in the launcher (Owen's first start, 2026-11-02, sat 44 s on
"Starting MechWarrior 3..." while mw3 did it with only a status line): before
the game starts, and right after ADD FROM MY DISC, any AVI of the install or
the write dir without its .fmv goes through mw3import on the work screen
("CONVERTING THE MOVIES", "Converting the movies (K of N)", the movie's name,
CANCEL; movie_import.h, its "progress K N name" lines). A cancel keeps the
ones done; mw3's own first-run import (`first_run_import`, its small progress
window) stays the fallback for a start without the launcher. None on Windows,
nor with --no-import (passed on to the game) or MW3_NO_IMPORT; MW3_IMPORTER
names a stand-in for the checks. `sdl_program_launcher_converts_the_movies_before_the_game`.

The file picker (Owen: very slow to open on macOS 26): SDL shows NSOpenPanel
as a sheet on the launcher's window, and the panel is another process that
answers through the main run loop, which the launcher's vsync frame loop
served once a frame. While a picker is up the launcher draws nothing and
waits in SDL_WaitEventTimeout (the run loop serving the panel at once). The
log times it: "the file picker asked for", "the picker took the focus N ms
after", "the picker answered (...) N ms after".

GAME FILES opens the same screen with the install in use ("Using
MechWarrior 3 1.2 at <folder>. No music.") and, while it lacks movies or
music, ADD FROM MY DISC (back to this screen after). A new pick goes back
to the games, not into the game, its folder saved to mw3.cfg. A disc or
archive install into `<write dir>/install` is built in `install.new` and
swapped in only once complete (`swap_in_folder`: the old copy set aside as
`install.old`, removed after the rename, put back if it fails), so a cancel
or a failure leaves the old one working and no second copy stays. A copy
picked to be used where it is leaves the launcher's own earlier copy
unpointed-at (mw3.cfg holds one folder), so that is removed.
`sdl_program_launcher_game_files_repicks`.

Keys and a pad (`nav`): the arrows, Tab or the d-pad step through the
plates (GAME FILES and QUIT included), Enter, Space, A or Start press,
Escape, B or Back go back. MW3_LAUNCHER_NAV (checks) feeds them as a list.

Gone in v1.0: the old Setup screen, "A folder instead", "Search
again", "Apply the 1.2 patch" (the patch is built in, patch12.h, so a 1.0
install needs nothing; `setup::apply_patch` and the launcher's kPatch pick
are kept but unreachable). The installer has no "MechWarrior 3 (no
launcher)" shortcut any more; mw3.exe is still installed.

Each screen logs its plates ("mw3: launcher: screen find: FIND THE GAME,
BACK"), which the checks follow.

Each operation runs on a worker thread; its `Progress` (stage, fraction,
message) goes to the screen under a mutex: the bar, the stage and the
current file (mockup 3). Cancel makes the callback return false
(Mw3 dev's calls stop within a file and change nothing). The result's
`summary` is shown in a line at the top of the launcher, green or red
(mockup 4); `warnings` under it; `detail` only in mw3.log. The first
start's notes are the notes screen above.

File and folder pickers are SDL's (`SDL_ShowOpenFileDialog`,
`SDL_ShowOpenFolderDialog`, as `sdl_install.cpp` waits on them now),
filtered to .iso/.cue, .zip, or Mech3.exe.

## Drawing it

- A 640x480 16-bit (565) canvas each frame, as the game's 2D path: the
  starfield scrolling slowly to the left (a few pixels a second, its ends
  cross-faded), the metal bands top and bottom scrolling at a third of
  that, the title and emblem, the plates.
- Text: SDL3_ttf (already linked) with an OFL font shipped beside the art
  (a condensed technical face, e.g. Saira Condensed or Rajdhani; OFL.txt in
  licenses\ and THIRD-PARTY.txt). The mockups stand in DIN Condensed.
- PNGs through SDL 3.4's `SDL_LoadPNG` (the PC's deps are 3.4.12): no new
  library.
- The hover ripple is the main menu's: `zrndr::ripple` (0x00558dd0,
  ported) on the canvas around the hovered plate, its ring spreading and
  fading as `PushButtonRipple` does (twod.md, Ripple; its radius, max
  radius and speeds from the main menu's dialog values).
- Sounds: with an install, the main menu dialog's rollover and select
  sounds through zSnd from the install; without one, silent (no sounds
  of ours).
- Mouse, keyboard (arrows, Enter, Escape) and a pad's d-pad and A/B.

## MechWarrior (1989)

Elthial's MechWarrior SDL port (github.com/Elthial/Mechwarrior-sdl-port;
MIT for its own code, credited on its entry's line, "Elthial's SDL port";
we ship none of it, so no licence file goes with ours). Shown when
`mechwarrior.exe` (`mechwarrior` elsewhere) is beside `mw3` or in a
`MechWarrior1989\` folder beside it (`MW3_MW1_PORT` names it, for checks).
It needs the player's MW1 folder (`BTECH.EXE`, any case): picked once with
a folder picker and kept as `mw1_game=` in mw3.cfg (`save_config_value`).
Started with `SDL_CreateProcess` as its own script (Run-MechWarrior.ps1)
does: `mechwarrior <game dir> <capture dir>`, the capture dir
`<write dir>/mw1/captures` (made first: the port wants it existing) and
MECHWARRIOR_SAVE_DIRECTORY `<write dir>/mw1/saves`; its MechWarrior.ini
is its own, beside it. The launcher's window hides until it exits, then
comes back with a line saying how it closed. Built 2026-10-31;
`--launcher-mw1` presses it at once (checks);
`sdl_program_launcher_starts_mechwarrior_1989` (a stand-in script).

## Built (2026-10-30, mac5/launcher-ui)

`frontend/sdl/launcher.cpp` (`sdlfe::run_launcher`), called from
`mw3_main.cpp` for a player's plain start (not `--hidden`, `--frames`,
`--shot`, a chain, `--mission`/`--fly`, `--movie`, `--dialog`, `-nofmv`, a
named install or `--install-from`/`--patch`; `--no-launcher` skips it):
- Its own SDL_Renderer window (fullscreen unless `--windowed`/`--window`),
  a 640x480 RGB565 canvas letterboxed into it. PLAY hands the window to the
  game (2026-10-31): the renderer is freed and `SdlWindow::init` claims the
  same window for its GPU device instead of opening a second one. The
  window is not high pixel density, as the game's never was (its sizes are
  the window's points). `--launcher-play` presses MECHWARRIOR 3 at once
  (for checks); `sdl_program_launcher_window_goes_on_to_the_game`. The
  window's events are flushed at the hand-over, so the launcher's lost
  focus cannot reach the game as a deactivation (program.md, Message pump).
- The install found quietly: mw3.cfg's, `<write dir>/install` (the disc
  install's), the finder's places, a development copy; each through
  `setup::check_install`. None: the setup screen first.
- The main screen: MechWarrior 3 (ready, its version and folder, or "not
  set up yet"), Pirate's Moon greyed, MechWarrior (1989) when its port is
  there (below), SETUP, QUIT.
- The setup screen: FIND (the finder, else a folder picker), INSTALL (a
  disc `find_discs` sees, else a .iso/.cue picker; `probe_source`'s line and
  INSTALL/BACK, then `install_from_disc` into `<write dir>/install`, a
  .cue's music into `<write dir>/music`), the 1.2 PATCH (enabled for 1.0 or
  a mix; a .zip picker, `apply_patch`), the MUSIC (a .cue picker,
  `add_music`), BACK. Each operation on a worker thread with the bar, its
  stage and file, and CANCEL; its summary on the main screen's line.
- Art from `assets/launcher` (beside the exe in a package, the repo's in a
  development build: `MW3_ASSETS_DIR`); the font `assets/launcher/font.ttf`
  when there is one, else a condensed system face (Bahnschrift on
  Windows, DIN Condensed on the Mac, DejaVu Sans Condensed on Linux): a
  placeholder, as is the title art's "MW3 REBUILD".
- The games are round badges (2026-10-31, Owen: the ripple is a circle's):
  `badge_mw3.png`, `badge_pm.png`, `badge_mw1.png` (assets/launcher, ours,
  their prompts in its README), each with its name and line beside it,
  the ripple from the badge's centre.
- Spaces (2026-11-02, the lead from setup.png: "No MechWarrior 3was
  found"): DIN Condensed's space is 2 pixels at 13 and its digits draw a
  pixel past their advance, so a space after a digit vanished. A font
  whose space is under a fifth of its size has each space doubled, drawn
  and measured alike, in the launcher's one text path (`Text::shown`,
  `widen_spaces` in text_fit.h); the gap after "3" goes from 2 pixels to 4
  (the font's own "a g" is 3). `launcher_narrow_spaces_widen`.
- Only the round badges ripple (2026-11-02, Owen after the PC's fresh
  install: the ripple belongs to a circle, as in the original's menus). A
  square plate hovered is brightened and lights its edge in the frame's
  amber (a line, a softer one inside); the rollover sound as before. The
  shot logs what the hovered entry drew (`hover <label>: ripple|edge`),
  and `sdl_program_launcher_screens` checks it on each screen with the
  edge's pixel.
- The ripple is the main menu's own (Owen: "much slower than the original
  ripple"): `mech3::menu_ripple_step` (menu_ripple.h), the one SlideButton
  runs (0x00485650): from 20 pixels, 120 a second, the amplitude 7 less 0.3
  a 30 fps frame; drawn by 0x00558dd0. `launcher_ripple_matches_the_main_menus`.
- Paced by vsync (else a precise sleep to 60 a second). Timed on Owen's PC
  (2026-10-31): the first frame drawn 306 ms after the start (art 86 ms,
  the install search 197 ms), shown in a window at 339 ms; 55.9 frames a
  second with vsync, 54.8 with the sleep, the draw 2.6 ms (direct3d11). The
  start was never slow: the old ripple was (80 pixels a second, 2 s long).
  The log gives these at every start and quit.
- The hover ripple is `zrndr::ripple` on the canvas. The sounds are the
  main menu's own (2026-10-31): sounds.zrd's MAINMENU set, menu_rollover
  (sfx_button2.wav at 0.7) when the hovered or focused button changes and
  menu_select (sfx_button6.wav) on a press, read from the install's
  soundsL.zbd (else soundsH.zbd) as zSnd reads them, through an SDL audio
  stream. None without an install (loaded once one is known, after SETUP
  too) or with `--nosound`; `MW3_LAUNCHER_MUTE` plays them at no volume
  (checks). `sdl_program_launcher_plays_the_menus_sounds`.
- Mouse, arrows/Tab, Enter/Space, Escape (back, or quit on the main
  screen).
- The first start's notes (2026-10-31): with no `install=` in mw3.cfg yet
  (PLAY saves one) and something to say, the launcher opens on BEFORE YOU
  PLAY, then OK to the main screen. The notes (`launcher_notes`,
  launcher_notes.cpp) are the start's in the launcher's words: 1.0 data or
  a mix (SETUP can apply the 1.2 patch), files of no known version, and no
  music (SETUP's ADD THE MUSIC, or a rip as track02.ogg ... in the write
  dir's music; the folder shortened to fit). Logged on every start. Before
  this, a player who came through the launcher never saw them: the install
  it hands over is not a searched one, so the message box after it
  (mw3_main, still there for `--no-launcher`) did not show.
  `launcher_first_start_notes`.
- `--launcher-shot FILE [--launcher-screen main|setup|work|notes]` draws one
  screen into a BMP; `sdl_program_launcher_screens` checks the four.

## The two programs (2026-11-01, the launcher split; games-layout.md)

- `mw3launcher` (`launcher_main.cpp` over the same `launcher.cpp`): the
  main icon. Windows: mw3launcher.exe, "MW3 Rebuild" in its version
  resource, no console in any build (its output in the write dir's
  mw3launcher.log; the game's stays mw3.log); the installer's main and
  desktop shortcuts, "MechWarrior 3", start it; "MechWarrior 3 (no
  launcher)" in the Start menu starts mw3.exe. Mac: "MechWarrior 3.app"'s
  own executable (CFBundleExecutable), mw3 beside it in Contents/MacOS.
  Linux: the AppImage's entry (mw3.desktop's Exec, its Name "MechWarrior
  3"). The installed names are the game's (Owen, 2026-11-01); the release
  files keep MW3-Rebuild-... (release.md).
  Each package script checks both programs are in it.
- MECHWARRIOR 3 starts `mw3` beside it (its own folder: `_NSGetExecutablePath`,
  `/proc/self/exe`, `GetModuleFileNameW`; `MW3_GAME_BIN` for checks) as
  `mw3 <install> [--write-dir D] [--windowed | --window WxH] [--nosound]
  [--hidden] --ready-file F`, saving the install to mw3.cfg first, with its
  own stdio (none: the game writes mw3.log itself; the launcher's log pipe
  in its stdout would hold the launcher's exit until the game's). The
  launcher stays on screen ("Starting MechWarrior 3...", input ignored)
  until the game writes F at its first frame with a swapchain image
  (`sdlfe::mark_ready`, sdl_render3d.cpp), so the switch shows no empty
  moment, then **closes**, the game running on alone (Owen, 2026-11-01;
  until then it hid and came back when the game ended). A failed start
  leaves it up: a game that exits first has its exit code on the status
  line; no first frame in 20 s says so on the status line ("has not shown
  a picture yet") and the launcher keeps watching, closing at the frame if
  it comes. `--hidden` keeps its window unshown and goes on to the game
  (checks); `MW3_LAUNCHER_READY_MS` shortens the 20 s for them.
  While the game works before its first frame it says what it is doing in
  F.status (`sdlfe::set_ready_status`: its first start's movie import,
  "Importing your movies (first start only)..." with the count when the
  importer gives one, then "Starting MechWarrior 3..."; removed at the first
  frame). The launcher reads it four times a second and shows it on its
  status line in place of the 20 s "not shown a picture yet" (Owen,
  2026-11-02: a first start sat a minute on "Starting MechWarrior 3...").
  Test: `sdl_program_launcher_shows_the_games_status`.
  Tests: `sdl_program_launcher_starts_mw3_as_its_own_program` (the
  launcher's process ends at the ready file, the stand-in game still
  running) and `sdl_program_launcher_stays_when_the_game_fails_or_is_slow`.
  MechWarrior (1989) is unchanged: it has no ready signal, so the launcher
  hides until it exits, as below.
- The gap, measured (the launcher's log: "the game's first frame N ms after
  MECHWARRIOR 3 was pressed"; the game's: "first frame shown N ms after the
  start"): Mac, the app, 1.94 s, of which the game's own start 1.76 s (a
  development build: SDL and mounts 0.20 s, the data version 0.08 s, the
  window and GPU device 0.30 s, the original's start-up to its first frame
  1.1 s) and the process's start about 0.2 to 0.45 s. The old one-process
  hand-over saved the window and SDL's start (about 0.5 s) but drew nothing
  either until the game's first frame; now the launcher is that picture.
  Windows: over ssh the game gets no swapchain (windows.md), so the gap is
  to be measured in a desktop session.
- `mw3` started alone: straight into the game when an install is found;
  the setup screen alone otherwise (`setup_only`, in the same window into
  the game). `--launcher`, `--launcher-shot`, `--launcher-play` and
  `--launcher-mw1` are mw3launcher's now.
- Tests: `sdl_program_launcher_starts_mw3_as_its_own_program` (a stand-in
  game keeps its arguments and writes the ready file: the install, the
  write dir, the window, --nosound, --ready-file; the launcher's first-frame
  and exit lines), `sdl_program_launcher_window_goes_on_to_the_game` (now the
  setup screen's hand-over in mw3: an installed copy picked, the window
  claimed, a frame shown), `sdl_program_plain_start_goes_straight_in_or_to_setup`
  (mw3 refuses --launcher), the screens, sounds and MechWarrior (1989)
  checks on mw3launcher.

## Open

- The working title ("MW3 REBUILD" on the title art) and the font: Owen's.
- The calls are Mw3 dev's `mech3::setup` (game_setup.h, mac4/setup-api).
- Once the GUI-subsystem build lands, mw3.log is the only console.
