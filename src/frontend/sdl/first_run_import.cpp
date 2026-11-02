#include "frontend/sdl/first_run_import.h"
#include "frontend/sdl/movie_import.h"
#include "frontend/sdl/sdl_child.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace sdlfe {
namespace fs = std::filesystem;

namespace {


// What the importer's stdout says: "progress K N [name]".
struct Progress {
  std::atomic<int> done{0}, total{-1};
  std::atomic<bool> finished{false};
};

void read_progress(SDL_IOStream* out, Progress* pr) {
  std::string line;
  char buf[512];
  size_t n;
  while (out && (n = SDL_ReadIO(out, buf, sizeof buf)) > 0) {
    for (size_t i = 0; i < n; ++i) {
      if (buf[i] != '\n') {
        line += buf[i];
        continue;
      }
      int k = 0, t = 0;
      if (std::sscanf(line.c_str(), "progress %d %d", &k, &t) == 2) pr->done = k, pr->total = t;
      else if (!line.empty()) std::printf("%s\n", line.c_str());  // the importer's own lines, passed on
      line.clear();
    }
  }
  pr->finished = true;
}

}  // namespace

namespace {
// The movies of `install`'s video\ not imported yet, imported.
bool import_from(const std::string& install, const std::string& write_dir, bool force) {
  const int missing = movies_missing(install, write_dir);
  if (missing == 0) return true;
  std::error_code ec;
  const fs::path failed = fs::path(write_dir) / "video" / "import-failed";
  if (fs::exists(failed, ec) && !force) {
    std::printf("mw3: %d movies not imported (an earlier import failed: mw3 --import tries again)\n", missing);
    return false;
  }
  const std::string importer = importer_path();
  if (importer.empty()) {
    std::printf("mw3: %d movies not converted: mw3import is not beside mw3 (README.txt)\n", missing);
    return false;
  }
  std::printf("mw3: converting %d movies (about 3 s each)\n", missing);
  set_ready_status("Importing your movies (first start only)...");
  const char* args[] = {importer.c_str(), "--install", install.c_str(), "--write-dir", write_dir.c_str(), "--missing",
                        nullptr};
  SDL_Process* proc = start_child(args);
  if (!proc) {
    std::printf("mw3: the import could not start: %s\n", SDL_GetError());
    return false;
  }
  Progress pr;
  SDL_IOStream* out = static_cast<SDL_IOStream*>(
      SDL_GetPointerProperty(SDL_GetProcessProperties(proc), SDL_PROP_PROCESS_STDOUT_POINTER, nullptr));
  std::thread reader(read_progress, out, &pr);
  // The progress window: a bar, the count in its title. Closing it stops the
  // import (the movies done so far are kept; the next start does the rest).
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_CreateWindowAndRenderer("MechWarrior 3: converting the movies", 480, 64, 0, &win, &ren);
  bool stopped = false;
  int shown = -2;
  while (!pr.finished) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
      if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED || ev.type == SDL_EVENT_QUIT) {
        if (!stopped) SDL_KillProcess(proc, false);
        stopped = true;
      }
    const int total = pr.total, done = pr.done;
    if (win && done != shown) {
      shown = done;
      char title[96];
      if (total > 0) std::snprintf(title, sizeof title, "MechWarrior 3: converting the movies (%d of %d)", done, total);
      else std::snprintf(title, sizeof title, "MechWarrior 3: converting the movies");
      SDL_SetWindowTitle(win, title);
      if (total > 0)
        set_ready_status("Importing your movies (first start only): " + std::to_string(done) + " of " +
                         std::to_string(total) + "...");
    }
    if (ren) {
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      SDL_RenderClear(ren);
      const SDL_FRect frame{16, 20, 448, 24};
      SDL_SetRenderDrawColor(ren, 0, 160, 0, 255);
      SDL_RenderRect(ren, &frame);
      if (total > 0) {
        const SDL_FRect bar{18, 22, 444.0f * static_cast<float>(done) / static_cast<float>(total), 20};
        SDL_RenderFillRect(ren, &bar);
      }
      SDL_RenderPresent(ren);
    }
    SDL_Delay(30);
  }
  reader.join();
  set_ready_status("Starting MechWarrior 3...");
  int code = -1;
  SDL_WaitProcess(proc, true, &code);
  SDL_DestroyProcess(proc);
  if (ren) SDL_DestroyRenderer(ren);
  if (win) SDL_DestroyWindow(win);
  const int left = movies_missing(install, write_dir);
  if (stopped) {
    std::printf("mw3: the import was stopped; %d movies left for the next start\n", left);
    return false;
  }
  if (code != 0 || left > 0) {
    std::ofstream(failed) << "mw3import exited " << code << ", " << left << " movies not made\n";
    std::printf("mw3: the import failed (%d movies not made); the game plays without them\n", left);
    return false;
  }
  fs::remove(failed, ec);
  std::printf("mw3: %d movies imported\n", missing);
  return true;
}
}  // namespace

bool first_run_import(const std::string& install, const std::string& write_dir, bool force) {
  bool ok = import_from(install, write_dir, force);
  // Ours: the movies a disc gave the write dir's video\ (the launcher's ADD
  // FROM MY DISC, for an install whose movies stayed on the CD).
  std::error_code ec;
  if (!fs::equivalent(install, write_dir, ec)) ok = import_from(write_dir, write_dir, force) && ok;
  return ok;
}

}  // namespace sdlfe
