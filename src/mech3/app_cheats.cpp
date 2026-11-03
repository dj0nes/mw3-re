// The cheat codes (mech3/cheats) wired to the program's objects: the
// network check, the message panel, the player's mech, the missions' win
// and loss. Not original code (docs/cheats.md).
#include <cstring>

#include "mech3/app_mech.h"
#include "mech3/app_sensors.h"
#include "mech3/chat_panel.h"
#include "mech3/cheats.h"
#include "mech3/mech.h"
#include "mech3/mech_net.h"
#include "mech3/mech_objective.h"
#include "mech3/message_panel.h"

namespace mech3 {

Objectives& mission_objectives();          // app_mission.cpp
ObjectiveHooks& mission_objective_hooks();

namespace {
// Each objective under o done, as when its condition is met (the salvage it gives too).
void complete_all(Objective& o, ObjectiveHooks& h) {
  if (o.kind == Objective::kGroup)
    for (auto& it : static_cast<ObjectiveGroup&>(o).items) complete_all(*it, h);
  if (!o.done) complete(o, 1, h);  // 0x0045e110
}

// There is no cow level: every enemy mech and vehicle still up killed by the
// player and salvaged whole, the coin flips won, the primary and secondary
// objectives done, the win.
void win(Mech&) {
  Cheats& c = cheats();
  c.max_salvage = true;
  c.whole = true;
  void* gun = chain_weapon();
  for (const ChainTarget& t : chain_hostiles()) {
    if (std::strcmp(t.kind, "mech") != 0 && std::strcmp(t.kind, "vehicle") != 0) continue;
    for (int i = 0; i < 200 && chain_alive(t.h); ++i) chain_hit(t.h, gun);  // true once it is down
  }
  c.whole = false;
  Objectives& o = mission_objectives();
  ObjectiveHooks& h = mission_objective_hooks();
  if (o.primary) complete_all(*o.primary, h);
  if (o.secondary) complete_all(*o.secondary, h);
  mission_win();  // 0x0048f740
}
}  // namespace

bool cheat_box_open(void* mech) {
  return entity_is_mech(mech) && static_cast<Mech*>(mech)->chat && cheat_box_is(static_cast<Mech*>(mech)->chat);
}

void cheat_box_type(void* mech, const char* text) {
  if (!cheat_box_open(mech)) return;
  ChatPanel* p = static_cast<Mech*>(mech)->chat;
  if (std::strcmp(text, "\x1b") == 0) {
    p->key(0x1b, 0x01);  // DIK_ESCAPE
    return;
  }
  for (const char* k = text; *k; ++k) p->key(*k, 0);
  p->key('\r', 0x1c);  // DIK_RETURN
}

float mech_heat_dissipation(void* mech) { return entity_is_mech(mech) ? static_cast<Mech*>(mech)->heat_dissipation : 0.0f; }

void wire_cheats(bool on) {
  CheatCalls& c = cheat_calls();
  c = CheatCalls{};
  if (!on) return;
  c.network = [] {
    const NetCalls& n = net_calls();
    return n.game && n.game() != nullptr;  // 0x00479480, as the chat keys ask it
  };
  c.player = [] { return world().mission_player; };
  c.hud_line = [](float seconds, const char* text) {
    if (message_panel()) message_panel_add_for(seconds, "%s", text);
  };
  c.set_invincible = [](Mech& m, bool on) { program_mech_set_invincible(&m, on); };
  c.lose = mission_lose;  // 0x0048f9e0
  c.win = win;
}

}  // namespace mech3
