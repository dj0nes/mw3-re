# MechWarrior 3 Rebuild 1.0.1

The text of the v1.0.1 GitHub release (between the rules below); the
packages were built from the tag v1.0.1 (docs/release.md).

---

**A fix release for 1.0, from the first day's reports. Thank you to
everyone who posted.** Install it over 1.0: your pilots, saves and
settings are kept.

## Fixed

- **Op 1, Mission 2's missile platforms (unwinnable mission).** The
  second platform could vanish, leaving only its frame and a target
  marker, so "Destroy Missile Platforms" never finished. This is the
  original game's "mission 2 glitch": on frames shorter than about
  6.5 ms (above about 150 fps) the game's easing table said "don't move",
  so a turret whose first aim came on a fast frame stayed pointing
  straight up, and the maths that turns it then collapsed its model to
  zero width while it stayed alive. Turrets now aim at any frame rate,
  and one pointing straight up keeps its shape. The same easing drives
  arm aiming and recoil, vehicle damping and smoke, which now behave the
  same at any frame rate too.
- **Custom key bindings.** Options → Controls could not set KEY A or
  KEY B: the screen never saw the key you pressed (on modern systems the
  key arrives before the screen asks for it). Click a command's KEY A or
  KEY B box and press the new key; Shift, Ctrl and Alt combinations work,
  and "Reassign Key" moves a key another command has. Mouse and joystick
  binding already worked.
- **Flight sticks.** On a stick that is not a gamepad (for example the
  Thrustmaster T.16000M) the twist and throttle reached the game in the
  wrong places: twisting drove the throttle, and the torso could not
  twist, so the reticle would not aim. Each axis now goes where the
  original expected it, read from the stick's own description of itself.
  The game also picks a real stick or pad over other devices that call
  themselves joysticks (some keyboards, headsets and virtual drivers),
  and unplugging one mid-mission no longer leaves it stuck.
  `mw3.log` lists every joystick found, which one was used and its axis
  map; `joystick=` and `joystick_axes=` in `mw3.cfg` choose a device and
  remap its axes by hand (README, Controls).
- **The stick's throttle lever reaches reverse.** The lever now runs from
  full reverse at the bottom to full ahead at the top, with stop at 37.5%
  of its travel (reverse is 0.6x forward speed, so each bit of travel
  changes speed by the same amount both ways) and a small zone held at
  stop. Backspace still flips direction as in the original, and
  `throttle_reverse=0` in `mw3.cfg` keeps the original's stop-to-full.
  The Controls screen's axis box now offers the stick's sliders too, so a
  slider throttle can be bound from the menu.
- **A crash late in a fight** (seen in an Op 4 snow mission, two Daishis
  left). A rebuild bug: looking up where a moving mech's part is (for the
  AI's aim, the target box and the radar) borrowed the 3D drawing's
  matrix stack outside a draw, and could write through its empty base.
  It now uses its own matrix, as the original does. The matrix stack is
  also bounded and reset every frame, and names in `mw3.log` whatever
  unbalances it, so a stray one can no longer crash the game.
- **The end credits** (and the main menu's CREDITS) sit in their box on a
  wide screen; the names stayed at their 4:3 place.
- **The mech lab's "Overwrite?" box** (saving a variant under a name that
  exists) no longer shows over a solid purple screen.

## Still known

- **macOS: window tools can delay the mouse button by about a second**
  (Magnet and similar). Quit the tool while you play, or exclude
  MechWarrior 3 in it.
- **Jump jets lighting up water (Op 3, Mission 5):** one report, not
  reproduced; a screenshot and `mw3.log` help.
- **APCs leaving the ground** near the town in Op 1, Mission 2, on very
  fast machines: being worked on.
- Mech speed in shallow water and on hills is the original's: speed has no
  slope term, and water only slows a mech deeper than a tenth of its height.
- Pirate's Moon is not supported yet.

## Downloads

| | Download | |
|---|---|---|
| Windows | `MW3-Rebuild-Setup-v1.0.1.exe` | An installer (a `.zip` of the same files is there too). |
| Mac | `MW3-Rebuild-v1.0.1-mac.dmg` | Drag the app to Applications. |
| Linux | `MW3-Rebuild-v1.0.1-x86_64.AppImage` | Make it executable and run it. Untested: built and checked under WSL, not playtested. |

You need your own MechWarrior 3 (an installed copy, the disc, or a disc
image); see the 1.0 release for getting started.

## Reporting bugs

Open an issue on the project's GitHub page with `mw3.log` from the
settings folder (the README says where it is).

## Checksums

```
71cc59238c48a85f671b6ca34a8728fc62d06eecb2ba77aa13935324c76d7624  MW3-Rebuild-Setup-v1.0.1.exe
db272bb4aad3a41f34f25a8ca6c84f9db4172d8080192a5f8013a0c876f3b8da  mw3-v1.0.1-win64.zip
b6dd7c89a53f6b415c039d7ea9d378351342954e9b7645ce07783d7fb8042b32  MW3-Rebuild-v1.0.1-mac.dmg
e942f30de575fb3de0e05e2c3f976cfbe4c7020d395450b9037ad61a4c7607d5  MW3-Rebuild-v1.0.1-x86_64.AppImage
```

---
