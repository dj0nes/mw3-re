# The 1.2 patch's data, file by file

What the official 1.2 patch changes in the game's data, against a 1.0
install, and whether the rebuild can redo each change itself when it loads
1.0 data. The rule (Owen): small values, record edits and our own message
wording are ours to write; the patch's own bytes and whole new files are not
copied. Built in sittings from 2026-10-31; what is done is listed under Built.

The patch (`data1.cab`, InstallShield) holds 31 files: `Mech3.exe`, two
read-mes, and the 28 data files below. The exe needs nothing: the rebuild is
ported from 1.2's.

Scripts used (scratch, to go into `tools/analysis/` with the layer):
reader archives decoded entry by entry and compared by path; interp.zbd's
scripts compared one by one; the message tables; force_eff.ifr's records;
c3's world compared by node name and model content with the file's saved
pointers masked; c3's anim with 1.0's 0xcd fill, pointers and float noise
masked.

## Summary

| File | 1.0 -> 1.2 bytes | What changed | Kind | Redo at load? |
|---|---|---|---|---|
| `Mech3Msg.dll` | 120,320 -> 123,392 | 37 new messages: 22 target names (the c2/c3/c4b targets moved from fixed names to `MSG_` ids), 3 resolution labels, the jump-jet limit, "rounds", `DefaultCtlConfig`, 3 multiplayer version/world texts, Alt/Shift/Button 1/9/10; 4 edited (a `\n` fix, two chat labels, "%1 Team"); message 4 dropped | text | yes: our own wording in a small table |
| `force_eff.ifr` | 2,936 -> 4,113 | jets2 phase 90° -> 0; water damper softer; "impact fall" reworked (longer, stronger, an attack and fade); new: "impact land" (1.0's old landing bump under its new name), "dead spring", "dead dance" (two shakes) | gameplay feel | yes: 8 records of small values |
| `zbd/reader.zbd` | 2,143,369 -> 2,146,707 | below | | |
| -- weapons.zrd | 77,078 -> 77,006 | 66 values: laser heat up; PPC/ER PPC speed 650 -> 900; LB-X burst damage down, impact up, fire rate 1 -> 0.5, wider burst; autocannons retuned; LRM/SRM/Streak damage x1.25 with more impact; LRM15/20 heat down | balance | yes: a value table |
| -- 17 `dfn_*.zrd` | small | torso twist limits (mostly 80 -> 100/110, Strider 90 -> 75, the lights 5 -> 10/15); single -> double heat sinks in the Cauldron, Vulture, Puma and Supernova loadouts; Puma's loadouts (ammo moved, a medium pulse for a large); training2 Bushwacker armour cut to near nothing; a Vulture skin/damage texture fix | balance, bug fixes | yes: record edits |
| -- hud.zrd | 2,648 -> 5,284 | blocks for 1152x864, 1280x1024, 1600x1200 | layout | yes: every value is 1.0's 1024x768 block moved with its edge (right, bottom or centre), checked on all 34 |
| -- dialog.zrd | 54,819 -> 55,555 | multiplayer host screen: chat up/down arrows added, the chat list moved 14 px | layout (multiplayer) | yes: record edits (the arrows' images are 1.0's) |
| -- briefing.zrd | 1 value | the salvage label's offset (10,10) -> (10,2) | layout | yes |
| -- sounds.zrd | 133,406 -> 133,600 | footsteps and soil sounds point at the new 16-bit wavs, the engine at its trimmed one, room 4 at `env_room04_p2.wav` (which neither version has); a playback-format key | sound | yes, with the wavs below |
| -- mechsoilfx.zrd | 7,319 -> 7,143 | the soil puffs lose GRAVITY (4 motions) | effect | yes |
| `zbd/interp.zbd` | 99,662 both | no script changed: the archive rebuilt (new dates, new order) | none | nothing to do |
| 9 `zbd/*_2s.wav` | new, 110-124 KB each | the footstep and soil sounds as 16-bit; 1.0's are the same sounds at 8 bits (within the rounding; water exactly) | sound quality | the sounds: yes (1.0's own); the extra bits: no |
| `zbd/mech_engine_np.wav` | new, 107,850 | the engine loop, 16-bit, with its last 176 samples cut: 1.0's loop ends on a ramp to near full scale, a click every time it loops ("np", no pop) | bug fix | yes: trim 1.0's own sound |
| `zbd/c1/reader.zbd` | 771,601 -> 774,352 | mission 1's secondary objective (destroy the Owens and two APCs) removed; new `envlights.zrd`, `intro.zrd` (animation sources; c1's compiled anim.zbd is unchanged, so likely unused) | bug fix; unused | yes (the two sources copied, sitting 5) |
| `zbd/c2/reader.zbd` | 758,993 -> 759,298 | mission 2's jump-jet ceiling -15 -> -90; the multiplayer start calls two more animations (lights, the minepit turret); new 8-byte `mobilerepair.zrd` | bug fixes | yes |
| `zbd/c2/readerm3.zbd` | 68,026 -> 73,391 | the boundary valves use new region pairs (`reg_inside2/outside2`, already in 1.0's archive); two target names to `MSG_`; new `net_14.zrd` (292 lines, a new path net) | bug fix; new content | yes (net_14 copied, sitting 5) |
| `zbd/c3/readerm2..m6` | +-30 bytes each | target names to `MSG_` ids; m3's two boundary regions' points moved (12 values) | text, bug fix | yes |
| `zbd/c3/readermp3.zbd`, `readermp4.zbd` | new, 7,262 and 6,662 | two new multiplayer maps on the c3 world (paths, boundaries, map info) | new content | not yet: when multiplayer is ported |
| `zbd/c3/gamez.zbd` | 12,999,820 -> 12,994,852 | the world re-exported: 153 of the 187 models that differ have the same faces stored in another order; 34 really change (277 faces out, 239 in): the dome complex's walls re-cut by about 1 m (gaps closed), a 6.3 m wall along one edge of all 7 helipads removed, three terrain corners raised 4.5 m and a map-edge tile given its missing point (seams you could see through), 6 small objects closed with 4 faces each, a damaged part's floating top removed, a feature on `base04` moved 11 m; also one texture (`cpad07`) dropped, a node renamed, a soil type fixed (`dome12` 9 -> 0) | level fixes | yes (sitting 6; 48.6 KB of 1.2's geometry) |
| `zbd/c3/anim.zbd` | 3,879,480 both | re-saved only: saved pointers, uninitialised padding, floats off by an ulp or -0 -> 1e-8 | none | nothing to do |
| `zbd/c4b/readerm4.zbd` | 34,205 -> 41,618 | five path nets the mission's orders use were missing in 1.0 (net_02, 05-08, 6.7 KB); one patrol point moved; a target name | bug fix (AI paths) | yes (the nets copied, sitting 5) |
| `zbd/c4b/readerm5.zbd` | 26,025 -> 50,984 | 1.0 shipped mission 5 with only six nets and c4's boundary; 1.2 adds 22 files: 13 are byte for byte 1.0's own c4b m6/m4 files, the other 9 are those with 1-13 values changed; targets get `MSG_` names | bug fix (AI paths) | yes, all 22: from the player's own m6/m4 archives plus small edits |

## What stays out

Owen (2026-10-31): copying small amounts of 1.2's data is fine, so a 1.0
player gets the exact 1.2. Left out:

- **c3's multiplayer maps** (`readermp3.zbd`, `readermp4.zbd`): until
  multiplayer is ported.
- **16-bit sound**: the footsteps play 1.0's 8-bit samples (the same sounds).
- **The unused texture `cpad07`** stays in 1.0's c3 texture table (no
  material uses it in 1.2; dropping it would renumber the table).

## How the layer would work (step 2)

`mech3/patch12_*`: applied while the data loads when the install's version is
1.0 or a mix (the launcher's version check), not behind `bugs`. Reader edits
as (archive, entry, path, old, new) records checked against the old value, so
a changed install is left alone; entries copied between the player's own
archives by name and checksum; the HUD blocks made by the anchoring rule; the
message table extended in code; the force-feedback records added; the engine
loop trimmed. The test: each patched file on 1.0 data parses equal to 1.2's,
and the list above is what does not.

## What is taken from 1.2's files

Real sizes (the plan's "a few KB" for c3's geometry was low). Values put in
by the edit table (weapons, mechs, missions; about 18 KB of table text in
all) and the force-feedback values are not counted here: they are single
values, not whole records.

| Taken whole | From | Bytes (as stored in 1.2's file) |
|---|---|---|
| c3: 28 models' new polygons, points, headers | `zbd/c3/gamez.zbd` | 48,640 |
| c3: one grey material | `zbd/c3/gamez.zbd` | 44 |
| c2 m3 `net_14.zrd` | `zbd/c2/readerm3.zbd` | 5,045 |
| c4b m4 `net_02/05/06/07/08.zrd` | `zbd/c4b/readerm4.zbd` | 6,669 |
| c1 `envlights.zrd`, `intro.zrd` | `zbd/c1/reader.zbd` | 2,614 |
| c2 `mobilerepair.zrd` | `zbd/c2/reader.zbd` | 8 |
| **Total** | | **63,020** |

Not counted because they come from the player's own 1.0 files: c4b m5's 22
paths and regions (from m6 and m4), the footstep and engine sounds, the HUD
blocks (from the 1024x768 block), the messages (ours, or names).

## Built

`mech3/patch12*` with `zrdr::set_load_hook` (zreader.h, ours): the hook
gets each entry read from an archive with the archive's name, and the layer
edits it when that archive measures as 1.0's (`data_file_version`, cached
per file until the mounts change). `GameEngine` installs it; the
differential tests' probe never does, so they read the files as they are.
Tested by `tests/test_patch12.cpp`: each covered entry of 1.0 with the layer
parses equal to 1.2's, and 1.2's are left alone.

1. Messages (`patch12_text.cpp`): 1.0's table plus the 37 keys, the 4 edits
   and the dropped version line gives 1.2's keys exactly and 1.2's texts for
   all but 4, which are our own sentences (the jump-jet limit and three
   multiplayer texts). HUD (`patch12_hud.cpp`): the three blocks from the
   1024x768 block by an explicit table of what hangs from which edge.
2. Weapons, mechs, force feedback. `patch12_edits.inc`, written by
   `tools/analysis/patch12_edits.py` from the two installs: each change as
   the smallest edit on the parsed tree (a value set, a key removed or
   inserted; KEY#n for the n-th KEY, @i for an item), applied in order on a
   copy of the entry and kept only if every edit's old value matched.
   weapons.zrd (66 edits, including AC20's and Ultra AC20's duplicate
   IMPACT_FORCE, dropped) and the 17 mech definitions (66). force_eff.ifr
   (`patch12_force.cpp`, applied where the SDL device reads the file): the
   five new effects written with our own IDs; the test compares records by
   name, type and values, a compound's parts by name.
3. The mission and front-end edits, the same way (37 entries in all, 205
   edits): the briefing label, the multiplayer host's chat arrows,
   sounds.zrd, the soil puffs' gravity, c1 m1's objective, c2's jet ceiling
   and multiplayer start, c2 m3's boundary valves, the target names, c3 m3's
   boundary, c4b m4's patrol point and net_09. The sounds sounds.zrd now
   names come from 1.0's own through `zsnd::set_wave_alias_hook` (ours,
   zsnd.h): the `_2s` sounds are 1.0's 8-bit ones (1.2's extra bits are not
   ours to copy), the engine loop is 1.0's less 176 samples at 22 kHz, at
   either sound quality; a loose 1.2 file, if there is one, comes first.

4. c4b mission 5's 22 files (`patch12_copies.inc`, the same script):
   `zrdr::set_missing_hook` (ours) asks the layer for an entry no archive
   and no loose file has; while mission 5's 1.0 archive is open, it reads
   the entry from 1.0's mission 6 or 4 archive (checked by size and CRC, so
   only the player's own 1.0 entry is taken) and applies its edits (0 to 13
   values each).

