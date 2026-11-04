**A small fix release for 1.0.1: lights made during play work again.**
Install it over 1.0 or 1.0.1: your pilots, saves and settings are kept.

## Fixed

- **Lights made during play.** A rebuild bug had one lighting test backwards:
  a light the game creates during a mission was switched off for the parts
  of the world it reaches and on for the parts it doesn't. That covers lights
  such as jump jet flames and weapon fire. A mech now glows when its jets
  fire or its guns flash, as the original's code does. If you were used to
  1.0.1, fights and jumps will look a little brighter.

## Still known

- **macOS: window tools can delay the mouse button by about a second**
  (Magnet and similar). Quit the tool while you play, or exclude
  MechWarrior 3 in it.
- **Jump jets lighting up water (Op 3, Mission 5):** one report, not
  reproduced. This fix touches the same lights, but whether it changes that
  report isn't known yet; a screenshot and `mw3.log` help.
- **APCs leaving the ground** near the town in Op 1, Mission 2, on very
  fast machines: being worked on.
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
