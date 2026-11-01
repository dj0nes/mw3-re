# mw3-re

Learning project: understand MechWarrior 3's executable with REA and rebuild its
mechanics. The goal is learning how REA and binary reverse engineering work, not
a byte-identical port.

## Decisions so far (2026-10-18)

- Target: a rebuild that runs on modern systems including macOS from day 1, no
  Wine. It does exactly what the original did, reusing the original's logic as
  closely as possible, but is not byte-identical.
- Language: C++. Game logic is a plain C++ library with no engine types, built
  from Ghidra's decompiled C: give Ghidra the structs first, then clean each
  function into real C++ and keep its original address in a comment
  (`// Mech3.exe 1.2 @ 0x00412340`). A thin SDL3 layer does
  the window, rendering (SDL's GPU API: Metal, D3D12, Vulkan), audio and input,
  standing in for the DirectX, MCI and Win32 calls one to one, and the
  original's own main loop stays as written (decided with Owen 2026-10-19:
  closest to the original and most portable). Godot (GDExtension) is the
  escape hatch if SDL gives too much trouble; the game library does not change
  either way.
- Later, after the port is complete (Owen, 2026-10-20, back burner): a
  high-res version with new models (Blender etc.) as an optional replacement
  layer in the same port, not a fork. Keep this possible now: the renderer
  stays separate from the game library; models, parts, firepoints and props
  stay looked up by name; a replacement mech keeps the original part tree,
  pivots and firepoints so the motions work; gameplay keeps the original
  geometry (collision, terrain, AI bounds). This is a personal learning
  project, not a product (Owen): nothing is sold; game files stay out of the
  repo as usual.
- Also at the end (Owen, 2026-10-23, back burner): the stealth object and
  BASIC mode, ported and wired to a debug key as a fly camera (the release
  cannot reach it; docs/vehicles.md, The stealth object). Until then the
  stealth object is made only as the mech swap's camera placeholder.
- Checking: differential tests run the original x86 function (in an emulator
  such as Unicorn, on the Mac) and the C++ version on the same inputs.
- Assets: never committed. A first-run importer reads the player's MW3 1.2
  install/disc and converts locally. mech3ax (TerranMechworks/mech3ax, Rust)
  documents the .zbd formats; port its parsers to C++.
- Music: wired up from the start, songs dropped in later. The original asks MCI
  to play CD track numbers, so the rebuild keeps those numbers: the game asks
  for "track N", and the audio layer plays `music/trackNN.ogg` (or .flac/.wav)
  from the player's folder if it exists, else stays silent. A ripped disc maps
  one to one. Which track plays when comes from the exe's MCI calls.
- Reference runtime: the original runs on Windows 11 (Owen's PC) with
  DDrawCompat. dgVoodoo does not work for MW3.
- UnMech3 (Unity) was tried and found weak (campaign barely works, last release
  Sept 2025, source not found). Not a base to build on.

## Guiding rule (Owen, 2026-10-18)

Stay as close to the original code as possible. Change it only to fix what makes
it (A) unable to run on macOS and modern Windows (Win32, DirectX 6, MCI, x86-only
tricks such as the ESP blit) or (B) break on modern Windows without helpers like
DDrawCompat. Each such fix is noted next to the function it touches. The
scaffold is shaped by the real decompiled code, not designed ahead of it.

Rendering (Owen, 2026-10-25): it should look at least as good as the
original at its best, and better where it can. The game's logic stays the
original's; how a picture is made does not have to (no emulating 1999
methods where a modern one does the job right). The software renderer is
not ported: hardware textures only, and a software look, if ever wanted,
is a shader pass (8-bit palette, no filtering, low resolution) left open
for later. Anything only the software path showed comes to the GPU path.

## Tools on this Mac

- Ghidra 12.1.4 and JDK 21 from Homebrew:
  `GHIDRA_INSTALL_DIR=/opt/homebrew/opt/ghidra/libexec`,
  `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`.
- REA 6.7.0 installed globally under nvm Node 24
  (`~/.nvm/versions/node/v24.22.0/bin/rea`); the default Node is
  older than REA allows. The CLI works without the MCP registration:
  `rea <command> game/mw3-1.2/Mech3.exe --provider ghidra --snapshot
  analysis-output/mech3.snapshot.json --json`.

## Game files (git-ignored, under `game/`)

