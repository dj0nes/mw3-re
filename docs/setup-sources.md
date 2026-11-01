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
2. Sitting 2 (2026-10-31): `mech3/source_map` (`map_source`): a folder, an
   exe, a disc or an image on disk (the existing `Disc`), or an archive walked
   forward with archives inside it (to 4 deep). Content decides the game: an
   installed copy's zbd/reader.zbd (its zArchive version: MW3 1, PM 2; 1.0 or
   1.2 by its size and CRC), a disc's DATA1.HDR file list (Pirate's Moon by
   its readersa1.zbd; the version by zbd/reader.zbd's size). A disc image in
   an archive is read as a stream up to its DATA1.HDR (on both discs it lies
   just after DATA1.CAB: MW3 sector 106705, about 209 MB in; PM 96735) and no
   further; raw or 2048-byte sectors told by the sync pattern or CD001; the
   volume label ("MW3", "MW3 PM") kept for the log only. A cue's tracks
   matched to the archive's entries after the walk (Redump's zips put the
   .bins before the .cue); an audio .bin is passed over after its first 17
   sectors (no sync). An installed copy outranks a disc, MW3 outranks PM.
   Tests: the installs on disk (1.0, 1.2 by its exe, PM refused, PM's
   image), an installed copy zipped and zipped again, a disc image built at
   test time around the 1.2 patch's data1.hdr (label "CDROM": content
   decides) in a 7z and as Redump's raw .bin with an audio track and cue in a
   zip, the same .cue and .bins on disk (each audio track's number and INDEX
   01 kept, for the music), PM's disc in a zip in a zip, a zip with no game; the real Redump zip
   (MW3_SETUP_LONG=1: "The MechWarrior 3 disc version 1.0, with its music
   (2 CD tracks)", label MW3, 3 seconds over the network share). Breaks: the
   zArchive version misread (both map tests fail), the cue's audio filter
   inverted (the archive test fails), the on-disk cue read as its path, not
   its text (as first written: the archive test fails).
3. Sitting 3 (2026-10-31): `mech3/source_install` (`stage_source`) and
   `setup::install_from_source`. One forward pass through the archives down
   to the game: an installed copy's files straight into `out_dir`.new; a
   disc's DATA1.HDR, DATA1.CAB and VIDEO\ files into
   `write_dir`/setup-staging (from a folder in an archive, or an image read
   sector by sector from its entry); a cue's audio tracks, found beside the
   image, straight to trackNN.wav from their INDEX 01. The disc then installs
   from the staged folder as any disc does (`install_from_disc`, which takes
   the staged movies with `movies_from` and moves them in before it writes
   mw3-install.txt, last). An installed copy gets its mw3-install.txt last
   too, then `out_dir`.new is renamed: a folder without the marker was never
   finished. An installed copy on disk is used where it is (`check_install`);
   a disc or image on disk goes to `install_from_disc` as before.
   - Safety: an entry's path is refused when absolute, with a drive ("C:",
     "C:x"), or with any ".." part, backslashes read as separators
     (`safe_relative`); then the final path is resolved, links followed, and
     must stay under its folder. Any such entry in the game's folder refuses
     the whole copy; entries outside the game's folder are passed over
     unread.
   - Space: before anything is written, at least the source file's size (plus
     64 MB) free where it goes; each file's size again before it is written.
     A failure, a cancel or a damaged archive removes the staging folder and
     `out_dir`.new.
   - Progress: the outermost file's bytes read (libarchive's filter
     position), to 0.6 of the bar for a disc (the cabinet's unpacking after),
     0.95 for a copy.
   - The real MW3 disc's primary volume descriptor carries its mastering
     tool's marks in fields ISO 9660 reserves (sysid "AL-MPS3"; "%/@", a
     Joliet escape, at byte 88; bytes at 1395), and libarchive's ISO reader
     takes such an image for something else ("Unrecognized archive format").
     `open_iso_stream` clears those fields of each primary descriptor as the
     image streams in (only the descriptors; the files are not touched). The
     test disc carries the same marks.
   - Windows: `NOMINMAX` before archive.h (it brings <windows.h>), and
     `LIBARCHIVE_STATIC` public on mw3core (the tests write archives with
     it). The setup tests pass on the PC (clang-cl, libarchive from
     build_deps_archive.cmd), where the link test is skipped when Windows
     will not make a link without the privilege.
   - Tests: `safe_relative`'s cases; an installed copy whose archive holds
     "../evil.txt", "zbd/..\\..\\evil.txt", "/tmp/...", "C:/..." or "C:..."
     (refused, nothing written); a link in the folder pointing outside;
     installs from a zipped copy (again over it; not over someone's folder),
     a disc's folder in a zip, an .iso in a 7z, Redump's .bins and cue in a
     zip (the music without its pregap); a cancel, a zip cut short and no
     room, each leaving nothing; the real Redump zip installed
     (MW3_SETUP_LONG=1: "MechWarrior 3 1.0 is installed ... (126 files and
     19 movies) ... The disc's 2 music tracks were added too", 51 s for the
     setup set over the network share). Breaks, each failing its test: no
     absolute check, no drive check, backslashes not separators, the
     resolved-path check always true, no space check before the first write,
     no clean-up on failure, no marker, the reserved fields not cleared.
