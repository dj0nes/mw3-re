# Lessons for the next port

Written with Owen at the end of the MW3 port (2026-10-31), for the next
project of this kind (Pirate's Moon first, then any other old game). What
worked, what we'd change, and why.

## The approach: keep it

The original's logic, ported function by function into plain C++, each
change from the original marked where it happens (`FIX (A)`, `FIX (B)`,
`FIX (bug)`, `FIX (trap)`), the play-changing ones behind one switch that the
differential tests turn off. A thin platform layer (SDL3) stands in for the
OS and graphics calls one to one, and the original's main loop stays as
written.

A *matching* decompilation (C that compiles back to the original bytes)
first, then modernised, was weighed and rejected:

- It needs the original compiler and flags (here Visual C++ 6) and months
  of coaxing register choices.
- The result is still 1999 Windows code (DirectX 6, MCI, Win32), so it needs
  the same platform layer anyway.
- It pays off only when exactness is the product (speedrunning, a reference
  decomp). For "plays like the original on today's machines", emulator
  differential tests give nearly the same confidence for a fraction of the
  work.

## Change these

1. **No stand-ins between ported pieces.** Ported functions, checked one by
   one against the original, were right. Most playtest bugs lived in the
   hooks joining them in the program: LRMs left with no lock target, recoil
   never connected, the lead circle fed the raw velocity, and (W39) the lead
   circle fed a mech's feet where the original reads its torso. Several
   were a stand-in carrying the original's address while doing something
   close but different. Rule from day one: a hook named after an address
   calls that address's port, or reports itself unported. Nothing between.
   A hook census that fails the build on unconnected hooks, from the start.
2. **Test whole sessions against the original, not only functions.** Early,
   record play sessions on the original (inputs per frame, snapshots of
   positions, heat, damage, targets) and replay the inputs in the port,
   comparing frame by frame. The first frame where they part points at the
   bug. That catches wiring bugs before a human playtest does.
3. **Automated builds on every platform from week one.** Windows-only
   breaks (`<windows.h>`'s `min`/`max` macros, twice) surfaced only when
   someone built by hand on Owen's PC. A CI build on Windows, Mac and Linux
   at every push finds them in minutes.
4. **Decide the program layout at the start.** Pirate's Moon was always
   planned; the two games, the launcher and the shared engine folder
   (`games-layout.md`) could have been set before the first port instead
   of moved into later.
5. **Shorter docs, and an issue tracker.** One short page per system (how it
   works, status, what's left), the evidence in a section below it. Playtest
   reports as issues, one per bug with comments, not one table with
   paragraph-long cells. Easier for the owner, and for the community later.
6. **List the event taps first when Mac input lags.** W40's second-late
   mouse was another app's (Magnet's) filtering event tap on mouse-down,
   found only after probes of SDL, relative mode and our pump.
   `CGGetEventTapList` names every tap, whether it filters, and its
   measured latency: a ten-line program, read-only, to run before anything
   else when one input lags on one machine.
7. **Settle the agent setup early.** One long-lived worker and a lead who
   reviews and merges worked; a second worker once things were steady. The
   early fan-outs to many short-lived subagents cost much of a week's usage
   for work that needed review anyway.

## Comments: keep them as they are

- The address comment on every ported function
  (`// Mech3.exe 1.2 @ 0x00412340`) is the most useful line in the code:
  any function traces back to the original in one search.
- Offset comments on fields (`// +0x3e4`) make reading the decompiler's
  output against the port direct.
- Short comments only where the logic is not obvious; the depth in `docs/`.
- Add one thing: a note on any hook or default that is *not* the original,
  saying so and why. W39's stand-in carried the original's address, so its
  comment made it look right.

## Keep these

- Structs given to Ghidra first, then each function cleaned into C++.
- Differential tests in Unicorn for every self-contained function, with
  break tests that show each test catches an error.
- The game data imported from the player's own copy; nothing shipped.
- Every play-changing fix behind one player-facing switch (`bugfixes=0` /
  `--original`), and every change in plain words in `CHANGES.md`.
- Hidden mechanics written up for players as they are found
  (`discoveries.md`).
- The lead reviewing everything before it reaches `main`; workers on their
  own branches and worktrees.
- Old networking ported faithfully over a modern transport (ENet for
  DirectPlay), the way SDL stands in for DirectX: no emulation of the old
  layer. Peers' packets treated as untrusted (the original trusted them).
