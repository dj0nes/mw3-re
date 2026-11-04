# Pirate's Moon: the function map and a porting order

Plan stage (2026-10-31, dev 2, branch `mac5/pm`): nothing ported. Builds on
`docs/pirates-moon.md` (the copy, the data, the function diff's method) with
the map against the port, the differential tests and the data formats.
Regenerate: `tools/analysis/pmdiff.py` (the pairs), then
`tools/analysis/pmmap.py` (the tables below; `analysis-output/pm-diff/
port-impact.txt` lists every ported function PM changed or dropped).

## The categories

Of MW3 1.2's 8,030 functions and PM 1.0's 8,567 (unwind funclets aside):

| | functions | meaning |
|---|---|---|
| identical | 1,488 | the same instructions once addresses are named by what they point at |
| compiler only | 3,856 | other instructions, the same calls, strings, constants and imports: PM was rebuilt with Visual C++ 6 (MW3: 5.0). Of these, 1,102 read or write struct fields at other offsets and 219 compare other small constants (pmflags.py); the rest, 2,642, look like the same source |
| changed | 1,531 | calls, strings, constants or imports added or removed: a source change |
| moved | 630 of the paired | paired, but the source range they sit in has another name (PM's tree is `branchep1\mech3ep1\...`); counted within the rows above |
| new in PM | 1,692 | no MW3 twin |
| gone (MW3 only) | 1,155 | no PM twin: removed (the movies, the fog, the old text reader), inlined by VC6, or rewritten past recognition |

"Compiler only" is not proof of the same source. pmdiff.py counts constants
from 0x100 up only, and not field offsets, so a struct that grew and a
small constant that changed both pass as "compiler only"; pmflags.py checks
both (above). Its median is 11 instructions different; 565 differ in more
than 50. A sample of 30 of those 565 (random, seed 1), read as pseudocode
(`tools/analysis/pmlogic.py`, names and addresses normalised): 15 read
fields at other offsets (the anim definition, the mech, the weapon and
vehicle classes grew in PM), one of them also changing logic (0x509640, the
anim definitions' node lookup: a check added that the second lookup is not
the first's node, and a flag test dropped); 1 more changes logic (0x43f660,
a constructor: a type byte 2 -> 3, set with a mov pmflags.py does not
check, and another vtable); 2 compare other small constants; and 12 are the
compiler or Ghidra (argument recovery, inlining, x87 compares decoded
another way, early returns for nested ifs). Of all 565, 237 read other
offsets and 86 compare other constants.

## The port's functions in PM (by the port's module)

| module | identical | compiler only | compiler, other offsets or constants | changed | gone | not in the diff |
|---|---|---|---|---|---|---|
| mech3/mech | 68 | 136 | 142 | 127 | 66 | 28 |
| gamez/zclass | 4 | 59 | 37 | 166 | 13 | 1 |
| gamez/ztwod | 30 | 128 | 32 | 47 | 20 | 5 |
| mech3/zai | 26 | 52 | 51 | 49 | 69 | 3 |
| gamez/zmodel | 6 | 61 | 26 | 54 | 33 | 4 |
| mech3/lab | 34 | 68 | 39 | 29 | 5 | 2 |
| mech3/hud | 27 | 34 | 53 | 41 | 8 | 0 |
| mech3/app | 13 | 39 | 23 | 34 | 21 | 7 |
| mech3/pilot | 14 | 28 | 29 | 30 | 20 | 2 |
| gamez/zeffect | 6 | 18 | 29 | 28 | 33 | 8 |
| mech3/menu | 19 | 41 | 31 | 8 | 11 | 3 |
| mech3/weapon | 7 | 11 | 49 | 23 | 11 | 6 |
| gamez/zinput | 28 | 54 | 2 | 6 | 1 | 0 |
| mech3/options | 25 | 37 | 6 | 10 | 9 | 0 |
| gamez/zgeometry | 0 | 51 | 14 | 21 | 0 | 0 |
| gamez/zsound | 3 | 39 | 5 | 22 | 16 | 0 |
| gamez/zweapon | 0 | 14 | 28 | 24 | 10 | 2 |
| gamez/zmath | 0 | 55 | 11 | 0 | 4 | 1 |
| mech3/controls | 8 | 24 | 10 | 14 | 4 | 0 |
| mech3/veh_basic | 8 | 9 | 20 | 11 | 0 | 1 |
| gamez/zvideo | 3 | 16 | 1 | 3 | 3 | 21 |
| mech3/zainet | 4 | 11 | 9 | 11 | 11 | 0 |
| mech3/player | 2 | 11 | 9 | 16 | 6 | 2 |
| mech3/mech_part | 4 | 9 | 24 | 8 | 0 | 0 |
| gamez/zrender | 3 | 22 | 1 | 9 | 3 | 5 |
| gamez/zinterp | 9 | 22 | 2 | 7 | 2 | 1 |
| gamez/zreader | 0 | 22 | 2 | 8 | 10 | 0 |
| mech3/veh | 1 | 6 | 17 | 14 | 2 | 0 |
| gamez/zfmv | 2 | 8 | 5 | 3 | 21 | 1 |
| mech3/mission | 0 | 14 | 5 | 14 | 5 | 1 |
| gamez/zimage | 2 | 14 | 9 | 8 | 4 | 0 |
| mech3/mech_fieldbase | 11 | 8 | 6 | 9 | 1 | 1 |
| mech3/briefing | 0 | 3 | 3 | 4 | 25 | 0 |
| gamez/zsiscript | 3 | 23 | 2 | 1 | 3 | 0 |
| gamez/zutil | 3 | 20 | 5 | 2 | 1 | 0 |
| gamez/zdeclient | 1 | 16 | 3 | 7 | 0 | 2 |
| mech3/pocket | 0 | 1 | 19 | 6 | 1 | 1 |
| mech3/debriefing | 4 | 6 | 7 | 9 | 0 | 1 |
| mech3/turret | 1 | 4 | 8 | 5 | 4 | 1 |
| mech3/zrdrvalve | 4 | 7 | 1 | 8 | 3 | 0 |
| mech3/sensors | 0 | 3 | 8 | 7 | 3 | 0 |
| mech3/zrdraiv | 6 | 6 | 5 | 3 | 0 | 0 |
| mech3/zrdrainet | 3 | 3 | 8 | 3 | 0 | 0 |
| mech3/zainetedit | 2 | 3 | 5 | 6 | 1 | 0 |
| mech3/force | 8 | 4 | 1 | 2 | 0 | 0 |
| mech3/message | 3 | 1 | 10 | 1 | 0 | 0 |
| mech3/action | 0 | 4 | 0 | 5 | 4 | 0 |
| gamez/zsave | 1 | 8 | 1 | 0 | 1 | 0 |
| mech3/entity | 2 | 4 | 2 | 0 | 1 | 0 |
| mech3/zai_anim | 0 | 3 | 0 | 2 | 2 | 2 |
| gamez/zgame | 1 | 3 | 0 | 5 | 0 | 0 |
| mech3/sound | 0 | 5 | 0 | 0 | 0 | 3 |
| gamez/znetwork | 0 | 5 | 0 | 1 | 1 | 0 |
| mech3/credits | 0 | 1 | 0 | 4 | 1 | 0 |
| mech3/mainmenu | 1 | 0 | 0 | 3 | 2 | 0 |
| mech3/salvage | 0 | 1 | 0 | 5 | 0 | 0 |
| mech3/lance | 0 | 0 | 1 | 3 | 1 | 0 |
| mech3/cockpit | 0 | 5 | 0 | 0 | 0 | 0 |
| mech3/quit | 1 | 3 | 0 | 0 | 0 | 0 |
| mech3/landmines | 0 | 2 | 1 | 1 | 0 | 0 |
| mech3/mech3 | 1 | 1 | 0 | 2 | 0 | 0 |
| gamez/zentity | 0 | 4 | 0 | 0 | 0 | 0 |
| mech3/pause | 0 | 0 | 0 | 1 | 2 | 0 |
| mech3/music | 0 | 1 | 0 | 0 | 1 | 0 |
| **all** | **412** | **1271** | **817** | **950** | **479** | **115** |

The port has 4,044 addresses; 115 are not in the diff (data tables, thunks
and functions Ghidra did not make). For the port: **1,683 (42%) need
nothing** (identical, or rebuilt with the same field offsets and constants),
**817 were rebuilt but read other field offsets or compare other constants**
(a struct whose layout PM changed: the logic may be the same, the layout is
PM's), **950 changed** and **479 are gone in PM**. Each changed or gone one is
listed with what changed in `port-impact.txt`; the offsets and constants of
the 817 in `compiler-flags.json` (`tools/analysis/pmflags.py`).

## MW3 functions not ported yet, by MW3 source file (range start)

| file | identical | compiler only | compiler, other offsets or constants | changed | gone |
|---|---|---|---|---|---|
| zaitarget.cpp | 107 | 122 | 25 | 38 | 74 |
| mech_map.cpp | 114 | 74 | 34 | 33 | 20 |
| mission.cpp | 72 | 78 | 12 | 32 | 58 |
| mech.cpp | 71 | 54 | 22 | 42 | 55 |
| zainetedit.cpp | 58 | 68 | 44 | 35 | 16 |
| player.cpp | 49 | 56 | 10 | 34 | 35 |
| zrdrainet.cpp | 62 | 69 | 23 | 9 | 10 |
| mech_data.cpp | 43 | 28 | 18 | 26 | 48 |
| zgameapp.cpp | 45 | 88 | 9 | 12 | 8 |
| class.c | 33 | 40 | 1 | 36 | 15 |
| briefing.cpp | 18 | 49 | 14 | 7 | 20 |
| zreader.cpp | 8 | 33 | 5 | 11 | 47 |
| zainetmechinout.cpp | 30 | 25 | 7 | 8 | 29 |
| gmod_init.c | 17 | 56 | 4 | 5 | 5 |
| zent_init.cpp | 40 | 19 | 12 | 11 | 5 |
| weapon.cpp | 29 | 21 | 12 | 4 | 13 |
| turret.cpp | 10 | 21 | 11 | 7 | 29 |
| zrdrvalve.cpp | 17 | 33 | 0 | 20 | 3 |
| mech_salvage.cpp | 9 | 16 | 13 | 14 | 15 |
| zrdraiv.cpp | 24 | 15 | 4 | 20 | 3 |
| mech_pocket.cpp | 28 | 9 | 11 | 5 | 12 |
| zsnd_parm.cpp | 28 | 26 | 1 | 10 | 0 |
| si_script.cpp | 11 | 33 | 5 | 4 | 9 |
| fmv_script.cpp | 13 | 23 | 6 | 7 | 12 |
| mech_anim.cpp | 8 | 6 | 30 | 4 | 12 |
| veh_basic.cpp | 10 | 22 | 4 | 9 | 7 |
| zrndr_draw.c | 4 | 28 | 3 | 13 | 4 |
| znet_dplay.cpp | 3 | 19 | 0 | 11 | 6 |
| znet_session.cpp | 5 | 21 | 1 | 2 | 1 |
| mech_objective.cpp | 10 | 7 | 3 | 1 | 6 |
| mech_parts.cpp | 3 | 4 | 11 | 5 | 4 |
| zimg_fonts.cpp | 4 | 16 | 0 | 2 | 4 |
| zvid_buff.c | 15 | 3 | 0 | 1 | 7 |
| mech_waynet.cpp | 6 | 10 | 5 | 2 | 2 |
| zinterp_parse.cpp | 13 | 8 | 1 | 2 | 1 |
| gmod_matl.c | 4 | 4 | 1 | 4 | 11 |
| zsnd_create.cpp | 2 | 11 | 2 | 3 | 6 |
| zvid_dd.c | 2 | 4 | 0 | 15 | 1 |
| zvid_init.c | 5 | 10 | 1 | 2 | 3 |
| zsnd_cd.cpp | 4 | 10 | 0 | 4 | 1 |

## New in PM, by PM source range (start)

- zaitarget.cpp: 162
- mech.cpp: 130
- zrdr_main.cpp: 122
- mission.cpp: 114
- mech_map.cpp: 97
- zainetedit.cpp: 85
- zainetmechinout.cpp: 73
- class.c: 71
- player.cpp: 55
- mech_data.cpp: 54
- zrdrainet.cpp: 51
- gmod_matl.c: 39
- weapon.cpp: 35
- mech_skin.cpp: 32
- cls_clutter.cpp: 32
- mech_anim.cpp: 29
- zent_init.cpp: 28
- briefing.cpp: 27
- zwep_init.c: 26
- mech_waynet.cpp: 24
- mech_objective.cpp: 23
- cls_world.c: 20
- zsnd_init.cpp: 20
- veh_basic.cpp: 17
- mech_pocket.cpp: 16
- gmod_const.c: 16
- cls_di.c: 15
- zvid_buff.c: 15
- promote1.cpp: 14
- gmod_init.c: 14

## Running the differential tests on PM (step 0, 2026-10-31)

**The address map** (`tools/analysis/pmaddr.py`, after pmdiff.py; writes
`analysis-output/pm-diff/addrmap.json`):

| | mapped |
|---|---|
| functions | 6,891 (pmdiff's 6,875 pairs, and 16 short functions by their instructions) |
| instructions inside them | 231,054 (each pair's instructions aligned; a hook mid-function lands on its twin) |
| import slots | 450 of 474 (by DLL and name, else the name alone: msvcp50 -> msvcp60); the 24 PM does not import (the movie DLLs, realloc, the critical sections) go to a scratch page |
| import thunks (`jmp [slot]`) | 236, by their translated instruction |
| globals | 11,224 addresses in 1,365 blocks: aligned instructions' operands, paired in order; an address between two pairs of a block (same delta) maps by it |
| vtable and callback-table slots | 14,710 of 15,446: each slot to the PM slot holding its function's twin whose table agrees most around it (sibling classes share their base's first slots) |

Left to chance: code nothing maps is looked for by its first 24 bytes in
PM's code (unique hits only).

**`original.py` on PM's exe** (`MW3_EXE=pm`): every image address a test
gives `call`, `read`, `write`, `u32`, `stub_import`, `stub_function` and
`import_code` is translated; `cpu.uc` is a proxy that translates hook
ranges and memory addresses and gives hook callbacks MW3's addresses back;
an address the map lacks raises `Unmapped`. The helpers that read the exe
themselves (`twod_world.iat_slots`, `calltrace`'s ret decoding and memory
snapshot) read MW3's and let the map translate. `run_exit.py` runs a test
and leaves with `os._exit`, since a failing test's normal exit can hang
Unicorn's process at exit on this Mac (the stuck UE processes).

**Gate before mac5/pm merges (after MW3 v1.0):** the full MW3 differential
set must pass on this branch (`pm_survey.py --exe mw3`, every test). Step 0
touched the shared harness: `original.py` (Original's own `uc` renamed
`_uc`, `uc` the same emulator on MW3; a scratch page mapped at 0x2e080000),
`calltrace.py` and `twod_world.py` (paths taken only on PM's exe). On MW3,
checked so far: test_exe_select, test_hud, test_fade, test_twod_msgbox,
test_ztimer, test_action_lists, test_actions, test_actionscreen, all pass;
the full run (6 to 10 hours, one test at a time) is left for before the
merge (the lead, 2026-10-31).

**The survey** (`tests/differential/pm_survey.py`: each test once on PM's
exe, one at a time, classed by the MW3 functions it names; results in
`analysis-output/pm-diff/survey.json`):

| the test's functions | tests | pass | unmapped | emulator error | mismatch | other |
|---|---|---|---|---|---|---|
| all unchanged (identical or compiler only) | 13 | 5 | 2 | 5 | 0 | 1 |
| some changed | 62 | 0 | 20 | 33 | 4 | 5 |
| some gone | 118 | 0 | 88 | 21 | 4 | 5 (1 time limit) |

Passing on PM as they stand: `test_ai_lists`, `test_motion`, `test_spans`,
`test_twod_blit`, `test_ztimer` (and `test_exe_select`). So of the tests
whose functions PM left alone, 5 of 13 carry over now; the 8 others fail on
the map or on a test's own MW3 assumptions (a struct size, a vtable slot
offset it patches in a copy, a global the map placed by a block's delta),
not on a difference found. Where a test names a changed or gone function it
fails, as it should: 8 real mismatches, the rest stopped before comparing.

What blocks most: a "gone" function the test stubs or calls (PM inlined
it: 0x40cbf0 `set_hw_card_dev`, 0x576e70's wrapper, 0x58468e), globals
outside the map's blocks (0x4e8f34, 0x5031b0), and the tests' fixed struct
and vtable offsets. Each module's tests are made to run on PM when that
module is ported for PM (step 1 onwards), with the PM version beside the
MW3 one.

## Does PM need MW3? (Owen, 2026-11-02)

No. PM's exe looks for nothing of MW3's, and the PM disc alone makes a
complete install (game/pm). What the exe reaches outside its folder:

- The registry: `HKLM\SOFTWARE\MicroProse\MechWarrior 3 EP1\<version>`
  (its own key: `InstallOptions` 0x5b9330, the settings 0x4826e0), and
  `HKLM\Software\Zipper\MechWarrior3\<version>\RunDir` (0x4852f0), the
  developer's key both games carry (protection.md section 1's RunDir; no
  installer writes it).
- The disc check 0x4854e0: `data.tag` on any CD-ROM drive (scanner
  0x5967c0). MW3's disc has a `DATA.TAG` too (both are InstallShield
  discs), so the original took an MW3 disc for PM's. MW3's own check looks
  for `video\c1.avi`. Not ported (the trap list, protection.md).
- A file not found where named is looked for on the CD-ROM drives
  (0x5c9bb0 through 0x5968d0), the movies among them, as MW3's. PM's
  disc has no VIDEO folder; whether a movie name PM's install lacks could
  be found on an MW3 disc is unchecked (the port plays imported movies
  only, so it cannot).
- The search paths are relative to the run folder (`zbd`, and
  `..\data\...` from the developer's tree).

The port: mw3pm mounts its own install and its write dir only (and mods),
names stay inside them (`zutl::Mounts::confined`, path-guard), and none of
the ported PM readers names an MW3 path. The check to repeat once mw3pm
loads data (step 1's end): a run with only game/pm mounted reaches the
menus and a mission, and the log shows no file outside it.

## The data formats against mech3ax

mech3ax reads PM (`GameType::PM`). Checked against the PM install's files:

| Format | MW3 | PM | mech3ax | Checked here |
|---|---|---|---|---|
| zArchive | version 1 | version 2: count, then a checksum | `archive`: the checksum is the CRC (polynomial 0x04c11db7, MSB first, init 0, no xor: our `zutl_zar::crc32`) over every entry's data in order, for readers only; 0 for mechlib, motion and sounds; motion's entries store length 1 | all 54 v2 archives: 50 readers' CRCs match, the 4 others hold 0; motion.zbd's 364 entries have length 1 (so the exe finds a motion's end by reading it) |
| gamez.zbd | 27, 0x24 header | 41, 0x28 header (a time stamp at +8) | `gamez/pm`: `HeaderPmC` | the 4 worlds: signature 0x02971222, version 41, time stamps of 1999-11-12 |
| anim.zbd | 39 | 50 | `anim/pm` | the 4 worlds: version 50 |
| mechlib.zbd | 27 | 41 | `gamez/mechlib` `VERSION_PM` | version 41 (docs/pirates-moon.md) |
| models, nodes | MW | PM layouts | `gamez/model/pm`, `nodes/pm` (13 files) | to port from the exe, with mech3ax as the check, as for MW3 |
| interp.zbd, motions, textures | | the same (textures: a third field) | no PM variant | read by the port today |

## Shared types, forked logic

Shared types by field, fork only logic (the lead, 2026-11-02). The same
1999 source means shared code in ours. A struct that grew between the games
is a binary artefact, not a reason to fork: pmdiff's "compiler only, other
offsets" functions (1,102) are MW3's source compiled over larger structs,
and the port names its fields (offsets only in comments), so those
functions run unchanged on PM. So:

- One type per engine struct (zcls::Node, World, Light, Lod and the rest),
  holding the union of both games' fields by name; a field only one game
  has is commented "PM only, +0x..". A file's layout (PM's 16-bit counts)
  is the reader's business, not the struct's.
- Readers are per game: PM's fill the shared types from PM's files.
- A function is forked only where PM's logic changed, through the game
  table (zgame::Table), as the archive and the text reader are.
- Shared code that reads a struct by raw offset (`get<T>(0x..)`,
  `raw[0x..]`) couples it to one game's layout; those reads become named
  fields, module by module, with MW3's tests as the guard. The count on
  2026-11-02: 515 get/put sites on Node, World, Light and Lod, 78 direct
  `raw[]` reads, 163 uses of the int32 count accessors, mostly in zClass.

The route for PM's world files (the lead, 2026-11-02): PM's readers
translate the file into the shared types as they are, in MW3's layout:
the counts as MW3's int32 (+0x54, +0x5c), the box centre and diagonal at
+0x64/+0x70, World's rows, lights and sounds where MW3 keeps them. PM's
extra fields are real C++ members outside the raw block (`Node::pm_virtual`,
`pm_box`, `pm_model`; `World::pm_area2`, `Light::pm_colors`, `Lod::pm_tail`,
`Partition::pm_tail` and `pm_y`), so MW3's raw readers never meet them.
PM's ported functions use the shared accessors, never PM's raw offsets,
with a one-line comment where the offset differs. A node's model is
`model` (MW3's) or `pm_model` (PM's; `model` stays null): the shared code
reading `model` (73 places: gamez/zeffect/zeff_anim_events.cpp 11, gamez/zclass/cls_di.cpp 8, gamez/zeffect/zeff_anim_save.cpp 7, gamez/zclass/cls_node.cpp 7, gamez/zclass/camera.cpp 6, gamez/zinterp/zinterp.cpp 5, gamez/zgeometry/zgeo_model.cpp 5, mech3/zainet_inout.cpp 3, gamez/zclass/cls_mechlib.cpp 3, gamez/zmodel/gmod_cull.cpp 2, gamez/zclass/cls_zbd.cpp 2, gamez/zclass/class.cpp 2, pm/gamez/zclass/cls_zbd.cpp 1, mech3/veh_controls.cpp 1, mech3/veh_basic_object.cpp 1, mech3/mech_skin.cpp 1, mech3/mech_build.cpp 1, mech3/app_turret.cpp 1, mech3/app_mech.cpp 1, gamez/zmodel/gmod_draw.cpp 1, gamez/zeffect/zeff_anim_run.cpp 1, gamez/zdeclient/zdec_crater.cpp 1, gamez/zclass/cls_util.cpp 1, gamez/zclass/cls_node.h 1) must be routed before a PM world is
drawn or collided with, apart from the walk, the model box and the header (The
debug start, below). PM's loader says in the log how many nodes carry a PM model, so a PM
world never silently draws nothing.

See docs/games-layout.md.

## The debug start (2026-11-04)

`mw3pm --fly c1 [--pos X Y Z] [--look YAW PITCH] [--shot FILE.bmp]` draws one
PM world through the real path: `pm::zcls` read, textures, the shared walk
`zcls::render_world`, `zmod::DrawTable`, `Render3D`
(`src/frontend/pm_debug_world.cpp`). It is a debug flag; the program otherwise
still stops at "data not loadable yet". All four worlds draw.

The models go through 0x543b10's own draw and device (`draw_pm_model`; sections below). Stubbed:
the camera's far clip (the file's 800 is opened to 4,000; the game's own setter is not ported),
world.zrd, sky, clutter, the world class's update, the interpreter's commands.

What the walk needed from the shared code for PM's nodes
(`test_pm_walk.py` runs PM's world render 0x50a9e0 against the exe, draw by draw,
with LoD fields and a ranged light edited, since shipped data reaches neither):

- `node_model_header` goes through the game table, and `draw_model_of`, the
  hidden-surface test's "has lights" and the callers that ask "has it a model"
  read it. PM sets a morphing LoD's flag 8 and factor on its model just as MW3
  does (0x507f30, 0x4f4ab0).
- `node_model_box` (MW3 0x4ed220, PM 0x4fef40) goes through the table. PM's
  0x55b1c0 and 0x55b360 read the same header offsets as MW3's 0x53b4e0 and
  0x53b680, so PM's calls the shared `zmod::model_box`.
- `node_box_corners` (MW3 0x4ebdb0, PM 0x4fda40) reads PM's box through its
  pointer (`pm_box`, PM's +0x70). It read box 0, empty on PM's world nodes, so
  every sphere sat at the origin and whole cells went missing depending on where
  the origin stood against the view.
- PM's LoD (0x5c bytes) has three more words at +0x34; MW3's +0x34..+0x4f are at
  its +0x40. `translate_lod` keeps MW3's layout in `raw` and the three words,
  whose use is unknown, in `pm_tail`. Read as they were, every fade, the uniform
  scale flag and the morph were the wrong fields.
- The cells (0x509dd0 against 0x4f6720), by the table's data: a light's "reaches
  this cell" bit is 0x800 (0x200 is PM's static bit); its reach test takes off the
  wrapped camera offset ([0x8d43b0]); PM has no fades on a cell. Its +0x40 count
  is another list, entries 0x5c bytes: over 20 of them call 0x504450, which is
  not ported, and the debug draw 0x50a1e0 needs [0x60d968], which nothing sets.

Left for the world class:

- `node_bound_box` writes the box at +0x74 and `node_box` returns that address. PM's
  writer 0x4fe740 (MW3 0x4ecc00) instead points +0x70 at the box it chose, +0x74
  only for the union of two, so on a PM node the shared one leaves the box the cull
  reads stale. `node_box_xz` and two more reads of +0x74 in `class.cpp` have not met
  PM's nodes either.
- `shadow_model_morphed` reads `node->model` and so returns on a PM node, safely,
  without casting: to route in phase 4.

The rest of the walk needed no fork: PM's 0x509830, 0x5092a0 and 0x5050e0 are MW3's
with PM's offsets.

Seen on screen, not yet explained: polygons with flag 0x2000 are strips (drawn as
triangles here); a large unlit shape in each world's view from the middle (black
in c1, c3 and c4, tan in c2), which may be a dome or a sky piece the game moves;
in c4 a flat green area beside the grid texture, which may be a priority or
overlay matter this draw leaves out; c1's stair-stepped sand and red edges and a
jagged orange edge low in c2, which may be what the transparency and priority stubs
do. Each is to check under the real draw against an original screenshot or the
data, not by assuming.

## A porting order

Each step is a sitting or a few, as for MW3; the counts are the port's
functions PM changed, and PM's new ones.

0. **The test bench**: the address map and `original.py`'s translation
   above, so every later step is checked against PM's exe.
1. **The readers**: zArchive version 2 (the CRC check, motion's lengths);
   `zrdr_main.cpp`, the reader rewritten (122 new functions against 61 of
   MW3's `zreader.cpp`); gamez.zbd 41 and its node, material and model
   changes (zClass 166 changed, zModel 54); anim.zbd 50 (zEffect 28);
   mechlib 41. Nothing loads before these.
   - **zArchive 2: done** (2026-11-02, `src/pm/gamez/zutil/zutl_zar`, through
     PM's `zgame::Table`):
     - The table reader 0x597610 and writer 0x597570, the CRC of every entry
       0x597a30, its check 0x597a10 and the file check 0x597af0. The object
       is 0x1c bytes, with the checksum at +0x18.
     - The only check is a tamper trap (protection.md section 6), left out
       when PM's Intro state is ported.
     - Motion's lengths of 1: PM's exe reads a motion from its start to its
       end. The port's bounded reader takes PM's bound as up to the next
       entry's start (else the table's), mech3ax's backfill. MW3 keeps an
       entry's length. A broken PM motion is still refused.
     - Tests:
       - `test_pm_zar.py` on PM's exe: the 54 shipped archives, damaged
         copies, 200 random reads and 50 writes, and mech3ax's rules on
         every shipped file. 0 mismatches.
       - `mw3pmtests`: all 364 motions load; a cut one is refused.
     - Left: `mods.cpp`'s `--check-mod` and the mod loaders still bound an
       entry by its stored length. When PM takes mods, they go through
       `zutl_zar::span` too.
   - **The reader (`zrdr_main.cpp`): done** (2026-11-02,
     `src/pm/gamez/zreader/zrdr_main`; zreader.md, Pirate's Moon):
     - Rewritten as a C++ object over the same files. Every function MW3
       also has returns what MW3's does, so the port keeps zrdr's for
       them. New: three getters, a stack of 32 open files, and `#include`
       in text files (through `zgame::Table::zrdr_parse_text`).
     - Tests:
       - `test_pm_zreader.py` on PM's exe: all 1,052 entries of the 50
         reader archives, and every getter on 52,337 keys.
       - `test_pm_zreader_text.py` on PM's exe: 840 text files, 300 of
         them with `#include`.
       - `mw3pmtests`: `pm_text_files_take_include`, and
         `pm_include_stays_inside_the_mounts` (`..`, a full path and
         `sub/../..` read nothing; ours, for mods).
   - **gamez.zbd 41 (done: (c1) textures and materials, (c2) models,
     (c3) nodes and the entry)**: PM's world file loader stacks files.
     Every gamez.zbd read appends to the material, model and node pools
     (MW3's replaced them), so one world can be made of several files.
     - Entry 0x516bc0 (MW3 0x4ff520). It keeps a list of the loaded files
       (0x639c28, 0x2c each: the path, then the 0x28 header; count
       0x639c2c, the current one 0x60ea00, the open file 0x639c30).
       The texture directory is read only while none is loaded
       (0x531860 = its count 0x63e1e8), so the first file's stays. The
       material reader returns its base index, which goes to the model
       reader; the model base goes to the node reader 0x516e90.
       A failure pops the entry (0x517a70).
     - Open 0x5179e0: the 0x28 header, signature 0x02971222, version
       0x29.
     - Textures 0x531920 (MW3 0x518160): entries of 0x2c at 0x63e1ec
       (the image +0, +4, the surface +8, the name +0xc, the state +0x20,
       set to 2, the group +0x24, the mip +0x28, zeroed: no mip chain).
     - Materials 0x555e00 (MW3 0x5363d0): same 0x2c layout and flags.
       It appends at the current count (blocks 0x6114ec large); the
       file's free and used heads are taken on the first load only.
       Later loads relink each new material onto the used list (flag
       0x20 clear) or the free list. Then textures, colours and cycles
       are resolved in index order (MW3: the used list's order).
     - (c1) ported (2026-11-02): `pm/gamez/zimage/zimg_texture`
       (0x531920), `pm/gamez/zmodel/gmod_matl` (0x555e00),
       `pm/gamez/zclass/cls_zbd` (the file list, 0x516ab0, 0x516ae0). A
       later file's texture is its own directory's entry, read from the
       file and found or added by name (0x532450, zimg::add_texture). The
       name is passed from the entry's +0xc with no end of its own, so a
       20-letter one runs on into the state's bytes (kept).
       `test_pm_gamez.py` on PM's exe: the 4 worlds alone, 3 stacks, 4 copies
       cut in the materials and one with 600 materials (every shipped file
       has 1000), alone and stacked, in 5-6-5 and 5-5-5: 34 runs identical.
       7 break tests caught (the file's texture state kept, a mip chain, the
       header taken on every load, no relinking, the last block freed
       downwards, the table's textures for every file, a short read's bytes
       dropped).
     - Models 0x558ba0 (MW3 0x538e30): 0x68 each, appended the same
       way (the free list through +0x64). Each one goes through
       0x5587e0(f, model, material base).
     - (c2) ported (2026-11-02): `pm/gamez/zmodel/gmod_const` (0x558ba0,
       0x5587e0), PM's own types (`pm::zmod::Model`, `Polygon` with its
       material list and UVs per material, `MaterialRef`), blocks of 750
       (0x53fe30 sets the size; the pool starts empty). The reader checks
       no read; a later file's empty slots go on the free list; a slot
       without parents but not all zero is reported (0x606) and read.
       `test_pm_gamez.py`: the models of the 4 worlds, 2 stacks, 4 copies
       cut in the models and a crafted model (light colours with
       fractions, a polygon with two materials each with UVs), alone and
       stacked, both colour formats: 66 runs identical in all. 7 break
       tests caught (no material base, one UV array, a later file's empty
       slots unlinked, the refs' polygons not cumulative, the light
       colours not rounded, the header taken on every load, normals read
       without the flag).
     - **The models' form** (lead's decision (B), 2026-11-02): after each
       model the exe converts it for the renderer. Under Direct3D
       ([0x8c1520], 0x572ae0) a model without 0x80 goes to 0x570b70 (model,
       0x7fffffff, 0x7fffffff): about 1,960 instructions over MSVC
       std::map/set (0x573100..0x576ba0), building triangle strips (0x2000)
       of polygons with UVs and normals. In software, a model with 0x80
       goes back through 0x5726f0. The port keeps the file's form. It is
       exactly PM's under Direct3D for every shipped world: 0x570b70 acts
       only on a model of type 0 with polygons, and every shipped model
       without 0x80 that has polygons is a facade (type 1; 645 in the 4
       worlds), so it returns at once. `test_pm_strips.py` runs PM's own
       0x570b70 on all of them in the emulator (0 changed) and fails if a
       type-0 model ever reaches it. Its other callers run later, on
       models made or copied at run time (0x5587bf with a vertex limit of
       4, 0x510ae2 in cls_clutter's range): to check when those are ported
       (a type-0 model without 0x80 there would be stripped in PM).
     - What reads model polygons for play, in PM: the segment tests
       0x569290, 0x569b00, 0x56a600 (MW3 0x54e2b0, 0x54ea30, 0x54f450:
       the weapons' hits, sight lines, the ground and ray queries), which
       take strips (0x2000) and fans alike; the AI's in/out nets 0x4c9af0
       and 0x4cced0 (new in PM, ZAiNetMechInOut.cpp, "Net without
       color"); cls_clutter 0x512a80 and 0x511b50 (new); model_copy
       0x559340 (MW3 0x5396c0). The rest are drawing (gmod_draw, light,
       puffers, the software path 0x5685c0/0x5689d0). Since the shipped
       form is unchanged under Direct3D, none of them sees a difference.
     - The renderer: it must draw the file's form, as PM's own renderer
       does. That means polygons with a list of materials, UVs per
       material, strips (0x2000) where the file has them, and facades.
       MW3's model draw 0x53f2e0 (2,753 instructions) has no pair in PM:
       PM's draw is new code, to read in step 2. The options for later:
       (i) a PM path in the renderer through the game table; (ii) a
       load-time conversion, ours and outside the ported code, of PM's
       models into the polygons the renderer draws (one per material
       layer, the same vertices, UVs and colours). The lean is (ii), as
       the high-res model layer would use it too. It needs PM's draw
       code read first, for how a polygon's materials combine (blending,
       order).
     - Nodes 0x516e90 / 0x5171a0 (MW3 0x4ff200 / 0x4feaf0), (c3) in
       progress. The pool appends in blocks ([0x60dc68] a block; blocks
       0x6393c8, count 0x6393c0, capacity 0x6393c4); the last block's
       rest is zeroed and freed (0x50c9d0); the live count 0x6393cc is
       reset to 0 on every file (kept). Each node: +0x70, the box index
       (0, 1, 2), becomes a pointer to its box (+0x74, +0x8c, +0xa4);
       the model index plus the model base (0x557ba0); +0x48 cleared;
       0x5171a0(node, f, node base); live flag 0x3000000 in +0xd0. The
       worlds then copy their lights' and sounds' data pointers, as MW3.
       PM's node is its own layout (mech3ax `nodes/node/pm`): the
       parent and child counts are 16 bits each (+0x54, +0x56), the
       arrays at +0x58 and +0x5c, the box centre +0x60, the box index
       +0x70 (MW3: 32-bit counts at +0x54 and +0x5c, arrays at +0x58 and
       +0x60). So the port gets `pm::zcls::Node`, as the models got PM's
       own types. In 0x5171a0 every node index read is offset by the
       node base (cameras' 4 nodes, the world's light, sound and
       partition lists, 0x5178e0 and 0x517960 with 12-byte entries);
       a world partition is 0x58 bytes. Ghidra mangles its switch; the
       jump table (0x5178ac) gives: 0 empty (0x5177e0, the links only), 1
       camera (0x1e8; its 4 nodes; lists 6 and 8; clip 0x506ef0 +0xb0/+0xb4,
       fov 0x507010 +0xd8/+0xdc), 2 world (0xcc, MW3 0xbc: rows +0x9c of
       +0x98 at +0xa0, lights +0xb0/+0xb4/+0xb8, sounds +0xbc/+0xc0/+0xc4,
       +4..+0xc cleared; lists 6 and 13; the fog 0x505600.. and 0x504fb0
       as MW3's), 3 window (0xf8; lists 6, 14), 4 display (0x1c; lists 6,
       15; clear colour 0x50e3c0), 5 object3d (0x90; list 6), 6 lod (0x5c,
       MW3 0x50; list 6), 9 light (0x100, MW3 0xd0: nodes +0xf8/+0xfc;
       lists 6, 9), 10 sound ("Must complete software" reported, then 0x94
       read, +0/+4 cleared, nodes +0x8c/+0x90; lists 6, 10); 7, 8 and
       above 10 report (0x400, line 0x5a6) and give -1. The data classes
       give 1; then the parents (i16 +0x54, into +0x58) and the children
       (i16 +0x56, into +0x5c) through 0x5178e0, each making it 1. A read
       error reports and gives -1 (0x51781d). The same as MW3's
       0x4feaf0 but for the node base, the three sizes, the light's list,
       the 16-bit counts and the partitions' 12-byte entries.
     - (c3) ported (2026-11-02): `pm/gamez/zclass/cls_zbd` read_zbd
       (0x516bc0), open_zbd (0x5179e0), the drop (0x517a70), read_nodes
       (0x516e90), read_node (0x5171a0) and the node lists (0x5178e0,
       0x517960), into the shared types (Shared types, forked logic,
       above), calling MW3's setters (camera clip and fov, the fog, the
       clear colour, the lists, free_node: at the one call the slot is
       already zeroed, as PM's 0x50c9d0 zeroes it). PM sets and clears
       bits 24 and 25 of +0xd0 (MW3: 24); 25's use is unknown. The table's
       `zcls_read_zbd` (MW3 0x4ff520, PM 0x516bc0) takes the interpreter's
       GameZReadZBDFile. `test_pm_gamez.py` (nodes): the 4 worlds, a stack
       and 3 copies cut in the nodes, alone and stacked, both colour
       formats: 22 runs identical, every node, its data, partitions, links
       and the class lists (PM's changed camera setters give the same as
       MW3's on these). 8 break tests caught (no node base in the
       partitions or the links, the counts as int32, the world's tail
       unshifted, the light's colours not skipped, 4-byte partition
       entries, bit 25 not cleared, no model base); bit 25 not set cannot
       show (the files carry it). `mw3pmtests`
       `pm_worlds_stack_through_the_table`: c1 then c2 through the table,
       c2's links and models in c2's part of the pools, versions 27 and 40
       and a bad signature refused with nothing changed (break: version 27
       accepted, caught).
   - **anim.zbd 50: done** (2026-11-03, `pm/gamez/zeffect/zeff_anim_init`,
     the table's `zeff_anim_read_zbd`: MW3 0x50a0e0, PM 0x523140). PM's
     file is read into the shared AnimDef and AnimState in MW3's layout,
     PM's own fields named beside it (`pm_path`, `pm_reset`, `pm_seq2`,
     `pm_block`, `pm_scripts`, the objects' and nodes' `pm_flags` and
     `pm_parent`):
     - The header: signature 0x08170616, version 50, a time stamp, the
       source count (0x54 each, read and dropped). Then a 0x6c block whose
       fields are MW3's block moved (`kFields` in the reader, matched by the
       run-time globals' users).
     - A def is 0x10c bytes (MW3 0x13c). Its node is found by name or by
       a path of names, one under the other (+0x20, count +0x24; none
       shipped). The reset sequence is optional (pointer +0xd0), and one
       more optional sequence follows it (+0xd4; none shipped). The objects
       (0x5c) and nodes (0x2c) carry a parent entry: found under that
       entry's node, else as MW3's lookup.
     - Where MW3 stops the read, PM goes on. A def whose node is missing
       is marked 5, and every node found from it on is cleared (0x524230).
       A puffer that cannot be made leaves the def without puffers.
       Node prerequisites chain children first to last (MW3: last to
       first). The prerequisite FIX (bug)s are MW3's (fixes.md: PM's
       0x376f report drops them all too).
     - PM's scripts (0x1c each, their name, object and data) come after
       the defs, then the messages as MW3's.
     - Through the table as well: PM's node alloc (0x4fc550), active
       switch (0x4fca50) and light (0x50e470, 0x100 bytes).
     - `test_pm_anim.py` on PM's exe: the 4 worlds' anim.zbd against
       their worlds identical (1,601 unported calls traced, every def,
       list, sequence, script and the node pool after); 29 mismatched,
       failing, crafted (a path, parents, a second sequence, a missing
       node and root, a reset left out, one file pointer over a path or
       after a missing node, an anim ref's pointer set) and cut runs
       agree; a cut in the scripts' names refused (the exe reads on
       unchecked). 14 break tests caught (the objects' parent at their
       flags, the nodes' record unshifted, the reset and the second
       sequence not read, the lookup without the path, a missing node's
       clear left out, a missing node or a puffer stopping the read as
       MW3's does, the prerequisite chain cleared, a block field from the
       wrong place, the anim refs' pointer kept, the parent ignored, a
       light unnamed, the lookup sharing from pointer 0).
   - **mechlib 41: done** (2026-11-03, `pm/gamez/zclass/cls_mechlib`, the
     table's `zcls_mechlib_open` and `zcls_mechlib_load`: MW3 0x4fc6d0 and
     0x4fc8f0, PM 0x510910 and 0x510b90). The library object, the
     materials' read (0x510c80), find, close, free and the material
     resolve (0x510770) are MW3's unchanged and stay shared:
     - The open is MW3's with version 41 ([0x60e61c]).
     - A tree (0x5110c0, 0x511210) is read as the world's nodes: the file's
       0xd0 bytes over a new object3d (class 5) or LOD (6, data 0x5c; 2 in
       the file), keeping the new node's data, flag bits 0-1 and list-7
       item (+0xcc); the box index at +0x70 made a pointer. Any other class
       makes the exe dereference null.
     - A model (0x511310, when +0x3c is set) comes from PM's pool
       (0x558f50) in PM's form: vertices, normals and morphs, then
       0x28-byte polygons, each with its vertex indices, normals (flag
       0x1000 and +0xc), its material indices, UVs per material, colours
       only when +0x1c is set. No lights are read; the file's refs are
       read and dropped.
     - The resolve (0x510fd0) makes each index a world material, then sorts
       the model by its polygons' first material (0x55c470, new in PM): a
       cycled material's polygons one ref each, first, then one ref per
       other material in the order first used; one ref keeps the polygons
       in place.
     - Left out: the trace lines to stderr (0x511640) and the renderer
       conversion after the load (0x510ab0 to strips under Direct3D, else
       0x510b00), as for the world's models.
     - Ours, as MW3's: a tree read with its entry's end; a count past it, a
       node of another class, a depth over 64, a polygon with no material,
       or an index past its array, a vertex, normal or library material,
       is refused with a line, nothing added.
     - Shared: the add-child's class 6 (LOD) is the plain link in both exes
       (MW3 0x4eb9b0 through a thunk, PM 0x515bb0); the port had stopped
       there.
     - `test_pm_mechlib.py` on PM's exe: all 76 trees over c1, 23 over c3
       (repeats found, not read), 10 with no world, MW3's file refused, and
       a crafted file over c1 and with no world (a cycled material, a
       polygon with two materials and their UVs, no colours, normals, a LOD
       with a model below it, a model without flag 0x80, box index 3): 6
       runs identical, the library's materials and trees, the texture and
       material tables, every model and node slot. With no world the exe
       adds a cycled library material by copying its cycle from null
       (0x5568f5; the library's have none), so that run's file clears the
       flag.
     - `pm_mechlib_reads_the_shipped_trees_whole` (76 trees, 2 LODs) and
       `pm_mechlib_fuzz_never_crashes` (180 cut, flipped or garbled trees:
       108 refused, 72 read, every cut one refused; what a refused tree
       leaves in the pool is checked whole).
     - The fuzz found the bounded reader's one fault: a broken material count
       drove its UV arrays past 5 GB in 4 s, and a tree marked broken went on
       being read. Now the first break stops it and its model has no counts
       (ours, bounded only; the exe's path is unchanged and still identical).
     - Breaks: 16 caught of 18. MW3's version, the child count at MW3's
       +0x5c, the box pointer, the LOD, colours always, UVs once, refs not
       skipped, normals without +0xc (it took a polygon with flag 0x1000 and
       +0xc 0), the second material, flag bits 0-1, the parent, no sort,
       cycled grouped as others; the fuzz catches the reader's stop before a
       broken count's UVs (over 2 GB) and after a broken polygon. Refs not
       skipped and the parent crash the probe's dump, which counts as caught.
       The alloc's flags 3 by `pm_model_alloc_starts_with_flags_3`: the only
       ported caller replaces the header, so the mechlib test cannot see them;
       `0x558f50` has others (`0x4cefc0`, `0x528120`, `0x528920`, `0x558440`,
       `0x559340`, `0x5b2d10`), to check as they are ported (`0x559340`
       overwrites bits 0-4 from its argument). Not caught, each with its reason:
       the one-ref shortcut (the regroup gives the same order), allocating
       before the check (calloc is lazy on the Mac: no memory seen; kept, as
       MW3's reader does).
     - A model with no polygons and a vertex, normal or morph count past the end
       is refused with no counts left (`pm_mechlib_refused_model_keeps_no_counts`;
       found in review: the check came only inside the polygons' block).
2. **The world**: the light maps and lighting (`gmod_matl.c`; PM drops MW3's
   fog; mapped below), `cls_clutter.cpp` (32 new), the world class
   (`cls_world.c` 20 new), the script interpreter's new commands (the
   dispatcher gained 97 calls).
   - **`gmod_matl.c` mapped** (2026-11-03, from the pseudocode and the asm; no
     port yet). Light maps are a small part of it. Of the file's 113
     functions by the function map, 42 have no MW3 twin (the table above's 39
     counts differently). Sorted by what calls them; the exe's data and code
     searched for pointers too, and no table reaches any of them:
     - **The Direct3D model draw: 18 new, 20 with the shared.** `0x543b10`
       (2,006 lines) does the job of MW3's `0x53f2e0` and has no byte twin.
       `0x53fe30` puts the software draw in the slot `[0xa4e100]`; `0x5400e0`
       replaces it with `0x543b10` when `[0x8c4b70]` is set. That is the
       Direct3D flag: its setter `0x5981d0` is MW3's `0x579210`, which sets
       `[0x72f348]` (render-matrix.md). Its new helpers: the near-plane
       clippers `0x549440`, `0x549630`, `0x549ab0`, `0x54a450`, `0x54a6f0`,
       `0x54aa50` (the shape of render-clip3d.md's six), `0x552e50` through
       six one-line wrappers (`0x54ade0`..`0x54ae80`), and `0x542a70`,
       `0x5439e0`, `0x54faa0`, `0x54fc60` (not read yet). To port.
     - **The software draw: 8 new, out** (the software path is not ported,
       CLAUDE.md). `0x541a40` is the twin of MW3's `0x53e050`, and only it
       reaches these. No pointer to any of them is in the exe's data or code,
       and no other caller. Each one against the rule that what only software
       showed comes to the GPU:
       - `0x548f50`, `0x5490e0`, `0x549290`, `0x549f30`: near-plane clippers
         of software's vertex buffers. Show nothing of their own; the Direct3D
         ones above do the job.
       - `0x54a3b0`, `0x54a3d0`, `0x54a3f0`: one-line wrappers of `0x5502d0`.
         Nothing of their own.
       - `0x5502d0`: the side-plane clipper, `0x552e50`'s software twin (1,161
         lines to 1,134, no calls out, 26 globals shared; the others are each
         one's vertex buffers). Compared by shape, not line by line. Nothing
         flagged.
       - `[0x6112e4]`, read only by `0x541a40`: not a software feature. The
         graphics options' detail level (`0x40b120`, MW3's `0x40d310`) clears
         it at the lowest level, and the software draw then skips work. It
         hides; it adds nothing to bring over.
       None is flagged. **Open until the draw `0x543b10` is ported:** `0x5502d0`
       and `0x541a40` are good evidence, not proof. `0x541a40`'s own 622 lines
       were read only where these globals are used. Compare both with what the
       Direct3D draw does then, since what only software showed comes to the
       GPU. Two more new functions are reached from both draws, so they stay in.
     - **The other 14.**
       - Setters and start-up, ported with the helpers that first read them:
         `0x5404f0` (`[0xa4e038]`, read by both draws), `0x540740`, `0x556420`
         (resets the draw state from `0x5400e0`). `0x541a30`: the setter of
         `[0x6112e4]` above; a stored value only, kept for `0x40b120`.
       - The loader's: `0x557be0` (the model pool's growth, done), `0x557ba0`
         (index to model, done), `0x557b30` (model to index, not ported; with
         its caller `0x515fb0`).
       - `0x546d50`, `0x5470a0`, `0x5473f0`, `0x547740`: the view-plane test
         `0x546a00` (MW3's `0x541760`) in other plane orders, swapped into
         `PTR_FUN_00611354`, which the scene walk calls (`0x5079a0`..). Not the
         light map's; they go with the world class.
       - `0x5415b0`, `0x543010`, `0x5432b0`: no caller, no pointer in code or
         data. Dead; not ported.
     - **The light map itself.** `[0x683cb0]` holds one texture. The script's
       `LightMapTexture` (`0x5b2d10`) sets it through `0x532450`, and
       `0x530810` clears it. The draw adds it to a polygon as one more shadow
       set, of kind 2 (the set with xyz): set's texture from the entry's +8.
       `0x4aefe0` changes the video mode (`0x4399f0`, `0x483700`, `0x484ed0`..):
       it keeps the texture's name, frees the texture, clears `[0x683cb0]` and
       finds it again after, as a mode change loses surfaces. `0x5995a0` (zvid_dd)
       marks that texture's surface as it is made. The draw uses the light map
       only when it is set, the option's `[0x8ccc64]` is not -1, and the device
       can: `0x59b8a0` reads the last fields of the D3D device description at
       `0xb0a074` (size `0xfc`), `[0x8ccc68]` being `wMaxSimultaneousTextures` and
       `[0x8ccc6c]` `wMaxTextureBlendStages`, both 0 unless `dwTextureOpCaps` has
       MODULATE, and both needing at least 2. The GPU path always has two
       textures to give, so that gate becomes true there.
     - **The option.** `LIGHT MAPS` is a graphics option: `0x43a060` offers
       `LIGHT MAPS` (3), `LIGHT MAPS A` (1), `LIGHT MAPS B` (2). `0x40b270`
       keeps a settings value and sets `[0x8ccc64]` from its bits 17 and 18:
       neither 0, bit 17 alone 1, bit 18 alone 2, both -1 (the draw's "no light
       map"); it leaves `[0x8ccc64]` alone when the value is 0. Bit 4 turns
       the `sunlight` node on or off.
     - **Shipped use.** Scripts and text data are stored plain, so a raw search
       of every installed file covers them: `interp.zbd` has `LightMapTexture`
       4 times, all naming `lightmap`. `LightSetLightMap`, `LightSetBicolored`,
       `LightSetAmbientColor`, `LightSetDiffuseColor` and
       `NodeSetOverrideAmbient/Diffuse` are in the exe alone: they wait for
       the interpreter's item.
     - **`LightMapTexture`: ported** (2026-11-03). The word through the shared
       dispatcher behind the game table's `zint_light_map_texture`, null in
       MW3's, so there it stays an unknown word; `pm/gamez/zimage/zimg_texture`
       holds `[0x683cb0]` and PM's free `0x530810`. It matches the whole word,
       unlike most commands (`LightMapTextureX` is a miss). With no argument
       it calls the add with null, whose real `0x532450` would crash on
       `strrchr`; the test's stand-in hides that, and no shipped line does it.
       `test_pm_light_map.py` on PM's exe: 8 lines identical (the light map,
       the cursor, the misses, the names). Breaks, 5 of 5: prefix match, not
       stored, argument not taken (the differential); MW3 knowing the word,
       the free keeping it (`pm_light_map_texture_is_pms_alone`).
     - **The near clippers: ported** (2026-11-03, `pm/gamez/zmodel/gmod_clip3d`):
       `0x549440`, `0x549630`, `0x549ab0`, `0x54a450`, `0x54a6f0`, `0x54aa50`
       with their carriers `0x54fea0` and `0x550100`, one loop and a mask of
       what each carries. The carriers' bits 1, 2 and bare 8 are the software
       clippers', not ported. Against MW3's six (render-clip3d.md): cdecl, not
       fastcall; no far reject and no plane mask, as the draw only calls them
       for a polygon its cull marked; a polygon that grew moves to arenas
       (`[0xa4cfac]`..), not fixed buffers; and **the point light is
       interpolated at a cut**, MW3's copied from the edge's first vertex.
       FIX (bug): the exe's scratch and shadow sets hold 64 vertices, unchecked,
       while PM's polygons allow 1,023 and shipped ones reach 313 (139 over 32 in
       c1, c3 and c4; mechlib's reach 52). The scratch holds the most a cut can
       make, 1.5n, and the shadow sets grow. The near
       value `[0x8d4b14]` comes from the view (`0x548990`, MW3's `0x526e10`, the
       shared `near_z`): it and the arenas' start (`0x543b10`) go with the draw.
       `test_pm_clip3d.py` on PM's exe at 24-bit x87: 9,000 polygons by default,
       every buffer, the result, the count and what moved, up to 64 vertices;
       past that the port alone (`pm_clip3d_keeps_the_largest_polygon_whole`).
       NaNs may differ in
       sign, and cases with extreme inputs in value (the x87 keeps a 15-bit
       exponent in its registers), as MW3's test allows. Breaks, 19 of 20. Not
       caught: a set without xyz carrying xyz at a cut, which only reaches the
       scratch (write-back checks the kind), so nothing can see it.
     - **The plane tests and vertex colours: ported** (2026-11-03,
       `pm/gamez/zmodel/gmod_cull`): `0x54faa0` (each vertex's plane code),
       `0x54fc60` (a polygon's), `0x5439e0` (each TL vertex's colour, with the
       fade's alpha), `0x5403d0` (the fade). Plane 8 never rejects a polygon:
       its count is not reset and runs on from before (kept). The colours use
       the 1.5 * 2^52 trick: as a 53-bit FPU gives it with `fpu53` (play), and
       the 24-bit result without, where the sum keeps multiples of 2^29, so it
       is 0 unless a value is huge. `test_pm_cull.py` on PM's exe: 4,000 of each,
       the colours at both precisions. Breaks, 16 of 16.
     - **The side clipper: ported** (2026-11-03, `gmod_clip3d`, `clip2d`):
       `0x552e50` with its wrappers `0x54ade0`..`0x54ae80`, which differ only by
       the mask (`0x54ae60` and `0x54ae80` are the same). Its polygon's codes
       (+0x34) pick the passes, left, right, top, bottom, a pass reading one
       scratch set and writing the other. Values go perspective-correct by rhw.
       A NaN is outside left and top, inside right and bottom. A set's overrun
       is the near clippers' FIX (bug): room for four passes at 1.5n.
       `test_pm_clip2d.py` on PM's exe at 24-bit x87: 7,500 polygons. As well as
       the near test's tolerances, a case with extreme inputs may end with
       another vertex count (2 in 7,500). Past 64 vertices the port alone,
       `pm_clip2d_keeps_large_polygons_whole`. Breaks, 22 of 22.
     - **Do polygons past 64 reach the clippers?** No. All 27 past 64 in the four worlds' models
       (c1 8 nodes of 313, 198 and 113 corners, `g5351`..; c2 6 of 189, `g2012`..; c3 6 of 231,
       `g5380`..; c4 5 of 104, `g6114`..; plain polygons, flat at y = 0, each under `inout`/`inoutmN`)
       are the `inout` boundary areas, which PM deletes at mission load as MW3 does (`0x4cddb0`,
       MW3's `0x4c3930`, render.md 6a): never drawn. Outside `inout` the largest is 64, and all 19
       of 49 to 64 corners are strips (`g3860`, c1), which `split_strips` makes triangles before a
       clipper sees them when they need clipping, and which otherwise draw whole, unclipped.
       The largest plain polygon that draws has at most 48. Corrected 2026-11-04: an earlier note
       here feared a 64-corner polygon near-cut to 65. On the exe's clippers in the emulator a
       65th vertex does fault (the two textured paths, a pointer overwritten at 0x6887d0; the other
       four write past the scratch silently), but no shipped data reaches them with one. The fix
       stays as a guard.
     - **For the zones (the lead, from W58, not on main yet):** in MW3 an AI
       mech's ski and collision queries honour the zones in the camera's view,
       so a lancemate in another zone loses the floor under him (0x476a30, zone
       mode on from 0x49e82b; 0x4161f6 and 0x4c33ff turn it off). Fixed in Skis
       and `Mech::collide_query` behind bugs. Check PM's twins of those two calls
       when PM's zones are reached, and tell the lead if PM differs.
     - **The draw, piece 1: the type dispatch ported** (2026-11-03,
       `pm/gamez/zmodel/gmod_draw`): `0x542a70` and `0x5416e0` (the facade's centre,
       MW3's `0x53dcf0` on PM's polygons). Type 0 the model-view, 1 a facade, 2
       points, drawn here to `zmod::DrawTable`'s point, the same seam as MW3's.
       The facade's turn is MW3's own, split out as `zmod::facade_turn`. Counted on
       all four worlds and mechlib: types 0 and 1 only, no points, no flag 0x40 or
       flag 8. Facade modes 0 and 1 in the worlds; **mechlib ships a mode 2**,
       `g4` in `headlight.flt`, the headlight's beam. Mode 3 ships nowhere. Modes 2
       and 3 (`0x538e40`, `0x539200`; MW3's `0x51f390`, `0x51f750`) are one shared
       version in `facade_turn`, which MW3 gains too with no effect on its data:
       its callees are twins, and PM's quaternion product `0x53ef90` differs from
       MW3's `0x5255d0` only in the order of its sums. FIX (bug): points with
       no vertices or no polygons draw nothing (the exe loops 2^32 times, or reads
       through null; `pm_points_without_polygons`). `test_pm_modeldraw.py`, importing
       `modeldraw_common.py`, on PM's exe with c1 and c3: 3,000 draws, the result,
       the default flag, the stack's top and the one below, the points and the
       camera-space vertices, editing the data for what shipped data lacks. Points
       past a shorter morph list are the last draw's, on both sides, so the test
       morphs them all. The stack's depth after the call is compared, since type 2
       pops the draw's push. The centre is compared bit for bit, and about 93% of
       the matrix floats are bit-identical. One exception: a sphere facade whose
       two vectors are nearly opposite, where the turn comes from their small sum
       and its rounding grows to 2^-23 / |sum|; the test allows exactly that and
       lists the cases (2 in 3,000, allowing 0.004 and 0.009, worst seen 0.0054).
       Breaks, 14 of 14. Emulator only.
     - **Phase 3: the draw up to its first lighting call** (`begin_model`,
       `build_records`, `lights_reaching`; `test_pm_records.py`). The exe's draw runs
       whole with a hook at the end of each ref's build (0x54471c), which compares
       what it built and skips the rest. The needs (fog from `[0xa4e038]`, where 2
       fogs every model, not only flagged ones; lights; tint), camera space with
       morphs, plane codes, normals, the gates; per polygon the back-face test, its
       flip with flag 0x400, polygon codes, fog factors and flags 8, 0x20, an
       all-fog polygon dropped, UVs, colours, normals, the extra UV sets, strips. PM's
       normal is (p2 - p1) x (p0 - p1), MW3's the other way, each with its own test.
       Not reached by this test: the lights' part of the needs, the bake's call, the
       model's own lights (0x547dd0). They come with phase 4.
       Shipped counts, for the arenas: largest model 500 vertices (mechlib; 351 in a
       world), 64 morphs, 420 polygons, 120 polygons in a ref, no polygon with extra
       materials (all four worlds and mechlib's 76 trees).
     - **Phase 4a: the polygon lighting** (`lights_reaching` 0x55ebf0, `sort_lights`
       0x5602e0, `shade_polygon` 0x5603f0, `light_records`; `test_pm_lighting.py`).
       Lights are made in the test, every kind and flag, with clouds and the bake's
       results. PM's lights have their own colours (flag 0x400, PM +0xb0..+0xdf), 0x200
       means already in the bake, 0x20 means the light is fog rather than colour.
       Left: the projected lights' test 0x562620.
     - **Phase 4b: the model's lights** (`draw_model_lights`, 0x547dd0;
       `test_pm_model_lights.py`). No shipped world model has any, so they are made in
       the test. The exe batches its opaque points; the port draws each as it comes.
       The test compares order within the points only. **Eye check, phase 6:** points
       meeting blended polygons, since draw order could change the result. The flare
       hook (0x588910) is still to wire to `flare_add`, and that mapping is unchecked.
     - **Phase 4c: the bake** (`bake_static_light`, 0x55fb30; `test_pm_bake.py`, and
       `test_pm_lighting.py` with the real bake feeding it). As MW3's: a spot's first
       vertex reads the light-to-vertex vector left from the last one, which the exe
       has on its stack, and below near^2 the range is tested with the squared distance.
       **Known gap:** with vertex shading off the exe's bake reads the last draw's
       normals; that path is not tested with the real bake.
     - **Phases 4d and 4e: strips and fans into triangles, the sets** (`split_strips`,
       `split_fans`, `shadow_sets`, `light_map_sets`; `test_pm_sets.py`, to the end of
       the sets at 0x54600e). Casters and lights are made in the test, up to all seven
       sets on a polygon. PM keeps a caster's texture at +0x64, MW3 at +0x60: the
       caster's own port must place it. A light map set: UVs in the square across the
       light's range, colour the light's PM +0xd4 by the vertex colour, fade at
       +0x153c from (far - d) * 255. The fade is the 1.5 * 2^52 rounding, so at 24 bits
       it is only ever 0 or full; c3 runs at 53 bits to reach the values between.
       `light_map_sets` goes through `fpu53`, so the shipped default has the real fade.
       **Open, for Owen's PC:** what the original shows, at the runtime's precision, and
       on the three-stage path, which reads as losing the fade.
       **Traced, mirrored UVs at 53 bits:** a polygon whose camera-space normal has x
       exactly 0 takes 0x5381c0's first branch, axis (1, 0, 0) at full length. With x
       tiny, the other branch gives (y, -x, 0) / approx_sqrt(x^2 + y^2): for y < 0 that
       is about (-1, 0, 0), and its approximate root scales it. In the case traced
       the exe had x = -3.03e-9 and the port, in floats, 0: u and v negated, length
       0.946. So the map turns 180 degrees and changes size by that factor. The shipped
       map is a round soft spot: the turn alone moves it by at most 9/255 (mean 2), with
       the size at most 33/255 (mean 11), in its soft ring.
       **Decided (the lead, 2026-11-04):** the port stays; it matches the exe at 24 bits
       and the scale is the original's own approximate root, which already leaves such
       steps between polygons. `test_pm_sets.py` compares these UVs at 53 bits and allows
       only this case, exe x under 1e-7 and ours exactly 0, with the exe's UVs ours turned
       and scaled by its own factor; it prints how many. **Eye check, phase 6:** an
       axis-aligned wall under a yaw-aligned camera with a light map on it, for a seam
       between neighbouring polygons: random cameras say nothing about how often in play.
       The test maps a zero page, for a strip's missing normal and the bench's null
       texture images. Every null read on the exe's side then succeeds, so a passing
       run does not show those two are the only null reads in this path.
       The draw's fade and shadow flag are the shared `DrawState`'s, as the walk
       sets them; `cull_state().fade` must follow it before `tl_colours` (phase 6).
     - **Phase 4f: the projected lights** (`gmod_projected`, 0x562620, 0x562840, 0x561fb0,
       0x562120; `test_pm_projected.py`, on records made in the test). MW3's 0x547740
       rewritten: the split's threshold is a fifth of the nearest light's far squared, at
       least 30; PM's own colours (flag 0x400); light map colours carried. A slip: the
       test on a set's kind starts one set late, so a first set that is a light map keeps
       old colours at a polygon's centre and splits (FIX (bug), behind `zfix bugs`; the
       probe keeps the original's path). Left, for the casters' port: `shadow_model` and
       `shadow_model_morphed` read `node->model`, so a PM node casts no shadow image and
       takes no morph there; safe, and to route with PM's caster (texture at +0x64).
       **PM draws no projected shadows until the caster's port:** do not read their
       absence as a phase 6 bug in the eye check.
       **Traced, a tie between edges:** a split takes the triangle's longest edge. A
       midpoint split leaves two edges equal in exact arithmetic; rounding makes one
       longer, not always the same one here and in the exe (968.056571 against 968.056552
       in the exe, 968.056335 against 968.056396 here, in the case traced at polygon 3021).
       The split falls on the other edge: another triangulation of the same polygon. On
       7 in 30,000 random polygons, all at 53 bits since the sums and factors follow
       PM's order: mean difference at most 0.95 of 255, worst 38.9, on 6.3% of an equilateral
       triangle (a three-way tie) under a short light whose pool is sharp. The original can
       flip the same way as the camera moves. `test_pm_projected.py` allows only a
       near tie the exe's own split saw, and only if the picture agrees; it prints how
       many. **Eye check, phase 6:** a projected light over a light-mapped polygon, bugs
       on and off (the first set's colours), and any shimmer at a short light's pool.
     - **Phases 5 and 6, to the device calls** (`draw_model`, `clip_near`, `project_records`,
       `clip_sides`, `fill_colours`, `finish_tl`, `submit`; `test_pm_submit.py`). The whole of
       0x543b10 runs against its device stubs: every call in order, kind, count, cull, primitive,
       shade and overlay, its TL vertices and its sets. Polygons drawn apart (a fade, an overlay,
       a strip, an alpha texture, sets) call one at a time; the rest of a ref go together as one
       indexed triangle list, always primitive 4 (0x5a0ea0, 0x5a0f30 push it themselves, and
       ignore 0x8c4c40). The device kinds take different arguments: flat (colour, alpha, count,
       overlay), lit (alpha, count, overlay), textured kinds (count, texture, fade, overlay).
       A flat polygon's u, v are whatever its arena held, and no texture reads them: not compared.
       Whole draws run under projected lights at 24 bits, on models under 120 units with at most 16
       corners (the split's 2,048 triangles); at 53 a tie falls either way (`test_pm_projected.py`).
       **Facing, bit for bit:** at the exe's own test, its inputs go to the port's `face`, and the
       sum and normal must match exactly at both precisions. It found the sum's order wrong in the
       port ((z+y)+x; the exe (x+y)+z), which a polygon-level compare hid, since it flips a polygon
       only on a tie. The exe stores e0x and e0y; the rest stay on the x87's stack, so double at 53.
       **Large polygons.** Every drawn node is compared, with every plane mask, in all four worlds
       (c1, c2 at 24 bits, c3, c4 at 53); only the 27 `inout` polygons past 64 corners are left out,
       which never draw (below). The 49 to 64 ones are strips, so nothing clips them whole.
       Two allowances, both at 53 bits only, both counted in the test's last line. **A flat mesh
       edge-on:** with the camera in its plane, facing is the rounding of a sum near 0 and the
       polygons passing differ (the exe's sums are double, ours float); allowed only when every
       triangle of both call lists covers under 0.05 square pixels. **A side cut:** a vertex a
       screen side made, on an edge reaching far off screen; the exe interpolates in double and
       we in float, under 1 pixel (largest seen 0.293). Counted only when the plain compare refuses
       the vertex. At 24 bits neither fires. Not chased (the lead, 2026-11-04): play-accurate, and
       the port's float is the same step at 24 bits. One exception at either precision: **a near
       cut under sets**, 0x54a6f0, leaves its fog to the arena (fixes.md), so the fog word of a
       polygon with a corner on the near plane is not compared there.
     - **The device side** (`pm/gamez/zvideo/zvid_tl`, the shared flushes; `test_pm_device.py`).
       The exe's 0x59d450, 0x59de30, 0x59e500, 0x59e830, 0x59eb60, 0x5a0060, 0x5a0390, 0x5a0f30,
       0x5a0ea0 and flushes 0x59f550, 0x59f9e0 run against a Direct3D stub that keeps the state
       its calls leave; every draw is compared with what Render3D gets, in order: primitive,
       state, vertex bytes, and each fog colour. They are MW3's draws with the vertices ready,
       and these differences:
       - **A queue entry carries more** (0x24 bytes to MW3's 0x10): the fog colour when it was
         queued, restored at the flush before it draws if it changed (0x59f78c), and an **additive**
         flag, count's bit 31, from the image's +0x0c bit 2 (0x59e7c8), which makes the flush send
         DESTBLEND ONE for that entry (0x59f76d: `(~count >> 31) << 2 | 2`) and 6 again after.
         No texture in the four worlds has that bit at load (0 of 437, 396, 362, 400); mech and
         interface textures not counted, and nothing found that sets it at run time. So the
         additive path is checked against the exe but is not in the eye-check shots.
       - **Limits:** overlay 3,000 polygons to MW3's 384 (`OverlayList::limit`, set by PM's
         start); queue 3,000 and 12,000 vertices to MW3's 6,000 and 24,000 (the port keeps the
         larger: more room, no report).
       - **Lit untextured below full alpha draws nothing** (0x59de38 `jl` to the return), where
         MW3's 0x57c700 queued it: fixes.md, candidates.
       - **Sets:** the polygon, then each set with its own u, v over the shared TL; kind 1 multiplied
         (ZERO, SRCCOLOR, clamped, SELECTARG1, no fog), kind 0 blended (wrap, MODULATE, fog). Kind 2,
         the light map, comes with its shader and is skipped until then.
       - **Strips** draw with CULLMODE CW (0x59b870) and only they do. FIX (A): culled here, per
         triangle, and handed over as a list, so no backend's winding can decide it. The stub cannot
         say which way CW cuts (the test uses the same formula as the port); that is by eye: the
         other sign loses most of the terrain (`c1`).
       Render3D gained `additive` (pipeline state: one more bit in the key, so no existing draw
       changes pipeline; no shader changed) and `Primitive::kTriangles`. MW3 is untouched:
       `--fly c1` and `c3` shots byte-identical before and after, `test_drawtable`, `test_shadows`,
       `test_shadow_image`, `test_puffers` pass. Overlays over 64 corners go on as fans (FIX bug;
       the exe overruns); not compared. Points still take MW3's `draw_point`; PM's 0x59ee90 batches
       them (0x59f8a0): not ported yet, an eye-check item. `draw_unlit` is gone.
       **The view: who sets it (closed 2026-11-04).** PM's `0x548990` and `0x53b730` read as MW3's
       `0x526e10` and `0x521c60`, field for field: camera +0xb0 near, +0xb4 far, the window's origin
       and size (or the screen's), `0x53b730(x, y, w/2, h/2, +0x1d4, +0x1d8, near, far)`; cx, cy =
       origin + half; fx, fy = cot * half / near; the slopes [0x8d43a4], [0x8d43a8] are the two cots;
       under Direct3D the clip rect is x, y, x + w + 0.001, y + h + 0.001 ([0x5e2b60] is +0.001), the
       points' rect x + w - 0.001. So the shared setter (`gmod_init`, camera +0x1d4 at the same
       place) is PM's, and `draw_pm_model` copies its rect rather than working one out. The debug
       start's stubs do not feed it but for the far clip's value.
       **Which way CW cuts.** 0x5467a6 pushes 2 to 0x59b870, SetRenderState(22, ..): CULLMODE
       D3DCULL_CW, which drops triangles running clockwise on screen, screen y running down (a
       positive cross product); a strip's odd triangles count reversed. `draw` here keeps the rest.
       Screen y grows with camera y (fy positive), and the exe's facing test keeps d < 0 on the same
       points; `test_pm_device.py`'s `winding_agrees` checks the two keep the same triangles.
     - **The 24-bit audit (2026-11-04).** Each draw test forced to 24 bits (`PM_24=1` for those with
       a 53-bit half), every field by distance in float steps. Bit for bit now, and held there
       (tolerance 0 at 24 bits): `test_pm_bake`, `test_pm_model_lights`, `test_pm_lighting`'s colours and
       lit values, `test_pm_sets`' positions, set u, v and colours and the records' other words,
       `test_pm_projected` whole, `test_pm_submit`'s vertices and sets. What moved them: PM's compiler sums
       three terms as (x + y) + z, and the port had (z + y) + x in `face`, `dot`, the light's distance (point,
       spot, bake, reach) and the light map's along, u, v. Breaks catch the facing sum, `dot`, the point and
       spot distances, along and v. The rest are read from the asm or by pattern: the bake's sum and u (asm;
       u's axis has a zero term, so no test can tell), the reach distance (0x537a50's sum not read to its
       end, the same compiler; it only meets a range compare).
       Not bit for bit, with the reason:
       - **Fog factors** (`test_pm_lighting`'s fog 62 to 96% exact on fogged polygons, the +0x10 word in
         `test_pm_sets`, the records' `data`; off by up to 3,424 steps on a value of 0.00096): shared
         `fog_vertices` keeps double, as the x87 does at 53 bits in play, and the exe's steps round at 24.
         Tried: float steps at 24 bits made every plain fogged polygon exact (the rest went exact after the
         order fixes), so that is the whole cause. Not kept: MW3's `test_fogsort` runs the original at
         extended precision, and a test-only branch in code MW3 ships is not worth it. The fog fields keep
         tolerance 2e-4; the specular byte may be 1 off.
       - **The facade's models** (`test_pm_records` positions 75 to 94% exact, `test_pm_modeldraw` 97.4%
         overall): shared `facade_turn` and its trig. **Not traced further**; its order and the x87's
         asin against the port's are the suspects. Worst 2e-4 relative, as before. Tolerance stays.
       - `test_pm_records`' other kinds lose 0.01 to 0.13% of positions, near zero or at most 128 steps:
         **unexplained**. The same draws in `test_pm_sets` and `test_pm_submit` are exact, so my guess is
         a facade left on a node between draws; not checked.
       `project_d3d` and `unproject_d3d`: not on PM's path. `src/pm` calls neither; it has `project_point`,
       `project_records` and `gmod_projected`'s local unprojection, each in PM's order. Their users are
       MW3's draws (`gamez/zmodel/gmod_draw`, `gmod_light`).
     - **The light map's shader** (`Render3D::draw_light_map`, `sdl_render3d`'s `vs_light_map`,
       `fs_light_map` in MSL, HLSL and GLSL; `light_map` in `zvid_tl`). 0x5a03b0's two-stage path:
       src = texture * map * colour, alpha = the texture's * the colour's (the fade), the map's alpha
       unused, the map clamped; blended SRCALPHA, ONE; fog on, with FOGCOLOR black around the draw, so
       fog dims what is added. A vertex is 0x28 bytes; in the buffer it takes two `TLVertex` slots, the
       pitch doubled, so nothing else about the buffer or its upload changes. One more pipeline key
       bit. `test_pm_device.py` compares the vertices, states and fog sends with the exe's two-stage
       path (option 2), and half its steps run at 53 bits for the colours. `mw3sdltests`'
       `sdl_render_draws_a_light_map` checks the pixels: added, faded, fogged, clamped.
       **Checked:** all three languages compile (GLSL with glslangValidator, HLSL with `dxc` on the PC, by the
       lead); only MSL has run. HLSL and GLSL have run nowhere: `sdl_render_draws_a_light_map` joins the
       Windows and Linux checks when those builds next run. A driver refusing these two shaders costs only
       the pools of light (one log line); `ok_` does not depend on them, so MW3 cannot lose its renderer to them.
       **The polygon's u, v under a light map (quirk):** 0x5a04bb keeps them only when the last set is
       a light map and the first is not; otherwise the sets before leave theirs in the vertices.
       **All three "on" settings take this formula** (`fixes.md`). The three-stage path (option 1, or
       auto when the device has 3 stages) is read from the code only: stage 2 modulates the colour
       by the diffuse but its alpha takes no diffuse, so it loses the fade. **Open, for Owen's PC:**
       what the original shows there.
       No world shows a light map yet: `LightSetLightMap` waits for the interpreter's item. To see one,
       `mw3pm --fly c1 --look 0 20 --map-light 2432 5 -2300 120` (debug only).
     - **`cls_clutter`: mapped; the settings ported** (`pm/gamez/zclass/cls_clutter`, 0x513030, 0x513310,
       0x513990; `test_pm_clutter.py`: the 64 shipped templates of the four worlds and crafted entries, each
       with every third named node missing, every field and node flag). The anim lookup (0x524b50) and a
       substitute's hit callbacks (0x5a4d80, 0x5a4dc0; 0x513c60, 0x513cf0) are hooks so far. The templates'
       nodes too (0x5139d0, 0x511ad0, 0x511b50): each world's script-named templates give 2 or 3 textures and
       20 to 23 pieces, and crafted scenes reach what shipped data does not (pieces off their plane, u and v
       outside 0..1, models that are not one textured polygon, a child that does not project). Next: the laying. The rest is not ported
       (0x5106b0..0x514b90, 65 functions, ~3,500 lines of pseudocode). Scattered props laid on the ground by its material. **It waits for the interpreter
       and the mission load**: its template names come from the script command `AddClutterTemplates`
       (0x512fd0, from 0x5b2e15, a list at [0x639c14]). The load (0x492f8c.., unless `-noclutter`,
       [0x62cc58]; "Loading Clutter"): 0x5116a0(1) turns it on; 0x513030(0x609980) reads the settings
       file named there, one record of 0x88 bytes per entry: `scale_range`, `translate_uv_range`,
       `far_fade_range`, `rotation_range` (degrees), `min_slope`, `max_slope` (cosines), `OnWeaponHit`,
       `OnCrater`, `OnCollide` (each `health`, an effect, a `model`), `align_normal`, `substitute`
       (weighted nodes, flagged 0x200000 and made inert) by 0x513310; 0x5139d0 finds each template node
       by name (0x50b920) and 0x511ad0 walks it to single-polygon textured models; 0x511b50 projects
       its children onto that polygon (0x568d00) for u, v in 0..1 (the "does not project to polygon" report),
       kept per material in a map at [0x639be4]; 0x513a30 -> 0x512d90 walks the world's grid and each
       node's tree (0x512d20, flag 4, not 0x4000000) and for each polygon of a type-0 model with
       textures (0x512bb0, not 0x800) lays that material's templates over each triangle: 0x512a80, a strip
       or a fan, the same triangle rule as `split_strips`; 0x512000 does the placing. 0x513a40 frees it
       all at the mission's end. 0x513d90 (from 0x517e45, a crater) sets flags 5 on clutter within a
       radius in the grid. 0x514170 is called from 0x564c67, 0x574999, 0x574b89, 0x590c79: not read.
       0x510830, 0x510910, 0x5109c0, 0x510b50, 0x510b90 are called from the 0x4863ec.. range: not read.
     - **World lighting in the debug start (2026-11-04).** `mw3pm --fly` now runs the mission's own
       script (`c1.gs`, with USEZBD and HARDWARE) instead of reading `gamez.zbd` directly, then the
       shipped `support\additive.gw`, and PM's `world.zrd` reader (`pm/mech3/mission_world`, 0x495ba0,
       0x495250: `defaults`, optionally a mission's block). **What the four worlds' scripts use**, run
       with USEZBD: 37, 53, 37, 62 lines. Tolerances (`SetBFETolerance 0.00005`, `SetInverseZTolerance`,
       ...), `SetVertexShading on`, `LightMapTexture lightmap`, `Object3DSetScrollAlways`, `AddClutterTemplates`
       (3, 2, 3, 2), `CycleTexture*` (c2, c4), `LoadSoils`, `MipBias -1`, `GameZReadZBDFile`. Everything else
       in the 30 scripts (`LightNew`, `WorldOrigin`, `LoadGameGen`: 491 lines) is in `ifndef USEZBD` blocks:
       the data was compiled from them, and the lights come from `gamez.zbd`. No script uses
       `LightSetLightMap`, `LightSetBicolored`, `NodeSetOverride*`; of PM's 12 words missing from the
       dispatcher only `TextureAdditiveTransparent` (104 textures, fire, flares, sparks, explosions: that is
       what sets the additive bit) and `MipBias` are shipped (`test_pm_additive.py`).
       **What was wrong before:** strips have no face normal, and without `SetVertexShading on` no vertex
       normals, so the sun gave them nothing (ambient 0.11, dark, with polygons the bright triangles of the
       `--map-light` shot beside them). That script line is the fix; those triangles are gone.
       `world.zrd`: fog (`hwrange`, colour, altitude, `node`), `camera nearfar_clip`, `sun` (orientation,
       diffuse, ambient, `diffuse_color`, `ambient_color`, which turn on PM's own colours), `fluctuate`. PM's
       light setters keep mixed colours and test static by 0x200 (`test_pm_light_setters.py`). 0x495dd0(1)
       after the blocks: the sun on, then off without its option (0x40b360, a parameter here), the clouds on
       unless the last `fluctuate` had amplitude 0 (c1's m2 does). `test_pm_world_params.py` runs 0x495ba0
       and 0x495250 against the port on every block of the four worlds and crafted ones. Left for the
       mission: wind, weather, temperature, jet ceiling, `ai_headlights`, underwater, `cloud_cover`, the
       puffers' reset (0x564ac0) and the caller's choice of block (0x495b3x, vt +0x74).
     - **Day and night.** Only c1 has `night_day.zrd`. `day`: single m1, standalone sa1, sa2, sa3, instant
       action, multiplayer. `night`: single m2 and m5, standalone sa4; those three are also `world.zrd`'s dim
       blocks (sun diffuse 0.15, ambient 0.05, against the defaults' 1.0 and 0.2). c2, c3 and c4 call neither
       and have one block. `--fly` shows `defaults` with `day`, so c1 there is a day mission's light.
       **The black shape is sky:** `horizon` > `hormain` holds `horizon_top` (flat, colour 0, a wall of radius
       1750), `horscrollday` (`sky01`), `horscrollnight`, stars. The camera follows it (`camera_set_horizon`, as
       0x4087f0's call; before, it sat at the world's origin) and `world.zrd`'s `fog node` picks `hormain`. The
       mission calls `day` or `night` on `horizon` (`night_day.zrd`, below): `--fly` applies it only where the world has that file (c1), `night` for `--variant m2`, `m5`, `sa4`; c2, c3 and c4 have none of those nodes, and their shots are byte-identical with and without the old list.
       **Not reproduced:** `MipBias -1`: read, not applied; the port's textures have no bias (fixes.md).
       **Stubs left in `--fly`:** a mission's depth-only clear is copied, not reached through the state;
       `horscroll*` scroll words run but with no frame's tick.
     - **The sky gap in c2 and c4.** From height, a band between the terrain and the sky that nothing draws.
       The band's rows at three pitches fit a sky ending about 9.1 degrees below horizontal (a fit, not read from
       the mesh) while the ground stops at the far clip, 600 in these two worlds (300 in c1 and c3, no gap up to
       y 500), so it opens about 96 units above the ground 600 away. At (1536, y, -1792), look 0 8, in c2:
       0 pixels at y 60, 1,687 at y 100, 73,602 at y 150 (at 1280x960); c4 at y 100: 1,079. `jet_ceiling` is 130
       in c2 and 145 in c4, so the ceiling does not rule it out. Whether a jet reaches it waits for ground
       queries on PM's models: come back to it then. PM's mission clears colour to 0 (0x45129a) and turns the
       colour clear off (0x451282, 0x4838db; 0x488f27 turns it on elsewhere), and the port's back buffer starts
       black (`SdlRender3D`; it used to hold undefined memory, magenta on this Mac), so the gap is black in
       `--fly`, which stays depth-only like the mission: there it keeps what the last frame left, which is
       black until something draws there.
       **For Owen's PC:** `docs/pm-eye-checks.md`, with the brightness of c1 and c3.
     - **Eye check, settled here** (no original compared, only that the port draws what the data says):
       the sky: day and night differ as `night_day.zrd` says (`--fly c1 --pos 2432 25 -2688 --look 0 -25`: clouds
       and small stars; with `--variant m2`: large stars, dim clouds), c2 clouds over a fog-coloured horizon
       (`--fly c2 --pos 1536 40 -1792 --look 0 -5`). c1 and c3 look nearly black from `--fly`'s default height
       because their fog is black with altitude 80..100: at height 25 the ground shows (c1 looking up,
       `--look 0 -25`; c3 `--pos 1664 25 -2048 --look 0 15`). The light map pool: `--fly c2 --pos 1536 40 -1792
       --look 0 15 --map-light 1536 40 -1700 80`, a pool over the water and the near land, soft-edged; the
       blended edge polygons get no map (opaque only). `--look 0` faces +z, so a light at -z of that camera
       lights nothing on screen until its range reaches the frame's near edge. A long polygon near a small
       light (range 5, on the bridge) redraws whole, and 10,360 of its 10,363 changed pixels move by 1 level:
       the map's clamped edge texel is not quite 0.
     - **Eye check, for Owen's PC:** `docs/pm-eye-checks.md`: what he does in the original, the port's picture beside it.
     - **Eye check, waiting:** a short light's pool on a regular polygon, on the mission's lights; a
       projected light over a light-mapped polygon, bugs on and off, on the caster's port; an axis-aligned
       wall's light map seam, on a mission's light on a wall (`--map-light` could stand in, not tried);
       flares, on wiring 0x588910 to `flare_add`; points meeting blended polygons, on 0x59ee90.
3. **The mission**: load, setup and the world parameters (`mission.cpp`
   114 new, 14 of the port's changed), the standalone missions
   (`readersa1..4`), the objectives.
4. **Mechs and weapons**: the definition reader and its new weapons
   (`XPulseLaser_Large`, `HeavyLaser_Large`, `TBOLT5`), the firepoint
   fix-up (`promote1.cpp`, `CZMechFirePoint::CFixup`), `mech.cpp` (130
   new) and `mech_map.cpp` (97 new), the mech data and skins.
5. **The AI**: targets (`zaitarget.cpp`, 162 new), the net editor and
   in/out nets (85 and 73 new), the valves' new keys, the movement modes
   (Circle, Direct, Random Move).
6. **The front end**: the load/save screens and saved games
   (`%s%02d.mw3`), the saved configurations (`.m3c`), the pilot and
   briefing screens; no movies (zFMV's 21 ported functions are gone).
7. **Sound and the HUD**: the sound syntax reader (rewritten smaller), the
   HUD objectives, spot and headlights.
8. **Multiplayer**: PM's game types (Mw3 dev has multiplayer).

The `mw3pm` program, its own mount of PM's install and its own settings
and saves come with step 1: two programs, `mw3` and `mw3pm`, over one
engine, started from the launcher or directly (docs/games-layout.md).
