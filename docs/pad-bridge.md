# The controller as mouse and keys (PadBridge)

Ours, not the original's. On the Apple TV there is no mouse or keyboard, and
the game's menus are point-and-click while many mission actions are
keyboard-only (controls.md). `PadBridge` (`src/frontend/sdl/pad_bridge.cpp`)
turns an Xbox-style controller into the mouse and key events the game
already reads, so no game code changes. It is always on for tvOS
(docs/tvos.md); on the Mac `pad_bridge=1` in `mw3.cfg` turns it on, to try
it with the same controller.

## Menus

| Controller | Does |
|---|---|
| Left stick | Moves the cursor |
| A | Left click at the cursor |
| B | Escape (back) |
| Menu (Start) | Escape |
| Y | Shows the system keyboard, or hides it (for the pilot's name) |

The cursor's speed is `600 * t^2` window points a second, where `t` is the
stick's tilt past a dead zone of 0.15, scaled to 0..1: slow near the centre,
fast at full tilt. It stays on the game's frame, not the black bars beside
a 4:3 frame on a 16:9 screen. In the menus every joystick button and the
d-pad are kept from the game (`SdlInput::pad_button_mask`), so a click is
not also a joystick press.

The system keyboard (tvOS) types into the game as key presses: SDL sends
each character as a key down and up, with Shift for capitals
(`SDL_SendKeyboardUnicodeKey`). The bridge adds nothing to that text, or
every letter would be typed twice.

## Missions

The sticks, triggers and buttons are the game's joystick, read by `SdlInput`
in DirectInput's XInput layout (input.md): the left stick is X and Y, the
triggers one Z axis, the right stick X and Y rotation, and the buttons
A, B, X, Y, LB, RB, View, Menu, LS, RS are buttons 1 to 10. What each does
is the game's own joystick setup (controls.md; button 1, A, fires).

Menu (Start) is Escape, the pause menu. LB is a hold layer: while it is
held, these buttons give keys and are kept from the joystick.

| Hold LB + | Action | Key |
|---|---|---|
| A | Target nearest enemy | Ctrl+E |
| B | Zoom reticule | Z |
| X | Target object under reticule | Q |
| Y | Cycle camera | X |
| RB | Toggle fire mode | Backslash |
| View (Back) | Radar map | Shift+R |
| Menu (Start) | Startup/shutdown | S |
| LS (press) | Centre legs | / |
| RS (press) | Lancemate command screen | F11 |
| D-pad up | Next nav point | N |
| D-pad down | Get up | G |
| D-pad left | Flush coolant | F |
| D-pad right | MASC | V |

The game's stock joystick table puts fire mode on button 5 (LB) and centre
legs on button 8 (Start); the bridge needs both, so those two actions are
in the layer instead. LB and Start are always kept from the joystick in a
mission.

Each layered key's down and up go with its button's press and release, even
if LB is let go first, so no Ctrl or key stays down. A controller switched
off mid-press lets go of every key and click it was holding.

## A controller that connects late

tvOS connects controllers after the app starts, often after the game has
opened its joystick. With the bridge on, `SdlInput` opens a missing joystick
as a gamepad taken out (it reads centred, as a pad pulled out mid-mission
does), and takes the first gamepad that connects. The Siri Remote is not a
joystick (`SDL_HINT_TV_REMOTE_AS_JOYSTICK` "0"), so it cannot be taken
first.

## Tests

`MW3_TEST_FILTER=pad_bridge ./build/mw3sdltests` runs the bridge's tests
with synthetic gamepad events: the stick's cursor, the frame's edges, A's
click, the LB layer, LB let go first, the pad switched off, the system
keyboard's text, and the masks. `sdl_input_takes_a_late_pad_and_masks_its_buttons`
checks the late controller and the mask with an SDL virtual gamepad.
