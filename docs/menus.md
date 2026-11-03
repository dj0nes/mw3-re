# The menus the main menu opens: Options, Credits and the pilot screen (ported)

Mech3.exe 1.2, 2026-10-20. The main menu's buttons (mainmenu.md) push *menu
objects*: shell states (game-loop.md) made on the heap, each with a zTwoD
screen (twod.md). Ported: the four menu objects of the options and the
credits, the credits screen, the game options and the graphics and audio
options, the cycle button they share, the controls screen (0x0042a590) with
its axis popup and its load/save popup (section 6), the file list and edit
bar the remaining screens share (section 2), and the pilot screen with its
state and the four states INSTANT ACTION, NEW CAMPAIGN, TRAINING and
MULTIPLAYER push (section 10; the pilot itself is pilot.md).
Code: `src/mech3/menu_states`, `credits`, `options_menu`, `controls_menu`,
`pilot_menu`, and the shared pieces in `menu_widgets`; checked by
`tests/differential/test_menus.py`, `test_ctlscreen.py`, `test_loadsave.py`,
`test_pilotscreen.py` and `test_app_states.py`.

## 1. The menu objects

The Main state's enter (0x00486990) makes nine (game-loop.md); the pause
screen's Options button (0x00492d70, case 2) makes the three option ones
again and pushes the game options. Main leaves (0x00486c50) and Pause leaves
(0x004931e0) delete them. Each is a shell state (0x0048eb10: name, screen at
+0x0c, vtable over 0x005c6038); its destructor (0x0048eb80) deletes the
screen.

| Global | Name | Constructor | Vtable | Screen (size) | Made |
|---|---|---|---|---|---|
| [0x00609868] | Game Options State | 0x00438be0 | 0x005bf7a8 | 0x004379f0 (0x1407c) | with the state |
| [0x0060986c] | Gfx and Audio Options State | 0x0043a640 | 0x005c0040 | 0x00438f60 (0x13f9c) | with the state |
| [0x00609870] | Controls Configure State | 0x0042d9d0 | 0x005be3d8 | 0x0042a590 (0x16c94) | with the state |
| [0x00609878] | Credits State | 0x004295e0 | 0x005bd938 | 0x004289a0 (0x11ae0) | on entering |
| [0x00609858] | Pilot State | 0x00488910 | 0x005c54e8 | 0x00486f40 (0x13184) | with the state; pushed by the next four (section 10) |
| [0x0060985c] | Action State | 0x0048a6c0 | 0x005c5b10 | 0x00488bc0 (0x17730) | with the state (section 13) |
| [0x00609864] | Campaign State | 0x0048aef0 | 0x005c5b40 | none | |
| [0x00609874] | Training State | 0x0048b120 | 0x005c5b70 | none | |
| [0x00609860] | Multiplayer State | 0x0049c900 | 0x005c7fe8 | three: 0x00493260, 0x00495f90, 0x0049bd80 (not ported) | with the state |

The option states' enter (0x00438c90 for the game options, 0x0042da50 for
the other two) is the shell's (0x0048ebf0) then the screen's +0x10 with 0:
the screen reads the options. Only the game options first switch to the
menus' screen mode 5 (0x004ab6b0(5), 640x480): the sub-screens open from it.
Resume is the shell's (0x0042da70). Escape (+0x28, 0x00438c80) pops with 1.
The credits state makes its screen on entering (0x00429600, the clock
[+0x11adc] zeroed) and deletes it on leaving (0x00429670: 0x0048ee00, then
the shell's leave). Their +0x24 (0x0042a130) answers 1, as the base.

## 2. The shared pieces (menu_widgets)

**The screen** (MenuScreen, mainmenu.md): +0x10 is the option screens'
read/apply (empty in the base, 0x004a5bc0). FastMenu's still screen
(0x00486ed0) is called on every one of these screens, not only the main
menu's. The sub-screens borrow the main menu's BACKGROUND (inline in each
constructor): with a main menu ([0x00609854]) its image (+0x110ec) and
whether it moves (+0x111e8), else (the option screens only) the dialog's own
BACKGROUND; a still background is placed at the origin with its view
rectangle as the source. The option screens also make it still while a
mission runs (0x0047fb70, the Core state's +0x19): the pause screen's
Options. The credits read [0x00609854] unchecked.

**BACK** (0x00428c60, on the credits and both option screens) only calls
the state's +0x28: no ACTIVATE sound or image, unlike every other button.

**The cycle button** (0x0048b280, vtable 0x005c5ba0, "cycle button" by the
text its destructor leaves in its label), 0x424 bytes: a ShellButton (its
own id -1; the cycle's id at +0x420 goes to the screen's +0x14) with a list
of choices (+0x15c, 8-byte items: a `_strdup`'d text and a value), the
current index (+0x16c, -1 for none), and TOGGLE_LEFT and TOGGLE_RIGHT read
as Controls of their own (+0x188, +0x2d4) and taken out of the dialog, at
their POSITION from the button. The choice shows in the first LABEL
(+0xa0, 0x0048b850; "" for none). A press (0x0048b6b0) steps back on the
left arrow, forward on the right one, or forward anywhere when +0x168 is set
(+0x94); the screen hears of it either way. Stepping (+0xa4, 0x0048b790)
wraps when +0x170 (always 1 here), else stops at the ends. The first choice
added is selected at once (0x0048bac0). Rolling over (0x0048b550) lights the
right arrow before the left. +0x90 (0x0048b490) enables the button and its
arrows. After binding (+0x88) each kind adds its choices:

| Kind | +0x88 | Choices (message, value) |
|---|---|---|
| ON / OFF | 0x00438ed0 | OFF 0, ON 1 |
| ON / OFF, one in red (0x00438090) | 0x00437fe0, +0xa0 0x00438040 | OFF 0, ON 1; ia_small_green, the alert one (+0x42c) in ia_small_red |
| LOW / MEDIUM / HIGH | 0x00438f10 | LOW 0, MEDIUM 1, HIGH 2 |
| CAMERA | 0x00437f90 | 1st Person 1, 3rd Person (high) 0, 3rd Person (low) 2 |
| RESOLUTION | 0x004394b0 | the device's modes (section 5) |
| VIDEO_DEVICE | 0x004394d0 | Software Render -1 (InstallOptions 0xff00), then each card by name (0xff) |
| TEXTURES | 0x00439550 | 2 MB 6, 4 MB 5, 8 MB 3, MAX 0 (section 5) |
| SOUND_QUALITY | 0x00439570 | HIGH 0 (InstallOptions 0x40000), LOW 2 (0x10000) |

The helpers 0x00438e10 (item or null), 0x00438e50 (value or 0), 0x0048b8f0
(select the first with a value) and 0x0043a710 (select every one with it:
the last stays) sit with the option screens.

**The file list** (0x0048c0e0, vtable 0x005c5cf8), 0x200 bytes: the list of
names the controls' load/save popup, the pilot screen (a subclass whose
destructor 0x00487890 is folded with the popup's), the mech lab
(0x0044b215..) and the pause screen pick from. A ShellButton (its id the
constructor's; +0x158 is its items vector's allocator byte, the id's low
byte) holding the game's **items** (+0x15c, 8-byte {_strdup'd text, value},
freed by 0x0044ed90; the popup appends with 0x0042fbf0) and SIZE (8)
**rows** (+0x16c, made by 0x0048d770 after binding: Texts of 0x4d8 bytes,
vtable 0x005c5c48, 0x0048bfe0, with an object that hears their rollover at
+0x4cc, the item's index +0x4d0 and the list +0x4d4) showing the items from
`top` (+0x180). Its entry (0x0048c410, after Control's read):
SELECTED_FONT and NORMAL_FONT (fonts.zrd names; the selected falls back to
the normal), SPACING (x y: every row moves x, each y further down; 0 12),
OFFSET (x y, the first row from the button), SIZE, ROLLOVER_SOUND and
SELECT_SOUND, BAR ((BITMAP OFFSET) an image under the selected row),
ITEMROLLOVER ((BITMAP OFFSET) under the row the mouse is on, with FONT read
from the list's own entry), UP and DOWN (push buttons, vtables 0x005c5e30 /
0x005c5da0, placed from the list's place and taken out of the dialog: the
list updates them). The list's rectangle (+0x188) is its image's (place
0x0048d2f0), or without one the rows' extent, set on each layout.

Layout (+0xa4, 0x0048cda0): the rows shown, each the item's text (passed as
the printf format: FIX (bug)), SPACING apart; the selected item's row
(+0x17c) is hidden and a row of its own (+0x178, SELECTED_FONT) takes its
place, over the BAR. A press (+0x30, 0x0048d6a0) on a row selects its item
(+0x98, 0x0048d0d0: the selection scrolled into view, the screen told (10,
index) through +0x8c), then the press is shown and the screen told (1, 0);
off the rows it presses an arrow. Each frame (+0x24, 0x0048c910) while the
mouse is in: the row under the cursor lights in FONT's colour (a Text flash,
0x0048c0b0) with the rollover sound (0x0048d3e0), the arrows light, and an
arrow held down repeats after 0.5 s, then every 0.15 s (+0x1f0..+0x1f8,
seconds of frame time); UP is enabled past the first item, DOWN while items
are below the last row; the rows, the selected row, the images and the
arrows update. Scrolling (0x0048d160) keeps `top` within 0..items. Slots
+0x90, +0x94 and +0x9c (may a row light, be chosen, show the rollover image)
answer 1 here; +0xa0 (0x0048d1c0) sets a row's font (by the item's index:
FIX (bug)). The list's +0x2c answers null: Window's routing uses its +0x5c
(0x0048d210: the rectangle or an arrow).

