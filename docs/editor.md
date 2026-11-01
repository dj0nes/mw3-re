# The Battlesport game editor and the stealth object

MW3 1.2 still carries Zipper's in-game editor from its earlier game,
Battlesport: a mouse menu titled "BATTLESPORT GAME EDITOR" over a mission,
with the view on the stealth object, an AI net editor and a vehicle editor.
Nothing in the release reaches it: it opens on debug command 0x9a, which no
key is bound to (0x0040db60 registers only 0x61, 0x70, 0x7e and 0x6c to the
debug handler 0x0040dba0). Mapped 2026-10-31 (dev 2); not ported yet. The
port will reach it with F11 under `--debug-camera` (or `debugcamera=1` in
mw3.cfg) only, as Owen asked: "exactly what they did", a developer's tool.

## The entry and the exit

**Command 0x9a** (0x0040dba0's switch, decoded from its byte table at
0x0040dea4 and jump table 0x0040de34) calls **0x0040e430**:

- [0x005fc314] = 1 (the editor is up);
- a fresh command map (0x0051d3a0) with two bindings (0x0051d2c0): Space
  (key 0x39) -> command 0x9b (select), key 0x439 -> command 0x9c (to trace);
- the camera on the stealth object: [0x00609a2c]'s vt+0x108 (0x004acd90 ->
  0x0040adf0 with the camera list [0x0060b140]);