- `game/mw3-1.2/` is a patched v1.2 install assembled from Owen's backups on
  Owen's Win98 backups (`MechWarrior 3.zip` = the disc ISO, `MW3 patches
  and cracks.zip` = the official 1.2 patch). Rebuild it from there if lost: the
  disc's `DATA1.CAB` is InstallShield (`unshield`, in Homebrew); every component
  merges into one folder without collisions, then the patch's `Patch Files`
  go on top, plus the disc's `VIDEO/`.
- `Mech3.exe` 1.2 (official, not a NoCD build): PE32 i386, sha256
  `95bc2c114c9b2e5c5ada8e40ca78cc79470f862e45313ffad0d1e8e6d4d916bf`.
- Music is CD audio played through MCI (`cdaudio`, `mciSendCommandA`), and the
  backup ISO is data-only, so this copy has no music.
- Cutscenes are Indeo 5 AVIs (`Ir50_32.dll` ships with the game).
- The install has software-texture sets as well as hardware ones: MW3 has a
  software renderer, the likely home of the blit trick.
- Pirate's Moon (expansion): after MW3 is complete, the same process (Owen,
  2026-10-20). Backup: `MW3 pirates moon.zip` in the same backups (the
  disc image inside). Same studio, same GameZ engine and MW3 code base, so
  the plan is to diff its exe against MW3 1.2 function by function (match by
  bytes and structure, then by call graph), port only what changed, and run
  the existing differential tests against the PM exe too (`original.py`
  takes the exe: `MW3_EXE=pm` or `Original(exe='pm')`). mech3ax reads PM
  formats, as a cross-check. Two games, two programs, one package (Owen,
  2026-10-31, replacing "both games in one program"; docs/games-layout.md):
  `mw3` and `mw3pm` are separate executables over one shared engine, plus
  a launcher program (ours) that starts whichever the player picks; each
  game also starts directly, without the launcher. One repo: `src/gamez/`
  shared, `src/mech3/` MW3 only, `src/pm/` PM only (PM's own and its
  changed versions, `// Pirate's Moon 1.0 @ 0x...`); separate CMake
  targets and test executables, so either game builds and ships alone. One
  download per platform holds all three. MW3 is tagged `v1.0` ("done")
  before any PM code reaches `main`; until then PM work stays on the
  `mac5/pm` branch. Each game mounts its own data and keeps its own saves.
- Pirate's Moon 1.0 is extracted for reading alongside MW3 (not ported):
  `game/pm/` (the install, `Mech3.exe` sha256 57cd2ee7...) and
  `game/pm-disc/` (the image; track 2 is CD audio). First findings and the
  differences from MW3: `docs/pirates-moon.md`. Community-reported MW3
  problems to check the port against: `docs/community-issues.md`.
- REA source for reading: `../rea` (beside this repo) (a clone, not a
  dependency).
- mech3ax source for checking formats: `../mech3ax` (beside this repo)
  (a clone, EUPL-1.2). Ports come from the exe; mech3ax is the cross-check.

## Known from Owen's own disassembly

- MW3 uses the stack-pointer blit trick (ESP loaded with a surface pointer,
  push/pop runs). In a rewrite it is replaced by engine drawing, not ported.

## Working with REA

- Ghidra sessions in REA are ephemeral: names set through REA are lost on close.
  `docs/symbols.csv` is the project's map of the binary; findings go in `docs/`.
- Each `rea` CLI call re-imports the exe (~80 s). `tools/analysis/readaemon.py`
  keeps one `rea mcp` session open (queries then take under a second); see
  `tools/analysis/README.md`. Not every CLI command takes `--snapshot`.
- REA cannot decompile an address Ghidra never made a function: it treats the
  address as a file offset and refuses. About 1,000 such functions are reachable
  only through vtables and callback tables; `tools/ghidra/CreateAndDecompile.java`
  creates them in a persistent headless project (`analysis-output/ghidra-project`).
- `analysis-output/decomp/<addr>.c` holds Ghidra pseudocode for all ~10,950
  functions; `func2file.json` gives each one's original source file; `mech3.asm`
  is the full objdump disassembly. Grep these before asking REA.

## Code layout and conventions

- `src/gamez/<module>/` and `src/mech3/` mirror the original source tree
  (`docs/module-map.md`); when the original file name is unknown, say so in the
  header and name it after its neighbours.
- Every ported function starts with `// Mech3.exe 1.2 @ 0x00xxxxxx`. Original
  globals become struct fields with their address in a comment.
