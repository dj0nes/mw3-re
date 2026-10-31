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
| compiler only | 3,856 | other instructions, the same calls, strings, constants and imports: PM was rebuilt with Visual C++ 6 (MW3: 5.0), so most are the same source |
| changed | 1,531 | calls, strings, constants or imports added or removed: a source change |
| moved | 630 of the paired | paired, but the source range they sit in has another name (PM's tree is `branchep1\mech3ep1\...`); counted within the rows above |
| new in PM | 1,692 | no MW3 twin |
| gone (MW3 only) | 1,155 | no PM twin: removed (the movies, the fog, the old text reader), inlined by VC6, or rewritten past recognition |

"Compiler only" is not proof of the same source: a change of an operator or
a constant folded into an instruction looks the same. Its median is 11
instructions different; 565 differ in more than 50 and are worth a look when
their module is ported.

## The port's functions in PM (by the port's module)

| module | identical | compiler only | changed | gone | not in the diff |
|---|---|---|---|---|---|
| mech3/mech | 68 | 278 | 127 | 66 | 28 |
| gamez/zclass | 4 | 96 | 166 | 13 | 1 |
| gamez/ztwod | 30 | 160 | 47 | 20 | 5 |
| mech3/zai | 26 | 103 | 49 | 69 | 3 |
| gamez/zmodel | 6 | 87 | 54 | 33 | 4 |
| mech3/lab | 34 | 107 | 29 | 5 | 2 |
| mech3/hud | 27 | 87 | 41 | 8 | 0 |
| mech3/app | 13 | 62 | 34 | 21 | 7 |
| mech3/pilot | 14 | 57 | 30 | 20 | 2 |
| gamez/zeffect | 6 | 47 | 28 | 33 | 8 |
| mech3/menu | 19 | 72 | 8 | 11 | 3 |
| mech3/weapon | 7 | 60 | 23 | 11 | 6 |
| gamez/zinput | 28 | 56 | 6 | 1 | 0 |
| mech3/options | 25 | 43 | 10 | 9 | 0 |
| gamez/zgeometry | 0 | 65 | 21 | 0 | 0 |
| gamez/zsound | 3 | 44 | 22 | 16 | 0 |
| gamez/zweapon | 0 | 42 | 24 | 10 | 2 |
| gamez/zmath | 0 | 66 | 0 | 4 | 1 |
| mech3/controls | 8 | 34 | 14 | 4 | 0 |
| mech3/veh_basic | 8 | 29 | 11 | 0 | 1 |
| gamez/zvideo | 3 | 17 | 3 | 3 | 21 |
| mech3/zainet | 4 | 20 | 11 | 11 | 0 |
| mech3/player | 2 | 20 | 16 | 6 | 2 |
| mech3/mech_part | 4 | 33 | 8 | 0 | 0 |
| gamez/zrender | 3 | 23 | 9 | 3 | 5 |
| gamez/zinterp | 9 | 24 | 7 | 2 | 1 |
| gamez/zreader | 0 | 24 | 8 | 10 | 0 |
| mech3/veh | 1 | 23 | 14 | 2 | 0 |
| gamez/zfmv | 2 | 13 | 3 | 21 | 1 |
| mech3/mission | 0 | 19 | 14 | 5 | 1 |
| gamez/zimage | 2 | 23 | 8 | 4 | 0 |
| mech3/mech_fieldbase | 11 | 14 | 9 | 1 | 1 |
| mech3/briefing | 0 | 6 | 4 | 25 | 0 |
| gamez/zsiscript | 3 | 25 | 1 | 3 | 0 |
| gamez/zutil | 3 | 25 | 2 | 1 | 0 |
| gamez/zdeclient | 1 | 19 | 7 | 0 | 2 |
| mech3/pocket | 0 | 20 | 6 | 1 | 1 |
| mech3/debriefing | 4 | 13 | 9 | 0 | 1 |
| mech3/turret | 1 | 12 | 5 | 4 | 1 |
| mech3/zrdrvalve | 4 | 8 | 8 | 3 | 0 |
| mech3/sensors | 0 | 11 | 7 | 3 | 0 |
| mech3/zrdraiv | 6 | 11 | 3 | 0 | 0 |
| mech3/zrdrainet | 3 | 11 | 3 | 0 | 0 |
| mech3/zainetedit | 2 | 8 | 6 | 1 | 0 |
| mech3/force | 8 | 5 | 2 | 0 | 0 |
| mech3/message | 3 | 11 | 1 | 0 | 0 |
| mech3/action | 0 | 4 | 5 | 4 | 0 |
| gamez/zsave | 1 | 9 | 0 | 1 | 0 |
| mech3/entity | 2 | 6 | 0 | 1 | 0 |
| mech3/zai_anim | 0 | 3 | 2 | 2 | 2 |
| gamez/zgame | 1 | 3 | 5 | 0 | 0 |
| mech3/sound | 0 | 5 | 0 | 0 | 3 |
| gamez/znetwork | 0 | 5 | 1 | 1 | 0 |
| mech3/credits | 0 | 1 | 4 | 1 | 0 |
| mech3/mainmenu | 1 | 0 | 3 | 2 | 0 |
| mech3/salvage | 0 | 1 | 5 | 0 | 0 |
| mech3/lance | 0 | 1 | 3 | 1 | 0 |
| mech3/cockpit | 0 | 5 | 0 | 0 | 0 |
| mech3/quit | 1 | 3 | 0 | 0 | 0 |
| mech3/landmines | 0 | 3 | 1 | 0 | 0 |
| mech3/mech3 | 1 | 1 | 2 | 0 | 0 |
| gamez/zentity | 0 | 4 | 0 | 0 | 0 |
| mech3/pause | 0 | 0 | 1 | 2 | 0 |
| mech3/music | 0 | 1 | 0 | 1 | 0 |
| **all** | **412** | **2088** | **950** | **479** | **115** |


The port has 4,044 addresses; 115 are not in the diff (data tables, thunks
and functions Ghidra did not make). For the port: **2,500 (62%) need
nothing** (identical or compiler only), **950 changed** and **479 are gone in
PM**. The changed ones are the work; each is listed with what changed in
`port-impact.txt`.

## MW3 functions not ported yet, by MW3 source file (range start)

| file | identical | compiler only | changed | gone |
|---|---|---|---|---|
| zaitarget.cpp | 107 | 147 | 38 | 74 |
| mech_map.cpp | 114 | 108 | 33 | 20 |
| mission.cpp | 72 | 90 | 32 | 58 |
| mech.cpp | 71 | 76 | 42 | 55 |
| zainetedit.cpp | 58 | 112 | 35 | 16 |
| player.cpp | 49 | 66 | 34 | 35 |
| zrdrainet.cpp | 62 | 92 | 9 | 10 |
| mech_data.cpp | 43 | 46 | 26 | 48 |
| zgameapp.cpp | 45 | 97 | 12 | 8 |
| class.c | 33 | 41 | 36 | 15 |
| briefing.cpp | 18 | 63 | 7 | 20 |
| zreader.cpp | 8 | 38 | 11 | 47 |
| zainetmechinout.cpp | 30 | 32 | 8 | 29 |
| gmod_init.c | 17 | 60 | 5 | 5 |
| zent_init.cpp | 40 | 31 | 11 | 5 |
| weapon.cpp | 29 | 33 | 4 | 13 |
| turret.cpp | 10 | 32 | 7 | 29 |
| zrdrvalve.cpp | 17 | 33 | 20 | 3 |
| mech_salvage.cpp | 9 | 29 | 14 | 15 |
| zrdraiv.cpp | 24 | 19 | 20 | 3 |
| mech_pocket.cpp | 28 | 20 | 5 | 12 |
| zsnd_parm.cpp | 28 | 27 | 10 | 0 |
| si_script.cpp | 11 | 38 | 4 | 9 |
| fmv_script.cpp | 13 | 29 | 7 | 12 |
| mech_anim.cpp | 8 | 36 | 4 | 12 |
| veh_basic.cpp | 10 | 26 | 9 | 7 |
| zrndr_draw.c | 4 | 31 | 13 | 4 |
| znet_dplay.cpp | 3 | 19 | 11 | 6 |
| znet_session.cpp | 5 | 22 | 2 | 1 |
| mech_objective.cpp | 10 | 10 | 1 | 6 |
| mech_parts.cpp | 3 | 15 | 5 | 4 |
| zimg_fonts.cpp | 4 | 16 | 2 | 4 |
| zvid_buff.c | 15 | 3 | 1 | 7 |
| mech_waynet.cpp | 6 | 15 | 2 | 2 |
| zinterp_parse.cpp | 13 | 9 | 2 | 1 |
| gmod_matl.c | 4 | 5 | 4 | 11 |
| zsnd_create.cpp | 2 | 13 | 3 | 6 |
| zvid_dd.c | 2 | 4 | 15 | 1 |
| zvid_init.c | 5 | 11 | 2 | 3 |
| zsnd_cd.cpp | 4 | 10 | 4 | 1 |


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

## Running the differential tests on PM

`original.py` loads PM's exe (`MW3_EXE=pm`), but none of the 194
differential tests runs on it yet: each names MW3's addresses, for its
functions (which moved in PM's relink) and for its globals (which moved
too). What it takes, as the first porting step:

1. **An address map.** Functions from `pairs.json` (6,875 pairs). Globals
   from the paired functions themselves: an identical or compiler-only pair
   reads and writes the same globals, so their absolute operands, aligned
   in order, pair MW3's data addresses with PM's (pmdiff.py already names
   them to compare; it would keep the pairs).
2. **`original.py` translating** a test's addresses through the map when
   the exe is PM's (calls, reads, writes, hooks), and refusing a test that
   touches an address the map lacks.
3. Then each module ported for PM runs its existing test on PM's exe against
   the PM version of the port, and the tests of unchanged functions run on
   both exes for free.

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

The launcher's game choice and the per-game mounts and saves come with
step 1 (CLAUDE.md: one program, both games).