- the camera mode saved ([0x005f4dac] = 0x0040f880; when set, 0x0040f8b0);
- the editor panel shown ([0x005f4db0]'s vt+0x04(1));
- 0x0040e7d0: the main menu (below).

**0x0040e7f0** ("EXIT GAME EDIT"): the command map popped (0x0051d490), the
panel hidden (vt+0x04(0)), the camera mode restored when it was saved
(0x0040f890), [0x005fc314] = 0.

## The panel and its menus

The panel ([0x005f4db0], vtable 0x005bc510, built by 0x0040e270(5, 0x5f),
freed by 0x0040e3b0) holds 25 text rows (0x4b0 bytes each, row object
vtable 0x005bc590, 12 pixels apart from y 0x5f).

A menu is data: `{int count; {char text[20]; void (*handler)(arg);} x count}`.
**0x0040e7b0**(ECX mode, EDX table, the title, "") sets [0x005fc30c] (mode)
and [0x005fc318] (table) and fills the rows (**0x0040e6a0**: every row
cleared with "" at 0x005f4c90, then each entry's text, the panel's width
the widest row's, its height count x 12).

**The frame, 0x0040e4b0** (while [0x005fc314]): the row under the mouse
(rows 0..24 by y; the row's vt+0x5c hit test) highlighted (+0x24c/+0x250
colours, +0x474 dirty), the old one restored; Space (command 0x9b's state,
0x0051d300 bit 4) on a row whose flags allow it calls that entry's handler
with the frame's argument; the camera's step (0x0059fd20 with [0x0072c52c]);
in modes 1..3 a 0.1 s step (0x00403140); 0x00404290 at the end.

### The main menu (0x005e0d30, 8 rows; 0x0040e7d0)

| Row | Text | Handler | What it does |
|---|---|---|---|
| 0 | PICK A COMMAND | none | the header |
| 1 | EDIT AI NETS | 0x00402d60 | the AI net editor (below) |
| 2 | EDIT VEHICLES | 0x004040c0 -> 0x004b86e0 | the vehicle editor: a 0x988-byte object (0x004b80f0, vtables 0x005c9898, 0x005c97c0), [0x0060b310]; also debug command 0x9d's |
| 3 | EDIT WEAPONS | 0x004bf620 | the shared empty function: nothing in MW3 |
| 4..6 | (blank) | none | |
| 7 | EXIT GAME EDIT | 0x0040e7f0 | the exit above |

### The AI net editor (0x005e0f18, 13 rows; installed by 0x00403690)

0x00402d60 first picks a point or net node under the mouse (0x0040e120 ->
0x004d55c0, the camera's pick vt+0xf8, 0x0058b1c0), then installs the menu
"AI NET EDITOR" in mode 1. It is the editing side of `CZAiNetEdit`
(0x004bd360, 0x004bd3d0; docs/ai.md: only the builder side is ported).

| Row | Text | Handler |
|---|---|---|
| 0 | Pick CMD or Point | 0x004bf620 (empty) |
| 1 | GOTO Net | 0x004036c0 |
| 2 | Change Net Params | 0x004036f0 |
| 3 | Node Params | 0x00403710 |
| 4 | Add veh to node | 0x00403820 |
| 5 | Move Node | 0x00403ac0 |
| 6 | Delete Node | 0x00403ae0 |
| 7 | Delete Net | 0x004bf620 (empty) |
| 8 | Save Changes | 0x00402e90 |
| 9 | Tree View | 0x00402c70 |
| 10, 11 | (blank) | none |
| 12 | Exit Net Editor | 0x00402ba0 |

"Save Changes" writes the net files; the port will write them to the write
dir, never into the install (FIX (A), agreed 2026-10-31).

## The stealth object

Made at every mission load ("Creating Stealth", **0x004a4de0**, [0x00609a2c],
vtable 0x005c8460; vehicle.zrd's `stealth`: no model, sounds or weapons,
health 10000, `mode` basic, rates 40 / 65, turns 2.5 / 3.5), with a
0x108-byte part (0x004a55c0, "ret_off") and its name (0x005a84c0); deleted
by the unload (0x00490480). It is on zEntity's list, sorted last; the
sensors clear its state each frame, so the entity walk never runs it. Its
frame runs from the player's tick while the view is on it.

**Its controls** (0x004ae270 over the base 0x004ac750, vtable 0x005c8c50,
IControls 0x005c8b98; 0x004ac860 is only the zeroed allocation) are the
basic vehicle's, a small AI, not input: their settings read from the
vehicle's zrd (0x004ae3c0, 0x004ae6b0), their frame **0x004ae850** (the
IControls vt+0x0c) calls **0x00401050** with the player's mech, a switch on
+0xa14:

| Mode | Functions | What it does |
|---|---|---|
| 0 | 0x00401100, 0x00401940 (0x00401400, 0x00401500) | follows a path: the waypoints at +0xa0c, the index +0xa10; throttle and turn (IControls vt+0x20, +0x28) toward the current point, the next one within 10 m |
| 1 | 0x00401680 | another path or approach behaviour (to trace) |
| 2 | 0x00401c30 | the same |
| 3 | 0x00401e30 | the same |
| 4 | inline | stop, then back to 0 when +0x760 clears |

Helpers 0x00401ab0, 0x00401bf0, 0x00401f50, 0x004020d0 (ported),
0x004abcf0. So the stealth is a camera dolly that drives itself along the
editor's paths.

**BASIC** (0x004a1d90, its ground 0x004a1f00), the stealth's movement
mode: the steering (0x004a20d0: the turn into the heading +0x330, wrapped
at 2 pi) or, in camera mode 3 (straight down), north-up panning (0x004a2000:
heading 0, the throttle on the sideways speed, stopped dead before
reversing); the speed step 0x004a2b10 (ported); the move on the horizontal
plane; then the four platform points' ground (0x004a2190), the highest plus
+0x88 of the motion block, pitch and roll 0: a level sled above the ground.

## The port, in sittings (agreed 2026-10-31; scope set by Owen 2026-11-01)

About 140 unported functions for S1..S6 and about 200 (5,500 lines of
pseudocode) for the vehicle editor.

**For v1.0: S1 to S4**, the stealth object and the view on it.

1. **S1** The stealth vehicle 0x004a4de0 with 0x004a55c0, 0x005a84c0, its
   controls 0x004ae270, 0x004ae3c0, 0x004ae6b0, 0x004ae850 (its mode switch
   0x00401050 a stub until S3); made at every mission load, as the
   original's "Creating Stealth" does, deleted at the unload. Tests:
   construct and read against the original; the program makes it in c1m1;
   the full suites and the campaign runs as the proof that play does not
   change, with the mission load time before and after.
2. **S2** BASIC: 0x004a1d90, 0x004a1f00, 0x004a20d0, 0x004a2000,
   0x004a2190; differential tests on random states and the real worlds'
   ground.
3. **S3** The controls' modes 0..4 (0x00401050, 0x00401100, 0x00401940,
   0x00401400, 0x00401500, 0x00401680, 0x00401c30, 0x00401e30) and their
   helpers (0x00401ab0, 0x00401bf0, 0x00401f50, 0x004abcf0); differential
   tests.
4. **S4** The entry 0x0040e430 and the exit 0x0040e7f0 for the camera: its
   command map (0x9b, 0x9c), the camera on the stealth and the camera
   mode's save and restore (0x0040f880, 0x0040f890, 0x0040f8b0); the key
   under `--debug-camera` / `debugcamera=1` only (F11, unless the default
   keymap binds F11 in play: then a key nothing uses), no keys of ours
   besides, the mission running on as in the original. The panel and its
   main menu (0x0040e270, 0x0040e6a0, 0x0040e7b0, 0x0040e7d0, the frame
   0x0040e4b0) too if the row object draws with primitives already ported:
   EDIT AI NETS and EDIT VEHICLES then say "not ported yet" in the log only,
   EXIT works; if the panel needs new HUD primitives it waits, and the key
   toggles the camera alone. Test: the key over c1m1 puts the view on the
   stealth and gives it back; without the flag, nothing.

**After v1.0, with Pirate's Moon** (its zainetedit.cpp changed: 35 changed
functions, 85 new; ported once with both versions side by side):

5. **S5, S6** The AI net editor: 0x00402d60, 0x0040e120, 0x00403690, the
   handlers above, the tree view; Save Changes to the write dir (FIX (A)),
   saying on screen where it saved; typed fields (names, numbers) bounded
   like any input.
6. **S7** The vehicle editor mapped (0x004b80f0's object, its screens and
   what each control edits and writes, to which file; every save to the
   write dir), then its own sittings (likely 3 to 5), named from that map.