**The game's edit bar** (0x0048b1d0, 0x0048b220): zTwoD's edit bar (twod.md,
The edit bar) whose first label, after binding, follows the bar: set_pos
puts it at the new place plus the label's place after binding (+0x380), an
absolute place, so a bar that moves takes its label that much further;
while it reads its entry (+0x388, 0x0042f860) it answers no rectangle
(0x0042f890) unless FastMenu. The popup's, the pilot screen's and the mech
lab's bars (0x0042ea08, 0x004870fd, 0x0044bae4) are its.

## 3. The credits (0x004289a0, vtable 0x005bd738)

dlgCredits.zrd's CREDITSDIALOG: CANCEL (+0x111ec, BACK above), CREDITS_SCREEN
(+0x113d8, vtable 0x005bd758, a Control), CREDIT_TEXT and CREDIT_LINES. A
float 4.0 at +0x11ad8 is read by nothing; +0x11adc is the clock.

CREDITS_SCREEN's read (0x00429200): RECT ((left top) (right bottom)) from
the control's place; SCROLL_RATE; SCROLLING_TEXT, a list of sections, each a
list of lines (("key" ...) "offset" (dx dy) "font" ("name")). A line's text
is its keys' messages run together; the line is a FontText plus its offset,
copied into the section (0x00429700) and the section into the list
(0x00429a70) as HP STL vectors, so every copy and every growth of the list
copies the lines' GDI fonts (ported as written, `HpVector`). A section is as
tall as its tallest line (text height + dy); the sections stack.

Each frame (0x00429030) the clock grows by the frame time and t = clock /
(sections x SCROLL_RATE); past 1 it starts again. 0x00429120 places every line
at dx + left, dy + y with y = (top - total) t + (1 - t) bottom (truncated):
the names rise from the rectangle's bottom (t = 0) until the last has left
its top (t = 1), and a line shows only wholly inside it. The shipped file
has 246 one-line sections at SCROLL_RATE 1.0: four minutes a pass, at the
same speed at any frame rate (it goes by time).

A line whose "offset" is not two numbers keeps the one read before: the
original reads each into the same stack slot. The shipped "Other Voices"
heading has `(20)`, so it takes the name above's (30 0): indented like a
name with no gap. The port keeps the offset across lines the same way, and
gives that one-number line the headings' (20 20) behind zfix bugs
(`credit_line_offset`; every other line at x 20, 46 headings, is (20 20)).

## 4. The game options (0x004379f0, vtable 0x005bee60)

GAME_OPTION_DIALOG. Read on entering (+0x10, 0x00438100 with 0), written on
ACCEPT:

| Button | Kind | Option (getter / setter, options.md) |
|---|---|---|
| DIFFICULTY | LOW / MEDIUM / HIGH | GameIntensity (0x0040cdf0 / 0x0040cc20) |
| CAMERA | CAMERA | CameraMode (0x0040ce70 / 0x0040ce10) |
| INVINCIBLE | ON red | Invincible (0x0040cf20 / 0x0040ced0) |
| INFINITE_AMMO | ON red | InfiniteAmmo (0x0040cf80 / 0x0040cf30) |
| HEAT_MGMT | OFF red | HeatMgmt (0x0040cfc0 / 0x0040cf90) |
| TORSO_AUTO_RTN | ON / OFF | 0x0040d010 / 0x0040cfd0 |
| THROTTLE_CONST | ON / OFF | 0x0040d060 / 0x0040d020 |
| MW2JJETS | ON / OFF | 0x0040d1e0 / 0x0040d1c0 |
| CMD_INVERT_MOUSE_Y | ON / OFF | MouseAccelY < 0; written -1.3 or 1.3 straight into the entry |

MOUSE SENSITIVITY (Owen, 2026-11-02) is ours, on GRAPHICS & AUDIO among
the volume sliders (below). With it on, INVERT MOUSE Y here sets only Y's
sign and keeps the magnitude whatever the bug-fix switch (the original's
reset to 1.3 stays for a screen without the slider: the probes).

The red choices are the cheats (protection.md: turning them on sets the
cheated flag). ACCEPT (0x00437f60) applies (+0x10 with 1), writes the
settings to the registry (0x00597a60) and calls the state's +0x28; BACK
leaves without applying. GFX_AUDIO_OPTIONS and CONTROLS_CFG (0x004380c0,
0x004380e0) push [0x0060986c] and [0x00609870], also without a press sound.

## 5. The graphics and audio options (0x00438f60, vtable 0x005bf7d8)

GFX_AUDIO_OPTIONDIALOG. MUSICSLIDER and SFXSLIDER (vtables 0x005bffb0,
0x005bff20) are Sliders whose press sets CDVolume (0x0040dab0) or
SoundVolume (0x0040da80) at once; the cycles: SHADOWS (Shadow),
SELF_SHADOWS (ShadowParts), LIGHTING (GfxFlags bit 0), RESOLUTION
(InGameVMode, the mode a mission runs in), VIDEO_DEVICE (HWCardDev),
TEXTURES (TextureMemory), SOUND_QUALITY (SoundLOD), OBJECT_DETAIL
(ObjectLOD) and SPECIAL_EFFECTS (EffectsLevel), the last two shown
inverted (level 0, the most, is HIGH).

