# MechWarrior 3 Rebuild

An unofficial fan rebuild of **MechWarrior 3** (Zipper Interactive and
MicroProse, 1999) for today's computers: Windows 10 and 11, macOS and Linux,
with no Wine, DirectX wrappers, DDrawCompat or dgVoodoo.

It is not a remake. The original `Mech3.exe` (patch 1.2) was taken apart and
its code rewritten function by function in C++, most of it checked against
the original running in an emulator. The game plays the way it always did,
minus the bugs and the problems it has on modern machines. It is a personal
learning project: free, not sold, and not affiliated with or endorsed by
Microsoft or MicroProse.

**No game files are included.** You need your own copy of MechWarrior 3.

> This is a playtest. Expect bugs, and please [report them](#reporting-a-bug).

## What you need

- **Your own MechWarrior 3**, any of these:
  - an installed copy, version 1.2 or 1.0 (the rebuild makes the 1.2
    patch's changes itself as 1.0's data loads, so it plays as 1.2; your
    files are not changed);
  - the original disc;
  - a disc image: an `.iso`, or a `.cue` with its `.bin` files (a `.cue`
    also gives you the music).
- **A computer** with a graphics card that does Direct3D 12 (Windows), Metal
  (Mac) or Vulkan (Linux):
  - Windows 10 or 11, 64-bit;
  - macOS 12 or later, Apple silicon or Intel;
  - Linux, x86-64, with your distribution's Vulkan driver.

Pirate's Moon and multiplayer are not supported yet.

## Download

From the [Releases](../../releases) page:

| | File | |
|---|---|---|
| Windows | `MW3-Rebuild-Setup-<version>.exe` | An installer. A `.zip` of the same files is there too. |
| Mac | `MW3-Rebuild-<version>-mac.dmg` | Drag the app to Applications. |
| Linux | `MW3-Rebuild-<version>-x86_64.AppImage` | Make it executable (`chmod +x`) and run it. |

**On a Mac, the first time:** the app is not signed by Apple, so macOS stops
it the first time you open it. Right-click (or Control-click) the app, choose
**Open**, then **Open** again. Or, after macOS has blocked it once, go to
**System Settings > Privacy & Security** and click **Open Anyway**. macOS
remembers your choice after that.

**On Windows**, SmartScreen may warn about an unknown publisher: click
**More info**, then **Run anyway**.

## The first start

The rebuild opens on its launcher.

- **It looks for your MechWarrior 3 install** in the usual places: where the
  original installer put it, `Program Files`, `C:\Games`, and on Linux and
  the Mac a Wine, Proton or Lutris copy. If it finds one, it says which
  version, and **MECHWARRIOR 3** starts the game.
- **If it finds none**, or you want to change something, **SETUP** has:
  - **Find my installed MechWarrior 3**: pick the folder yourself.
  - **Install from my disc or disc image**: copies the game from the disc
    or an `.iso`/`.cue` into a folder of the rebuild's own. No Windows and
    no original installer needed.
  - **Apply the 1.2 patch**: only for game data of another version than
    1.0 or 1.2 (or a mod). Give it the official patch's `.zip`; your install
    is not changed.
  - **Add the music from my disc image**: see below.
- On the first start it also tells you anything worth knowing about your
  copy (no music yet, or game files of an unknown version).
- **The movies** play from your own copy. Where they cannot be played
  directly, the rebuild converts them once, the first time (about a minute).

Your MechWarrior 3 files are only ever read, never changed.

## Music

The original played its music from the CD's audio tracks, which an
installed copy does not have. Two ways to get it back:

- With a disc image (a `.cue` and its `.bin`): **SETUP > Add the music from
  my disc image**.
- Your own rip: put the tracks in the rebuild's `music` folder (below) as
  `track02.ogg`, `track03.ogg` and so on (or `.flac`, `.wav`, `.mp3`),
  numbered as on the CD.

## Playing

- The game runs fullscreen at your desktop's resolution, widescreen
  included. **Alt+Enter** switches between fullscreen and a window.
- In a window, press and release **Ctrl+Alt** to free the mouse; click in
  the window to take it back.
- Gamepads work as they did on Windows 98, and force feedback becomes rumble.
- On a widescreen the menus use the whole width too. For the original's 4:3
  menus, put `widemenus=0` in `mw3.cfg` (or start with `--no-wide-menus`).

### Bug fixes, and playing exactly like the original

The rebuild fixes the original's bugs where the intent is clear: things that
ran faster on a fast computer, the mobile field bases that would not come
back, lancemates walking into walls, and more. Quirks that players know and
plan around are kept. Every change is listed in plain words in
[CHANGES.md](CHANGES.md).

To play exactly as the original did, turn the fixes off: start with
`--original`, or put the line `bugfixes=0` in `mw3.cfg` (in the folder
below). The changes the game needs to run on a modern computer stay on.

## Where your saves and settings go

| | Folder |
|---|---|
| Windows | `%APPDATA%\MicroProse\MechWarrior 3\` |
| Mac | `~/Library/Application Support/MicroProse/MechWarrior 3/` |
| Linux | `~/.local/share/MicroProse/MechWarrior 3/` |

It holds `mw3.cfg`, your pilots and saves, the settings, the converted movies
and the music, and `mw3.log`. Uninstalling the rebuild keeps it; deleting it
starts you over.

## Reporting a bug

Open an [issue](../../issues) and include:

- what you did and what happened (a screenshot helps);
- the mission, and whether it happens with `--original` too;
- your system (Windows, Mac or Linux, and the version);
- **`mw3.log`** from the folder above. It is written new at every start and
  the one before is kept as `mw3.old.log`, so if you started the game again
  after a crash, send `mw3.old.log`.

## Hidden mechanics

Taking the game apart turned up hundreds of hidden mechanics, quirks and
bugs, collected for players in [docs/discoveries.md](docs/discoveries.md).
A few:

- Lasers never cause critical hits, and criticals only happen once a part's
  armor is down to 15%.
- Ammo criticals don't explode: they just empty a bin. CASE in a side torso
  does nothing at all, and the stock Mad Cat carries both of its CASE there.
- Ultra autocannons never jam; they simply fire twice as fast.
- TAG works, but the missiles home on the spot you marked, not the target,
  and take 7 seconds to arrive.
- Pilot files track hidden Instant Action and multiplayer stats that the
  game never shows.

## Building from source

See [docs/building.md](docs/building.md). How the code is organised (the
shared engine, MechWarrior 3, Pirate's Moon and the launcher):
[docs/games-layout.md](docs/games-layout.md).

## Credits and licence

- **MechWarrior 3** was made by Zipper Interactive and published by
  MicroProse in 1999. All the game's content is theirs and comes from your
  own copy.
- The rebuild is by Owen Morgan, under the [MIT licence](LICENSE). The
  original's code was studied with Ghidra and
  [REA](https://github.com/morluto/rea), the data formats cross-checked with
  [mech3ax](https://github.com/TerranMechworks/mech3ax), and the port checked
  against the original in the [Unicorn](https://www.unicorn-engine.org/)
  emulator.
- It runs on [SDL3](https://www.libsdl.org/) (with SDL3_ttf, FreeType and
  SDL3_sound), and converts the movies with a minimal LGPL build of
  [FFmpeg](https://ffmpeg.org/). Each download lists them in
  `THIRD-PARTY.txt`, with their own licence files.
- The launcher can also start **MechWarrior (1989)** through
  [Elthial's SDL port](https://github.com/Elthial/Mechwarrior-sdl-port), if
  you have that installed beside it.

MechWarrior is a trademark of Microsoft. This project is not affiliated with
or endorsed by Microsoft, MicroProse or Zipper Interactive.
