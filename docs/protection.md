# Copy protection, anti-tamper and anti-cheat in Mech3.exe 1.2

Static analysis of `game/mw3-1.2/Mech3.exe` (the official 1.2 patch, not a
NoCD build) from `analysis-output/mech3.asm`, `decomp/` and `func2file.json`,
compared with the cracked "Exe 1.2 NOCD" from Owen's backup (data only, never
run). Nothing here was run in the emulator except where noted. "Unverified"
marks inference.

Decision (Owen, 2026-10-20): the rebuild removes these traps; they guard
nothing now. Each removal is marked `FIX (trap)` in the code and listed in
`fixes.md`. The cheated flag stays, being a game rule.

## Rebuild status

| Trap | Status |
|---|---|
| 1 Disc check | not ported; will not be (the startup loop and 0x575d40 are left out, movies come from the imported folder) |
| 2 Exe self-check | not ported; will not be (InitInstance and 0x47fed0 skip it) |
| 3 Loadout checksum | removed: `Config::load` calls `validate(false)` (`mech_config.cpp`); the CRC is still written on save. The second compare in 0x4260c0 is not ported yet |
| 4 Multiplayer rules check | left out (multiplayer-port.md piece 8, `FIX (trap)` in `net_send_event`'s ping) |
| CPU-speed divide, line clamp | not ported; FIX (B) notes for when the system-info and software line code are |

## Summary

| # | Trap | Where | Runs | On failure | Remove in rebuild? |
|---|---|---|---|---|---|
| 1 | Disc check: `video\c1.avi` on a CD-ROM drive | 0x482f90 in the main window ctor 0x482d10, scanner 0x575d40 | once, at window creation | "Please insert the MechWarrior 3 CD!" OK/Cancel, loops; Cancel = `ExitProcess(0)` | yes (also FIX (A): no CD drives) |
| 2 | Exe self-check: CRC-32 of the mech roster + structure table | 0x4854c0, called first in InitInstance 0x480000 | once at start; flag re-applied by 0x47fed0 | flag 0x608f70: after the intro the game goes to the Exit state and closes, no message | yes |
| 3 | Saved loadout checksum | Config::Validate 0x4254d0 (CRC 0x4255b0 vs +0x88), loader 0x4260c0 | every config load | the loadout is silently dropped | skip the compare, keep writing the CRC |
| 4 | Multiplayer rules self-check (host only) | 0x46fbd7..0x46fc33 in 0x46f160; set up by 0x49aca0 / 0x499e92 | every 10th ping (about 51 s) | game over (0x4707c0) | yes |
| - | "Cheated" flag 0x609994 | set by 0x40ced0, 0x40cf30, 0x40cf90 | | campaign: mission not counted, must replay | **no**, it is a game rule (see below) |

Not found: no CD key, serial or registration check; no volume-label, disc-size
or CD-audio-track check; no debugger detection (`IsDebuggerPresent` is not
imported, no `int 1/3`, timing or SoftICE tricks; the `int3` bytes are function
padding and the `int 0x42` lines at 0x42ce6d are a jump table misread as code);
no time bomb (`time()` is used only to seed `srand` at 0x49b0f6, 0x501b04,
0x509033); no CRC or size/time check of data files. The registry is read only
for `HKLM\Software\Zipper\MechWarrior3\1.0\RunDir` (0x4828d0, `SetCurrentDirectoryA`,
silently skipped if missing) and the settings key (startup.md).

## 1. Disc check (KERNEL32 GetLogicalDriveStringsA / GetDriveTypeA)

IAT slots: `GetDriveTypeA` 0x5bb11c, `GetLogicalDriveStringsA` 0x5bb12c. Each
has one caller, both in **0x575d40** (between `zsnd_create.cpp` and
`zutl_zar.cpp`).

**0x575d40 `find_on_drive(ECX = drive type, EDX = relative path)`**: 

```
575d5e  call [GetLogicalDriveStringsA](0x100, buf)
575d84  sprintf(0x72c540, "%s%s", root, path)      ; "D:\" + "video\c1.avi"
575d8a  call [GetDriveTypeA](root)
575d90  sub eax,3 / je  -> type 3 (fixed) : if ECX==3, _stat(0x72c540)
575d95  sub eax,2 / jne -> type 5 (CD-ROM): if ECX==5, _stat(0x72c540)
575dd4  found = 1 when _stat returns 0
575e09  return 0x72c540                    ; else next root, 0 after the last
```

So it returns the full path of the file on the first drive of the wanted type
that has it, or 0. Every caller passes ECX = 5; the fixed-disk branch is never
used.

**The check** (0x482f85..0x482fbb, inside 0x482d10, the main window's ctor,
reached from `Mech3App::CreateMainWindow` 0x47ff90 and 0x482c50):

```
482f85  mov esi, [ExitProcess]
482f90  push 0 / mov edx, 0x5e6030 ("video\c1.avi") / mov ecx, 5
482f9c  call 0x575d40
482fa3  jne 0x482fbd                       ; found: carry on (menu, title)
482fa5  push 0x50 / push 7 / push 0x11
482fab  call 0x4abfe0                      ; MessageBoxExA(text msg 0x50, caption msg 7, MB_OKCANCEL|MB_ICONHAND)
482fb3  cmp eax, 2 / jne 0x482f90          ; OK: look again
482fb7  push 0 / call esi                  ; Cancel: ExitProcess(0)
```

Message 0x50 is "Please insert the MechWarrior 3 CD!", 7 is "MechWarrior 3"
(`msgtable.py`). It runs once, when the window is created during InitInstance.
It looks only for **the first intro movie on a drive Windows calls a CD-ROM**:
no volume label, no file size, no audio tracks. A copy of the disc on the hard
disk does not pass, a mounted ISO (Windows reports it as a CD-ROM) does.

**The other two callers** use 0x575d40 as an asset fallback, not a check:

- 0x599cb0 in 0x599c00 (fmv stream ctor): builds `"%s\%s"` (dir, name),
  `_stat`s it, and only if missing looks for the same relative path on a CD; if
  that also fails it frees the path and the movie is skipped. This is why the
  movies play from the disc on a normal install (the installer does not copy
  `VIDEO\`).
- 0x5a7ba6 in 0x5a7b50 (zTwoD, between `twod_wintext.cpp` and
  `zent_init.cpp`; called from 0x59f45b for up to 10 names from a reader list):
  same pattern, `strncpy` the CD path over the local one; if neither exists,
  +0x34 stays 0 (the object behind it, 0x59a4a0, is not created). Likely the
  animated menu movies (unverified).

**Rebuild:** drop the startup loop entirely (it is also FIX (A): no CD drive,
and the importer already copies `VIDEO\` from the disc). Replace both fallbacks'
"look on the CD" step with "look in the imported data folder", or nothing,
since the importer puts the movies where the first `_stat` finds them. Keep
0x575d40 unported.

### Command-line switches parsed next to it (0x482d10)

Space-separated, prefix match (`strncmp`):

- `-nofmv` (0x5e6050): `[0x608e49] = 0`; read at 0x4820e1 (Intro state, the
  SPLASH movies), 0x482398 and 0x48b056 (other movie points). Skips the movies.
- `-seasick` (0x5e6044): 0x40ce90(1), sets bit 9 (0x200) of the options word
  `[0x5f4d04]`. Many menu screens read it through 0x40cec0 and store its
  inverse at their +0x138 (0x40a1da, 0x428a50, 0x42a642), and 0x4489e7 clears
  it around a load and restores it. Meaning not traced; by the name and the
  readers, likely "turn off menu/camera motion" (unverified).
- `-cd` (0x5e6040) `-cdN`: `[0x608e3c] = atol(N)` (0 when bare). Not a disc
  check: 0x480fdf uses it to force CD music track N for missions when
  `0 < N < track count` (0x573040); otherwise the track comes from the mission
  number or `rand()`.

### What the NoCD crack changes (112 bytes)

VA = file offset + 0x400c00. Same file size; patches only.

CD check, 4 jumps:

| VA | Original | Cracked | Effect |
|---|---|---|---|
| 0x575d9c | `83 ff 05 75 3d` `cmp edi,5 / jne +3d` | `83 ff 03 eb 3d` `cmp edi,3 / jmp +3d` | a CD-ROM drive no longer goes to `_stat` (the cmp is dead) |
| 0x575de2 | `75` jne (found) | `eb` jmp | 0x575d40 always returns after the first drive, with the path built from the first root (`A:\video\c1.avi` or `C:\...`), found or not |
| 0x575df0 | `74` je (end of list) | `eb` jmp | unreachable after the patch above; belt and braces |
| 0x599cb7 | `74` je (not on CD) | `eb` jmp | the fmv stream treats every CD lookup as failed, so it never copies the bogus path; movies play only from the install folder |

Net result: the startup check always passes, and movies must be copied to the
hard disk (`VIDEO\` next to the exe) or are skipped. 0x5a7ba6 is not patched:
it copies the bogus path, `_stat` fails, same as no CD.

Two unrelated fixes ride along; the rebuild needs the same protection in its own
way (unverified that the crack author meant exactly this, but the code leaves
little doubt):

- **CPU speed divide by zero.** 0x5988c0 is Intel's classic `cpuspeed` routine
  (RDTSC against `QueryPerformanceCounter`, CPUID at 0x597f30.. for the MMX bit
  23). It converts elapsed counter ticks to microseconds,
  `us = ticks * 100000 / (freq / 10)` (0x5989cb..0x5989ed), waits only until
  `ticks >= 1000` (0x5989a0), then divides cycles by `us`
  (`div edi` at 0x598a12, 0x598a1a, 0x598a8b, 0x598a9a, 0x598aad). When the
  performance counter runs at a few MHz this is fine; when it runs at the CPU
  clock (common on later Windows and multi-GHz CPUs), 1000 ticks are under a
  microsecond, `us = 0`, and the game dies with a divide error at startup
  (`ticks * 100000` also overflows 32 bits beyond 42,949 ticks). The crack NOPs
  the five `div edi` (`f7 f7` -> `90 90`), so the MHz result is garbage but the
  game starts. The result goes to the system-info object +0x14 (0x598061, from
  the settings code at 0x598000..); its use is not traced (likely only logged
  or used for default detail). Rebuild: do not port the measurement; return a
  fixed or SDL-measured value. Note as FIX (B).
- **Line clamp.** The call at 0x5a69ad (in 0x5a68b0, zTwoD; callers 0x467939
  and 0x5a6b43) goes to 0x562940, which jumps through `[0x8008c0]`; in 16-bit
  software mode 0x557bd4 sets that to 0x563b80, a Bresenham line that writes
  16-bit pixels with **no clipping**. The crack sends the call to a code cave
  threaded through the NOP gaps between small functions at 0x5a7575..0x5a76aa
  (each piece ends in a `jmp` over the next real function). It clamps the
  endpoints, x (ECX and arg 1) to <= 0x3ff and y (EDX and arg 2) to <= 0x2ff,
  that is to a 1024x768 surface, and EAX to <= 0x63f, then jumps on to 0x562940.
  The clamp is a `dec eax` loop with an **unsigned** compare, so a negative
  coordinate counts down from about 4 billion: a stall of a second or two each
  time, not a crash. The thick-line path (0x562c80, at 0x5a6976) is not
  clamped. Rebuild: clip lines to the real surface (SDL does). What draws these
  lines is not traced (likely a HUD or map overlay going off-screen).

### How no-CD patches were made (plain words)

The game does not encrypt or hide anything. A cracker ran it without the disc,
saw the "Please insert the MechWarrior 3 CD!" box, and set a breakpoint on the
Windows calls a disc check has to use: `GetDriveTypeA`, `GetLogicalDriveStringsA`
or `MessageBoxExA` (SoftICE in the 90s, W32Dasm for the dead listing). That
leads straight to the one function that walks the drives. Changing a few
conditional jumps (`75`/`74`, "jump if not equal / equal") into plain jumps
(`eb`) makes it answer "found" every time. Then they checked what else used that
function (the movie player) so the movies would load from the hard disk
instead. The patch was shipped as the whole exe or as a small byte-patcher that
compares and rewrites those bytes, which is why a NoCD exe is the same size as
the original and differs only at a handful of places.

## 2. Exe self-check (0x4854c0)

```
4854c0  crc = 0
4854d7  0x576c10(&crc, 0x5e6078, 0x474)    ; mech roster
4854ea  0x576c10(&crc, 0x5e6998, 0x17c)    ; internal-structure table
4854ef  return crc == [0x5e6b14]           ; 0x00efd77d
480010  [0x608f70] = !result               ; InitInstance, the first call it makes
48001e  if set: app +0x1dc = state(4)      ; Exit State after the intro
47ff3f  0x47fed0: same again whenever it picks the post-intro state
```

CRC-32, polynomial 0x04c11db7 MSB first, start 0, no final xor (table built at
0x576bc0 into 0x72eec0). Recomputed in Python from the exe: 0x00efd77d, equal to
the stored value. The expected value sits right after the second table
(0x5e6998 + 0x17c = 0x5e6b14). 0x47fed0 is called from InitInstance (0x480051)
and from about nine menu command handlers that start a game type (0x483753 ..
0x48399c, each 0x478a40 then 0x47fed0(1)), so the Exit state is forced every
time, not just at boot. No message: the splash movies play, then the program
ends. Details in startup.md.

**Rebuild:** the tables become C++ data, so any intentional balance change would
trip it. Remove the comparison: `tamper_failed = false` (or skip 0x4854c0) in
InitInstance and drop the 0x608f70 test in 0x47fed0. Keep 0x576c10 (the
loadouts and multiplayer use it).

## 3. Saved loadouts (Config::Validate 0x4254d0)

Already understood (mechdef-head.md). Save (0x424480 -> 0x4255a0) stores
CRC-32 0x4255b0 (name, packed config, +0x70.., same CRC routine) at +0x88.
Load (0x424600 -> 0x42474c) calls Validate(true): CRC must equal +0x88, then the
rule checks (weight, unplaced items, jump jets, armor). 0x4260c0 (caller
0x42641b) loads a config and compares 0x4255b0 with +0x88 a second time. A
mismatch drops the loadout silently. The elemental (mech index 7) is never
checked. Validate(false), without the CRC, also runs at 0x41445e (mech setup),
0x44d3a1 and 0x46d578 (a packed loadout unpacked by 0x4259c0, likely a
multiplayer peer's; an invalid one is dropped and listed at 0x605f94,
unverified).

**Rebuild:** ignore the `crc` argument in Validate and drop the second compare
in 0x4260c0; keep computing and writing +0x88 on save so files stay loadable by
the original game. Keep the rule checks: they are game rules (an overweight mech
is illegal in the mech lab too), not protection.

## 4. Multiplayer rules check (host only)

**The block.** The multiplayer game object (`[0x60861c]+0x6c` when its type is
3, 0x479480; class 0x4353a0, gametypes.md; also the lobby's `+0x116ec`) holds a
16-byte rules block at +0x60, its CRC at +0x88, and the live rules at
+0x70.. and +0x8c..:

| Block | From the lobby (`[0x6099d4]`) | Live copy (0x49b790, game start) |
|---|---|---|
| +0x60 bit 0 | check box +0x12ffc | +0x8c |
| bit 1 | check box +0x13168 | +0xa8 (0x490ab0: calls 0x490950 at mission start) |
| bit 2 | check box +0x132d4 | +0x8f |
| bit 3 | check box +0x13440 | +0x8d = **unlimited ammo** (passed to 0x4b0220 on respawn, 0x46da80, 0x46dde0) |
| bit 4 | check box +0x135ac | +0x8e (0x46da80: camera mode 1 on spawn) |
| bit 5 | `+0x15050`, options locked | |
| +0x64 u16 | list +0x11f88 (likely the map) | |
| +0x66 u16 | list +0x123ac, score limit | +0x70 (>= 100 = unlimited, 0x7fffffff) |
| +0x68 u16 | list +0x127d0, time limit (minutes) | +0x74, `[0x609920]` = minutes x 60 |
| +0x6a u16 | list +0x12bf4 (likely a tonnage limit) | +0x78 |
| +0x6c | `rand()` (salt) | |

Names of the five check boxes are not traced except bit 3. +0x8c is tested in
0x46dde0 (respawn): when 0 the player goes to camera mode 4 instead of a new
mech, so it is likely "respawn allowed" (unverified).

**Setup.** On the host, Lock Options (button 0xb -> 0x49aca0) sets
`+0x15050 = 1`, zeroes +0x60..+0x6f and +0x88, fills the block,
`srand(time(0))`, stores `rand()` at +0x6c and takes the CRC into +0x88
(0x49b129). A client does the same when the host's options message 0x20 arrives
(0x499cf0; zeroing at 0x499e92, CRC at 0x49a0e0, no `srand`). The CRC itself
is never sent (message 0x20 carries only the bits and the four choices; likely,
not every sender checked).

**The check** (0x46f160 case 0x10, the ping sent every 5.1 s,
network-messages.md):

```
46fbc9  [0x5e5690]++                       ; ping sequence
46fbce  if host (0x586d20) and seq % 10 == 0:
46fc03    crc = 0x576c10(0, game+0x60, 16)
46fc08    if game+0x88 != crc: call 0x4707c0   ; game over, stats saved, back to the front end
46fc15    if game+0x8c: game+0x60 |= 1
46fc25    if game+0x8d: game+0x60 |= 8
```

**What "sets bits 0 and 3" means.** It is not a self-destruct. It folds the two
live flags back into the block: if +0x8c and +0x8d still match the boxes ticked
at lock time, bits 0 and 3 are already set and the OR changes nothing. If
something turned unlimited ammo (+0x8d) or +0x8c on in memory after the lock,
the OR sets a bit that was 0 at lock time, and the **next** check fails. So it
guards the host's own copy of the rules (score/time limits, the check boxes,
the map choice) against a memory editor or trainer, with the live unlimited
ammo and +0x8c flags watched through the block. A change is caught 51 to 102 s
later, and the game simply ends as if time ran out. It only runs on the host
and only checks the host's memory; clients are not checked.

Side effect worth keeping in mind: the lock calls `srand(time(0))` on the host,
reseeding the shared `rand()`.

Open: whether Launch (0x49b790, which ORs bits 0-4 again but does not recompute
the CRC) can run without Lock Options. If it can, a host who launches unlocked
with any box ticked would fail the first check about 51 s in. Test on Owen's PC.

**Rebuild:** delete the compare at 0x46fc08 (keep the setup and the CRC so the
code stays shaped like the original; nothing else reads +0x88). No network
compatibility issue, the CRC stays on the host.

## 5. Other things that look like protection but are not

- **"Cheated" flag `[0x609994]`.** Set to 1 when the player turns on
  invincibility (options bit 1, 0x40ced0), unlimited ammo (bit 2, 0x40cf30) or
  turns heat off (bit 3 cleared, 0x40cf90). It is the missions object's +0xc4
  (0x6098d0 + 0xc4), and each mission's start (0x491d20, through ESI, so a
  search for the address misses it) sets it anew: 1 if one of the three is on
  then, else 0, and 0 in a network game (mission-frame.md section 2).
  Read at 0x442588 (0x442540, a mech-data method that makes one extra
  vt+0x24 call outside multiplayer when set; meaning not traced), 0x47ac64 and
  0x47d042 (end of mission) and 0x47b6f6 (debrief screen). In a campaign, a
  mission finished with the flag set is treated like a failed one (with
  `[0x609926]`, not traced): the debrief offers "Replay Mission" (0x173) instead
  of "Next Mission" (0x174), the score is not added to the "Campaign Total"
  (0x47c573), and the results are not carried over (0x47ae10 / 0x435240(0)).
  This is a designed game rule, not a trap. **Keep it.** The `[0x608e45]` byte
  that stops these setters from touching the mech has no direct writer
  (unverified what sets it).
- **interp.zbd file-time check** (0x596620, 0x596810; interp.md): a loose
  script with a different mtime disables the archive, a newer one is loaded
  instead of the archived copy. A developer convenience for editing scripts, not
  protection. Keep (or treat as a modding feature).
- **Pilot file header** (0x478ce0): the first two dwords must equal pilot
  +0x4/+0x8 (likely magic and version), otherwise the file is recreated. Format
  versioning, not protection, but note it wipes a pilot file with a wrong
  header.
- **Version strings**: `GetFileVersionInfoA` on `mech3.exe` (0x4af0f0) only
  formats "%d.%d.%d.%d" for the window title and menus (0x4af1d0); no compare.
  `GetVersionExA` (0x57a3d0) picks the DirectDraw path by Windows platform.
  The "net version" strings at 0x5e8eb4.. belong to the AI waypoint-network
  reader (`ZRdrAiNet.cpp`), and `EXPIRES` (0x5f2038) is a weapon flag.
- **Single instance** (`FindWindowA("Mech3Class")`, startup.md): keep.

## For the community (plain words)

- The CD check only looks for the first intro movie, `video\c1.avi`, on any
  drive Windows calls a CD-ROM. Any disc or mounted image with that file
  passes; no label or key is checked.
- Without the disc the game asks "Please insert the MechWarrior 3 CD!"; OK looks
  again, Cancel quits.
- Hidden launch switches: `-nofmv` skips the movies, `-cdN` forces CD music
  track N in missions, `-seasick` sets an otherwise menu-controlled option
  (meaning not yet known).
- The well-known 1.2 NoCD exe also fixes a startup crash on fast PCs (a CPU
  speed test that divides by zero) and stops a line-drawing routine from
  writing outside the screen in software mode. That is likely why some people
  found the NoCD exe "more stable" than the original.
- In multiplayer the host quietly re-checks the locked game rules every 51
  seconds; tampering with them in memory (for example turning on unlimited ammo
  after the lock) ends the game a minute or two later as if the clock ran out.
- In the campaign, switching on any cheat (invincibility, unlimited ammo, heat
  off) even once means that mission's result is not kept: you get "Replay
  Mission" instead of "Next Mission", and it stays that way until you restart
  the game, even if you switch the cheat off again.
