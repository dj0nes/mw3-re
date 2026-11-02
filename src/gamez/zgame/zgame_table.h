#pragma once

// Ours, not the exe's: which game the shared engine runs (docs/games-layout.md).
// An engine function Pirate's Moon changed is picked through the running
// game's table at run time, never by #ifdef in shared code. MW3's table is the
// default; mw3pm sets PM's (src/pm/pm_game.cpp) before anything loads. Each
// entry arrives with its port (docs/pm-map.md, A porting order); a game's
// table names only functions, no data.
namespace zgame {

struct Table {
  const char* name;  // for the start-up print: "MechWarrior 3", "Pirate's Moon"
  const char* exe;   // the exe its address comments name: "Mech3.exe 1.2", "Pirate's Moon 1.0"
};

const Table& mw3_table();
const Table& game();          // the running game's
void set_game(const Table& t);  // once, at start

}  // namespace zgame
