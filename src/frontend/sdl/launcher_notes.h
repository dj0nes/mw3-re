// Ours: the launcher's first-start notes (docs/launcher.md).
#pragma once

#include <string>
#include <vector>

namespace sdlfe {

// What a player may want to know about the data, the movies and the music,
// in the launcher's words (its ADD FROM MY DISC where it can help): the
// data's version from setup::check_install ("1.0", "1.2", "1.0 with 1.2
// files", "unknown"), whether there are movies and music, where a rip goes.
std::vector<std::string> launcher_notes(const std::string& version, bool music, bool movies, const std::string& music_dir);

// Any of the CD's audio tracks (track02..track20 as .ogg, .flac, .wav or
// .mp3) in one of `dirs`.
bool have_music(const std::vector<std::string>& dirs);

// Whether there are movies to play: AVIs in the install's or the write dir's
// video\ (the latter added from the disc), or imported ones (.fmv).
bool have_movies(const std::string& install, const std::string& write_dir);

}  // namespace sdlfe
