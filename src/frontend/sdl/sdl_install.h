// Ours, not the exe's: finding the player's MechWarrior 3 install at start
// (FIX (A): the original was started from its own folder; the port is not).
// mw3.cfg's install= when it still holds the game, else the places the
// original's installer wrote, else a folder dialog. docs/program.md, The
// install.
#pragma once

#include <string>
#include <vector>

namespace sdlfe {

// A folder that holds the game's data: zbd/reader.zbd (Mech3.exe is not
// needed, the port never runs it, nor video/: movies are optional). `missing`,
// when given, names what is not there ("zbd\", "zbd\ and video\").
bool install_valid(const std::string& dir, std::string* missing = nullptr);

// The install a picked file stands for: its folder, or the folder above when
// the file is in zbd/ or video/ (a picked reader.zbd).
std::string install_of_pick(const std::string& file);

// The folder the installer's "Program" value names: a folder, or Mech3.exe's
// full path (InstallShield's App Paths default).
std::string install_of_program_value(std::string value);

// Where the original may be installed, in order: the installer's registry
// values (HKLM SOFTWARE\MicroProse\MechWarrior 3\1.0 "Program", App Paths
// mech3.exe; their 32-bit and VirtualStore views), the installer's default
// folders, then this program's own folder and the one above it.
std::vector<std::string> install_candidates();

// mw3.cfg's install= line set to `install` (added, or replacing the old one),
// the other lines kept; false when the file cannot be written.
bool save_install(const std::string& write_dir, const std::string& install);
// mw3.cfg's KEY=VALUE line set (first, any other KEY= line dropped).
bool save_config_value(const std::string& write_dir, const std::string& key, const std::string& value);

// The search: the first valid of `configured` (mw3.cfg's) and the
// candidates; with `may_ask`, else a message box and a file dialog until a
// valid folder is picked (a wrong one says what it lacks and asks again).
// Empty when none: Quit or Cancel, or not allowed to ask.
// `found_by_search`: the result did not come from `configured`.
std::string find_install(const std::string& configured, const std::vector<std::string>& more, bool may_ask,
                         bool* found_by_search);

}  // namespace sdlfe
