// Ours, not original code: running mw3import, the movies' converter (docs/fmv.md,
// The imported movies), shared by the launcher's install work and mw3's
// first-run fallback (first_run_import.h). Not on Windows, whose helper plays
// the AVIs as they are.
#pragma once

#include <functional>
#include <string>

namespace sdlfe {

// mw3import(.exe) beside the program (in a Mac app, Contents/MacOS); "" if absent.
std::string importer_path();

// The AVIs in `source`'s video\ with no .fmv yet in the write dir's video\.
int movies_missing(const std::string& source, const std::string& write_dir);

// mw3import for `source`'s missing movies, waited for here. `progress` hears
// each "progress K N name" line (done, total, the movie) and stops the import
// by returning false. 0 when every movie was made, 1 stopped, -1 when it
// could not start or failed.
int run_importer(const std::string& source, const std::string& write_dir,
                 const std::function<bool(int done, int total, const std::string& name)>& progress);

}  // namespace sdlfe
