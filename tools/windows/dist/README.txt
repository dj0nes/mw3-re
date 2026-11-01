MechWarrior 3 rebuild: playtest build for Windows (64-bit)
==========================================================

What this is
------------
An unofficial fan rebuild of MechWarrior 3's program: the original game's
logic, rewritten to run on modern Windows without DDrawCompat, dgVoodoo or
other helpers. It is a personal learning project, free and not sold, and is
not affiliated with or endorsed by Microsoft or MicroProse. MechWarrior is a
trademark of Microsoft.

This is a playtest build: expect bugs, and please report them (below).

It contains no part of the game. You need your own MechWarrior 3: an
installed copy (version 1.2 is best; 1.0 plays), or the disc or a disc
image. Pirate's Moon is not supported yet.

Installing
----------
Run MW3-Rebuild-Setup.exe and start "MechWarrior 3 Rebuild" from the Start
menu or the desktop. (Or, from the zip: unzip this folder anywhere (not
inside your MechWarrior 3 folder) and run mw3.exe.)

On the first start:
- It looks for your MechWarrior 3 install (where the original installer put
  it, then Program Files\MicroProse\MechWarrior 3 and usual folders such as
  C:\Games\MechWarrior3). If none is found, it asks you to choose Mech3.exe
  in your MechWarrior 3 folder. The folder is remembered. You can also name
  it:  mw3.exe "C:\path\to\MechWarrior 3"
- Only a disc or a disc image (.iso, or .cue with its .bin), nothing
  installed? Install from it with:
      mw3.exe --install-from "D:\"          (the disc's drive)
      mw3.exe --install-from "C:\path\to\MechWarrior3.iso"
  A .cue also gives the music. Add --patch (below) to patch it too.
- It names the data version it found. Version 1.0 plays as 1.2: the
  rebuild makes the 1.2 patch's changes itself as the data loads, without
  changing your files. Only data of another version (or a mod) is told
  about the official patch, which it can read without running its
  installer: start once with
      mw3.exe --patch "C:\path\to\the patch.zip"
  (the ZIP the patch came in, or its unpacked folder).
- The movies play through the Indeo 5 codec in your own install, by the
  small helper mw3indeo.exe. If that cannot play them, mw3import.exe
  converts them once instead, with a progress window (about a minute). The
  game runs without movies if both fail.

No install? Install from the disc
---------------------------------
You do not need the disc's own installer. With the disc in the drive (or a
disc image: a .iso, or a .cue with its .bin files):

    mw3.exe --install-from D:\ --patch "C:\path\to\the 1.2 patch.zip"

copies the game into a folder of its own (in the settings folder below, or
--install-to "C:\Games\MechWarrior 3") and remembers it. --patch is
optional: the disc's 1.0 plays as 1.2 without it. From a .cue, its CD audio becomes the music too.

Music
-----
The original played its music from the CD's audio tracks, which an install
does not have. With a disc image (a .cue and its .bin), this writes the
music where the game finds it:

    mw3import.exe --cue "C:\path\to\MechWarrior3.cue" --music-only

Or put your own rip in the saves folder's music\ folder as track02.ogg,
track03.ogg and so on (or .wav, .flac, .mp3), numbered as the CD's tracks.

Keys and options that differ from the original
-----------------------------------------------
- The game opens fullscreen at your desktop's resolution, widescreen
  included. Alt+Enter switches between fullscreen and a window.
- In a window, press and let go of Ctrl+Alt to free the mouse; click in the
  window to take it back (that click does not fire).
- The rebuild fixes some of the original's bugs (the list is CHANGES.md in
  the project on GitHub). To play exactly as the original did, start with
      mw3.exe --original
  or put the line  bugfixes=0  in mw3.cfg. The fixes the game needs to run
  on a modern PC stay on.
- mw3.exe --windowed starts in a window (windowed=1 in mw3.cfg makes that
  the default).

Saves, settings and the log
---------------------------
Everything the game writes goes to:

    %APPDATA%\MicroProse\MechWarrior 3\

(paste that into Explorer's address bar). It holds mw3.cfg (the install
folder, windowed=), your pilots and saves, the settings, the converted
movies (video\) and music (music\), and mw3.log. Uninstalling the rebuild
keeps this folder. Your MechWarrior 3 install is only read, never changed.
Deleting this folder resets everything.

mw3.log is written new at every start; the start before it is kept as
mw3.old.log, so after a crash the crash's log is mw3.old.log if you have
started the game again since.

Known gaps
----------
- No multiplayer.
- Pirate's Moon is not supported yet.
- Widescreen: missions use the whole width; the menus, the mech lab and the
  briefings keep the original's 4:3 picture, centred. Resizing the window
  in the middle of a mission may put the HUD out of place (we could not
  make that happen here; a report with your window size helps).

Reporting a bug
---------------
Open an issue at https://github.com/telltaleatheist/mw3-re/issues
Please include:
- what you did and what happened (a screenshot helps),
- the mission, and whether --original plays the same way,
- mw3.log from the folder above (mw3.old.log if you started again since).

Licences
--------
The libraries this program uses and their licences are listed in
THIRD-PARTY.txt; their own licence files are in licenses\.
