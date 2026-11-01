// Ours: the launcher's first-start notes (docs/launcher.md).
#pragma once

#include <string>
#include <vector>

namespace sdlfe {

// What a player may want to do about the data and the music, in the
// launcher's words (SETUP's buttons where the launcher can do it): the data's
// version from setup::check_install ("1.0", "1.2", "1.0 with 1.2 files",
// "unknown"), whether any music track is there, and where a rip goes.
std::vector<std::string> launcher_notes(const std::string& version, bool music, const std::string& music_dir);

// Any of the CD's audio tracks (track02..track20 as .ogg, .flac, .wav or
// .mp3) in one of `dirs`.
bool have_music(const std::vector<std::string>& dirs);

}  // namespace sdlfe
