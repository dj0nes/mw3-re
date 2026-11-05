// Ours (tvOS): sdl_child.h for the Apple TV, which cannot start a process
// (docs/tvos.md). A child fails to start, as a missing helper does. There
// is no launcher to tell (--ready-file) and no console to replace, so the
// rest does nothing.
#include "frontend/sdl/sdl_child.h"

namespace sdlfe {

SDL_Process* start_child(const char* const* args) {
  (void)args;
  SDL_SetError("tvOS cannot start a process");
  return nullptr;
}

void set_ready_file(const std::string& path) { (void)path; }
void mark_ready() {}
void set_ready_status(const std::string& text) { (void)text; }

bool log_to_file(const std::string& write_dir, const std::string& name) {
  (void)write_dir, (void)name;
  return false;
}

size_t resident_kb() { return 0; }

}  // namespace sdlfe
