// Ours, not original code: the program's arguments on tvOS, which has no
// command line (docs/tvos.md). The app bundle carries the install in game/
// and the converted movies and the music in media/ (tools/tvos/build.sh
// --data); the launcher is skipped.
#pragma once

#include <string>
#include <vector>

namespace sdlfe {

// `base` is the bundle's folder ending in a slash, as SDL_GetBasePath gives it.
std::vector<std::string> tvos_default_args(const std::string& base);

}  // namespace sdlfe
