**Lancemates now find their way to you, and lights made during play work
again.** Install it over 1.0 or 1.0.1: your pilots, saves and settings are
kept.

## Lancemates that walk round things

In the original, a lancemate walks in a straight line at whatever you told
it to reach. If a wall, a river or a cliff is in the way, it stands there or
bumps along it for good. Now, when that straight walk fails, it looks for a
way and takes it.

- **Follow, attack and repair orders.** Lancemates go round walls, buildings
  and parked vehicles, up ramps and along narrow walkways, and across a river
  at its bridge or ford. Told to attack something behind a wall, they find a
  spot to shoot it from.
- **The mission where you meet Epona Rhi (Op 2, Mission 4).** Lancemates could
  be thrown back by their own feet when you were in another part of the map,
  and stood at the tunnel's mouth for ever. In our test runs they now reach
  the works from the start of the mission in about a minute, and cross the
  river in about half a minute.
- **They can now come to you where the original wouldn't let them.** The
  original marked areas of each map for the AI to keep out of. A lancemate
  will now enter one to reach you, if there is a way.
- **Doors and falling buildings.** The map they walk by updates when a door
  opens or a building comes down, so they can use the new way. We tested this
  on a made-up door, not on every real one.
- **On open ground nothing changes.** They walk exactly as in the original.

The honest limits:

- **Sometimes it's slower.** The new way is careful around parked vehicles,
  so in places with mobile HQs parked in the way a lancemate can take the long
  way round: in one of our test routes it took about 70% longer than 1.0.1.
  It does get there.
- **A narrow moving walkway is unproven.** We haven't seen a lancemate follow
  you up onto the conveyor in Op 2, Mission 4, because our test runs don't
  move its pieces. Tell us how it goes.
- **Enemies don't get this.** They keep the original's walk, so the game is no
  harder.
- **Multiplayer doesn't get this.** It stays off there until it has been
  checked in network games.

This is part of **bug fixes**: turn bug fixes off and lancemates walk as they
did in the original.

## Fixed

- **Lights made during play.** A rebuild bug had one lighting test backwards:
  a light the game creates during a mission was switched off for the parts
  of the world it reaches and on for the parts it doesn't. That covers lights
  such as jump jet flames and weapon fire. A mech now glows when its jets
  fire or its guns flash, and gunfire can brighten nearby water for a moment.
  This follows the original's code, but we haven't compared how strong it
  looks with the original running. If you were used to 1.0.1, fights and
  jumps will look brighter; if something looks too strong, a screenshot helps.
- **A mech that walked by itself with no joystick attached.** If you had saved
  controls with the throttle on the stick (a slider from 1.0.1's Controls screen,
  or a changed dead zone) and then played without the stick, the throttle opened
  on its own, and a slider on the torso's axes turned the torso. Now a missing
  stick does nothing, and your binding is kept for when it is back. This is part of bug fixes.
  Checked by automated tests only: we have no joystick to try it on, so reports
  with `mw3.log` are welcome.
- **Undefined colour in a new picture.** A rebuild bug: when the picture
  changed size during a mission, the parts of the screen nothing draws on
  could show random memory until something covered them. They now start
  black. Nobody has reported seeing this; it turned up while working on
  another part of the game.

## Still known

- **macOS: window tools can delay the mouse button by about a second**
  (Magnet and similar). Quit the tool while you play, or exclude
  MechWarrior 3 in it.
- **Jump jets lighting up water (Op 3, Mission 5):** one report, not
  reproduced. In our tests jump jets don't light the water, with this
  release's light fix or without it, so we can't say the fix changes that
  report; a screenshot and `mw3.log` help.
- **APCs leaving the ground** (Op 1, Mission 2 and elsewhere): an APC at top
  speed can hit a steep bank and jump. It happens at ordinary frame rates too,
  and we are still finding out whether the original does the same. Not fixed yet.
- Mech speed in shallow water and on hills is the original's: speed has no
  slope term, and water only slows a mech deeper than a tenth of its height.
- Pirate's Moon is not supported yet.

## Downloads

| | Download | |
|---|---|---|
| Windows | `MW3-Rebuild-Setup-v1.0.2.exe` | An installer (a `.zip` of the same files is there too). |
| Mac | `MW3-Rebuild-v1.0.2-mac.dmg` | Drag the app to Applications. |
| Linux | `MW3-Rebuild-v1.0.2-x86_64.AppImage` | Make it executable and run it. Untested: built and checked under WSL, not playtested. |

You need your own MechWarrior 3 (an installed copy, the disc, or a disc
image); see the 1.0 release for getting started.

## Reporting bugs

Open an issue on the project's GitHub page with `mw3.log` from the
settings folder (the README says where it is).
