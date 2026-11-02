# Mods

Ours (Owen, 2026-11-01: "players and hobbyists can add their own game data
without any change to the exe later"; small, "nothing big and dramatic").
With no mods enabled nothing is mounted and nothing in play changes; nothing
is ever written into the install or into a mod.

## A mod (M1, 2026-11-01)

A folder in the settings folder's `mods\` (the write dir: on Windows
`%APPDATA%\MicroProse\MechWarrior 3\mods`, on the Mac `~/Library/Application
Support/MicroProse/MechWarrior 3/mods`), laid out like the install
(`zbd\...`, `video\...`), with a `mod.txt`:

```
name=Hard Rain
version=1.0
author=Someone
for=mw3 1.2
description=Rain on every world.
```

`name` is required; `for`, when given, must start with `mw3` (a Pirate's
Moon mod is left out of MW3). Other keys are ignored, `#` starts a comment.

**Enabling.** `mw3.cfg` in the settings folder, one line a mod, the folder's
name, in order (the first wins where two hold the same file):

```
mod=hardrain
mod=newmaps
```

`mw3 --mod NAME` (repeatable) uses that list for one run instead,
`--no-mods` none, `mw3 --mods` lists the mods found and which are enabled.
At start the log names each enabled mod and every file it replaces or adds;
a name that is no usable mod (no folder, no `mod.txt`, no `name`, another
game) is said and left out.

**Where it sits.** The settings folder's own files first (what the game
saved: a player's files always win), then the enabled mods in order, then
the imported 1.2 patch (`patch12\`), then the install (`zutl::mount_mod`,
assets-layer.md). The world folders a mission adds (`zbd\<world>\`) come
from every mount, the mods' too.

**Game data only.** A mod never serves the player's own files: `pilots\`,
`configs\` (saved loadouts), `keys\` (controls), `saves\`, `mw3.cfg`,
`settings.reg`, the logs and `movie-shapes.txt` (`zutl::mod_may_serve`).
Such files in a mod are listed as ignored.

**Replacing.**

- A whole file: the mod's copy of any game file (an archive such as
  `zbd\c1\gamez.zbd`, `zbd\rmechtex.zbd`, `zbd\reader.zbd`) is read instead.
- One entry of a reader archive, as text (ours, mods only): a loose `.zrd` in
  the mod, in the archive's own folder, is read instead of that archive's
  entry (`zbd\fmv.zrd` for `zbd\reader.zbd`'s fmv.zrd; `zbd\c1\world.zrd` for
  the c1 world's reader). The game's own rule (zreader.md) reads a loose file
  only when no archive has the name; inside a mod the loose copy comes first.
  Each such file is named in the log; one that does not parse leaves the
  archive's entry in use.
- mech3ax (TerranMechworks, `unzbd` / `rezbd`) unpacks the archives to JSON
  and PNG and packs them again byte for byte: reader, image and texture,
  sound, interp, mechlib, motion, gamez and anim archives. It reads
  Mech3Msg.dll but cannot rebuild it (M2's `strings.txt` is the way to change
  text).

**Multiplayer.** Every player's gameplay data must match. A number over the
enabled mods' gameplay files (`mods_gameplay_hash`) travels in the lobby's
player data, with the names of the mods that count (`mods_identity`, set at
start-up). A joiner whose number differs from the host's is sent back with
"Your mods don't match the host's. The host uses: X. You use: Y.", and LOCK
refuses while any player's differs, naming theirs and ours. Strings,
images, sounds, music and movies do not count, so a translation or a
texture pack never blocks a game; with none on either side nothing changes.
The block's layout: multiplayer-port.md, the mods check.

Tests: `test_mods.cpp` (mod.txt; the folders found; the mounts in order,
the first winning, unusable names left out, nothing served for pilots or
mw3.cfg, writes to the settings folder; a loose fmv.zrd over reader.zbd's,
the archive's own without the mod or with a broken copy; the gameplay
number ignoring textures, sounds, strings and music).

## Planned

- M2 (v1.0): new worlds (`world=` in mod.txt, past the original's nine
  folders), Instant Action maps on new or existing worlds (`ia_world=`),
  multiplayer maps on the declared worlds (the original's own add-on scan,
  `readermp<n>.zbd` with its `mpinfo.zrd`), and `strings.txt` (id=text over
  Mech3Msg.dll).
- M3 (v1.0): bounded readers for the formats that trust their sizes
  (mechlib, motion, anim, the texture libraries, interp, sounds, the
  binary .zrd), a refused file named with its mod; `mw3 --check-mod DIR`.
- After v1.0: a custom campaign, new mechs and weapons (the exe's name
  tables made data), loose `.zan` motions, the launcher's MODS list.
