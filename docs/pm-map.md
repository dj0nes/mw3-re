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
       - `mw3pmtests`: `pm_text_files_take_include`.
2. **The world**: the light maps and lighting (`gmod_matl.c` 39 new; PM
   drops MW3's fog), `cls_clutter.cpp` (32 new), the world class
   (`cls_world.c` 20 new), the script interpreter's new commands (the
   dispatcher gained 97 calls).
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
