# zReader: the game's data files

Mech3.exe 1.2, found with REA + Ghidra and the raw disassembly (2026-10-19).
Ported to `src/gamez/zreader/` and `src/gamez/zutil/zutl_zar.*`, checked against
the original in Unicorn on every file in `reader.zbd`
(`tests/differential/test_zreader.py`).

## Two layers

**The archive, `zutil/zutl_zar.cpp`.** A `.zbd` is a plain concatenation of files
with the table of contents at the end:

    file data ... | Entry[count] | uint32 version (1) | uint32 count

Each entry is 0x94 bytes: start, length, a 64-byte name, flags, a 64-byte comment
and a FILETIME. MW3 accepts only version 1 (`0x5767b0`); mech3ax also knows a
version 2 with a checksum, used by Zipper's later games. Names are matched with
`_stricmp`. `reader.zbd` holds 178 files (`briefing.zrd`, `mechs.zrd`,
`sounds.zrd`, ...).

**The reader, `zreader/zreader.cpp`.** Each file is one tree of 8-byte nodes,
`{int type; value}`: 1 int, 2 float, 3 string, 4 list. Stored as type then value;
a string is length then bytes; a list is a count then count - 1 nodes. In memory a
list keeps that count in its own first slot, `list[0] = {1, count}`, which is why
the stored count is one more than the items (mech3ax's reader notes the off-by-one
and guesses this; the exe confirms it).

Objects are flat lists of key/value pairs: `("CD_TRACKS" (("menu" 2) ...) ...)`.
`find` (0x5569e0) returns the node after the first string equal to the key.

The typed getters take that node: a string, a float (an int is converted), an
int, fixed lists of two or three floats or ints (0x556b70-0x556cf0; the integer
lists truncate floats with MSVCRT `_ftol`), and a boolean that is an int or one
of `true`/`on`/`false`/`off` in any case (0x556da0). Each has a keyed wrapper that
calls `find` first (0x5570b0-0x557150). A list item of the wrong type leaves that
output as it was.

## Loading

At startup (0x480000) the game points the reader at `..\data\common\zrdr` and
tries `detail.zrd`. Only if that fails does it open `zbd\reader.zbd` as the
current archive and try again, so the developers' loose text files override the
archive. `load` (0x556740) drops the directory and looks up name + extension in
each open archive, in the order they were opened. The sound module loads
`sounds.zrd` into 0x72c440 and passes its `CD_TRACKS` node to the CD code
(0x5706f0 -> 0x572450).

A name no open archive has is looked for as a loose text file (0x556240:
the path as given if `_access` finds it, else along the search path) and
read by the text branch below. None ship with the game; the controls files
the game writes, `keys\<name>.zrd`, are text (controls.md, Setup). The archive writer (0x5765b0, 0x576890, 0x576990, 0x576720) is not
ported either; the game never builds archives at run time.

## Text files (ported)

0x5563d0(handle), read a byte at a time (ReadFile, 0x5563a0). Ported in
`zreader.cpp` (`parse_text`, and `load`'s text branch), checked by
`test_zreader_text.py` on 540 files.

- **Balance check** 0x5562a0: counts `(` and `)` outside `"..."` strings and
  `;` comments (to the line's end, CR or LF); a quote inside a comment does
  not count, a `;` inside a string does not start one. Not zero: no tree
  (the load returns 0). The file is rewound.
- **Lists** 0x5563f0: tokens up to the `)` that closes the list, or the
  file's end; a `(` reads a nested list. Items are kept in order (a queue of
  8-byte nodes from a pool, 0x555cd0 at start). The root is the list of
  everything at the top level, so a file `( KEYS (...) )` gives
  `[[KEYS, [...]]]`; `find` searches nested lists anyway.
- **Tokens** 0x556510: delimited by `(`, `)` and spaces (MSVCRT isspace). A
  `;` skips to the line's end without ending the token (`ab;x\ncd` is
  `abcd`). A `"` starts a quoted part that may sit in a token's middle;
  the closing quote writes a 0 into the token, so text after it is lost
  (`"ab"cd` is `ab`), and a quoted token is always a string. A token in
  progress when the file ends is dropped.
- **Values** 0x556690: an unquoted token strtol (base 10) reads whole is an
  int (out of range: LONG_MAX or LONG_MIN), else one strtod reads whole a
  float (exponents `e`, `E`, `d`, `D`; `5.`, `.5`), else a string: `0x1c`,
  `1e`, `-` are strings.
- **Directives** 0x557550, at a token's start only: the word after `#` up to
  a space (0x557650): `ifdef NAME` (0x5577d0 looks NAME up in a list at
  0x72bf94 that nothing ever fills, so it is always false), `else`,
  `endif`; any other word is skipped. A false `#ifdef` scans for the next
  `#` (in comments and strings too) and reads directives until its `#else`
  (the else part is read) or its `#endif`; an `#else` met while reading
  skips to the `#endif`. The nesting count 0x72c1ac is global and carries
  from file to file; a stray `#endif` takes it below 0, which only shifts
  the comparisons.

Bugs, fixed in the port (fixes.md):

- An unquoted token of 256 characters or more writes its terminator over
  the low byte of the token reader's return address, which then returns
  into the middle of an instruction; the "token too long" message after
  the 257th character is never reached cleanly. FIX (bug): the token is
  dropped and the list ends, as the message intends.