**MOUSE SENSITIVITY** (ours, Owen 2026-11-02): a third slider of the same
kind under the volumes, our own record edit (`patch12::apply_ours`, on any
data version): the same record and art (sliderunfilled / sliderfilled, no
new images), the label spaced as the original's ("M O U S E  S E N S I T I V I T Y"),
at the volumes' 25-pixel step: MUSIC and SFX moved up 10 (y 47, 72), ours at
97, VIDEO_DEVICE from 110 to 122. It sets MouseAccelX and Y's magnitude,
0.5 to 3.0 in tenths (Y keeps its sign, the game options' invert box); it
starts from MouseAccelX, the original's 1.3 untouched, so a player who never
moves it plays as the original. Saved as MouseAccelX/Y with the other
settings; a mission takes them at its start, the menus keep 1.3
(CoreState::leave). Bound only where the program turns it on
(`options_sensitivity_slider`): the probes run the original's screens. Not
behind the bug-fix switch: an option whose default is the original's.
`options_mouse_sensitivity_slider_headless`.

**The device** (0x00439a90, and inline when VIDEO_DEVICE is pressed,
0x0043a0d0): RESOLUTION and TEXTURES are rebuilt for it; LIGHTING and
SELF_SHADOWS are enabled for a 3D card, SHADOWS also needs its capability 3
(0x00579b70); the device is selected for a moment to show its own lighting,
effects and shadow settings (the _SW or _HW entries, options.md), then put
back; zModel's and zRender's GfxFlags are pointed at its entry (0x00526750,
0x00557ae0).

**Resolutions** (0x0043a470): the modes from 0x004ab860's range: software
3..5 (320x240 doubled, 640x480), a 3D card 5..7, 7 (1024x768) cut to 6 below
4.5 MB (three frames of 1.5 MB) of free video memory and to 5 below 2.75 MB (three frames of 960,000 bytes). The
message table names 1152x864, 1280x1024 and 1600x1200 (0xfda..0xfdc) and the
list would show them, but no range reaches past 7: they stay registry-only
(community-issues.md). The old choice is kept, pulled into the range.

**Textures** (0x0043a310): software 2 MB, 4 MB and MAX by InstallOptions
0x100, 0x200, 0x400; a card 2, 4 and 8 MB by InstallOptions 1, 2, 4 and its
memory ([0x0072f38c], in MB; 2 if unknown). TextureMemory is selected if
set, else the last one added.

**ACCEPT** (0x00439440) queues the pop, then applies through the state's
screen (+0x10 with 1, 0x004395c0): a device other than the one read on
entering asks "You have changed a setting that requires the game to be
restarted" (0x004ac150, messages 0xf2e / 0xf2f); yes sets HWCardDev,
replaces the state stack with the Exit state (0x00590250(state(4))) and
applies the game options' screen too; no puts the old device back. The rest
is written either way; lighting and both shadows only when HWCardFlag and
InstallOptions 0xff allow a 3D card, else written off.

## 6. The controls screen (0x0042a590, vtable 0x005bd968)

CTL_CFG_DIALOG, 0x16c94 bytes, made with its state (0x0042d9d0). Its +0x10
is empty (0x004a5bc0): the state's enter reads nothing, the lists are filled
by the constructor. Its parts:

| Offset | Name | Class (vtable) | Press (+0x30) |
|---|---|---|---|
| +0x111ec | TITLE_TEXT1..6 | six Texts (0x4cc) | |
| +0x12eb4 | ACCEPT | slide (0x005be268) | 0x0042d960 |
| +0x130a0 | CANCEL | slide (0x005be1c8) | 0x0042d730 |
| +0x1328c | CMD_RESET_TO_MOUSE_BTN | slide (0x005be128) | 0x0042d210 |
| +0x13478 | CMD_RESET_TO_JSTICK_BTN | slide (0x005be088) | 0x0042d2b0 |
| +0x13664 | CMD_COMMAND_BTN | ListBox (0x005bdff0) | toggle; +0x90 0x0042d3a0 |
| +0x13cb4, +0x14304 | CMD_KEYA_BTN, CMD_KEYB_BTN | ListBox (0x005bdf58, 0x005bdec0) | 0x0042d3c0 / 0x0042d450; right 0x0042d3f0 / 0x0042d480; +0x90 0x0042d420 / 0x0042d4a0 |
| +0x14954 | CMD_JOY_BTN | ListBox (0x005bde28) | 0x0042d4d0; right 0x0042d530; +0x90 0x0042d550 |
| +0x14fa4 | CMD_MOUSE_BTN | ListBox (0x005bdd90) | 0x0042d5b0; right 0x0042d5e0; +0x90 0x0042d610 |
| +0x155f4 | CMD_SET_BTN | cycle (0x005bdce8): the groups (+0x88 0x0042ad70) | 0x0042d640 |
| +0x15a18, +0x15b64 | CMD_NEXT_CMD_BTN, CMD_PREV_CMD_BTN | Control (0x005bdc58, 0x005bdbc8) | 0x0042d6f0, 0x0042d710 |
| +0x15cb0, +0x15dfc | CMD_EXPORT_BTN, CMD_IMPORT_BTN | Control (0x005bdb38, 0x005bdaa8) | 0x0042d890 (save), 0x0042d8c0 (load) |
| +0x15f48 | CMD_ASSIGN_AXIS_BTN | slide (0x005bda08) | 0x0042d8f0 |
| +0x16134 | CMD_DESCRIPTION | FontText | |
| +0x165e4 | OPTIONTEXT | the title (0x005bd988); not in the shipped dialog | |
| +0x166a4 | LINES | Image | |
| +0x16760 | | the axis popup's state (0x1c bytes) | |
| +0x1677c | | the load/save popup's state (0x18 bytes) | |
| +0x16794 | CURRENT_FILENAME | FontText | |
| +0x16c44 | | the capture: 0 none, 1 key A, 2 key B, 3 stick, 4 mouse | |
| +0x16c50 | | each stick source's lowest and highest reading (8 pairs) | |
| +0x16c90 | | dirty: the map changed since the file was read or written | |

The constructor builds the parts (the two popups' states make their screens
there, so ASSIGN_AXIS_POPUP is read before CTL_CFG_DIALOG), resets zInput,
reads the dialog with the option screens' BACKGROUND (section 2; with
FastMenu the background stays in the list, unlike theirs), fills the lists
with group 0, selects group 0, shows the file name. The shipped dialog's
PRESS_A_KEY primitive is never bound.