- Comments (Owen, 2026-10-19): short ones where decompiled logic is not obvious
  (a quirk, a magic number, why a branch exists), one or two lines, pointing to
  `docs/<topic>.md` for more. How a system works, the evidence and open
  questions go in `docs/`. No comments that restate the code, no long blocks.
- The target is play-accurate, not byte- or bit-identical (Owen, 2026-10-19).
  Differential tests compare floats within a tolerance; no chasing x87 precision.
- Goal (Owen, 2026-10-20): the game working as intended where the original
  has bugs, and running well on modern systems. Port the logic as written,
  quirks that are design included (see zgameapp.cpp); change it for (A)/(B)
  problems and for bugs whose intent is clear (crashes, typos, misplaced
  data), marked `FIX (A)` / `FIX (B)` / `FIX (bug)` and listed in
  `docs/fixes.md`. The differential test checks the original path and says
  where the fix departs. Where the intent is a guess (balance), list it in
  `fixes.md` as a candidate for Owen. Every change from the original also
  gets a plain-words line in `CHANGES.md` at the repo root (Owen,
  2026-10-20: the list of what the rebuild changes). Play-changing fixes
  sit behind `zfix::options()` (`per_second`, `bugs`; per-frame steps are
  scaled to a 30 fps frame), which the probe library turns off so the
  differential tests check the original path. The copy-protection and anti-tamper
  traps are removed (Owen, 2026-10-20), marked `FIX (trap)`
  (`docs/protection.md`).
- Self-contained functions get a differential test (`tests/differential/`) that
  runs the original x86 in Unicorn against the C++ through `tests/probe.cpp`
  (or a `tests/probe_<topic>.cpp`).
- FPU precision: in play the x87 runs at 24-bit (single) precision, because
  Direct3D 6 sets it and the game does not pass DDSCL_FPUPRESERVE
  (SetCooperativeLevel flags 0x13 at 0x577c4b). Tests of code that runs while
  the game is up set the emulator's FPU to 24 bits; ports use float, and
  mw3core builds with `-ffp-contract=off` (clang fuses multiply-adds
  otherwise; the per-file `#pragma STDC FP_CONTRACT OFF` lines say the same). Then
  the clippers match bit for bit (`render-clip2d.md`). Only the CRT start-up
  sets the precision itself (0x5ac5a0: `_controlfp(_PC_53)`, before WinMain).
  Checked in play (Owen, 2026-10-20): mechs cast visible shadows in hardware
  mode on Windows 11 with DDrawCompat, so the reference runtime does not
  truncate there, and the four emulator findings that need 24-bit (point-lit
  textures black, frozen cloud shading, blank shadow images, black projected
  lights; `fixes.md`) do not happen in play. The port gives those four the
  full-precision result by default, with the 24-bit one behind a switch for
  the tests. Whether Win98 with real DirectX 6 hid them stays unknown.
- `tools/viewer` (`mw3view`, SDL3 GPU, Metal shaders for now) is a development
  viewer that draws a mission world from the ported loaders. It is not the
  render port (zRender, mapped in `docs/render.md` with a porting order).
- Every quirk, bug or hidden mechanic found goes in `docs/discoveries.md`, in
  plain words for players (Owen wants to share these with the MW3 community).

## Working with agents (2026-10-20)

- Standing rule (Owen, 2026-10-20): **by day no subagents: the work is
  done in the main window; at night possibly a couple, when Owen says so.
  Agents never start agents of their own** (every agent prompt says so).
  Owen sets the number: 5, then 2 after five at once hit the account's
  usage limit, then 3 overnight, then none by day after that night used
  about 40% of the week's usage.
