# Work logs and the specialists' protocol

The lead session ("mw3 lead", formerly "mw3 mac 1") hands out one small
item at a time to the worker over SendMessage, reviews and merges its
branches. Since 2026-10-25 (Owen) one worker, "mw3 dev" (formerly "mw3 mac
4"), takes every area; the area specialists below are paused. The worker
keeps its log here, so it can pick up after a compaction by reading it.

| Session | Area | Log |
|---|---|---|
| mw3 dev | Every area, one item at a time (from 2026-10-25) | `dev.md` |
| mw3 dev 2 | Every area, one item at a time, alongside mw3 dev (from 2026-10-29; worktree mac5) | `dev2.md` |
| mw3 mac 4 (now mw3 dev) | The AI, to 2026-10-25: the brain, orders, behaviours, AI firing, lancemates; then render, vehicles, the cleanup | `ai.md` (history) |
| mw3 mac 2 (paused) | Game systems: mechs, movement, weapons, effects, the world's queries | `game.md` |
| mw3 mac 3 (paused) | Front end and presentation: fades, the HUD, screens, render glue | `frontend.md` |

## Rules for a specialist

- Read `CLAUDE.md` first; it holds the project's conventions.
- Run on Opus 5.5 (`claude-opus-5-5`). Porting and analysis need it.
- Start no agents or subagents. The lead runs the reviews.
- Work in your own worktree, never in the main checkout. Set it up once:
  `git worktree add .claude/worktrees/<session> -b <session>/<item>
  main`, with symlinks to the main checkout's `game/`, `analysis-output/`
  and `.venv/`, and your own `build/`. Then, for each item, run
  `git checkout -b <session>/<item> main` in that worktree.
- Keep scratch files in the worktree's `scratch/` folder, which is not
  tracked.
- Keep your context small:
  - Grep `analysis-output/decomp` and `mech3.asm` instead of reading whole
    files, and keep tool output short.
  - While iterating, run only the tests you touch.
  - Tests cover what was touched, nothing more (Owen, 2026-11-03,
    replacing "the related set plus the full suites"). Before reporting,
    run the tests of the code you changed: its module's units and program
    tests by name, the differential tests of the functions changed, every
    test importing a changed harness, and the break tests. A PM change in
    one area (say its controls) runs that area's tests only. The full
    `mw3tests` / `mw3sdltests` run only when shared code changed (a
    `src/gamez/` header or type both games use, zfix, the program's main
    loop or input/render layers), and then once per lead batch, not per
    branch. The campaign runs only for loading or play changes. The lead
    does not rerun what the dev ran on the same tree; the lead runs the
    combined batch once before a merge to main or a tag. While
    iterating, `MW3_SKIP=headless ./build/mw3tests` runs the units in under
    a second, and `MW3_TEST=name` runs one test by name.
  - Keep each test run short (Owen, 2026-10-22: runs of 10 to 20+ minutes
    slowed the night down). Break runs are where the time went: each break
    reran a differential at its full count (lab step 5b2a: 28 breaks x 83 s
    for 60 runs, 20 minutes). Run a break at the smallest count that
    reaches its branch (10 to 20 runs, or the seeds known to reach it), and
    rerun at the full count only a break that the short run misses. Aim to
    keep a whole break pass under 5 minutes.
  - A differential that takes over 3 minutes at its default count runs at a
    reduced count in the related set; the full count runs at the lead's
    checkpoints (`tests/differential/run_all.sh`, in the background).
  - End emulator tests with `sys.stdout.flush(); os._exit(code)`: Unicorn
    processes can hang at exit on this Mac, in a state nothing can kill.
    Run long tests in the background with their output going to a file in
    `scratch/`, then read the file, so a hang never blocks the session.
  - Run every differential test, and every break run's test, under
    `tests/differential/run_capped.py --mem-gb 4 --secs <limit> -- <cmd>`
    (macOS ignores `ulimit -v`): it kills the run's process group over the
    memory cap (exit 125) or the time limit (124) and prints the peak. A
    test left alone once grew to 193 GB over 11 hours (2026-10-21).
  - After two failed attempts at the same fix or mismatch, stop retrying:
    go back to the assembly and the callers and work out what is really
    happening before the third try.
- Never kill processes by name (`pkill -f mw3tests` and the like): the
  other sessions run the same programs. Kill only a pid you started.
- A `CHANGES.md` line goes under its heading (bug fixes that change play,
  crashes, protection, modern machines), never at the end of the file: the
  last section lists what the rebuild keeps on purpose.
- Every item comes with a differential test with break tests, its docs,
  `fixes.md` and `CHANGES.md` lines for any change from the original, and
  player-facing finds in `discoveries.md`.
- Commit messages carry no `Co-Authored-By` or `Claude-Session` lines, and
  the author email is the noreply address set in the repo's git config.
- Commit on your branch and never push or merge to main. Report to "mw3
  lead" with SendMessage: the commits, what is ported (by address), the test
  results, the findings, the fixes and what is left. Keep the report short.
- Owen compacts each specialist after every major feature (`/compact`), so
  keep the log current enough to pick up from it alone.
- After reporting, append a few lines to your log (the item, its commits,
  and what you would need to remember), then wait for the next item.
