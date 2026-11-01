# SETUP from any source: "Point to your game"

A v1.0 item (Owen, 2026-10-31): the player points at whatever they have and
it just works, with no installing and no unpacking by hand. Ours, not
original code. Planned and agreed with the lead 2026-10-31 (dev 2); built in
sittings, each reported.

## What a player may point at

- an installed copy: its folder, or its Mech3.exe;
- the disc, or the drive it is in;
- a disc image: .iso, or a .cue with its .bin tracks (the CD audio becomes
  the music);
- an archive holding any of those, at any depth: a .zip (Redump's zips of a
  .cue and its .bins; the Internet Archive's zip of an image's zip with box
  scans and metadata beside it; the abandonware sites' zip of an ISO), a .7z,
  a .rar, a .tar and its compressions.

## How (the plan)

1. **The archive layer** (sitting 1, `mech3/source_archive`): libarchive
   3.7.9 with zlib and liblzma (docs/deps.md), forward-only readers. An
   archive on disk opens by its table (zip and 7z list their entries without
   reading the data); an archive inside an archive opens straight from its
   parent's entry, read once, in order, so nothing is unpacked to disk; an
   ISO 9660 image reads as a stream of 2048-byte sectors, a .bin's raw
   2352-byte sectors through `cooked_sectors` (MODE1: the data at byte 16).
   RAR comes from libarchive's own clean-room readers (no unRAR code, so no
   unRAR licence); not encrypted archives, and a multi-volume RAR only with
   every part there.
2. **The map** (sitting 2): the source walked, archives recursed into, to
   find an installed copy (zbd/reader.zbd beside Mech3.exe) or a disc
   (DATA1.HDR and DATA1.CAB at its root, in a folder, an .iso or a cue's
   data track; the cue's FILE names matched inside the archive). **Which
   game**, by content: the zArchive version (MW3 1, Pirate's Moon 2), the
   gamez version (27 and 41), the 28 data files' size and CRC (1.0 or 1.2,
   data_version.h), Mech3.exe's SHA-256, the cab's file list on a disc.
   Pirate's Moon, or its data offered as MW3, is named plainly ("This is
   MechWarrior 3: Pirate's Moon. Its support comes later; MechWarrior 3
   itself is needed here.") and nothing is installed; after v1.0 the same
   map recognises PM for its own install.
3. **The install, one forward pass where it can be** (sitting 3): an
   installed copy's files stream straight into the install folder; a disc
   image streams sector by sector: DATA1.HDR and DATA1.CAB (the
   InstallShield cabinet needs its header first and random access) go to a
   staging folder in the write dir, freed once the cabinet is unpacked (the
   space checked first); VIDEO\ and the cue's audio tracks (music/trackNN.wav)
   stream straight from the archive. Then the existing install as now (the
   cabinet merge, the 1.2 layer for 1.0 data, the movies).
4. **SETUP and the start** (sitting 4): one "POINT TO YOUR GAME" button
   (the system's picker, a file or a folder), with the automatic search
   ("search again", for a moved install) as its first step; the same picker
   when a plain `mw3` start finds no install. Plain words for what was
   found, the version, the music.
5. **Packages and docs** (sitting 5): the three libraries' licences in each
   package, THIRD-PARTY.txt, README, the Windows build.

## Tests

Archives written at test time by libarchive's own writer from small test
files (nothing committed): zip, 7z, a zip inside a zip, an ISO 9660 image, the
same as raw 2352-byte sectors, an ISO inside a 7z, a damaged zip, a file that
is no archive (`test_setup_sources.cpp`). RAR cannot be written by
libarchive (nor by any free tool), so RAR is checked only against a real
.rar when one is at hand. Later sittings add the installed copy, the
disc-in-an-archive shapes, Pirate's Moon refused, and the real backups' MW3
zip (a long run).

## Built

1. Sitting 1 (2026-10-31): the pins and their signatures (docs/deps.md),
   `build_deps.sh` building the three static on the Mac (universal, checked)
   and Linux, `tools\windows\build_deps_archive.cmd` for the PC (written; to
   be run), CMakeLists.txt finding libarchive (the deps prefix, `deps\archive`,
   or Homebrew's) and defining MW3_HAVE_LIBARCHIVE, `mech3/source_archive`
   (ArchiveReader, cooked_sectors), `test_setup_sources.cpp` (4 tests; the
   raw sectors' offset broken: the ISO test fails).
