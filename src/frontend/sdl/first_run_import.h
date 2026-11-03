// Ours, not the exe's: the movies converted on a player's first run (FIX (A):
// the original played the disc's Indeo 5 AVIs through Video for Windows; the
// port plays the importer's copies, docs/fmv.md section 6). When the
// install's video\*.AVI have no .fmv yet in the write dir's video\, and
// mw3import sits beside mw3, it is run for the missing ones (about a minute
// for the 19) with a small progress window; without it the game plays
// without movies, as before.
#pragma once

#include <string>

namespace sdlfe {

// The import, if one is due: `force` runs it even after a failed attempt
// (the write dir's video\import-failed). False when it could not run or
// failed; the game goes on either way.
bool first_run_import(const std::string& install, const std::string& write_dir, bool force);

}  // namespace sdlfe