5. Entries 1.2 adds that 1.0 has nowhere, whole (`patch12_added.inc`, the
   same script, through the missing-entry hook while their archive is
   open): c2 m3's net_14, c4b m4's net_02 and 05-08, c1's envlights.zrd and
   intro.zrd, c2's mobilerepair.zrd (7.5 KB of text in all).
6. c3's world (`patch12_world.cpp`, `patch12_world.inc` from
   `tools/analysis/patch12_world.py`): `zcls::set_open_hook` (ours) hands
   the world loader 1.0's gamez.zbd rebuilt in memory, in 1.0's own model
   order: 28 models whose faces changed (1.0's polygons where 1.2 kept them
   byte for byte, 1.2's new ones: 48.6 KB from 1.2), `dome12`'s soil 9 -> 0,
   one added grey material (linked into the used list), and 290 node words
   (bounding boxes, flags, the helipads' and targets' records, the rename,
   two targets whose models 1.2 removed). Byte equality with 1.2's file is
   not the aim (1.2 saved its models in another order, with stale pointers);
   the test compares what the game gets: every node's record and data
   (pointers aside), and each object's model as its faces (points, normals,
   UVs, colours, flags, material by texture or colour and soil): all 20,000
   nodes and 7,306 objects match, and the loader reads the rebuilt world.

## Found on the way (in discoveries.md, What the 1.2 patch changed)

- The engine loop clicks in 1.0: its last 176 samples ramp to near full
  scale; 1.2 cut them.
- Room 4's sound (`env_room04.wav`) is empty in both versions, and 1.2 points
  it at a file that does not exist.
- 1.0's c4b mission 5 shipped with c4's boundary and six of its nets; 1.2
  copied in mission 6's.
- 1.2 dropped c1 mission 1's secondary objective (destroy the Owens and the
  two APCs).
