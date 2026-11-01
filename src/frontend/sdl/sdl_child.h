// Ours, not original code: a helper program (mw3import, the Video for Windows
// helper) started with its stdin and stdout piped to us. On Windows, when mw3
// has no console of its own (the package build is a GUI program, docs/
// windows.md), a console child would open a console window beside the game;
// SDL starts it without one only as a "background" process (CREATE_NO_WINDOW),
// whose exit code then always reads 0 (callers judge the result otherwise).
#pragma once

#include <string>

#include <SDL3/SDL.h>

namespace sdlfe {

SDL_Process* start_child(const char* const* args);

// With no console (Windows: the package build, or a double-click start),
// stdout and stderr into `write_dir`/mw3.log, the start before's kept as
// mw3.old.log. False (nothing changed) with a console or elsewhere.
// Here, not in mw3_main.cpp: <windows.h>'s near/far macros break game headers.
bool log_to_file(const std::string& write_dir);

// The process's resident memory in KB (the soak test's lines); 0 where unknown.
size_t resident_kb();

}  // namespace sdlfe
