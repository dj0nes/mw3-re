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
drawn or collided with. PM's loader says in the log how many nodes carry a
PM model, so a PM world never silently draws nothing.

See docs/games-layout.md.

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
     - **Do polygons past 64 reach the clippers?** One, narrowly. The eight
       largest in c1 (313 and 198 vertices, flag 0x4000, which the draw never
       reads) are the flat `inout` boundary areas, which PM deletes at mission
       load as MW3 does (`0x4cddb0`, MW3's `0x4c3930`, render.md 6a), so they are
       never drawn. Outside `inout` the largest is 64 (`g3860`, c1; none past it
       in any of the four worlds). A 64-vertex polygon losing exactly one corner to the
       near plane makes 65: on the exe's clippers in the emulator the two
       textured paths then fault on a pointer the 65th vertex overwrote
       (0x6887d0), and the other four write past the scratch silently. So the
       fix guards a real but rare fault; not seen in play.
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
       The facade's turn is MW3's own, split out as `zmod::facade_turn`. Shipped
       worlds use only types 0 and 1, modes 0 and 1, no flag 0x40, no morphs. As
       MW3's port, modes 2 and 3 draw as a default model. FIX (bug): points with
       no vertices or no polygons draw nothing (the exe loops 2^32 times, or reads
       through null; `pm_points_without_polygons`). `test_pm_modeldraw.py`, importing
       `modeldraw_common.py`, on PM's exe with c1 and c3: 3,000 draws, the result,
       the default flag, the stack's top and the one below, the points and the
       camera-space vertices, editing the data for what shipped data lacks. Points
       past a shorter morph list are the last draw's, on both sides, so the test
       morphs them all. About 93% of the matrix floats are bit-identical and the worst
       differs by about 1e-6, except a facade about a centre thousands of units out
       with the camera near it, where the tolerance grows with their ratio (no
       shipped model has flag 0x40); the centre itself is compared bit for bit.
       Breaks, 14 of 14. Emulator only.
     - Order from here: the option's state; the other helpers in groups,
       each with the setters it first reads; then `0x543b10` by call trace, as
       `0x53f2e0` was. Emulator only until the world class and the mission can
       draw in `mw3pm`.
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
