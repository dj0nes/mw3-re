# MechWarrior 3 Rebuild 1.0

The text of the v1.0 GitHub release (between the rules below); the
packages were built from the tag v1.0 (docs/release.md).

---

**An unofficial fan rebuild of MechWarrior 3 for today's computers:
Windows 10 and 11, macOS and Linux.** The original game's program
(patch 1.2) was taken apart and rewritten function by function in C++,
most of it checked against the original running in an emulator. It plays
the way MechWarrior 3 always did, minus the bugs and the problems it has
on modern machines, with no Wine, DirectX wrappers, DDrawCompat or
dgVoodoo. It is a personal learning project: free, not sold, and not
affiliated with or endorsed by Microsoft or MicroProse. MechWarrior is a
trademark of Microsoft.

**No game files are included. You need your own MechWarrior 3**: an
installed copy (1.0 or 1.2), the disc, or a disc image (.iso or .cue/.bin, or a zip
of one). If you no longer have yours, you can find a copy on the internet.

## What 1.0 is

1.0 is MechWarrior 3 complete: the whole single-player game (the
campaign, Instant Action, training, the mech lab, the briefings, the HUD
and the movies) and multiplayer, by the original's own logic. Pirate's
Moon, the expansion, is next, as a game of its own beside this one.

## Getting started

| | Download | |
|---|---|---|
| Windows | `MW3-Rebuild-Setup-v1.0.exe` | An installer (a `.zip` of the same files is there too). |
| Mac | `MW3-Rebuild-v1.0-mac.dmg` | Drag the app to Applications. |
| Linux | `MW3-Rebuild-v1.0-x86_64.AppImage` | Make it executable and run it. Untested: built and checked under WSL, not playtested. |

Start **MechWarrior 3**. It finds your install by itself; if it cannot,
it asks for your game: point it at the disc image or zip and it takes it
from there.
The Mac app is not signed by Apple. The first time macOS says
"MechWarrior 3 Not Opened": click **Done**, then on macOS 15 and later open
**System Settings > Privacy & Security**, click **Open Anyway** and then
**Open**; on macOS 12 to 14, right-click the app and choose **Open**. Windows SmartScreen may warn about an unknown publisher:
**More info**, then **Run anyway**. The README has the rest.

## New since the playtest builds

- **Multiplayer.** The original's network games, up to 8 players, over
  the internet or a local network, Windows, Mac and Linux together. Games
  on your local network show up by themselves; join any other by its
  address. Hosting opens the game's port (UDP 27930) on your router by
  itself when the router allows it, shows the address to give your
  friends with a click to copy, and says in plain words what to do when
  the internet cannot reach you (a port to forward, a second router, or a
  provider that blocks hosting). The in-game chat, the briefing, the way
  back to the lobby, teams and the scores all work as they did.
  Escape in a match shows the quit box the original never drew, without
  stopping play; a player who leaves disappears for the others at once;
  a match keeps running in the background.
- **Mac and Linux downloads**, beside Windows: a disc image for the Mac,
  an AppImage for Linux.
- **The launcher**, installed as "MechWarrior 3": the rebuild's front
  screen. It starts the game and closes as soon as the game is on screen.
- **Your game from anything you have** (the launcher's GAME FILES): an installed copy, the disc, an
  `.iso`, a `.cue` with its `.bin` files (the music comes too), or the
  file you downloaded (`.zip`, `.7z`, `.rar`, even a zip inside a zip).
  Nothing needs unpacking by hand, and no Windows or original installer
  is needed.
- **1.0 plays as 1.2**: the 1.2 patch's changes are made as 1.0's data
  loads, so a 1.0 copy needs no patch. Your files are not changed.
- **16:9 everywhere**: the missions, menus, mech lab, briefings and
  multiplayer screens use the whole width of a wide screen, and the
  movies keep their shape (the intro and the ending fill the width, the
  4:3 movies are centred). The movies also play smoothly with their
  sound.
- **Mods**: folders of your own game data (textures, sounds, text,
  missions, new worlds and multiplayer maps) in a `mods` folder, turned
  on in `mw3.cfg`. A broken mod file is refused and named in the log
  instead of crashing the game, and `mw3 --check-mod FOLDER` checks a mod
  before you play it. In multiplayer every player needs the same gameplay
  mods, and the game says whose differ. How to make one: docs/modding.md.
- **Mouse sensitivity**, a slider on the Graphics & Audio screen.
- **Cheat codes** in single player (ours, not the original's): Shift+Enter
  in a mission opens a box for six of them. The README lists them.
- **More bug fixes**, all listed in plain words in CHANGES.md. To play
  exactly as the original did: `--original`, or `bugfixes=0` in
  `mw3.cfg`.
- **For the curious**: `--debug-camera` and Shift+F12 in a mission open
  the game editor the original carries but never lets you reach, with
  its free-flying camera.

## Known open items

- **macOS: window tools can delay the mouse button by about a second.**
  Tools that filter mouse clicks to snap or move windows (Magnet and
  similar) hold each press back, so in a mission the mouse fires only when
  you let go; the keyboard is not affected. Quit the tool while you play,
  or add MechWarrior 3 to its excluded apps. (The other half of that Mac
  report, a mouse that never fired, was saved controls losing the mouse's
  fire button; fixed, and controls already saved that way are mended.)
- **Jump jets lighting up water (Op 3, Mission 5).** One report says all
  the water and some other surfaces light up while the jump jets fire. Not
  reproduced on Windows or the Mac. If you see it, a screenshot and
  `mw3.log` help, with whether you were in the cockpit or the outside view.
- Pirate's Moon is not supported yet.

## Reporting bugs

Open an issue on the project's GitHub page. Please attach `mw3.log` from
the settings folder (the README says where it is on each system), or
`mw3.old.log` if you started the game again after the problem; say what
you did, what happened, and whether `--original` does the same.

## Checksums

SHA-256 of each download (also in `SHA256SUMS`):

```
753d58063f71ef244b94b9ebb4f1902d388cd896d9045d2687d2371707e2072c  MW3-Rebuild-Setup-v1.0.exe
160041e14f61ace21deff63b4ef67987817525da41e8a7f80b93c4a6a9f7ef86  mw3-v1.0-win64.zip
f2f7bc6e9944815d3facfff90081436e9ad9512fef111f6185e04e6aff10a823  MW3-Rebuild-v1.0-mac.dmg
855485a2ea54dbb103d281a4fa861d0afdac591a4c5fa05234d5fd3629b9ee10  MW3-Rebuild-v1.0-x86_64.AppImage
```

## Licenses

The rebuild is under the MIT licence. Its libraries (SDL3, SDL3_ttf,
FreeType, SDL3_sound, ENet, miniupnpc, libarchive, zlib, liblzma, miniz,
pl_mpeg, and a
minimal LGPL build of FFmpeg for the movie converter) and their licences
are listed in `THIRD-PARTY.txt` in each download, with their licence
files.

---

## Notes for Owen (not part of the release text)

- The package file names follow the README's Download table; check them
  against what the packaging scripts actually produce, and fill in the
  checksums from those files.
- The library list in Licences should match each package's
  THIRD-PARTY.txt; check it when the packages are built.
- docs/release.md's v1.0 checklist decides when the tag is cut.