- How agents run (Owen, 2026-10-20, after comparing with another project's setup,
  which did a night's work for about a quarter of our usage): up to two
  long-lived specialists by area (e.g. game systems; front end and HUD),
  reused and compacted rather than restarted, each keeping a work log in
  `docs/worklogs/<area>.md` to pick up from; small assignments (one
  function family or one screen, not a whole subsystem), one at a time,
  the specialist waiting between them; targeted test runs while working
  and the related set once before reporting; short tool output. Everything
  stays on Opus.
- The sessions (Owen, 2026-10-25): one worker, "mw3 dev", takes every
  area one item at a time, and the lead session "mw3 lead" assigns,
  reviews and merges, over SendMessage by the protocol in
  `docs/worklogs/README.md` (one worker plus one reviewer evens out the
  usage). They were "mw3 mac 4" and "mw3 mac 1"; the area specialists
  "mw3 mac 2" (game systems) and "mw3 mac 3" (front end) are paused, and
  restarting one means pausing the dev. Porting and analysis run on Opus
  5.5. The lead reviews each item itself and makes the review's fixes
  directly (Owen, 2026-10-21, dropping the Sonnet review subagents).
  Everything uses the 5.5 models.
- The specialists stay peer sessions (Owen, 2026-10-20 night, reversing
  a plan to move them into long-lived subagents): co-agents given orders
  cost a half to a third of subagents (the night of 69 fresh subagents
  drained the most), and they compact on their own. Owen compacts them
  after each major feature when he is there.
- Each agent takes a piece with clean boundaries:
  leaf functions, or systems that meet others through a hooks struct the
  test stubs identically on both sides.
- Each agent works in its own git worktree (Agent `isolation: worktree`),
  links the ignored `game/` and `analysis-output/` in, builds its own
  `build/`, and commits on its own branch. The lead reviews, runs the tests
  and merges; nothing reaches main unreviewed. (Agents in the shared tree
  broke each other's builds through half-finished CMake edits.)
- Each piece arrives with a differential test, break tests that show the
  test catches errors, its doc, and player-facing finds for
  `docs/discoveries.md`. Quality over speed (Owen: "do it right even if it
  takes longer").

## Next steps

Where things stand (2026-10-20): every data format loads; the render path is
ported end to end (docs/render.md, `mw3view --real`); the main loop, game
states, options, damage, heat, power, the mission script interpreter and the
world parameters are ported. Next, in parallel where the pieces meet only
through hooks:

1. The game half's systems: mech movement and physics (terrain, collision,
   jumping), the rest of zWeapon (spawn, tick, homing, beams; `weapons.md`),
   the AI, sound (zSound) and music.
2. The 2D front end (zTwoD) and its menus, the mech lab, the HUD; then the
   movies (Indeo 5).
3. Projected shadows and puffers (in progress), then the remaining render
   pieces (lens flares, the cockpit pass; the software path is not ported,
   see the guiding rule).
4. The SDL3 program itself: platform implementations (window and message
   pump, clock, input, audio, Render3D) and the hooks wired to the ports, so
   the port starts like the game; a debug start straight into a mission for
   testing while the menus are not ported.
5. Any resolution and 16:9, up to 4K, as a setting (Owen, 2026-10-20;
   `fixes.md` candidates): after the settings loader and the menus.
6. Open from earlier: the lava-per-frame decision (Owen), mission mode 3 and
   the controls value in heat, the loose-text .zrd tokenizer, weapons/ammo
   and pilot structures, the detail.zrd loader and registry settings.

## Work queue (small pieces, 2026-10-19 night)

Each item is one sitting; findings go in `docs/`, quirks also in
`docs/discoveries.md`.

- [x] Textures: traced and ported (`docs/textures.md`), identical on all
      10,612 textures.
- [x] Textures: `tools/analysis/tex2png.py` exports any library as PNG.
- [x] Game types: mapped (`docs/gametypes.md`); 3 is multiplayer.
- [x] Network: message types mapped (`docs/network-messages.md`); state sync.
- [x] Damage: ported and emulator-checked from TakeDamage to Die
      (`mech3/mech_parts`, `mech_damage`, `docs/damage.md` section 4).
      Left: the weapons' side 0x4b0de0 / 0x4b1040, the kill credit
      0x41b420, the death animations and the torso jolt's decay.
- [ ] Weapons at run time: weapons.zrd into the definitions, the whole
      weapon manager (groups, bins, projectile and beam weapons, crits, arm
      restriction, def callbacks), and zWeapon's damage path and flight step
      ported and emulator-checked (`gamez/zweapon`, `mech3/weapon`,
      `test_weapons_zrd/run/hit.py`). Left: spawn, the tick, homing,
      collision, beams and mines in zWeapon, the vehicle builder 0x4b4660 and
      the AI planner (`docs/weapons.md`, Left to port).
- [x] Controls: mapped (`docs/controls.md`); +0x1c is GetThrottle.
- [x] Power: ported and emulator-checked (`docs/heat.md`).
- [ ] Mech definition: the reader 0x46ba40 ported whole, with its entry
      0x46b7a0 and cache 0x4846a0, identical to the original on all 19 files
      (`mech3/mech_reader`, `mech_skin`, `mech_config`, `mech_names`;
      docs/mechdef-*.md), the dtor 0x46b9b0 and the saved loadouts' loader
      0x424b70 and writer 0x424f60 (`test_mechsave.py`). Left: the second
      loader 0x4260c0 and the mech lab's callers (`mechdef-head.md`).
- [x] Motion: ported and emulator-checked (`docs/motion.md`).
- [x] Mechlib: mapped (`docs/mechlib.md`), with a porting order.
- [x] Mechlib: materials and models ported (`src/gamez/zclass/cls_mechlib`),
      identical on all 442 materials and 770 models.
- [x] Mechlib: the node tree ported (`cls_mechlib` read_tree, `object3d`,
      `cls_node`), identical on all 57 trees; the tree copy, destroy and reset
      and the mech data loader ported (`docs/mech-build.md` section 14);
      mechlib models are the world's `zmod::Model`s now.
- [x] Mission worlds: mapped (`docs/gamez.md`), walk scripts in
      `tests/differential/explore/gamez_*.py`.
- [x] Mission worlds: `gamez.zbd` ported end to end (`zclass/cls_zbd`
      0x4ff520 and the section readers), identical on the six worlds and on
      broken files. Left for later: the re-read path (0x4ff730, 0x4ff7a0,
      0x538640), `docs/gamez.md` porting order step 7.
- [x] Mission scripts: mapped (`docs/interp.md`), decoder in
      `tests/differential/explore/interp_*.py`.
- [ ] Movement: mapped (`docs/movement*.md`); ported and emulator-checked:
      the controls' smoothing, the gait, turning, Move, jump jets, landing,
      the action queue (falls, getting up), the ground probes on the real
      worlds (`mech3/mech_control`, `mech_move`, `mech_skis`, `mech_anim`).
      Left (`movement.md`, Left): Mech::Update 0x419fc0, collision, the
      engine's ground and segment queries (cls_di.c), animation sampling,
      the pose on the parts.
- [ ] 2D front end (zTwoD): mapped (`docs/twod.md`); objects, layers, the
      mouse routing, images, push and check buttons, text (GDI through
      `ztwod::Host`), the cursor, sliders, list boxes, text boxes, the
      message box, dialogs from their .zrd and the software blit ported,
      emulator-checked on every shipped dialog (`test_twod.py`,
      `test_twod_msgbox.py`, `test_twod_blit.py`); the main menu and the
      shared menu pieces (`mech3/mainmenu`, `menu_widgets`,
      `test_mainmenu.py`); the options, graphics, controls and credits
      screens, the edit bar, the file list and the controls load/save box
      (`docs/menus.md`). Left: the HUD primitives, an SDL3 Host, the
      ripple draw 0x558dd0, the pilot screen and the campaign, Instant
      Action, training and multiplayer screens (menus.md, Left).
- [x] Mission animations: reader, post-load pass 0x50ae60 and the runtime
      (every shipped event type, instances, reset/start/stop, the action
      lists) ported and emulator-checked frame by frame on all six missions
      (`zeffect/zeff_anim_*`, `docs/anim-run.md`). Hooked: zSound play, the
      ground and ray queries (cls_di.c), events 3/16/20/21/36/37/38, weapon
      detonation, zClass's node/frame update (0x4f7e00, 0x4f7f70).
- [x] Mission scripts: the archive, reader, macros, preprocessor, dispatcher
      and every command's arguments ported (`gamez/zinterp`,
      `mech3/mech3_interp`), identical on every shipped script and on the six
      worlds (`docs/interp.md` section 5). Engine calls go through
      `zint::engine()`; the unported ones are null until their modules land.
      Open: the debug console 0x5960a0, the mission load 0x48faf0.
- [x] Puffers (smoke, fire, trails): ported and emulator-checked bit for bit
      (`zmodel/gmod_puff`, `zrender/zrndr_draw`, `zeffect/zeff_anim_puffer`,
      0x57caa0; `docs/puffers.md`). The anim event 42 is ported, waiting for
      the anim run to call it.
- [ ] AI: mapped (`docs/ai.md`: files, classes, the tick, a porting order);
      the data side ported and emulator-checked on every shipped file (net
      and region files, aiv.zrd, targets.zrd, the valves registry;
      `mech3/zrdrainet`, `zrdraiv`, `zainet_node`, `zai_params`, `zai_valve`,
      `test_ai_readers.py`). Next: regions, the run-time net, net queries,
      valves, targeting, behaviours, the brain (`ai.md` section 6).
- [ ] Assets: interp scripts, gamez.zbd and anim.zbd traced (`interp.md`,
      `gamez.md`, `anim.md`); trace what remains in `docs/assets.md`, one per sitting,
      cross-checked against mech3ax.