- A quoted part has no length check (a stack overrun past 256) and reads on
  forever at the file's end; an `#ifdef` or `#else` whose `#endif` is
  missing scans forever at the end. FIX (bug): both stop there.
- FIX (A): stdio for ReadFile, and zutl::open (the write dir, then the
  mounts) for `_access` and the search path.

`test_zreader_text.py` runs 0x5563d0 in the emulator (the list pool 0x555cd0
and the defines list's constructor 0x557490 first; MSVCRT's strtol, strtod
and isspace as stand-ins of the same grammar as `crt::msvc_strtol` /
`msvc_strtod`) against `parse_text` on 40 controls files and 500 random
ones, the #ifdef depth carried from file to file; trees compared node by
node. Result: 540 identical (25 unbalanced). Breaking the port showed the
test catches it, 9 of 9: a comment ending the token, the balance counting
parentheses in comments, quoted tokens read as numbers, no `d` exponent,
strtol wrapping, the #else not taken, #endif not counting, items reversed,
the last token kept at the end of the file. The fixes are checked in
`mw3tests` (`zreader_text_files`).

## Pirate's Moon (zrdr_main.cpp)

Pirate's Moon 1.0 renamed `zreader.cpp` to `zrdr_main.cpp` and rewrote it
(2026-11-02, `src/pm/gamez/zreader/zrdr_main`, docs/pm-map.md step 1). The
files are the same: all 1,052 entries of PM's 50 reader archives are MW3's
binary trees. The state is now a C++ object at 0x008c1750 (the search path,
a `std::list` of open archives, the current one; open 0x00579310, find
0x00579300, close 0x00579330, init 0x00578040, shutdown 0x005780b0), and
most functions take their arguments on the stack where MW3's took ECX and
EDX. What they return is MW3's, checked on PM's exe
(`test_pm_zreader.py`: every tree, and every getter on 52,337 keys), so the
port keeps zrdr's. New:

- **Getters**: an int value 0x00578f70 (an int, a one-item list too; `fild`
  then `_ftol`, which give the int back), and keyed float and string values
  0x00579090 / 0x005790b0 (find, then 0x00556fe0's and 0x00557030's PM
  copies). The keyed list getters moved (0x005790d0-0x00579170) and MW3's
  find wrapper 0x005569e0 is gone (0x00578950 calls find with 1).
- **A stack of open files**: every text byte is read from the current
  handle 0x008c19f4, the top of up to 32 (0x008c1974, the top index
  0x00612f08, push 0x00578720, pop 0x005782a0). At a file's end the byte
  reader 0x00578250 closes it and goes on in the file before, so an
  included file is read in place. The balance check 0x00578150 runs on the
  first file only.
- **`#include NAME`**: the directive word 0x005795e0 gives 0 ifdef, 1 else,
  2 endif, 3 include, 4 anything else; 0x00579440 replaces 0x00557550,
  and opens the file (0x00578750, through load's search 0x005780e0) at
  once. It returns true, so an `#include` met while a false `#ifdef` is
  skipped is read and ends the skip (kept). Ours: a name that is absolute,
  has a drive (`:`) or a `..` part opens nothing, so a file (a mod's too)
  reads only inside the mounts; nothing shipped includes anything. The #ifdef depth moved to
  0x008c19fc, kept across files as MW3's; the defines list 0x008c176c is
  still empty. The list pool is made with 0x200 elements (MW3: 64).
- **Binary read 0x005791b0**: MW3's 0x00557180 on the current handle
  (strings through zArchive's 0x00597280), its error line now 0x59d.

The shared `zrdr::load` reads a loose text file through the running game's
table (`zgame::Table::zrdr_parse_text`): PM's `pm::zrdr::parse_text` takes
`#include`, MW3's skips the word. `test_pm_zreader_text.py` checks it
against PM's exe on MW3's 540 cases and 300 with `#include` (2,693 files
included, up to 32 open).

## Original behaviour worth knowing

- `find` is depth-first through every nested list and does not tell keys from
  values: a string *value* equal to the key matches too. Kept.
- When the matching string is the last item, `find` returns the slot one past the
  end, and an empty list `()` still has its item 1 read by the typed getters.
  Both read heap memory beyond the list. In `reader.zbd` 5,597 of the 13,646
  lookups the differential test makes land there. The port gives every list one
  zeroed spare slot, so those reads mean "no value" (FIX (A), `docs/fixes.md`).
- `get_float` copies a float's bits with `mov`; only ints are converted (`fild`).
  Ghidra's pseudocode shows a cast on both, which is wrong for floats.
- `reset` (0x5766d0) does nothing when the archive has no table, so a failed open
  leaves the path for `destroy` to free.
- `get_bool_int` (0x556da0) rejects floats, and a list whose only slot is its
  count; `get_bool` (0x557150) is true only for exactly 1.

## MSVCRT pieces the reader needs

`crt::msvc_ftol` stands in for `_ftol`, the compiler's float-to-int conversion:
it truncates into 64 bits and the caller keeps the low 32, so 3e9 becomes
-1294967296; NaN and 64-bit overflow give 0x8000000000000000 (low bits 0).
`crt::msvc_stricmp` folds ASCII only, as MSVCRT does in the C locale.
