#pragma once
#include <cstdint>

// Ours (Owen, 2026-11-01): cheat codes in single player (campaign, Instant
// Action, training), typed into a one-line box over the running mission;
// refused in a network game, where the keypad keys stay the chat's. Each
// lasts for the mission it was typed in. Not original code; no cheated flag
// (docs/cheats.md).
namespace mech3 {

struct Mech;

// What is on (cleared at each mission's start).
struct Cheats {
  bool radar_all = false;    // black sheep wall: every mech and vehicle on the radar, at any range
  bool invincible = false;   // power overwhelming
  bool max_salvage = false;  // food for thought: the salvage's coin flips always won
  bool fast = false;         // operation cwal: the player's reload and heat dissipation 10x
  bool whole = false;        // while there is no cow level kills: salvaged as if undamaged
};
Cheats& cheats();
void cheats_clear();

enum class CheatCode {
  kNone = -1,
  kRadarAll,    // "black sheep wall"
  kLose,        // "game over man"
  kInvincible,  // "power overwhelming"
  kWin,         // "there is no cow level"
  kMaxSalvage,  // "food for thought"
  kFast,        // "operation cwal"
};
// The code typed, case and extra spaces ignored.
CheatCode cheat_code(const char* text);
const char* cheat_name(CheatCode c);

// What the codes reach in the program (app_cheats.cpp); null calls are skipped.
struct CheatCalls {
  bool (*network)() = nullptr;                            // a network game: no cheats
  Mech* (*player)() = nullptr;                            // the mission's player (Shift+Enter's)
  void (*hud_line)(float seconds, const char* text) = nullptr;  // 0x0040fe20's message panel line
  void (*set_invincible)(Mech& m, bool on) = nullptr;     // Mech +0x630, as program_mech_set_invincible
  void (*lose)() = nullptr;                               // Missions::lose 0x0048f9e0
  void (*win)(Mech& player) = nullptr;                    // the cow level: objectives, kills, Missions::complete
};
CheatCalls& cheat_calls();

// A line typed into the box: the code's effect, its HUD line and a log line.
// The toggles turn off when typed again. False when it is no code.
bool cheat_enter(Mech& player, const char* text);

// The box on the player's mech (in its chat panel's place); false in a
// network game, where the keys are the chat's.
bool cheat_open(Mech& m);
bool cheat_box_is(const void* panel);  // the chat panel is the cheat box

// Ours: Shift+Enter (Enter alone is NEXT_WEAPON) to command 0x9d, bound at
// each mission's load when no command has the key.
constexpr int32_t kCheatCommand = 0x9d;
constexpr uint32_t kCheatKey = 0x41c;
void register_cheat_key();

// Operation cwal's rate on a vehicle's reload and heat dissipation: 10 on
// the player's, else 1.
inline float cheat_rate(bool player) { return player && cheats().fast ? 10.0f : 1.0f; }

}  // namespace mech3
