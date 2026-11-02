#include "frontend/sdl/launcher_notes.h"

#include <cstdio>
#include <filesystem>

namespace sdlfe {

std::vector<std::string> launcher_notes(const std::string& version, bool music, const std::string& music_dir) {
  std::vector<std::string> out;
  // 1.0's data and a mix need no note: they load as 1.2 (docs/patch12.md).
  if (version == "unknown")
    out.push_back("Some MechWarrior 3 files are neither version 1.0's nor 1.2's (another version or a mod). They may not "
                  "play right. If you have the official 1.2 patch, SETUP can apply it (your install is not changed).");
  if (!music)
    out.push_back("No music yet: the original played it from the CD's audio tracks, which an install does not have. "
                  "SETUP's ADD THE MUSIC takes it from your disc image (a .cue and its .bin), or put your own rip in " +
                  music_dir + " as track02.ogg, track03.ogg and so on.");
  return out;
}

bool have_music(const std::vector<std::string>& dirs) {
  std::error_code ec;
  for (const std::string& d : dirs)
    for (int t = 2; t <= 20; ++t)
      for (const char* ext : {"ogg", "flac", "wav", "mp3"}) {
        char name[32];
        std::snprintf(name, sizeof name, "track%02d.%s", t, ext);
        if (!d.empty() && std::filesystem::is_regular_file(std::filesystem::path(d) / name, ec)) return true;
      }
  return false;
}

}  // namespace sdlfe
