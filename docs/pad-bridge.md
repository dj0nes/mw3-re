# The controller as mouse and keys (PadBridge)

Ours, not the original's. On the Apple TV there is no mouse or keyboard, and
the game's menus are point-and-click while many mission actions are
keyboard-only (controls.md). `PadBridge` (`src/frontend/sdl/pad_bridge.cpp`)
turns an Xbox-style controller into the mouse and key events the game
already reads; the one change on the game's side is its joystick axis
defaults (The sticks, below). It is always on for tvOS
(docs/tvos.md); on the Mac `pad_bridge=1` in `mw3.cfg` turns it on, to try
it with the same controller.

## Menus

| Controller | Does |
|---|---|
| Left stick | Moves the cursor |
| A | Left click at the cursor |
| B | Escape (back) |
| Menu (Start) | Escape |
| Y | Opens the system keyboard (it opens by itself for a text field) |

The cursor's speed is `600 * t^2` window points a second, where `t` is the
stick's tilt past a dead zone of 0.15, scaled to 0..1: slow near the centre,
fast at full tilt. It stays on the game's frame, not the black bars beside
a 4:3 frame on a 16:9 screen. In the menus every joystick button and the
d-pad are kept from the game (`SdlInput::pad_button_mask`), so a click is
not also a joystick press.

## Text fields and the system keyboard

The game's text fields (the pilot's name, save names, the lab's
configurations, the multiplayer names and chat) are its edit bars. One has
the focus while zInput's char callback is set, which `zin::text_focus()`
reports. Each frame the bridge follows it: the field takes the focus, the
system keyboard opens; the field lets go, it closes. Y opens it by hand.

While it is up:

- SDL's hint `SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS` is "1", so the
  controller drives UIKit's keyboard. It is "0" again when the keyboard
  closes, so Menu is the game's again and does not leave the app.
- The bridge gives the game no clicks or keys from the pad.
- B or Menu, let go, means the keyboard was closed with its Back; Enter
  (sent by its Done) closes it too.

SDL on tvOS does not watch the keyboard's own show and hide, so the bridge
keeps that state itself.

The keyboard types into the game as key presses: SDL sends each character
as a key down and up, with Shift for capitals (`SDL_SendKeyboardUnicodeKey`),
plus Backspace and Enter. The bridge adds nothing to the text event, or
every letter would be typed twice. zInput turns a key into its character
with the key's own Shift (`zin::key_char`, FIX (A)): Windows' keyboard state
was as of the key's message, the host's is as of the read, by when the
keyboard's Shift is already up.

On the Apple TV a new pilot confirmed with an empty name is "Pilot"
(`mech3::g_default_pilot_name`), in case the keyboard cannot be used.

## Missions

The sticks drive the mech tank style, and every button and both triggers
give keys of the game's own key table (controls.md). The joystick is the
sticks alone: its buttons and hat are kept from the game, so the stock
joystick table (A fires, LB fire mode, Start centre legs) never doubles a
key.

| Controller | Does | Key |
|---|---|---|
| Left stick up and down | Throttle: centred stops, up ahead, back reverse | (axis) |
| Left stick left and right | Turn the legs | (axis) |
| Right stick left and right | Twist the torso | (axis) |
| Right stick up and down | Aim up and down | (axis) |
| RT | Fire weapon (the selected group) | Space |
| LT | Alpha Strike (every weapon) | Keypad Enter |
| A | Jump jets (held) | J |
| B | Crouch | C |
| X | Zoom (lock the zoom reticule, on and off) | Keypad 0 |
| Y | Next weapon group | ] |
| RB | Target nearest enemy | Ctrl+E |
| View (Back) | Radar map | Shift+R |
| Menu (Start) | Pause menu | Escape |
| LS (press) | Centre the torso to the legs | Shift+/ |
| RS (press) | Target the object under the reticule | Q |
| D-pad up | Next nav point | N |
| D-pad left, right, down (held) | Look left, right, back; let go: forward | Alt+Keypad 4, 6, . then Alt+Keypad 5 |

Hold LB for the less common actions:

| LB + | Does | Key |
|---|---|---|
| A | Get up | G |
| B | Flush coolant | F |
| X | Cycle camera | X |
| Y | Previous weapon group | [ |
| RB | Target next enemy | E |
| RT | Toggle fire mode | \ |
| LT | Toggle MASC | V |
| View | Lancemate command screen | F11 |
| Menu | Startup/shutdown | S |
| LS | Centre the legs to the torso | / |
| RS | Override thermal shutdown | O |
| D-pad up | Previous nav point | Shift+N |
| D-pad down | Damage display | D |
| D-pad left | Clear target | T |
| D-pad right | Radar size | R |

Each action is bound once (`pad_bridge_every_action_once` checks the table,
`mission_bindings()` in pad_bridge.cpp). A trigger is pressed past half way
and let go under a quarter. Each key's down and up go with its button's
press and release, even if LB is let go first, so no Ctrl, Alt or key stays
down. A controller switched off mid-press lets go of every key and click it
was holding.

### The sticks

The game reads its stick through an axis table: throttle, twist, pitch and
turn, each an axis, a scale, an offset and a dead zone (controls.md). Its
stock table is for a 1999 flight stick: throttle on Z as a 0..1 lever
(centred is half ahead), twist on Rz, which a controller lacks, pitch on Y.
With the bridge on, `mech3::set_pad_stick_axes` makes the table's joystick
defaults a controller's, and the UseJoystick option and the controls
screen's joystick reset then write those:

| Axis | Source | Scale |
|---|---|---|
| Throttle | Left stick Y | x -1 (up is ahead) |
| Turn | Left stick X | x -1, as the stock turn |
| Twist | Right stick X | x -1, as the stock twist |
| Pitch | Right stick Y | x -1 (up aims up; `CMD_PITCH_UP` is +1) |

The game's stick throttle sets the throttle target outright once the stick
moves, so a centred stick is a stop and pulled back is reverse; the mech
eases to the target at its own rates. `SdlInput` shapes the controller's
sticks (`pad_sticks`, `shape_stick`): a round dead zone of 0.15 on the left
stick, straight past it; 0.12 on the right, squared past it, so small moves
aim finely and a full push twists at full speed. A controls file saved from
the controls screen still wins, as on any build.

MW3 has no arm lock to turn on: its arm weapons converge on the torso's
reticle already. The nearest command, Toggle Torso Mode (Ctrl+Keypad 0),
freezes the reticle over a target's part (look.md), so it is left off the
controller.

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
keyboard's text, and the masks.
`pad_bridge_opens_the_keyboard_for_a_text_field` checks the keyboard
following the game's focus, Y, B and Done;
`sdl_system_keyboard_types_into_a_text_field` sends what SDL's keyboard
sends through SdlInput and zInput into a field: "Ab1", Backspace, Enter,
each once. `sdl_input_takes_a_late_pad_and_masks_its_buttons`
checks the late controller and the mask with an SDL virtual gamepad.
