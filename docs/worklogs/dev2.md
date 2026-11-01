# Work log: mw3 dev 2

The second worker session, from 2026-10-29 (worktree `.claude/worktrees/mac5`,
branches `mac5/<item>`). The protocol is in `README.md`. "Mw3 dev" (mac4)
works alongside it; stay out of its files.

## Item 1: TARGET's mode-1 view crash (2026-10-29, ahead of item 2)

- From Owen's Windows playtest: an access violation in
  `zcls::render_subtree` (`Node::parent_count`) through app_hud.cpp's
  `view_draw_node`.
- Cause: `ProgramHudHooks::object_node` read every target's node as a
  mech's (`Mech::node`). TARGET's mode 1 also shows turrets, whose
  `self` and `node` are 32-bit handles, so the read was a garbage pointer.
  The exe reads the object's +0x04, whatever its class (0x00441410 ->
  0x004428b0).
- Fix: `hud_object_node` (app_sensors.cpp, the sensors' per-class node)
  behind `object_node`. targets.zrd objects never reach mode 1 (vt+0xa8).
  Removed objects resolve to null by handle, and dead mechs keep their node.
- Test: `mission_c1m1_target_views_a_turret_headless`, with the helper
  `program_hud_target_other`. Break: the old read crashes with Owen's stack.
- Branch `mac5/target-view-crash`. hud.md section 22, The target's node.

## Item 2: the damage diagram, mode 2 (2026-10-29)

- Owen: at full health only one arm shows; every part should be blue
  when undamaged, yellow when slightly damaged, and so on.
- Found so far: the logic matches the exe. At full health all eight parts
  read 1.0 and hold their `_blue` image (0x004475c0, 0x00447a50; logged in
  play). In rimage.zbd seven `dmg_<part>_blue` images are pure (0,0,255)
  = 565 0x001f, and `dmg_rarm_blue` is (0,4,255), the only part shown.
- Cause: `dmg_bg_blue` is a translucent 0x001f fill (alpha plane). The
  layer's cover (FIX (A), render-frame.md) records its alpha and colour.
  blit16's keyed path writes the part's 0x001f without clearing the cover,
  so the overlay draws it as the translucent background. Green, yellow and
  red, and the right arm's 0x003f, differ from 0x001f. Fix: cover_opaque on
  every opaque write into the 2D layer (blit16's ten paths, line16,
  prim2d's put), `zrndr::cover_opaque` now public.
- Tests: `fix_layer_opaque_ends_a_blend_of_its_colour` (each writer, 10/10
  breaks caught), mw3sdltests `sdl_host_damage_diagram_shows_every_part`
  (the real silhouettes at each band; unfixed it shows Owen's picture).
- Shots in the outbox (2026-10-29, mfd-damage-diagram-*): all blue at full
  health; after `--vehicle-ahead 40 --vehicle-hold` hits, left arm and leg
  green. The exe's first damage band is green (above 0.7), not yellow.
- Branch `mac5/hud-damage-diagram` on main cd45f5f (item 1 merged).
  every opaque write in blit16; extend `fix_layer_blend_keeps_its_alpha`.

## Item 3: finding the install at start (2026-10-29)

- Owen's request through the lead: mw3.cfg's install=, else the standard
  places, else a message box and a file dialog; save to mw3.cfg; check runs
  never ask. Ours, FIX (A).
- The installer, read from the MW3 disc (Owen's backup ISO; SETUP.INS and
  _USER1.CAB's value.shl): `SOFTWARE\MicroProse\MechWarrior 3\1.0` gets
  "Program" and "Version" (PUBLISHER_NAME, PRODUCT_NAME, PRODUCT_VERSION),
  InstallationInfo's App Paths for PRODUCT_KEY `mech3.exe`, and the default
  folder PROGRAMFILES\MicroProse\MechWarrior 3. Whether "Program" is the
  folder or the exe is not in the script, so both are taken.
  Mech3Register.exe names nothing. Pirate's Moon's key is "MechWarrior 3 EP1".
- `src/frontend/sdl/sdl_install.{h,cpp}` (in mw3sdl). In mw3_main.cpp only
  `find_install(Args&)` and mw3.cfg's install= read into `cfg_install`, to
  keep clear of mw3 dev's start-up item. `MW3_FIND_INSTALL=1` skips the
  development copy (for checking the search and the dialog).
- Tests: `sdl_install_is_found_and_saved` (4/4 breaks caught),
  `sdl_program_check_runs_never_ask_for_the_install`. On the Mac the box
  opened (a 260x352 window of mw3's, by CGWindowList; no accessibility or
  screen-recording access to click or capture it). The dialog's pick flow
  needs a person: the lead tests it on Owen's PC.
- Branch `mac5/find-install` off main cd45f5f.

## 4. W7: TARGET's view of a mech (2026-10-30)

- TARGET's camera (0x00440b00) is the target's centroid (entity +0x14) less
  the contact direction x 1.2 x size, aimed along player centroid -> target
  centroid. 0x00441410 itself places nothing. The program's
  `ProgramHudHooks::entity_position` gave a mech's place (+0x04, the ground)
  for both points, so the view centred on the feet of every mech; the target
  triangle (0x00440510) too. The probe's hook read +0x14 already, so the
  differential test never saw it.
- Fix: `entity_position` reads the centroid node; `object_position` and
  `vehicle_position` keep the place they meant.
- Test: the MFD/TARGET mission test checks TARGET's camera height against
  the Owens' centroid (5.2 m both); the place read put back gives 0.0 and
  fails.
- Shots: outbox/2026-10-30/W7-target-{owens,madcat}-{before,after}.png
  (c1m1 Owens, c3m2 Mad Cat, `--mech-ahead 60 --tap 0x12@860`, frame 960).
- Branch `mac5/target-camera` off main 730a01c.
## 5. W9: the lab's skin change blinks (2026-10-30)

- Reproduced: `--lab-mech 9 --click-at 362,304@2900 --shot-series 12`:
  frame 2903 showed the 3D mech alone on black.
- The skin swap 0x0044fd90 opens and closes the 3D lock (0x00576d00 /
  0x00576d60) while the screen's draw holds it (checked: back.locked 1).
  The exe: the inner lock is a no-op, the inner Unlock really unlocks, the
  screen's unlock does nothing; DirectDraw's pointer stays writable, so the
  2D drawn after it reaches the flip. Nothing holds or skips the frame.
- Ours: `GameEngine::unlock_back` lays the layer over the frame at the
  unlock, so the 2D drawn after the inner unlock was lost.
- Fix (A) in `zvid::lock_3d` / `unlock_3d`: a lock of the locked back buffer
  is counted and its unlock only counts down (`zfix::options().nested_lock`,
  off in the probes). After: frames 2899..2909 all show the lab.
- Tests: `fix_lock_inside_a_lock_keeps_the_outer` (both paths; 2 of 2
  breaks), test_d3d_frame.py (2000 locks identical, the original's path).
- Branch `mac5/lab-skin-blink` off main 730a01c.
## 6. S8 and S10: the lab view (2026-10-30)

- Both as the original; no code change.
- The 3D window is (210, 52)-(430, 288) ([0x005e4688], 0x00450460, its only
  caller 0x0045015d) on every page; the camera's place and rotation are read
  from [0x005e4648] / [0x005e4678] only at 0x00450321 and nothing else
  writes them. The frame draws the screen under the 3D lock (0x00448f10),
  then the mech (0x00450690), so the mech covers anything 2D in its window.
- S8: the criticals line is the page's dialog text at (220, 60), inside
  that window: covered whenever the head turns past.
- S10: shots at frame 3000 of the chassis, criticals and groups tabs, at
  1280x960 and at 1280x720, frame the Bushwacker alike; the visual pass's
  crop (640 scale, origin about (330, 0)) matches this geometry with the
  mech side-on. The turn (0.8 rad/s) decides how it sits.
- Branch `mac5/lab-view-s8-s10` off main 730a01c (docs only).
## 7. Fullscreen polish (2026-10-30)

- Alt+Enter: `SdlPump::handle` swallows Alt+Enter (and that Enter's release,
  even after Alt is let go; repeats do not toggle) and calls
  `SdlWindow::switch_fullscreen`. Back to a window: the frame's shape at its
  opening size (default_window) or create()'s size, scaled into 90% of the
  display's usable area, centred. Not saved: windowed= in mw3.cfg stays.
- Focus: the exe's OnActivate (0x00590c00) idles the loop and suspends
  zInput (0x0051b050); on return each suspended device's state is cleared
  (0x0051b080). Already ported. Added: `SdlInput` drops buttons, the latch
  and held keys on focus loss (a release outside the window never arrives,
  so a held trigger kept firing); `SdlSoundOutput` mutes buffers without
  DSBCAPS_GLOBALFOCUS (0x8000; 0x005752e0 never asks it) while unfocused,
  their positions moving on, as DirectSound's did. Music (MCI CD) untouched.
- Mickeys: DirectInput's relative mouse is raw device counts, unrelated to
  the screen; zInput adds them to its cursor (0x0051b910) and the look reads
  them. The port divided them by the window's width (fullscreen at 2560 wide:
  a third of the speed). Captured now: 1 a count (SDL's relative motion is
  raw; SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE defaults to 0). Uncaptured
  keeps the frame mapping. On the Mac, relative motion is in accelerated
  points, so the feel there is the OS's.
- High DPI: mouse in window coordinates against SDL_GetWindowSize, the draw
  in SDL_GetWindowSizeInPixels; on Windows SDL3 is per-monitor DPI aware
  (density 1, 150% only the display scale). Geometry test at densities 1,
  1.5, 2.
- Tests: `sdl_fullscreen_keys_focus_and_mickeys` (breaks 4 of 4: the Enter
  passed on, the captured scale, the buttons kept, no mute),
  `sdl_frame_maps_alike_in_points_and_pixels`.
- For Owen on Windows: Alt+Enter both ways in a mission and in a menu (no
  menu button pressed); Alt+Tab mid-mission holding fire and W, release
  outside, return: nothing held, sound back; mouse look the same speed
  windowed and fullscreen; at 150% scaling the cursor on the buttons it
  draws over, and the frame sharp at the display's pixels.
- Branch `mac5/fullscreen-polish` off main 730a01c.

## 8. The Windows package (2026-10-30)

- `tools\windows\package.cmd`: its own tree `build-win-pkg` with
  `MW3_PACKAGE=ON` (CMake: mw3 and mw3import without MW3_GAME_DIR), then
  `build-win\dist\mw3-<describe>-win64\` and the zip (tar -a). Contents and
  the dumpbin check (`check_dist.ps1`): docs/windows.md, Packaging.
- `mw3 --version`, `mw3import --version` (with FFmpeg's configuration):
  no window needed, so the load of every DLL is checkable over ssh.
- `mw3import` in a package build takes the install from `--install`, else
  mw3.cfg's `install=`.
- The finder's usual folders gained `\Games\MechWarrior3`, `\Games\MW3` and
  `\MechWarrior3` (Owen's install is C:\Games\MechWarrior3).
- Version resource from `version.rc.in` (git describe, commit count);
  placeholder icon `mw3.ico` from `make_icon.py` (ours: a reticle).
- README.txt and THIRD-PARTY.txt in `tools\windows\dist\`. Licences
  checked: SDL3 and SDL3_ttf zlib (SDL3_ttf's DLL has FreeType (FTL),
  HarfBuzz and PlutoSVG/PlutoVG (MIT)); SDL3_sound zlib, its decoders dr_*
  and stb_vorbis public domain or MIT(-0), libmodplug public domain,
  TiMidity the Perl Artistic License (its header); FFmpeg LGPL 2.1+ (BtbN
  lgpl-shared, dynamically linked, unmodified); pl_mpeg MIT; the VC++
  runtime's redist terms.
- On the Mac: a `MW3_PACKAGE=ON` tree builds; neither exe holds the dev
  path; `mw3 --frames 1 --hidden` with a fresh write dir finds no install
  and says so; `mw3import` without one says to give `--install` or start
  mw3. The .cmd, .ps1 and .rc are untested here (Windows only).
- Branch `mac5/windows-package` off main 46135f7.
- The PC's run (lead, 63c3a5b): check_dist 17 files, both --version print
  from a fresh folder with only Windows' DLLs on PATH, the finder found
  C:\Games\MechWarrior3 and saved it; every licenses\ folder filled. Then:
  BtbN's FFmpeg is --enable-version3 (LGPL v3) with ~20 libraries built in.
  Done (a): THIRD-PARTY.txt says LGPL v3; `ffmpeg_notices.ps1` lists the
  built-in libraries and their licences from the configure line. (b), a
  minimal LGPL v2.1 FFmpeg, is written up in windows.md for later: this
  session's permission check refused building the downloaded FFmpeg source,
  so it waits for Owen's approval.

## 9. Release performance (2026-10-30)

- `--frame-times N` and `--mission ia<N>[w<W>]` in mw3_main (measuring
  only). Numbers, profiles and the candidates: docs/performance.md.
- Release on the Mac: 2.6 to 4.5 ms a frame of CPU, the same at 1080p and
  4K (the 2D canvas is 960x540 at both); vsync-bound at 60 in a window.
  Hot spot: `overlay_16`, 53 to 61% of the frame (three overlays a frame,
  two of them empty in c4m2, each a full 960x540 conversion and upload).
  The AI's sight lines about 3%, collision and ground 2 to 3%.
- Seen: the opening fade at 1920x1080 darkens only the top-left quarter.
- Branch `mac5/perf` off main 46135f7; nothing changed but the two flags.
- The fixes (the lead: 1, 3, 2, 4): 1 empty overlays skipped, 3 staging
  buffers kept and cycled, 4 the RGBA table; 2 (the drawn box) made, pixel
  identical, no gain (the HUD reaches every edge), not kept (patch in my
  scratch). 1080p c4m2 4.51 -> 1.98 ms, c3m6 3.45 -> 1.92, IA 3.37 -> 1.75.
  Nine guard shots pixel-identical after each; mw3sdltests' pause and
  screen tests moved to --frame-ms 33 (speed-dependent before).
- S14: the colour tint's quad (the opening fade) took the canvas's rect in
  frame pixels; `tint_quad_on_frame` maps it (FIX (B)). The end fade (the
  still) was right; menus are 640x480. Test `fix_tint_quad_covers_the_frame`
  (test_program.cpp; the mapping's removal caught).

## 10. Sanitizer runs (2026-10-30)

- build-asan (ASan + UBSan, recover). Runs, times and findings:
  docs/sanitizers.md. Found: the waypoint arrow's freed image (FIX (bug),
  the exe's read; a crash risk here), and uninitialised Mech fields in
  tests' stack mechs (initialised in the struct).
- New test `mission_c1m3_hud_views_follow_targets_as_they_die_headless`:
  every kind of c1m3 hostile shot dead while TARGET shows it, the views
  cycling; `chain_object` added for it.
- No "action chain" exists; Instant Action ran as `--mission ia1w3` (the
  perf branch's flag, cherry-picked here as its commit).
- Branch `mac5/asan` off main 873bf47.
## 11. What only the software renderer showed (2026-10-30)

- Audit, no porting: docs/software-path.md. Nothing the Direct3D path
  lacks; the software side is everywhere the reduced one (the 134 animation
  ELSE branches are the plainer effects; its texture sets are the same names,
  smaller; no shadows, no light points). Candidates for Owen: the fog's start
  (software 500 m, Direct3D 32 m), the software look as a shader option.
- Method: the frames', walks' and model draws' call graphs diffed; the 83
  readers of [0x0072f348]; the anim scripts' if-hardware; the texture sets.
- Branch `mac5/software-audit` off main 464a2e0.

## 12. The HUD's scale and the first start's size (2026-10-30)

- Owen: the HUD "as big as it was" at every size. The canvas is 480 high
  and of the frame's shape (853x480 at 16:9), stretched by height / 480
  (1.5, 2.25, 3, 4.5); was a whole scale (1x at 720p, 2x at 1080p). The
  layer draws sharp bilinear (`DrawState::sharp`, Metal and HLSL), its
  texels premultiplied; whole scales are exactly nearest. Test
  `sdl_layer_scales_sharp` (GPU read-back; plain bilinear fails it 3 times).
  The anchored, pause-click and canvas tests now expect 853x480.
- Owen: 16:9 by default. A play run with no InGameVMode saved takes mode 16
  at the desktop's size (`first_start_screen` / `first_start_size`); saved
  wins; checks and shots keep 5. Test
  `settings_first_start_plays_at_the_desktop_size`.
- Shots (outbox 2026-10-30/hud-scale): c1m1 with TARGET, the MFD, the radar
  and the weapon list at 640x480 (in its 1280x960 window), 720p, 1080p,
  1440p and 4K, the objectives box at 1080p, and 1:1 crops of the weapon
  list at 1.5, 2.25 and 3.
- Branch `mac5/hud-scale` off main 9b02519.

## 13. 16:9 at most (2026-10-30)

- Owen: no ultra-wide shots or geometry; 16:9 is the widest, pillarboxed
  beyond. `vmode_size` caps mode 16 at 16:9 (3440x1440 -> 2560x1440,
  3840x1080 -> 1920x1080); 5:4 unchanged. Tests: the sizes, the first start
  at 21:9, the fit at 21:9 and 32:9; without the cap 6 checks fail.
- Branch `mac5/wide-clamp` off main 9f68bfd.
## 14. The mouse in a window (2026-10-30)

- Owen: in a window (Alt+Enter) the mouse could not leave. `SdlInput`
  (FIX (A), input.md section 8): in a shown window the menus never capture
  (zInput's mouse put at the free cursor, 0x0051b4f0); a mission captures;
  Ctrl+Alt alone (`kReleaseKeys`), the pause and the focus lost let go; a
  click inside takes it back, unseen. Fullscreen and hidden runs unchanged.
  mw3_main gives it `windowed`, `in_mission` (the Core state) and
  `absolute_cursor`.
- Test `sdl_window_lets_the_mouse_go` (headless, every path). Not tried in
  a shown window here (it would take the Mac's mouse); for Owen.
- Branch `mac5/mouse-release` off main 9f68bfd.

## 15. Playtest W11: lance orders and the radio (2026-10-30)

- Found: the launch's stand-in mech at the pilot's +0x14 (set_launch) made
  every brain see a player without allies at the load: no radio (0x4e7e20),
  no answers. The exe's +0x14 is empty then (0x491868). Removed.
- The orders reach the selected lancemate only (0x4d7240), as the exe; with
  none selected, nothing. Selected, each changes his order (test). F8's
  follow walks him to ~100 m.
- MFB (M): as the exe (0x47a7a0): pos_wait or occupied, then contact; silent
  without a base up. Checked in c1m1, c1m2, c2m1 by a temporary print.
- c1m4's Dominic test: its exact numbers changed (his lines now play).
- `--briefing` stops in the Load state (old and new binaries alike).
- Owen's follow-ups: the key path end to end (c2m1 test); c1m4 is the
  exe's (the player has no allies there); later missions were never hit by
  the stand-in (mates built after the launch; checked with it put back).
  Fixed too: F6 / F7 on a non-mech vehicle (0x4e6fe0). MFB second M: the
  dock's 5 s quiet time and the line's MINDELAY, as the exe.
- Branch `mac5/lance-orders` off main 96a5108.
## 16. W13: a mode changed mid-mission (2026-10-30)

- Owen's shots (from d02a386): RESOLUTION set to 1920x1080 in a mission
  begun at 640x480. Two bugs (resolution.md section 11):
  - The camera kept 80 degrees across. The mode switch now sets the angle
    again.
  - The menu screens Core made on the 853x480 canvas kept the 106 origin.
    They are now made at 0, all but the pause screen.
- Tests: `mission_c1m1_mode_change_keeps_the_camera_shape_headless` and
  `mission_c1m1_options_from_the_pause_keep_the_menus_frame_headless`. Each
  fails without its fix (1 and 4 checks).
- Branch `mac5/w13-wide` off main d02a386.

## 17. W17: a lancemate round a wall (2026-10-30)

- The follow order steers straight. Behind a wall on c2m1, Dominic bounced
  off, and the contacts' push held his carrot inside his slack: he stood at
  73 m (the exe's). The mission nets are unit routes, so routing over them
  was not possible (Owen's (b), changed to local wall-following with the
  lead).
- `FollowState::detour_goal` behind `bugs`, with the brain hook
  `wall_between` (the world's segment query). ai.md section 17.6, fixes.md,
  CHANGES, discoveries.
- Test `mission_c2m1_dominic_follows_round_walls_headless` (the wall, the
  exe's path, c2m2, open ground, the give-up). `test_ai_attack.py` and
  `test_ai_node.py` pass.
- Branch `mac5/w17-follow-path` off mac5/lance-orders (8861a16).
## 18. The field bases stay after a repair (2026-10-30)

- The exe: close, reset (bases released), idle. A lancemate's refit is
  taken only while a player's call waits.
- Ours behind `bugs` (Owen's rule: one switch): `DockParked`. vehicles.md,
  After a repair; fixes.md, CHANGES, discoveries.
- Test `mission_c2m1_field_bases_stay_after_a_repair_headless` (the
  player, then Dominic by F10, then the player again, then M-M and the
  third M; the exe's path with `bugs` off). The c1m1 dock tests take
  either end.
- Branch `mac5/mfb-stay` off main d02a386.


## 20. W16 with the parked dock (2026-10-30)

- On lead/w17, W16's test waited for a second places step that the parked
  dock skips (the same point goes straight to the contact). The test now
  takes the rest as idle or parked (0 or 7), counts the second call as
  answered at the first front past the places that is not parked, does
  the second contact, and checks the player (worn before the call) is
  repaired: internals only, since the armour comes from the store and a
  debug launch has none. With bugs off the dock rests at idle and the
  second call jams, as before; the stuck-base give-up (a first call) is
  unchanged. No code change: the two fixes meet as designed.
- Branch `mac5/w16-park` off lead/w17 8e2ea92.
## 19. Suites at once: one scratch folder per process (2026-10-30)

- Under load (three mw3tests and an mw3sdltests at once, all from one
  build), two of three mw3tests failed
  `campaign_menus_to_c1m1_through_the_briefing_headless` ("restored"). Every
  run used the same `<build>/campaign_write`, which another run emptied and
  rewrote. They also failed the c2m1 lance keys test (a state's address
  reused; fixed in ab121b6 on the W17 branch).
- `MW3_TEST_TMP` is now `mw3_test_tmp()` (tests/check.h, main.cpp):
  `<build>/test-tmp/<pid>`, removed at exit; folders of dead runs are cleared
  at the next start. The same load: 3 x 242/0 and 34/0, no folder left.
- Branch `mac5/test-tmp` off main fa9aab7.


## 22. W22: the picture after the pause at 16:9 (2026-10-30)

- Reproduced on the Mac in the real program: `--briefing c1m1 --window
  1920x1080` with InGameVMode 13 in a scratch settings.reg, L, X X (the
  tether), Escape, Escape, shots at 1300 with and without the pause. At
  640x480 no difference.
- Not the camera: its place, angles, window, projection and the matrix
  stack were the same before and after (printed each frame), and the W13
  angle call off changed nothing.
- `SdlScreen2D::present_16` set the GPU viewport to the 2D canvas
  (853x480) and kept it; the resume is the same mode, so nothing set the
  frame's back. Fix: the viewport restored after the layer, as overlay_16
  did. Tests: `sdl_layers_keep_the_3d_viewport` (2 checks fail without),
  `mission_c1m1_pause_keeps_the_view_at_16_9_headless` (camera and angles
  across the pause in each view; the fake screen never draws the layer,
  so it does not catch this one).
- Branch `mac5/w22-camera` off main d7831d08.
## 23. W19: the system cursor in the middle of a fullscreen mission (2026-10-30)

- Not reproducible here (a hidden window has no visible cursor, and the
  shots show no software cursor at the centre). Read from the code: since
  bb64d484 `SdlInput::refresh` called `SDL_SetWindowRelativeMouseMode` only
  when its `captured_` flag changed (before, every acquire did). A mode SDL
  refused or dropped stayed off with the flag on: the cursor showed, the
  game's SetCursorPos (0x0051b3c0, not relative) put it at the centre each
  frame, and the motion's xrel still aimed.
- Fix: refresh compares SDL's relative state and SDL_CursorVisible with
  what it wants; a held mouse hides the system cursor itself.
  `sdl_held_mouse_keeps_the_cursor_hidden` drops SDL's relative mode
  behind SdlInput's back (3 checks fail without the fix).
- Owen to confirm on the PC. Branch `mac5/w19-cursor-fix` off main d7831d08.
## 24. W21: the HUD cut off after the options' resolution (2026-10-30)

- Reproduced on the Mac: `--briefing c1m1 --window 1920x1080` at
  640x480, Escape, `--click-at` OPTIONS (133,309), GRAPHICS & AUDIO
  (420,78), RESOLUTION's arrow (325,290) eight times, ACCEPT twice,
  Escape. Everything right of x 640 on the 853x480 canvas gone. Shift+V
  through the modes did not show it.
- Printed each frame: the buffer 853x480 but zrndr::target2d 640x480 for
  good. 0x004ab6b0 locks before the switch, so the target took the old
  mode's size and pointer; `screen_size` remade the buffer under it.
  Shift+V passes mode 12 (the same canvas), hiding it.
- Fix: `screen_size` sets the 2D and finish targets on the new buffer.
  Test `mission_c1m1_mode_change_moves_the_2d_target_headless` (4 checks
  fail without). Branch `mac5/w21-hud` off main d7831d08.

## 25. W30: lasers at c2m3's start (2026-10-30)

- Not reproduced on the Mac: `--briefing c2m3`, LAUNCH clicked at
  (565,437); the first 35 frames show no shot. A scripted click lets go
  the next frame. Read from the code: a press stays latched until a
  mouse read takes it (the one-frame click fix), and a held button reads
  down; either at the mission's first read is its fire. The exe's load
  took seconds; ours is at once.
- Fix: into a mission (SdlInput::refresh) a latched press is dropped and a
  held button is swallowed until let go. `sdl_mission_start_drops_the_launch_click`
  (3 checks fail without). Owen to confirm. Branch `mac5/w30-launch-fire`
  off main d1e5b9e6.

## 26. W27: the follow along the leader's way (2026-10-30)

- Breadcrumbs in FollowState (trail_record, trail_goal), the W17 detour
  made WallDetour and given to the fight's approach. Details, the numbers
  and the open pop-through: ai.md 17.6, W27.
- What it took on the walked routes: sight as wide and tall as a mech and
  past his own body (the segment queries hit his model), crumb by crumb,
  straight steering to a crumb, a teleport starting the trail afresh.
- The test's maps (MW3_W27_TRACE): the exe's stop on these routes is the
  standstill on open ground, not a wall contact.
- Branch `mac5/w27-breadcrumbs` off main d1e5b9e6.
## 27. W25: the message box over the lab or a mission (2026-10-30)

- The box (0x004ac150, ztwod::MessageBox) grabs buffer 2 as its backdrop
  and runs its own loop of lock, draw, flip; our 16-bit buffer is the
  menus' 640x480 while the lab's and a 16:9 mission's 3D are full size, so
  the picture behind the box was the buffer's copy, low-res.
- Fix: SdlTwodHost::screen_grab(2) over a GPU frame holds that frame on the
  GPU (SdlRender3D::hold_frame, a texture copy) and returns a key-coloured
  backdrop; GameEngine::video_flip restores the kept frame and lays the
  box's layer over it; the backdrop's free lets it go.
- Tested: `sdl_render_keeps_a_shown_frame` (the kept frame back pixel for
  pixel at 1280x720). The lab's overwrite and Ctrl+Q's box were not driven
  here (Ctrl+Q before a win opens none). Owen to confirm.
- Branch `mac5/w25-box-backdrop` off main 98665790.
## 28. W35: armour not repaired at c3m1 (2026-10-30)

- The paths repair: a c3m1 test (worn to half, two repairs, bugs on: the
  first call then the parked dock; off: two calls) brings armour and
  internals to full each time with a store holding armour.
- Owen's save (Deathstalker0301, loaded in the program through the pilot
  screen with a temporary print): the store holds standard armour tech 3
  x108 (and ferro tech 1 and 2), his mech is standard tech 3. The MFD's
  armour bars (the lance display, mode 3) are refilled each frame from
  the mech. So by the exe's rule his armour comes back; not reproduced.
- Added: each repair logs what it did with the armour (mech_repair.cpp),
  so the next one in his mw3.log says. Branch `mac5/w35-repair` off main
  98665790.
## 29. W36: the pause at 16:9 (2026-10-30)

- The pause was W13's exception (made at the canvas's origin 106); now at 0
  like every menu, so its buttons are at the left edge over the full
  backdrop; its title is a marquee wrapping at the display's width, so no
  move. Back from the options (mode 5), PauseState::resume sets the
  mission's mode again (behind any_resolution).
- Tests: the new round trip (opened, options, back: mode 13, 853 wide,
  RESUME and QUIT the same) and the click test's 16:9 origin now 0; the
  pause set 7/0. Shot in the outbox (w36-pause). Branch `mac5/w36-pause`
## 30. W37: the lab from Instant Action (2026-10-30)

- A port bug, not a fix of the exe: Engine::game_slot_count / game_slot
  (briefing.cpp) asked the campaign only; the lab from Instant Action's
  briefing got no slots, so no mech and nothing to edit. Now the current
  game's (CampaignGame or ActionGame, the exe's vt+0x38/+0x34).
- Test `instant_action_lab_has_the_players_mech_headless` (fails without);
  the instant_action, lab and briefing sets pass. Branch `mac5/w37-ia-lab`
  off main 98665790.

## 21. The first playtest release's package (2026-10-30)

- Branch `mac5/release-0.1` off main 48d694fe.
- `package.cmd`: mw3, mw3import (FFmpeg, the fall-back) and the 32-bit
  helper mw3indeo (`build_indeo.cmd build-win-pkg`); miniz's licence;
  check_dist, ffmpeg_notices and ffmpeg-configuration kept. THIRD-PARTY
  names the helper and miniz.
- `mw3.log` (run_log.cpp): stdout and stderr teed by pipe and thread to
  the write dir's log; the last start's is `mw3.old.log`. Checked on the
  Mac (two starts: rotation, both streams, the console unchanged);
  run_log.cpp compiles under mingw x86_64.
- The start's notes: data not 1.2 (`--patch`), files odd or missing, no
  music (`mw3import --cue ... --music-only`, or a rip), logged each start
  and boxed on the first. The 1.0 note checked on game/mw3-1.0. The box
  itself not seen here (a check run never shows it).
- Player messages that pointed at docs/ now point at README.txt.
- README.txt rewritten as the playtest guide (names `--install-from`
  from the next batch); docs/release-notes-0.1.md drafted for Owen.
## 31. The launcher, first build (2026-10-30)

- Branch `mac5/launcher-ui`: main 98665790, mac4/setup-api merged locally
  (dev.md's conflict as the union), the design commit, then the build.
  What it does and what is left: launcher.md, Built.
- Shots (`--launcher-shot`): main with the development copy found, setup
  with none, the progress screen; `sdl_program_launcher_screens`.
- Not yet: one window from launcher to game, MechWarrior (1989), sounds,
  the release's first-start notes inside it.
## 32. 16:9 menus, in progress (2026-10-30)

- Branch `mac5/menus-wide` (off main 98665790): `--wide-menus` (zfix
  wide_menus, a display switch like any_resolution, off by default),
  Engine::menu_vmode, GameEngine::widen_screen with per-screen group tables
  (main menu 0x486260, game options 0x4379f0), the background widened from
  its edges (Host::image_new). Lead approved tables (2026-10-30).
- The main menu and the game options are done: the underlines (the button's
  AUXBITMAP) move with their buttons, the options' LINES overlay is stretched
  at column 367, and menu_vmode is chosen once at start-up (screens are laid
  out when made). Test menus_wide_spread_by_their_groups_headless;
  resolution.md section 12.
- Graphics/audio done (table 0x438f60); widen_screen moves controls first
  and sets absolute positions (a cycle's value moved twice before). Lead
  wants a 4:3 original shot beside each 16:9 one.
- Launcher merged to main dd875bb9 (lead, 2026-10-31). When next on it:
  the MW3 button's "Ready: version 1.2, <path>" overflows with a long path
  (shorten from the middle with "..." or a second line); then one window
  into the game, MW1, sounds, first-start notes. mw3.log has two writers
  now (log_to_file without a console, RunLog otherwise).
- Controls done (0x42a590 had its own construct path, now widened; LINES
  stretched from row 300; CheckButton BOUNDS move).
- Pilot screen done (adopt_screen widens the pilot screen and the controls'
  two popups by size; file list arrows and rect; edit bars moved as images).
  The popups have no table yet: check them with message boxes.
- Briefing done (widened after construction; plain-image backgrounds by
  width, mirror inset 40; polyline points; waypoint shift_x; --click-at
  origin only over a mission, --briefing's ACCEPT marked right).
- Debriefing done (own construct path widened; per-screen side shading;
  Line2D ends). Test goes on through LAUNCH, a win, the debriefing.
- Lab done (one block; edge panels' looks extended; 3D window scaled to the
  render target and shifted; pulled rects anchored by place; the left
  mirror fixed in extend_texture).
- Instant Action done (table 0x488bc0, ALLY_TITLE stretched).
- Popups done (backdrops left in place); message boxes done (Object2D::read
  takes the dialog origin: FIX (B) row extended).
- Credits (lines stretched to the middle), salvage (block, shaded) done;
  training and multiplayer are the pilot screen. All menu screens done;
  the lead merges menus-wide. Old next list:
  message boxes; a test per screen (rects anchored), shots each.
- Shots: scratch run with VMode irrelevant; `--wide-menus --window
  1920x1080`, InGameVMode 13 in a scratch settings.reg; OPTIONS at canvas
  (58,360).

## 33. Launcher follow-ups (2026-10-31)

- Branch `mac5/launcher-path` (off main ffef80f0). The lead's list, in order:
  1. the path overflow: done (fit_middle by pixel width, the "Ready" words
     kept; text_fit.cpp in mw3text so the tests reach it);
  2. one window from the launcher into the game: done (LauncherChoice::window,
     SdlWindow::init(hidden, adopt), --launcher-play; not high-DPI);
  3. the MW1 entry: done (find_mw1/run_mw1, mw1_game=, save_config_value,
     --launcher-mw1; test with a stand-in script);
  4. the menu sounds: done (Sounds in launcher.cpp from the install's archive,
     --nosound, MW3_LAUNCHER_MUTE for the test);
  5. the first-start notes inside it: done (kNotes screen, launcher_notes.cpp,
     have_music; sounds load on any screen).

## 34. Linux (2026-10-31, mac5/linux)

- Built in WSL2 (Ubuntu 24.04) on Owen's PC, all under ~/mw3-linux (src,
  prefix, deps, build, game copy). GCC fixes, PIC mw3core, the finder's
  Wine/Proton/Lutris places, Linux launcher fonts, make-appimage.sh,
  docs/linux.md. mw3tests 262/262, mw3sdltests 46/46 (one at a time,
  run_capped --mem-gb 5, 7 for the screens test).
- The hidden-frame fence in SdlRender3D::present (software Vulkan fell
  behind, 200 MB/s); SDL_SyncWindow after a resize.
- build_deps.sh linux (minimal LGPL FFmpeg, Owen's permission rule) into
  src/build-deps/linux/prefix; the AppImage bundles mw3import; --import
  --movie INTRO converted 19 movies and played the intro.
- My unthrottled SDL run OOM'd WSL (13 GB cap) and broke WSL's service until
  Owen's wsl --shutdown. WSL stops a distro with no wsl.exe client attached:
  run jobs attached, never nohup.
- Then the README for players, docs/building.md, docs/release.md. Queue
  empty: waiting for Owen's playtest or a merge problem.