**The lists** (0x0042c160, the group's commands, from controls_cfg's groups;
docs/controls.md): the items are {_strdup'd text, the command}. A command
whose zInput name is empty is left out. Its row: its name message (the
entry's +0x00), key A's and key B's names, then the stick and mouse
columns: for a command with an axis (the throttle, turn, twist and pitch
keys, entry +0x08) bound to a stick axis the stick column shows
"Joystick <X> axis" (0xf5c) and the mouse column its button; bound to a
mouse axis, the mouse column shows "Mouse <X> axis" (0xf5d) and the stick
column its button. Then row 0 is selected. Selecting a row (0x0042c4e0)
shows that row in all five lists, enables PREV (not on row 0), NEXT (not on
the last), ASSIGN_AXIS (only for an axis command), and the description,
message +0x100; a row other than the current one ends any capture. NEXT and
PREV step a row (0x0042d1c0).

**Capture.** A press on a key or button column starts its capture (the
capture mode, zInput reset; the column checked); clicking a row of that
column selects the row and starts it too. Each frame (0x0042be10) the
other columns are unchecked and zInput asked: key A and B take the last
key (0x0051aa00), the stick the first button (0x0051dd20), the mouse the
first button (0x0051baf0). Escape (key 1) cancels; for keys it arrives as
the key, which the bind treats as cancel. The binds (0x0042c670, 0x0042c830,
0x0042c960, 0x0042cb00): a key or button another command has asks
"Reassign Key" / "Reassign Joystick Button" / "Reassign Mouse Button"
(0xf27..0xf2b, with the key's name and both commands'); no leaves it. A key
is taken from the other slot of its holder explicitly, from the same slot by
zInput's bind; a stick button replaces the command's stick axis, a mouse
button its mouse axis (0x0042ce90, 0x0042cfa0). Then the lists are filled
again and the row reselected. A right click unbinds (a bind of key or
button 0). In stick capture the readings of the eight stick sources are
sampled when it starts; the first whose range since then exceeds 1.0 (half
its travel) opens the axis popup with that source. After a mouse button is
bound the mouse column ignores presses for 10 frames ([0x006056f4], counted
down every frame the screen is idle), so the click that bound it does not
start another capture.

**The axis popup** (ASSIGN_AXIS_POPUP, state 0x0042e6a0 "Assign Axis Popup
State", screen 0x0042da80, 0x12194 bytes): MB_OK, MB_CANCEL (the state's
+0x28), CMD_AXIS_CYCLE_BTN (the stick's X, Y, Z, Rx, Ry, Rz and the mouse's
X, Y, Z; 0x0042dd20: 0x00430180 offers the stick's six whatever the stick
has, its sliders never), AXIS_VALUE (a slider) and INVERT_CHECKBOX (labels
right-aligned to their place, 0x0042f920). Entering (0x0042e720) takes the
stick, selects the found source or the axis's own and checks INVERT when
its scale is negative, grabs the screen behind into the (unlisted)
background; leaving frees it. Each frame (0x0042e470) the source's reading
-1..1 shows as 0..1. OK (0x0042e5b0) binds the source to the selected
command's axis (0x0042cca0) with scale 1 (-1 inverted), the offset that makes
the reading now 0, no dead zone; a source another axis has asks "Reassign
Axis" (0xfb6, 0xfb7) and takes it.

**Files and defaults.** 0x0042d0b0 writes or reads `keys\<name>.zrd`
(controls_cfg) and puts its path in ActiveCtlCfg; the file name shows as
"CURRENT CONFIG: <name>" with a `*` while dirty. The two reset buttons load
the default keys (0x00411150) and the mouse's axes (pitch Y, twist X) or the
stick's (0x00410950's), then save them as `keys\DefaultCtlConfig.zrd`
(message 0xfc0) and set UseJoystick. CANCEL with changes resets the
defaults and the stick's or mouse's axes (by whether a stick is present)
and reads the active file again; ACCEPT with changes asks "Save Changes"
(0xfb4): yes opens the load/save popup to save, no leaves with the changes
kept in memory. EXPORT and IMPORT open the popup to save or load.

**The load/save popup** (LOAD_SAVE_POPUP: state 0x0042f230, vtable
0x005be978, named "Assign Axis Popup State" again; screen 0x0042e890,
vtable 0x005be6d8, 0x11cd4 bytes, loaded without drawing): an image of the
screen behind (+0x11c10, the dialog's first object), LOAD_BOX (+0x11b54),
MB_OK (+0x111ec, 0x0042f160), MB_CANCEL (+0x113d8, BACK's 0x00428c60),
EDITBAR (+0x115c4, the game's edit bar, vtable 0x005be7a0) and FILELIST
(+0x11954, the file list, vtable 0x005be6f8). Its +0x00 is Window::update
(0x0042efe0).

The bar refuses `: | \ < > / ? " *` and holds 19 characters (20 bytes); a
click gives it the focus (0x0042f1b0), Enter or Escape takes it away
(0x0042f1a0) and nothing else: neither loads or saves. While it reads its
entry it answers no rectangle unless FastMenu (0x0042f860, 0x0042f890). A
row chosen in the list puts its name in the bar (0x0042f1c0, through the
dialog's +0x115c4).

Entering (0x0042f2b0) copies the mode (+0x11cd0: 1 save, 0 load), grabs
the screen (zVideo's buffer 2) as the backdrop, puts ActiveCtlCfg's file
name in the bar (`_splitpath`) and fills the list (0x0042eff0): every file
FindFirstFileA / FindNextFileA find for `keys\*.zrd` (folders skipped),
by its name without the extension, its place in the listing as its value;
the handle is never closed (FindClose is not even imported). FIX (A): the
listing is `Engine::list_files` (`zutl::list`: the write dir's and the
mounts' `keys`, each name once, in NTFS's order, case aside), as the
controls file's open finds them. Leaving (0x0042f340) frees the grab. OK
(0x0042f160) loads or saves the bar's name through the state's owner
(0x0042f380 -> 0x0042d0b0, `keys\<name>.zrd`, whatever the bar holds, empty
included) and calls the state's +0x28.

## 7. Resolution (for the any-size work, fixes.md Candidates)

The dialogs are 640x480 pixel positions, as the main menu's. Each screen's
background view is the display size when it is made: 640x480 from the main
menu, but the mission's mode when the pause screen makes the option screens
(before the game options switch to mode 5). With the background still (a
mission running, or FastMenu) the view is the source rectangle of the whole
1280x960 image, so on a screen taller than 960 lines or wider than 1280 the
draw reads past the image: exactly the modes above ~1000 lines (1280x1024,
1600x1200) players see crash in the pause screen's Options
(community-issues.md). In the original the switch to 640x480 comes before
the first frame, and the screen-side clip of both blits keeps that draw
inside the image, so the crash itself is likely in a draw before the switch
(FastMenu's still screen is drawn at construction) or in the switch's own
grab of the old screen (0x004ab6b0, zVideo); the port, which has no mode
switch and will draw menus at any size, cuts every source rectangle at its
image (FIX (B), `zrndr::blit16`, fixes.md; `tests/test_menus.cpp`). The
scrolling background's four pieces cover at most twice the image: beyond
2560x1920 it needs tiling further.

The controls screen adds: its lists' rows are SPACING (15) pixels apart from
LIST_OFFSET, with LISTWIDTH and WORDWRAP in pixels; the axis popup draws
message_box at (212, 158) over the screen it grabs on entering (zVideo's
buffer 2 at the display's size, then given to an image that is not drawn,
so only its own parts show); AXIS_VALUE's fill is the bitmap's width in
pixels. At any size the dialog positions need one scale (or a centred
640x480 frame) for the screen and its popups alike, and the grab the
display's real size.

The load/save popup is drawn in place at LOAD_BOX (200, 82) over the
screen it grabs (zVideo's buffer 2, the display's size, as its first
object); the file list's rows are SPACING pixels apart (15 shipped) and its
hit area the rows' text extent in pixels (the fake GDI's in the tests);
the edit bar's caret is as tall as the label's DT_CALCRECT box and drawn
with zRender's 2D line into the 16-bit buffer, pixel for pixel.

The pilot screen (section 10) is PILOTDIALOG's 640x480 places too: the
pilots' rows 56 pixels apart from (81, 129), the games' 14 apart from GAMELIST
at (384, 39) and its arrows' places from it, the rename bar moved to the
chosen row's pixel place (so it follows any scale the rows get), and the main
menu's 1280x960 background drawn the same way as the other menus'. It grabs
nothing of the screen, so only the one scale (or a centred frame) is needed.

## 8. The port

The states are `mech3::OptionsState` and `CreditsState` (menu_states.h);
the program's `GameEngine` (app_menus.cpp) makes them for the main menu's
`construct` calls, a 0x10-byte block of the module memory standing for each
(its handle is its address for the hooks), pushes them for `push_state`,
deletes them on their +0x00, and answers `state_screen` (+0x0c) for them;
the screens are handles as the main menu's (mainmenu.md, The port), with the
screen's real size for the offsets into it. The options are reached through
`mech3::Engine` (the getters and setters, `set_mouse_accel_y`,
`set_in_game_vmode`, `settings_write`), zVideo's devices and memory through
new hooks (0x00579a10, 0x00579a40, 0x00579aa0, 0x00579a70, 0x00579b70), the
app's queue through `pop_state` and `replace_with_exit`. 0x004ab860 is
ported as `Engine::video_mode_range`.

The controls screen (`mech3::ControlsScreen`, controls_menu.h) is made by
`construct(0x0042a590)` with its own handle, so its popups' states are at
handle + 0x16760 and + 0x1677c as in the original: `push_state` with those
handles pushes them, and `vcall` (their +0x28) and `state_screen` find them
(`GameEngine::popup_state_of`). The axis popup's state makes its screen
itself and hands it to the engine (`adopt_screen`); the screen goes with it.
zInput's map and capture, the stick's reading (0x0051dce0, 0x0042fea0), the
controls setup (groups, defaults, the file's read and write), ActiveCtlCfg
and the three-string message box are `mech3::Engine` hooks whose defaults
are the ports (zin, controls_cfg, options). The load/save popup's state
makes its screen (`LoadSavePopupScreen`) and hands it to the engine the same
way; its OK goes through the state's `apply` (0x0042f380) to the controls
screen's `load_save`, and its listing through `Engine::list_files` (FIX (A),
`zutl::list`). The file list and the game's edit bar are `mech3::FileList`
and `ShellEditBar` (menu_widgets.h), zTwoD's edit bar `ztwod::EditBar`
(twod.md, The edit bar); typed characters arrive through zInput's text
callback (the SDL front end's key events, ToAscii'd by zin).

## 9. Testing

`test_menus.py` (30 runs a screen: 90/90 identical, 436,719 calls; 40 a
screen also identical): the original constructor and screen run in the emulator on
`twod_world.py`'s world, plus stubs for every option getter and setter, the
app's push, pop and replace, the restart question, the registry write,
zVideo's device list, names, capabilities and memory, the card memory
callback [0x0072f38c], the GfxFlags binds, the CRT's `_strdup` and
`sprintf`; the state (its +0x28) and the screens reached through states
(+0x10) are fakes whose calls are recorded. Even runs the shipped dialog,
odd runs a synthetic one (buttons missing or moved, arrows and labels
missing, RADIUS / MAXRADIUS; for the credits random SCROLLING_TEXT, RECT and
SCROLL_RATE, 0 included); one in eight with no dialog. FastMenu, the display
size (640x480 to 1600x1200), the main menu's background (there, moving or
still), a running mission, InstallOptions, the devices and every option
vary. The plan: construct, read the options, 30 to 90 frames of mouse input
over the buttons and arrows mixed with presses of every bound button, cycle
and slider, the option screens' +0x10 both ways, the cycles' +0x90, +0x94,
+0xa0, +0xa4, the device change, busy, bake, dirty-all and restore; then a
dump and the deleting destructor. Compared: every call and a dump of the
dialog, its objects and the screen's own state (each cycle's choices, index,
arrows; the credits' sections and lines; the slider values; the entries
written). `test_app_states.py` has seven new cases (the three option enters,
resume, escape, the credits' enter and leave), 40 seeds each: identical.

Breaking the port showed the test catches it, 23 of 24 (12 runs each): the
credits scrolling from the top, a line shown on the rectangle's edge, the
tallest line without its offset, no restart past t = 1, the offset not
carried across lines, RECT not from the control's place; a cycle that stops
at its end, the left arrow winning the rollover, click_steps ignored, no
first selection, the arrows left in the dialog, the alert font on the wrong
choice; inverted Y by the wrong sign, ACCEPT without the registry write, the
camera choices reordered, the background moving in a mission; detail not
inverted, the resolution clamped to the wrong end, 8 MB offered from 4,
shadows without capability 3, the old device not put back when the restart
is refused, lighting written without a card, the slider's reading flag left
set. Missed, and unobservable: after-bind clearing kHidden too (nothing is
hidden at that point). `tests/test_menus.cpp` checks the FIX (B) cut (and
fails with it removed).

`test_ctlscreen.py` (24 runs: 24/24 identical, about 348,000 calls): the
controls screen 0x0042a590 and inside it the axis popup, on the same world
plus stubs for zInput's map (the 0x0051d2xx wrappers; the names a stub's
"C/K/J/B<code>", empty for some commands), its capture (the last key, the
stick's and mouse's buttons), the stick's state and readings (0x0051dce0,
0x0042fea0, random floats), the controls setup (fixed groups of 6..9 rows a
run, their titles, the defaults, the file's read and write), the message
formatting (0x00575fd0) and the message box with its three strings,
UseJoystick, the CRT's `_splitpath`, `sprintf`, `_mkdir`; the outer state
and the axis popup's state +0x28 (its vtable copied with that slot a stub)
are recorded, the shell's enter and leave (0x0048ebf0, 0x0048ec40, around
the popup's halves) are stubs; the load/save popup's screen is built and
destroyed with the screen (`CopyRect` stubbed; its plan is
test_loadsave.py's). The command entries, the axis table and its ready flag, ActiveCtlCfg
and the mouse guard are random and handed to the port. Even runs use the
shipped dialogs, odd runs synthetic ones (buttons and primitives missing,
moved, RADIUS / MAXRADIUS, arrows and labels missing; one in eight without
CTL_CFG_DIALOG). The plan: 40 to 110 frames of mouse input over the lists'
rows, buttons and arrows (so the captures run), mixed with presses of every
bound button, list (left and right) and arrow, row selections, the group
cycle (stepped, then pressed), the four binds with Escape, keys and buttons
and 0, the axis bind, both axis clears, loads and saves, the popup's fields,
enter and leave halves, frames and presses (OK, NO, the cycle, INVERT, the
slider), dirty-all and restore; then a dump and the deleting destructor.
Compared: every call and a dump of both dialogs and their objects, the
lists' items and values, the group and source cycles, the capture mode, the
ranges, dirty, the guard, the slide buttons' ripple, the axis table,
ActiveCtlCfg and the popups' fields.

Breaking the port showed the test catches it, 26 of 26 (8 runs each): the
stick threshold at 0.5, the range's high not sampled, a mouse axis shown as
a stick axis, mouse sources from 10, the description's id, NEXT enabled on
the last row, Escape bound as a key, key B left on the old command, a stick
button keeping the axis, the question's names swapped, the other axis
keeping its source, the bind keeping the dead zone, a load leaving dirty,
the star when clean, CANCEL's twist on mouse Y, the mouse reset turning the
stick on, ACCEPT leaving without saving, a 9-frame guard, the popup ignoring
INVERT, the offset's sign, INVERT from scale > 0, the sliders offered, the
INVERT labels not moved, key A's right click not reselecting, a row change
keeping the capture, the group press listing group 0. `mw3tests`
(`test_program.cpp`) builds the screen through the program's engine at
start and checks its lists, groups, popups (the load/save one's bar and
list bound) and the popup states' handles.

`test_loadsave.py` (24 runs: 24/24 identical, about 148,000 calls; 40 runs
also identical): the
load/save popup's state 0x0042f230 and its screen on `twod_world.py`'s
world, plus stubs for `_strdup`, `strchr`, `_splitpath`, `CopyRect`, a fake
directory behind FindFirstFileA / FindNextFileA (0 to 20 entries, folders
among them, names with no, an upper-case or a second extension), zInput's
text callback 0x0051a9a0 (kept, so the plan can type through it), zRender's
line 0x00562940, the owner's load or save 0x0042f380 and the state's +0x28;
the shell's enter and leave around the state's halves are stubs (the plan
activates the screen as the enter does). Even runs use the shipped dialog,
odd runs a synthetic one (FILELIST's SIZE 0..10, SPACING, OFFSET, POSITION,
an image of its own, BAR, ITEMROLLOVER with or without FONT, the two sounds,
the fonts and the arrows missing or moved; EDITBAR moved or relabelled;
either missing; MB_OK and MB_CANCEL as the option screens' mutations). The
plan: the enter with a listing (again later with others), the leave, 40 to
120 frames of mouse input over the rows, arrows, bar and buttons, with
dwells holding the button down on one spot (the arrows' repeat), presses of
OK, CANCEL, the bar and the list (left and right), typed characters (letters,
the refused ones, Backspace, Delete, the cursor keys, Enter, Escape, 0, a
numeric bar), the bar's text (cut at 19), the base bar's click, the list's
select, scroll, layout, show and hide, dirty-all and restore; then a dump and
the state's destructor. Compared: every call and a dump of the dialog, its
objects, the bar (line, cursor, refused set, focus, caret points, colour and
flags), the list (items, rows, the selected row, the arrows, images, fonts,
sounds, the lit row, the repeat), the grab and the mode. The FIX (bug) of
the empty list's rectangle is applied to the original by a code hook
(0x0048d06d) so the rest compares.

Breaking the port showed the test catches it, 39 of 42 (16 or 24 runs
each): a full line taking one more, Backspace deleting forward, home going
to the end, Backspace not moving, right past the end, nothing refused,
numeric bars refusing `-`, set_text's cursor at 0, the caret at the text's
left, "W" keeping its width, the prefix measure one short, the focus not
clearing the callback, the polyline one segment short, the caret in the
bottom colour; rows without SPACING's x, rows a spacing lower, the selection
scrolled one too far, scrolling to one item less, DOWN enabled at the end,
UP enabled from the second item, the repeat at the delay's pace, the repeat
starting early, the rollover sound from the select slot, leaving the list
keeping the lit row's flash, the selected row not hit, the rectangle's
right edge exclusive, its bottom without the text height, no font fallback,
the arrows not placed from the list, the flash in the bottom colour, both
arrows' rollover in one frame, the rollover image's x offset, the listing's
values from 1, the bar with the whole path, 21 bytes, a choice showing the
first name, OK's mode inverted, the leave keeping the backdrop, `*` allowed.
Missed, and unobservable: the caret's first point (its width is 0), the
list's press telling the screen (0, 0) (the popup's +0x14 is empty), the
reading flag inverted (the popup loads without drawing, so nothing asks
for the rectangle while reading). `test_twod_line.py`: the line 0x00563b80
on 3,000 random lines inside random targets identical, 1,000 lines past
the edges drawing only their pixels on the target (break tests: the error
step at >=, the right clip one past: both caught). `mw3tests` checks the
listing (`controls_file_listing`) and the line's clip.

## 10. The pilot screen (0x00486f40, vtable 0x005c5060)

The main menu's INSTANT ACTION, NEW CAMPAIGN, TRAINING and MULTIPLAYER push
their states (section 1); each one's enter (0x0048a750, 0x0048afd0,
0x0048b140, 0x0049cab0) is the shell's, then the Pilot State's game set
(0x00488ad0 on [0x00609858]: 2, 1, 4, 3), then the Pilot State pushed. The
screen ends with a pop: 2 a pilot accepted, 3 cancelled, and the state below
goes on in its resume (+0x20):

| State | 2 (a pilot) | 3 (cancelled) |
|---|---|---|
| Action (0x0048a7b0) | the pilot's Instant Action selected, the shell's resume (1), the Action screen's reset (0x0048aac0, section 13) | resume (0), pop (1) to the main menu; 4: resume (1) |
| Campaign (0x0048b000) | when movies are on and the newest save is operation 1, mission 1: the "campaign" movie (fmv.zrd); the shell's resume (1). Its next frame (0x0048af20), the movie done, pops itself (0) and replaces the main menu with the briefing (the Load State, state 6) | pop (1) |
| Training (0x0048b170) | resume (1), pop (0), the pilot's training selected, the briefing replaces the menu | resume (0), pop (1) |
| Multiplayer (0x0049cbb0) | resume (1), the network on (0x0047fc40), a lobby pilot made when there is none, its multiplayer game selected, the screens told (0x00494920; not ported) | resume (0), the network off, pop (1); 4: resume (1), 0x0049b660 |

The Action state skips the pilot screen when coming back from a game
([0x00608e47], the app's +0x1bf: the same pilot's Instant Action at once).
The campaign's leave (0x0048afa0) only frees its movie (not the shell's
leave) and its Escape does nothing (0x005ac5c0); the multiplayer's Escape
does nothing either (0x0049bfd0).

**The state** (0x00488910, "Pilot State", vtable 0x005c54e8), 0x14 bytes:
the screen made with it, +0x10 the game (-1 at first). Enter (0x004889a0):
from a DirectPlay lobby (0x00586ce0) the lobby's player becomes the pilot
(0x004788b0 with 0x00588a60's name, when there is no pilot) and the state
pops at once (2); else the screen's game is set, GAMELIST shown (hidden for
training), the training cycles shown only for training with a pilot chosen,
the games listed (0x004882b0), the screen reset (0x00487b30: the chosen pilot
is [0x0060861c], the list filled again), then the shell's enter. Escape
(+0x28, 0x00488990) is the screen's cancel. 0x00488ad0 sets the game and
does the same showing and listing.

**The screen**, 0x13184 bytes, PILOTDIALOG of the main menu's dialog.zrd
tree ([0x00609854] +0x12600: not loaded again, not unloaded), with the main
menu's BACKGROUND image read unchecked (still at the origin when the main
menu's is still), LINES and LINES2; FastMenu bakes the background, the lines,
CANCEL and ACCEPT. Then PILOTLIST, GAMELIST, EDITBAR, RENAMEBAR, DELETE,
RENAME, SCENARIO_LIST, SCENARIO_DESC and SCENARIO_COMPLETE; RENAMEBAR is
hidden, the screen made active, every pilots\ file listed (0x00478560) and
the list filled (0x00488770). Its parts:

| Offset | Name | Class (vtable) | |
|---|---|---|---|
| +0x111ec | CANCEL | slide (0x005c5448), id 0x17 | the edits finished, then cancel: no pilot ([0x0060861c] = 0), pop (3) |
| +0x113d8 | ACCEPT | slide (0x005c53a8), id 0x18 | the edits finished; with a pilot chosen [0x0060861c] = it, pop (2) |
| +0x115c4 | DELETE | ShellButton, id 0x19 | the edits finished, then "Ok to delete pilot: %1?" (Warning, 0x1e0, 0xf26): yes deletes it and its files |
| +0x1171c | RENAME | ShellButton, id 0x1a | unless editing, the rename starts |
| +0x11874 | RENAMEBAR | the game's edit bar (0x005c5278) | Enter (0x00487de0, not with no name) renames, Escape (0x00487e10) cancels, then zInput reset |
| +0x11c04 | EDITBAR | the game's edit bar (0x005c5310) | Enter (0x00487cf0): a pilot of that name made (or the one listed) and chosen; Escape (0x00487d40): with no pilots and no name the screen cancels, else the name is cleared; then zInput reset |
| +0x11f94 | PILOTLIST | file list (0x005c51d0), +0x200 a byte | a row (0x00487e80): the edits finished first, then that name's pilot chosen (none for no row) |
| +0x121a0, +0x1225c | LINES, LINES2 | Image | |
| +0x12318 | GAMELIST | file list (0x005c5128) | a click (0x00488040) is ignored for Instant Action; a row (0x00487f90) chooses a save, for a campaign only |
| +0x12518 | SCENARIO_LIST | cycle (0x005c5080) | the training missions; a choice (0x00487440) shows the same row in the other two and sets the pilot's training mission (its value, within 1..4) |
| +0x1293c, +0x12d60 | SCENARIO_DESC, SCENARIO_COMPLETE | cycles | the mission's text, INCOMPLETE / PASS |
| +0x12198 | | the game (1 campaign, 2 Instant Action, 3 multiplayer, 4 training) | |
| +0x1219c | | the pilot chosen | |

Both bars refuse `: | \ < > / ? " *` and tab and hold 15 characters (16
bytes); a click gives one the focus unless a rename is open (0x00487dc0).
Every press tells the screen (+0x14, 0x004880a0), which acts on the four
buttons' ids and draws everything again (dirty-all) whatever the id. "The
edits finished" (0x00487b90) is an open rename ended (renamed) or a typed
name made (Enter); the buttons act after it.

Each frame (+0x00, 0x00488170), unless a bar is open: Delete (DIK 0xd3)
deletes the chosen pilot as DELETE does, Ctrl+R (0x213) starts the rename.
A rename (0x00487bd0) hides the chosen row, puts RENAMEBAR at the row's place
with its text and gives it the focus; ending it renames the pilot (unless
cancelled or the new-name bar is open), shows the row and lists the pilots
again. A pilot chosen (0x004881e0) has the screen's game selected and the
games listed (0x004882b0), and the new-name bar emptied:

| Game | GAMELIST's label | Lines |
|---|---|---|
| Campaign | "S A V E D  G A M E S" (0x27c) | each campaign's "Op N, Mission M" (0x21d), its index as the value; the newest chosen (so the pilot plays it) |
| Instant Action | "I N S T A N T  A C T I O N" (0x279) | Player, Lancemate, Enemy kills (0x184..0x186), a blank, Attack wins and losses, Defend wins and losses (0x187..0x18a), a blank, Progression Waves (0xdb7) |
| Multiplayer | "M U L T I P L A Y E R" (0x27d) | Kills/Deaths (0x27e) |
| Training | (GAMELIST hidden) | the cycles: Training 1..4's names (0x21e, 0x222, 0x224, 0x226), texts (0x21f, 0x223, 0x225, 0x227) and INCOMPLETE / PASS (0x280, 0x281); the first not passed shown (the first when all are) |

The list (0x00488770) holds every pilot by name, the item's value the
pilot's address (read by nothing; 0 in the port, FIX (A)); the pilot asked
for (or the one chosen) is selected, else the first row; for training the
cycles are shown again and filled. With no pilots at all the new-name bar
takes the focus (when the caller asks: not on the screen's construction).

The screen's destructor (0x004878b0) deletes every pilot but the current
one (0x004785f0), so saves them.

Found: the GAMELIST, PILOTLIST, both bars and SCENARIO_LIST reach the screen
through their owner unchecked, so a PILOTDIALOG without one of them crashes
(the shipped one has them all). DELETE with no pilot chosen (before any
exists, the new-name bar open) formats the name at address 0x18 and crashes:
FIX (bug), nothing happens. Deleting the pilot accepted before leaves
[0x0060861c] dangling (pilot.md: cleared in the port).

**640x480.** The dialog's places are 640x480 pixels, as the main menu's
(section 7): the lists' rows SPACING apart (56 for the pilots, 14 for the
games), the rename bar placed at the chosen row's pixel place, the
background the main menu's 1280x960 image. At any size the screen needs the
same one scale (or a centred 640x480 frame) as the other menus.

**The port.** `mech3::PilotState`, `PilotScreen` and its parts
(pilot_menu.h); the state makes its screen and hands it to the engine
(`adopt_screen`), as the controls' popups. The four game states are
`mech3::GameMenuState`; the program's engine makes them and the Pilot State
for the main menu's `construct` calls (app_menus.cpp), so the buttons open
the pilot screen. What is not ported is an Engine hook (the Action screen
0x0048aac0, the network 0x0047fc40, the multiplayer screens, the lobby's
session and name). The screen sets [0x0060861c] (`mech3::current_pilot()`)
and tells the engine (`pilot_chosen`), which keeps a handle of the pilot at
the original's address for the parts that read it there. The message box
asks through `message_box1`, the records' lines through
`message_format_ints`.

**Testing.** `test_pilotscreen.py` (60 runs: 60/60 identical, about 415,000
calls): pilots made, given saved campaigns (with or without their file),
records and passed training missions, saved and destroyed in the emulator
(the pilot module real, over test_pilot.py's fake file system), then the
Pilot State 0x00488910 and its screen on `twod_world.py`'s world with a fake
main menu holding the dialog.zrd tree; the port runs the same on a real
folder. The plan: the game set, the state's enter before the shell's (the
lobby's path one run in four), 40 to 120 frames of mouse input over the
rows, arrows, bars and buttons, presses of every bound part, characters
typed into the bars (the refused ones, the editing keys, Enter, Escape),
Enter and Escape on both bars, the lists' and the cycle's choices, renames
started and ended, the bars' text, the state's Escape, the screen's reset
and list, dirty-all and restore; Delete and Ctrl+R answered by the script;
one of the four game states made (its unported screens stubs), entered
(pushing the pilot screen, or with the Instant Action flag going straight
on), told 2, 3 or 4, its frames, leave and Escape (the shell's halves stubs
on both sides, the app's replace compared as its queue, the movies and the
network as calls);
then a dump, the state's destructor and a dump of the pilots. Compared: every
call (the message box, pops, messages, formatted lines, the pilot module's
store fill and starting mechs, the lobby), a dump of the dialog and its
objects, both lists (items, rows, the selected row), the bars, the cycles,
the screen's game and pilot, [0x0060861c], every pilot's current game,
campaign and training mission, and every file. Even runs use the shipped
dialog, odd runs a synthetic one (the lists moved, resized, respaced,
without labels; the bars and cycles moved; buttons missing or moved). The
FIX (bug) of the empty list's rectangle (section 9) and the two above are
applied to the original by hooks so the rest compares.

Breaking the port showed the test catches it, 28 of 30 (24 runs
each): ACCEPT popping 3, CANCEL keeping the pilot, the Delete or Ctrl+R key
codes, Enter not making the typed pilot, the row shown while renaming, the
bar at (y, x), no list after a rename, the cursor not erased before the
question, the name kept on a choice, the Instant Action lines' order, the
first save chosen, the first passed mission shown, the mission clamped to 3,
no row selected by default, no focus with no pilots, Escape without the
input reset, tab allowed, 17-byte bars, clicks on the Instant Action list,
the game set without listing, the lobby's pop, the other pilots kept,
renaming to no name, a choice while editing, the cycles shown without a
pilot, the score's numbers swapped, the training label not hidden, no
dirty-all (rename to no name was caught in one series of runs and missed
in two). Missed, and unobservable: RENAME acting while editing (the rename
itself refuses then). The game states, 5 of 7 (32 runs): training entered
as Instant Action (a real slip the test found while it was written: the
state's own enum shadowed the game type), the briefing's state, the
multiplayer page, the network left on, the campaign's pop; missed, as rare
in the plan: the opening movie on another mission, the Instant Action flag
kept.

## 11. The pause screen (0x00492750, vtable 0x005c6110)

Escape in a mission: the Core state's frame (0x00481f70) pushes the Pause
State (0x00608f48), whose enter (0x00493040) hides the cursor, waits a short
busy loop and makes the screen (new 0x11b20, `mech3/pause_menu`). Playtest
#3: without it the program showed the mission's 2D layer as it was, its key
colour all over (the purple).

- The constructor: zSound's snapshot of the sounds playing (0x0056eb10,
  +0x11b18) stopped (0x0056f0a0), the playing sound groups taken away
  (0x005732f0, +0x11b1c), the render target as the main menu's, then a still
  of the screen (0x00579500, buffer 1) at 0, 0 as the first object
  (+0x11a5c). dlgPause.zrd's PAUSEDIALOG, read without FastMenu's bake:
  PAUSETEXT (a marquee, +0x1199c), QUIT, RESUME, OPTIONS, and REPLAY only in
  a campaign or Instant Action (the game's vt+0x2c, asked twice when it is
  not a campaign). A byte at [0x006099c4] would hide and disable OPTIONS,
  but nothing in the exe writes it.
- The buttons are plain SlideButtons (vtable 0x005bd888) with ids 0..3; a
  press reaches the screen's +0x14 (0x00492d70):
  - RESUME (0): the state's +0x28 (the Pause State's escape, pop(6)), the
    snapshot resumed (0x0056f130) and freed (0x0056f190), the groups given
    back ahead of any started since (0x00573300). Escape does the same pop
    without the sound calls: the paused snapshot and the groups stay where
    the screen keeps them until its destructor, which does not free them.
  - QUIT (1): the snapshot and the groups freed (0x0056f190, 0x00573350),
    pop(0), then by the game's kind: a campaign merges back what a lost
    field base cut (0x00434cb0) and resets the lancemates (0x00435240(0)),
    then the Main state replaces the stack (state(2)) and the mission is
    cleaned up (0x00491780(1)); Instant Action fades out to the debriefing's
    Win State (0x0048ef00 on 0x006099a0, 2 s, black) and does no cleanup
    here; training sets the app's reopen flag (0x0047fe00(1): the training
    menu opens again) and goes to Main; any other kind goes to Main.
  - OPTIONS (2): the three option objects made again (0x00438be0,
    0x0043a640, 0x0042d9d0 into [0x00609868..0x00609870]; the Pause State's
    leave deletes them) and the game options pushed.
  - REPLAY (3): pop(0), then 0x0048fa80 on the missions object: unless in
    multiplayer, the mission ended now as lost (+0x55 and +0x56 set, +0x58 the
    delay 0), so its next update goes to the end screen. The loss's voice
    and red flash (0x0048f9e0) do not play.
- The destructor (0x00492ae0) releases the still (0x00517b10) and the members.

FIX (A): after a 3D frame the program's 2D buffer holds only the HUD layer
over its key colour (the GPU has the scene), so the zTwoD host's screen grab
reads the frame shown back from Render3D (`GameEngine::shown_frame`), as the
fade state's grab already did; menus keep the buffer. Tested by
`test_menus.py pause` (the screen against the original: construction,
frames, every press in every game kind, the flag, the destructor; 23 of
23 planted breaks caught) and
`playtest_escape_pauses_the_mission_and_resume_returns_headless` (Escape,
RESUME, Escape again, Escape on the screen; the backdrop from the shown
frame). Not yet: the Pause State's enter and leave in a differential case.

## 12. The mission's quit box (0x0048dfe0, vtable 0x005c5f70)

zTwoD's message box (twod.md, The message box) as the Core state keeps it
for the whole mission: its start (0x00480c70) makes it (new 0x12688, from
dlgMessage.zrd's MESSAGEBOX, drawing deferred) and turns it off (vt+4, 0);
the mission frame updates it (vt+0) and, when the frame asks to stop in a
multiplayer game (the game's kind 3), shows it (0x0048e2d0); the state's
leave deletes it (vt+8). Outside multiplayer it is never shown. Playtest
#21 saw its construction reported as unported at every mission start.

In the port (playtest W44/W46, 2026-11-01): the frame's update (vt+0) of
the hardware path runs inside its 2D pass with the HUD, else nothing drew it
(FIX (A)): an Escape in a match left it up unseen, its mouse button 1
unbound, the mouse's fire gone for the rest of the match. With bug fixes on
(Owen's W46) it does not block: the mouse stays bound, each fresh Escape
toggles it, Y answers OK (the match left), N Cancel, Y's and N's own
commands lifted while it shows; with them off, the exe's (visible now). Its
OK fades to 0x00609848, a second Multi-Exit State (0x00470650 builds it):
the port's `states().multi_exit`, the mission ended, the main menu. The
leave's ENet close now sends the disconnect at once (the others drop the
player then, not ENet's timeout).

- Construction (0x0048dfe0): the message box (0x005a6e60, flag 1), a
  SlideButton at +0x12494 (id -1; never bound, added or used), OK and
  CANCEL shown for good, the other six buttons and the backdrop hidden, the
  MESSAGE text message 0xde1 and the TITLE 7 by id (0x00409bd0), the
  cursor's capture off (0x005a7860 on the Window's +0x10 cursor).
- Shown (0x0048e2d0), when off: on, and the command on mouse button 1
  (0x0051d270(1), kept at +0x12680) unbound while it shows: its button
  (0x0051d230, +0x12684) noted and the command bound to button 0
  (0x0051d2b0), so a click answers the box instead of firing.
- Closed (0x0048e320), when on: the command bound back to its button,
  then off.
- OK (+0x10, 0x0048e390): the network session closed (0x0046d100), the
  app's network off (0x0047fc40(0) on [0x00608c88]), a fade over 2 s to
  the multiplayer side's state 0x00609848 (0x0048ef00, not ported), the
  box's own OK (result 1), then closed. CANCEL (+0x14, 0x0048e3d0): the
  box's own, then closed. The user button (+0x2c, 0x0048e3f0) jumps to the
  box's. The destructor (0x0048e180 / 0x0048e1a0) only runs the members'.

Ported in `quit_box.cpp` (`QuitBox`); GameEngine answers the Core state's
`message_box_new` with one and routes its vt+0 / +4 / +8 and
`message_box_quit_prompt`. Tested by `test_menus.py quit` (construction
from the shipped and mutated boxes, frames, on and off, the prompt and the
close with and without a command on the mouse, every bound button pressed,
the destructor; 60 runs match; 19 of 19 planted breaks caught, once the
dump took in the backdrop, which is not in the dialog's list).

## 13. The Action screen (0x00488bc0, vtable 0x005c5518)

Instant Action's setup, between the pilot screen and the briefing. The
Action State (0x0048a6c0, "Action State", vtable 0x005c5b10) makes it with
itself (new(0x17730)); its enter (0x0048a750) is the shell's, the pilot
screen's game set to 2 and then, back from a game ([0x00608e47], the app's
+0x1bf), the pilot's Instant Action selected, the flag cleared and the
screen reset, or else the pilot screen pushed. Its resume (0x0048a7b0): 2 (a
pilot) selects the pilot's Instant Action, the shell's resume (1) and the
reset; 3 the shell's resume (0) and a pop (1); 4 the shell's resume (1).

**The screen**, ACTIONDIALOG of the main menu's dialog.zrd tree, with the
main menu's BACKGROUND (as the pilot screen's), LINES, LINES1 (not in the
shipped dialog), LINES2, LINES3, ALLY_TITLE (the INSTANT ACTION and ALLIES
titles: one ShellButton with two labels), CANCEL and ACCEPT; FastMenu bakes
those. Then the cycles (section 2's cycle button):

| Offset | Name | Vtable | |
|---|---|---|---|
| +0x111ec | DATABASE_LIST | 0x005c5870 | the world: c1..c4 (0xe33..0xe36: Big Lake and the others), value 0..3 |
| +0x11618 | SCENARIO_LIST | 0x005c5720 | the scenario (table below), value its row; stepping (0x00489390) refills the enemies |
| +0x11a44 | SCENARIO_DESC | 0x005c57c8 | its description, stepped with it |
| +0x11fc8 | ALLY_LIST x5 | 0x005c5918 | 0x434 apart: the commander's mech, the mobile field base, three lancemates |
| +0x134cc | ENEMY_TITLE | 0x005c57c8 | a cycle with no choices |
| +0x138f8 | ENEMY_LIST x6 | 0x005c59c0 | 0x868 apart, each with its ENEMY_CONFIG_LIST (0x005c5a68) at +0x434 |
| +0x16b68 | ENEMY_WAVE | 0x005c5678 | the wave, in Progression only |
| +0x16fa8, +0x17194 | CANCEL, ACCEPT | slide buttons, ids 3 and 4 | |

The five ALLY_LIST, six ENEMY_LIST and six ENEMY_CONFIG_LIST are one control
each in the dialog, bound again and again: each copy is placed at the first
one's place plus its OFFSET (read as two ints, 0x00489580) times its number.
Every cycle of the screen reads its LABEL's `font` and `inactive_font` (the
second defaulting to the first; 0x00489320): a choice of none shows in the
inactive one. The worlds and scenarios get their choices at once (whether
or not the dialog loaded); the enemies' configurations are hidden.

The scenarios (0x005e6fb0, 16 bytes; 0x005e7010 = 6, the first "None"):

| Row | ia | Mode (ActionGame +0x44) | Name, description |
|---|---|---|---|
| 1 | ia1 | 0 | Battle (0x282, 0x283) |
| 2 | ia1 | 1 | Battle Waves (0xdaf, 0xdb0) |
| 3 | ia1 | 2 | Battle Progression (0x284, 0x285) |
| 4 | ia2 | 3 | Attack (0x286, 0x287) |
| 5 | ia3 | 4 | Defend (0x288, 0x289) |

**The reset** (0x0048aac0, on entering): the world and scenario chosen as
kept ([0x00605874], [0x00605878]: the row, the description too); the
commander's list every mech but 7 ("Commander: %1", 0x18b), the field base
off (-1) or on (10), each lancemate none or every mech but 7 ("Lancemate:
%1"); in Progression the waves listed up to the pilot's best (+0x68, at most
"Wave 20") and its wave shown, else every mech for the enemies (below); then
the kept allies' rows ([0x00605820..]) and, in Progression, the wave: the
best or the kept one ([0x0060587c]), whichever is higher, stepped by 0 (so
shown). The kept choices are saved in the pilot's file with its Instant
Action record (pilot.md).

**The enemies.** A wave (0x0048a810, fixed): each enemy the wave's mech or
none (0x005e33a8, the line-up's table), its arrows off, in the inactive
font; its configuration list only the wave's loadout. Custom (0x0048a930):
the first enemy every mech (no none; the player's mech chosen), the others
none and every mech, arrows on, then the kept rows ([0x00605838] the mechs,
[0x00605858] the loadouts). Choosing an enemy's mech (0x00489c10) lists
that mech's loadouts from its definition (0x004846a0; none hides the list),
never a training one; one loadout shows without arrows in the inactive
font. ENEMY_WAVE's step (0x00489710) shows the chosen wave.

**ACCEPT** (0x0048a140): the pilot's ActionGame emptied (+0x7c = 0); the
scenario's mission is looked for (`..\data\<world>\zrdr\aipath\ia<n>` or
`.\zbd\<world>\readeria<n>.zbd`, _access) and without it an earlier row
taken; the mode (+0x44), the world's name (+0x38), the scenario's name
(+0x3c, the objectives' Instant Action header) and description (+0x40); the
field base (+0x48); the commander's mech on the player's slot; each
lancemate added on the player's side; the wave (+0x6c): the chosen one + 1
in Progression, else 1 and each enemy added with a copy of its loadout; the
world (+0x74) and mission (+0x78); the choices kept, the pilot saved, a pop
(0) and the briefing (Replace with the Load State, 6). CANCEL pops (1).
Either button, and any other id, then draws everything again.

Quirks kept: the commander is chosen by the mech's number, not its row
(mech 7 has none), so past 7 the next mech would show; the kept row, set
from the first ACCEPT on, replaces it. With no choice an enemy list reads
as mech 0, so an empty one (at the screen's making) lists the Annihilator's
loadouts. Stepping off a scenario keeps the line-up unless it was None or
Progression, and the test for Progression's wave reads the same list, so
the wave is kept only by ACCEPT. The fallback's reader is looked for without
".zbd" (never there), so only its AI paths count. Fixed (fixes.md): after
the 20th wave won, the reset shows the wave past the table (the strings
after it as mechs) before stepping back to Wave 1; here none. ACCEPT reads
the world's name unchecked (the shipped screen always has one).

**The port.** `mech3::ActionScreen` and its cycles (action_menu.h); the
program's `construct(0x00488bc0)` makes it and `action_open` (the Action
State's enter and resume) resets it. The loadouts come through
`Engine::mech_loadouts` (mech3::mech_dfn), the files through
`Engine::file_exists`. A loadout choice's value is its index (FIX (A): the
exe keeps the Config's address).

**Testing.** `test_actionscreen.py` (40 runs: 40/40 identical, about
595,000 calls): the Action State and its screen made before or after a
pilot (the pilot module real over test_pilot.py's fake file system), each
mech's loadouts made by the exe's own 0x00423d00 (some for training, some
mechs without a definition), the kept choices and the pilot's best wave and
mech at random, _access answering at random; then 30 to 90 frames of mouse
input over the cycles and their arrows, presses, choices (in and out of
range) and steps of every cycle, the wave and custom fills, the reset, the
state's enter and resume, CANCEL and ACCEPT. Compared: every call, the
dialog and its objects, every cycle, the screen's fill state, the kept
choices, the pilot's ActionGame (its line-up with each loadout by name) and
the pilot file's bytes. Even runs use the shipped dialog, odd runs a
synthetic one (the line-up lists moved and re-offset, labels without
`inactive_font`, buttons moved or missing). The fix above is applied to the
original by a hook. 60 runs: 60/60, about 876,000 calls.

Breaking the port showed the test catches it, 34 of 35 (40 runs each): the
inactive font not read, OFFSET's two swapped, the line-up kept leaving
Progression, the description not stepped, the best wave capped at 21, the
enemies not stepped, none in the normal font, none read as mech 0 skipped,
training loadouts listed, the wave's loadout from its first slot, arrows
with one loadout, the wave step one off, 21 waves listed, arrows with one
wave, the copies not offset, the worlds' values from 1, CANCEL popping 0,
the enemies kept in Progression, a wave left choosable or with arrows, the
first enemy offering none, the kept enemies ignored, the commander by row,
mech 7 listed, the field base's value, the kept wave's maximum swapped, no
fallback, the fallback with ".zbd", lancemates on the enemy side, the wave
not plus one, enemies without loadouts, the field base always on, the
mission from the row, the pilot not saved. Missed, and unobservable: the
loadout lists shown at the making (the first enemy step hides or shows each
again).

## 14. Left

- The mech lab's bar (0x0044bae4, vtable 0x005c14f0) and its lists
  (0x0044b215..0x0044bc5c), on the same widgets; the file list's layout with
  a font for the rows it may not light (0x0048ccf0, +0x6cc: a list of the
  mech lab's; the pilot screen's lists use the plain 0x0048cda0). Done
  (checked 2026-10-28): the bar is `LabEditBar` (CONFIG_EDIT, vtable
  0x005c14f0), the lists lab_screen.cpp's, both layouts menu_widgets.cpp's.
- What follows the pilot screen: the multiplayer screens and their network (0x0047fc40,
  0x00494920, 0x0049cb20, 0x0049b660); the campaign and training go on to the
  briefing (the Load State, ported), which reaches the pilot's game through
  the module-memory handle at [0x0060861c] (not yet bridged, pilot.md). The
  bridge is done (the pilot's game through GameEngine's pilot and launch,
  mission-plan items 19 and 20); the multiplayer screens stay open, with
  the network work.
- The menu objects' constructors and destructors run in the program but are
  not in a differential case (their calls are the screen's construct and
  delete, compared through the screens' tests).
