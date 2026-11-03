# zInput: keyboard, mouse, joystick and the command map

Mech3.exe 1.2, 2026-10-20. The engine's input module
(`C:\zipper\GameZMech3\zInput\`: `zin_kbd.cpp` 0x0051a170..0x0051ab90,
`zin_init.cpp` 0x0051b0a0 and its neighbours to 0x0051b360; the mouse
0x0051b3c0..0x0051baf0, the command map 0x0051bb30..0x0051d490 and the
joystick 0x0051d520..0x0051ddc0 follow with no file name of their own;
0x0051dfa0.. is zMath). Ported in `src/gamez/zinput/` on the program's
DirectInput-shaped `platform::Input` (program.md), checked against the exe
by `tests/differential/test_zinput.py`. Key-name table and strings:
`tests/differential/explore/zin_keynames.py`.

## 0. Start, devices, polls (zin_init.cpp)

`start(window, instance)` 0x0051b0a0 (from the engine start, 0x00480570):
DirectInputCreateA(0x500); the keyboard (0x0051a170), the mouse (0x0051b5f0)
and the joystick (0x0051d520), each a flag byte (0x00638584 keyboard, 85
joystick, 86 mouse: bit 0 present, bit 1 suspended) and a use count
(0x00638588, 8a, 8c); one use each. `poll(callbacks)` 0x0051b360 reads each
device whose flags are exactly 1 and that has users: the mouse, the
joystick, then the keyboard; `callbacks` lets them run the command map's
callbacks. A use count's first user clears that device's state (0x0051b290
keyboard, 0x0051b2c0 joystick, 0x0051b310 mouse: the menus' shell takes the
mouse on entry and lets it go on leave, 0x0051b340). `reset` 0x0051b1c0 clears
all three (the menus call it on every button press). `suspend` 0x0051b050 and
`resume` 0x0051b080 are zGameApp's deactivate and activate: suspend only sets
the flags' bit 1 (DirectInput drops a foreground device by itself); resume
clears each suspended device's state. `stop` 0x0051b170.

Errors go to 0x0051ddc0, which names the DIERR_ code and calls the report
function, empty in the release build (game-loop.md).

## 1. The keyboard (zin_kbd.cpp)

Buffered: GetDeviceData of up to 0x80 events (DIPROP_BUFFERSIZE 0x80,
non-exclusive, background) into 0x00638598. A **key code** is the DIK code
with the modifiers held: 0x100 Alt, 0x200 Ctrl, 0x400 Shift (0x006385a8;
either Ctrl, Shift or Alt key). Codes below 0x7de have a state and a
callback at 0x006385ac (8 bytes each). States: a down event gives 1 (3 when
it was already 1), an up event adds 4. Before each read (0x0051a550) the keys
of the previous read age: released (bit 4) to 0, exactly 1 to 2 (held).
Keys not in the last read keep their state. So `& 3` is "down", `== 1`
"pressed this frame", and a key pressed and let go within one frame reads 5.

A read (0x0051a5d0), per event: a modifier key updates the modifiers; any
other key, with modifiers held, first marks its unmodified code released,
gets the modifier bits written into the event (0x40 Alt, 0x20 Ctrl, 0x10
Shift, so the next ageing finds the same code) and becomes the modified
code; then the state changes, and on a key down the text callback
(0x0063c49c, set by 0x0051a9a0 for the line editors) gets the key's
character (0x0051ab90: ToAscii; the keypad without NumLock gives the
editors' cursor codes 1 home, 2 left, 4 end, 6 right, 0x7f delete). Then the
codes of the read are listed once each (0x006385a0, grown one at a time);
Alt-F4 (0x13e) runs its callback at once whatever else happens, if it has
one and is down; then, when `callbacks` is set and no text callback is,
every listed key that is down runs its callback. Any result but 0 or
DIERR_INPUTLOST (re-acquired) drops the read, DI_BUFFEROVERFLOW too.

`read_key(wait)` 0x0051aa00 takes events one at a time and returns the last
plain or modified key that went down. Quirk: the count it asks for is the
last read's, so after an empty read it asks for none; with `wait` (unused,
the controls screen passes 0) it would never return.

**The menus use raw keys, not commands**: 0x0051a9b0(DIK) is what Escape
(1) and the main menu's hotkeys read: O 0x18, C 0x2e, M 0x32, I 0x17,
T 0x14, Q 0x10, R 0x13 (game-loop.md, Main).

**Key names** (0x0051c6b0): by DIK code, each message `MSG_KEY_<name>` from
Mech3Msg.dll or the name itself (`COMMA`, `NUMPAD7` ...); Left Alt is looked
up as `MSG_KEY_HOME` (falling back to `LMENU`), so it shows as Home. 0x0051c480
prefixes Ctrl-, Alt-, Shift- (`MSG_KEY_CTRL` ..). Joystick buttons
`MSG_JBTN_n` / "Button n" (1..10), mouse buttons Left, Right, Middle.
0x0051aee0 finds a key by its name.

## 2. The mouse

Exclusive and foreground (the app's input mode 5 as the cooperative level,
0x0051bae0; the controls' mode bit 8 makes it background), read with
GetDeviceState into a DIMOUSESTATE. zInput keeps an integrated position on a
screen it is told the size of (0x0051b590, from the options' screen mode,
options.md): 15 dwords at 0x0063d1d8 (0x0051b7b0, `zin::MouseInfo`): x, y,
wheel (500 centred), the same from the centre in -1..1, the last motion
and its share, and the left, right and middle button states (1 pressed, 2
held, 4 released: this read against the last, 0x0051b6e0).

Each read (0x0051b7d0): the counts times the acceleration (0x005ece48/4c,
MouseAccelX/Y, 1.3; cut toward zero; the options' MOUSE SENSITIVITY slider,
ours, sets their magnitude, menus.md; a mission takes them at its start,
the menus 1.3 again from CoreState::leave) moved onto the position, clamped to the
screen; the wheel unscaled and unclamped; with `callbacks`, each button
pressed this read runs its command. Reset (0x0051ba80) forgets the buttons
and zeroes the motion, but not the wheel's: quirk kept, a reset adds the last
wheel count to the wheel position again. 0x0051b440 puts the mouse at a
point from the centre, 0x0051b4f0 at Windows' cursor (windowed missions with
mode 8), 0x0051b560 at the centre; each moves Windows' cursor along
(ClientToScreen, SetCursorPos).

Without a mouse the original crashes at the start: 0x0051b5f0 ignores
CreateDevice's failure and calls through the pointer it did not get. The
port carries on with no mouse (FIX (bug), fixes.md).

## 3. The command map

The game's 0xa7 commands (InitInstance: 0x0051c630(0xa7, "DefaultCmdMap"),
startup.md; the CMD_ names and default keys are set by mech3's controls
setup, 0x00411150: controls.md, Setup). A map (0x3f5c bytes, `zin::CmdMap`) holds per
command one dword of bindings (key1 bits 0-10, key2 11-21, joystick button
22-25, mouse button 26-27; 0x0051beb0), a callback and a name (0x50 bytes),
and reverse tables: key1 and key2 to command (0x7de each), joystick button
(16) and mouse button (4) to command. The bindings are the settings store's
entry named for the map (0x00597d90, type 7, per user), so they are saved
with the other settings (settings.md; `zin::Host::settings_block`, which the
SDL host answers from the store; null keeps them in the map).

Because the map is the saved block, the original saved whatever it held
at each settings write (the exit's 0x0040cb20, the options' ACCEPT
0x00437f60): a quit box (0x0048e2d0) still up at exit saved mouse button 1
unbound, and the mouse never fired again (playtest W40). FIX (bug): the
port writes a kept copy instead (`mech3::controls_keep_saved`,
`controls_write_settings`), taken at the load and when the controls menus
set the map: the screen's ACCEPT, and its load/save box's load, save and
the two resets (all through 0x0042d0b0); CANCEL reads the set back through
the same box. The live map itself is never written.

The live map's changes outside those menus, none of them meant to last:

- the start: the defaults (0x00411150), the registry's read, the active
  controls file (0x004120a0); the kept copy is taken after them;
- the quit box (0x0048e2d0): mouse button 1 while it is up (bug fixes off);
  with them on (ours, W46) the Y and N keys' commands while it is up;
- the game editor's Shift+F12 (ours, debug builds only, `register_app_keys`),
  bound at each start;
- the interp console (0x005a40b0) and the debug keys (0x0040e430) push a
  copy of the map (`push_map`), so the block is not touched;
- `set_callback` and the controls' mode (0x00426650) change callbacks and
  the axis table, not the bindings.

The registry's read fills the bindings only, and without a controls file
the original rebuilt nothing, so the reverse tables (key, joystick and
mouse button to command) stayed the defaults': the quit box's
`mouse_command(1)` named the defaults' command.

MouseAccelX/Y (the options' 0x005f4d2c/30) follow the same rule (Owen,
2026-11-02): the settings write the values kept at the load and when an
options screen applies at its ACCEPT (`settings_keep_mouse`), not the live
ones. CoreState::leave's 1.3 / 1.3 for the menus is zInput's working copy
(0x0051b790 writes 0x005ece48/4c), so the player's value was never touched by
it, in the original or here; the rule guards whatever else might. The port rebuilds them after
the read (FIX (bug), `zin::map_refresh`). Then, ours: a map an older build
saved with the box's unbind (TRIGGER1 with no mouse button, button 1 on no
command) gets button 1 back on TRIGGER1, with a log line
(`mech3::controls_repair_mouse_fire`; fixes.md).

- `bind(cmd, name, key1, key2, joy, mouse)` 0x0051c190 (0x0051d2c0): each
  input taken from the command that had it, the command's old one freed.
- `set_callback(cmd, fn)` 0x0051c2b0 (99 callers): only a command with a
  key keeps a callback; its keys get the shared key callback 0x0051c340
  (which runs whatever command the key is bound to now), the first given
  winning (0x0051a9c0).
- `command(cmd)` 0x0051c370 (0x0051d300): the or of its keys', joystick
  button's and mouse button's states (0 while the map is disabled, +0x3f58).
  PlayerControls reads the mech's commands this way (controls.md).
- `push_map` 0x0051d3a0 / `pop_map` 0x0051d490: a stack of maps; push with
  none copies the current one (the interp console 0x005a40b0 and the
  debug key setup 0x0040e430 push theirs). Each change rebuilds the tables
  and the keys' callbacks (0x0051bca0).

## 4. The joystick

The first attached joystick (EnumDevices), as an IDirectInputDevice2 with
c_dfDIJoystick2; exclusive, foreground with force feedback (DIDC_FORCEFEEDBACK,
0x0051ddb0: the engine start then makes the force-feedback object 0x00433090,
likely), background without. Its setup (0x0051d700): for each of X, Y, Z, RX,
RY, RZ the stick has (GetObjectInfo by offset), the range -1000..1000 (read
back when refused) and a dead zone, 10% on X and Y, none on Z (the
throttle), 25% on the rotations; each axis's centre and scale kept at
0x0063c6fc (0x0051da90). Reads (0x0051dc20): Poll and GetDeviceState
into a DIJOYSTATE2, rotations the stick lacks zeroed, the last one kept as
the previous; with `callbacks`, buttons 1..10 pressed this read run their
commands. Button states as the mouse's (0x0051dcf0). Reset (0x0051dd60)
centres the hats (0xffff) and clears the button bytes 1..10. Quirk kept:
those are buttons 2..11, so button 1, the trigger, keeps its state through a
reset.

## 5. The port

`src/gamez/zinput/`: `zinput.h` (the globals as `zin::State` with their
addresses), `zin_init.cpp`, `zin_kbd.cpp`, `zin_mouse.cpp`, `zin_cmdmap.cpp`,
`zin_joy.cpp`. FIX (A): DirectInput is `platform::Input` (open is
CreateDevice + SetDataFormat + the buffer size; set_cooperative_level;
the joystick's GetCapabilities, GetObjectInfo, DIPROP_RANGE and
DIPROP_DEADZONE), the Win32 calls (ClientToScreen, ScreenToClient,
SetCursorPos, GetClientRect, MapVirtualKeyA, GetKeyboardState, ToAscii)
`zin::Host`. On SDL (`SdlInput`, `SdlZinHost`): an exclusive foreground
mouse is SDL's relative mouse mode while acquired (the cursor hidden and held,
as DirectInput's), lost with the focus (reads give DIERR_INPUTLOST until
zInput takes it back); motion is scaled to the game's 640 pixels across the
window; the joystick's ranges and dead zones are applied to SDL's axes; the
characters come from SDL's keymap with the shift and lock states; the
client area is the 640x480 frame.

`mech3::GameEngine` wires the app's hooks: input_init 0x0051c630,
input_start 0x0051b0a0, input_devices_shutdown 0x0051b170, the activate and
deactivate 0x0051b080 / 0x0051b050, input_poll 0x0051b360, key_state
0x0051a9b0, input_clear 0x0051a300, input_reset 0x0051b1c0, input_capture
/ input_release 0x0051b310 / 0x0051b340, the mouse's centre, position and
acceleration, the options' screen size 0x0051b590, joystick_present
0x0051dc10, force_feedback_present 0x0051ddb0. zTwoD's mouse (0x0051b7b0)
and reset are zInput's.

FIX (bug): a key code past the table (a newer key such as DIK_POWER 0xde ..
DIK_MEDIASELECT 0xed, pressed with Ctrl, Alt and Shift held) wrote past the
key states into the text callback's pointer and the joystick's globals; such
codes are skipped. Key names past the table (0x0051c480 with a code of
0xde..0xff read the joystick's names and the mouse's numbers as names) give
none.

## 6. Testing

`test_zinput.py`: 200 random plans against the exe, the x87 at 24 bits.
Fake DirectInput COM objects in the emulator (IDirectInput's CreateDevice
and EnumDevices; each device's QueryInterface, Release, GetCapabilities,
Get/SetProperty, Acquire, Unacquire, GetDeviceState, GetDeviceData,
SetCooperativeLevel, GetObjectInfo, Poll), the Win32 and CRT imports and
the message lookups stubbed; a fake `platform::Input` and `zin::Host` in
the probe (`tests/probe_zinput.cpp`); both ask one scripted World: devices
present or not, acquires that fail, queued key events (modifiers, presses
and releases in one read, Alt-F4 in pieces), mouse counts and buttons,
joystick states, read errors (DIERR_INPUTLOST, DI_BUFFEROVERFLOW,
DIERR_NOTACQUIRED), axes missing or refusing their range, message keys
with and without text. Steps: start, polls with and without callbacks, key
states, binds, callbacks, command states, names, map pushes and pops, mouse
moves, screens, accelerations, resets and waits, joystick buttons and
resets, suspend and resume, use counts, the text callback, read_key, key
characters and names, shutdown and stop. Compared after every step: the
result, every call out with its arguments (callbacks fired included), and
a dump of the globals (9,000 dwords: flags, every key's state and callback,
the event buffer, the joystick's and the mouse's states, the axis setup, the
map's bindings, callbacks and tables) and the names. Result: 200 plans,
51,830 calls out, identical. Scenarios the original cannot survive are not
played: no mouse, GetKeyboardState failing (it then reads its stack buffer
as it was), names past the tables.

Breaking the port showed the test catches it, 12 of 12: a repeated press
not giving 3, presses not ageing to held, Shift read as Ctrl, the mouse
clamped one pixel wide, a second key not freed when rebound, the right
button's command reading the left, the joystick centre halved, Alt-F4 run
while up, the wheel's reset quirk fixed, an overflowing read kept, the
joystick reset's off-by-one fixed, the first key's callback not set.

In the program (2026-10-20, Owen's click that did not fire): the mouse is
exclusive and foreground, so `SdlInput` refuses it while the window is
not in front. The original's logic recovers by itself: the frame's
OnActivate resumes zInput (0x0051b080), and every mouse read retries the
acquire while `mouse_acquired` is 0 (0x0051b7d0); after a lost read the
next one acquires. Checked on the GPU with SDL events pushed into `mw3
--mission c1m1`: focus lost at start, back 0.8 s later, a left click: the
read is lost once, then acquired; CMD_TRIGGER1 goes pressed and held and
shots fly. mw3tests `mission_c1m1_left_button_fires_after_focus_headless`
plays it headless (a fake exclusive foreground mouse refused until the
window comes to the front): the left button alone puts shots in the air,
hits the Owens and fires the hot group. Nothing in the glue needed
changing. Why the click looked dead in play: frame 40 of c1m1 shot with
and without a click at frame 20 is the same to the pixel while the click's
shots are in flight; the shots (the Mad Cat's first group, its machine
guns) draw nothing in the program yet (weapons' side).

## 7. Gamepads and force feedback on SDL (2026-10-30)

**Gamepads** (FIX (A), `SdlInput`). A device SDL knows as a gamepad (an
XInput pad, a PlayStation or Switch pad) is read in the layout DirectInput
gave an XInput pad on Windows. X and Y are the left stick. Z is the
triggers on one centred axis, the left one up and the right one down. RX
and RY are the right stick; there is no RZ. Buttons 0..9 are A, B, X, Y, LB,
RB, Back, Start, LS and RS, and the d-pad is the hat. SDL's raw order
differs by platform, and its triggers rest at one end. On the raw path the
game's defaults (0x00410950: throttle Z x -0.5 + 0.5, twist RZ x -1)
pinned the twist at full and the throttle at its end. Other sticks stay
on the raw path. Either way an axis the device lacks reads 0, as in
DirectInput's DIJOYSTATE2, and the dead zones are DirectInput's: the travel
past the zone scaled to the whole range. Tests (mw3sdltests, SDL's virtual
devices):
- `sdl_input_virtual_stick_reads_as_directinput`: a flight stick's axes,
  dead zones, hat and buttons;
- `sdl_input_virtual_gamepad_reads_as_directinput_xinput`: the layout.
  Break: the pad read raw, the twist pinned at -1000 and RZ present;
- `sdl_input_virtual_gamepad_through_zinput_drives_the_axes`: through
  zInput's setup and read and the axis table, the throttle half at rest,
  full on the right trigger, stopped on the left; the twist still.

**Force feedback.** The exe's object at [0x00608620] (0x00433090..0x00433c80;
the port's `mech3/force_feedback`, once misnamed the network game and
statistics) is made by the app's start (0x00480570) when zInput's joystick
reports DIDC_FORCEFEEDBACK (0x0051ddb0). It loads force_eff.ifr through
Immersion's IFORCE2.dll, and twelve effects from it by name:

| Effect | Played by | Gain |
|---|---|---|
| Recoil, laser_buzz | a shot, 0x00433630 by the weapon's mech-lab index | the table below |
| Spin_Move | a collision's jolt, 0x004335d0 | its amount (0..1) x 80 |
| impact_force | a hit, 0x00433720 | the force x 100, from the hit's side |
| footfall | each step, 0x004337c0 | the speed x 100 |
| impact_land | a landing, 0x00433800 | the jolt x 100 |
| i_fall | a fall, 0x00433840 | 100 (the file has no i_fall: nothing; FIX (bug) the port takes "impact fall") |
| jets1 | the jump jets on and off, 0x00433870 | |
| Spring | the mission's start, 0x004338a0 | |
| dead_spring, dead_dance | the player's death, after every effect stopped | |
| water | wading, 0x00433930 | |

The 1.0 disc's force_eff.ifr (2936 bytes; 1.2's is 4113) has no dead
spring or dead dance, and its "impact fall" has the parameters of 1.2's
impact land; the 1.0 exe asks for "impact fall" and has no i_fall or
death effects (its strings). 1.2 renamed the landing, added the death's
two and the fall (0x00433840, "i_fall"), and its file added the fall as
"impact fall", so the mismatch came with the patch. On 1.0's data the
port, as the 1.2 exe, finds no landing or death effect. The 1.2 patch's
import brings 1.2's file (assets.md, The 1.2 patch).

The shot's table: the autocannons, LB 2/5/10-X and SRMs Recoil at 60;
the small, medium and large lasers (plain, ER, pulse) laser_buzz at 50, 80,
100; the ER PPC, Gauss, LRMs and Ultra AC/5..20 Recoil at 100; the Streak
SRMs Recoil at 40; the machine gun, Flamer, LB 20-X, NARC, TAG and Ultra
AC/2 nothing. Quirk kept: an effect's Stop (0x00433bc0) stops its first part
once per part, so a compound's second part runs out by itself.

The file (`SdlForceDevice::parse`): "ifpr", a count (18), then each record a
size (from the size field), its name, its type, five bytes and key/value
strings: Magnitude 0..10000; Duration, Period, the attack and fade in
microseconds; InfiniteDuration; a compound's ContainedObjects by ID. The
types are Vector Force, Periodic, Compound, Spring and Damper. The exe's
names are the file's with '_' for ' '.

FIX (A): IFORCE2 is `platform::ForceDevice`, and on SDL `SdlForceDevice`
plays each part as rumble. A Periodic part at 100 ms or less goes on the
fast motor, anything else on the slow one, at Magnitude x gain, for its
Duration (an infinite one until stopped). The conditions (Spring, Damper:
forces on a stick's axes) have no rumble equivalent and play nothing; a
real force-feedback stick through SDL_Haptic could play them later. The
directions are kept but unused, since a rumble has none. `SdlInput` reports
DIDC_FORCEFEEDBACK when SDL says the device rumbles. Tests:
- `sdl_force_device_plays_the_ifr_effects_as_rumble`: the shipped file on
  a virtual pad's Rumble;
- `force_feedback_effects_and_the_games_calls_headless`: every method on
  a recording device; in c1m1 the object made, the Spring, the water and
  the steps. Break: the calls unwired, none.

## 8. The mouse in a window (2026-10-30)

Owen, after trying Alt+Enter: in a window the mouse could not leave it (to
resize it, say). FIX (A), platform only (`SdlInput`, sdl_input.h); the
game's zInput is untouched. The exe's mouse is exclusive (DISCL_EXCLUSIVE |
DISCL_FOREGROUND): DirectInput held and hid the cursor whenever it was
acquired, menus included, which on a 1999 full screen was all there was.
In a shown window (`windowed`: not fullscreen, not hidden):

- **The menus never capture.** The cursor is free; the OS cursor is hidden
  over the window (the menus draw their own) and shows outside it. zInput's
  mouse is put at the cursor each read through the exe's own 0x0051b4f0
  (`mouse_from_cursor`, the window mapped through the fit), with no motion
  counted, so the menu cursor sits under the system's; once the cursor has
  moved, so a screen's opening position stays until then. The clicks are the
  game's as before. The menu cursor then moves at the system's speed, not
  MouseAccel's.
- **A mission captures** (the Core state, `in_mission`). `kReleaseKeys`
  (Ctrl+Alt: both held, then let go, with no other key between; one
  constant) lets go: the OS cursor shows, and the game sees no motion, wheel
  or buttons. A click inside the window takes it back, and neither its press
  nor its release reaches the game (no shot). The Ctrl and Alt keys
  themselves still reach the game as keys.
- **The pause** is a menu: it lets go; back in the mission it is held again.
- **The focus lost** lets go (as before). Ours (Owen, 2026-11-02; it
  changes "a click takes it back"): back, a mission that held the mouse
  when the focus left takes it again at once, no click needed: the cursor
  hidden, relative on. Before, the cursor stayed where it was and the aim
  moved under it until a click. The click that brought the focus back
  (a button down within 250 ms of it, `kActivationNs`) is still not the
  game's. A mouse let go by Ctrl+Alt before the focus left stays let go
  until a click inside. Entering a mission without the focus starts let go.
- The game's own cursor moves (SetCursorPos, `set_cursor_pos`) never move
  the player's cursor in a window.

Fullscreen, and hidden runs (checks, shots), are as before: the mouse held
while acquired, Ctrl+Alt nothing, the focus's return held at once.

`sdl_window_lets_the_mouse_go` drives `SdlInput` through each path: the
menus (absolute, the clicks), a mission, Ctrl+Alt spoiled by a key between
and then whole, the muted motion, wheel and button, the recapturing click
unseen and the next one seen, the pause and back, the focus lost and back
(held again at once, the activating click unseen and the next one seen, a
return with no click, a Ctrl+Alt release kept), and fullscreen. Breaks: the
click passed to the game fails 2 checks; a key between not spoiling the
chord fails 1; no recapture with the focus fails 4, no activation guard 1,
the Ctrl+Alt release not kept 2.

## 9. Which stick, and its axes, on SDL (W54, 2026-11-03)

A Reddit player on v1.0 with a Thrustmaster T.16000M: "the cockpit rotate
and aim recticle are screwed up. I can't aim with my joystick".

**The axes** (FIX (A), `SdlInput::map_axes`). SDL numbers a raw stick's
axes by the ones the device has, packed in DirectInput's order: its
DirectInput backend sorts them by data-format offset, macOS by HID usage,
Linux by ABS code (SDL 3.4.12's source). The exe's c_dfDIJoystick2 keeps
each axis at its own place in DIJOYSTATE2, and the port read SDL's axis k
as DIJOYSTATE2's axis k. The T.16000M (X, Y, Rz twist, slider throttle)
therefore gave its twist as Z, its lever as RX, and nothing as RZ. The
game's defaults with a stick (the controls screen's reset: throttle Z x
-0.5 + 0.5, torso twist RZ, pitch Y, turn X) then drove the throttle with
the twist and never turned the torso, the reticle with it. Each SDL axis
is now put at its DirectInput place, taken from, in turn:
- mw3.cfg's `joystick_axes=` (SDL's axis 0, 1, ... each X, Y, Z, Rx, Ry,
  Rz, S0, S1 or -), the player's own;
- the device's HID report descriptor (SDL_hid, its vendor and product, a
  joystick, gamepad or multi-axis interface only): the Generic Desktop
  usages its inputs carry, X..Rz to their own places, Slider, Dial and Wheel
  to the sliders. Only when their count is SDL's;
- `kKnownSticks`, by vendor and product, each from a dump of the device's
  own descriptor: the T.16000M (044f:b10a; ivyl/input-db's Windows dump);
- SDL's packed order as before.
The log names the source and the map as a `joystick_axes=` line, so a
player can correct it from there. A gamepad keeps DirectInput's XInput
layout (section 7).

No permission prompt: SDL_hid_enumerate lists only joysticks, gamepads and
multi-axis controllers by default (SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS,
"avoiding macOS keyboard capture permission prompts"), and the port opens
only those interfaces; macOS's Input Monitoring covers keyboards, and the
descriptor is an IOKit property. Not tried on a real stick (none attached to
the Mac). On Linux hidraw is usually root-only: the open fails, silently,
and the table or the packed order is used.

**Which device.** The exe took the first attached joystick (EnumDevices;
section 4) and its options have no device list. Ours: mw3.cfg's
`joystick=` (a part of the name, any case), else a gamepad, else a device
with 2 axes and 4 buttons or more, else the first (headsets, RGB keyboards
and other odd HID devices show up as joysticks). Each device found is
logged: name, vendor:product, GUID, gamepad or not, axes, buttons, hats.

**Taken out and back.** DirectInput's last state stayed (an unplugged stick
held its turn and fire). Ours: a stick taken out reads with no buttons and
its stick axes centred; Z and the sliders, the throttles, keep their last
reading (centred, the stick's throttle would jump to half). The same device
back (its GUID) is read again with its map. One plugged in after the start
is taken at the next start, as zInput set up without one (logged).

Tests (mw3sdltests): `sdl_joystick_axes_from_the_t16000m_descriptor` (its
exact descriptor, the slots, the cfg words), `sdl_input_t16000m_twist_reaches_rz`
(a virtual T.16000M by descriptor, by the table, by mw3.cfg),
`sdl_input_picks_the_stick_over_an_odd_device` (an odd device first; by
name), `sdl_input_stick_taken_out_reads_centred`. Breaks: the packed map,
no table entry, the unplugged stick not read, its turn held, always the
first device, one usage an input item: each caught.

## Open

- The controls screen (0x0042be10 ..), which uses read_key and the wait
  functions (mech3's controls setup and file: controls.md, Setup).
- The settings store entry the bindings live in (0x00597d90).
- The static object at 0x0051af60..0x0051b020 (lists at +0x4848) between
  the two files: not traced.
