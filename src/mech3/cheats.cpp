#include "mech3/cheats.h"

#include <cctype>
#include <cstdio>
#include <string>

#include "gamez/zinput/zinput.h"
#include "mech3/chat_panel.h"
#include "mech3/mech.h"

namespace mech3 {

namespace {
Cheats g_cheats;
CheatCalls g_calls;

constexpr const char* kNames[] = {
    "black sheep wall", "game over man", "power overwhelming", "there is no cow level", "food for thought",
    "operation cwal",
};

// The box: the chat panel's console with its own title, Enter the code and
// Escape the close.
class CheatPanel : public ChatPanel {
 public:
  explicit CheatPanel(Mech* m) : ChatPanel("CHEAT", m) {}
  void line_entered() override {
    std::string t = line_text();
    const size_t gt = t.find('>');
    if (gt != std::string::npos) t = t.substr(gt + 1);
    Mech* m = mech;
    if (m) mech_chat_close(*m);  // deletes this
    if (m) cheat_enter(*m, t.c_str());
  }
  void on_escape() override {
    if (mech) mech_chat_close(*mech);
  }
};

void open_for_player(int32_t) {
  if (Mech* m = g_calls.player ? g_calls.player() : nullptr) cheat_open(*m);
}
}  // namespace

Cheats& cheats() { return g_cheats; }
void cheats_clear() { g_cheats = Cheats{}; }
CheatCalls& cheat_calls() { return g_calls; }

CheatCode cheat_code(const char* text) {
  std::string s;
  for (const char* p = text ? text : ""; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (std::isspace(c)) {
      if (!s.empty() && s.back() != ' ') s += ' ';
    } else {
      s += static_cast<char>(std::tolower(c));
    }
  }
  if (!s.empty() && s.back() == ' ') s.pop_back();
  for (int i = 0; i < static_cast<int>(sizeof kNames / sizeof kNames[0]); ++i)
    if (s == kNames[i]) return static_cast<CheatCode>(i);
  return CheatCode::kNone;
}

const char* cheat_name(CheatCode c) {
  const int i = static_cast<int>(c);
  return i >= 0 && i < static_cast<int>(sizeof kNames / sizeof kNames[0]) ? kNames[i] : "";
}

bool cheat_enter(Mech& player, const char* text) {
  if (g_calls.network && g_calls.network()) return false;
  const CheatCode c = cheat_code(text);
  bool on = true;
  switch (c) {
    case CheatCode::kNone:
      return false;
    case CheatCode::kRadarAll:
      on = g_cheats.radar_all = !g_cheats.radar_all;
      break;
    case CheatCode::kInvincible:
      on = g_cheats.invincible = !g_cheats.invincible;
      if (g_calls.set_invincible) g_calls.set_invincible(player, on);
      break;
    case CheatCode::kMaxSalvage:
      on = g_cheats.max_salvage = !g_cheats.max_salvage;
      break;
    case CheatCode::kFast:
      on = g_cheats.fast = !g_cheats.fast;
      break;
    case CheatCode::kLose:
    case CheatCode::kWin:
      break;
  }
  char line[64];
  std::snprintf(line, sizeof line, "Cheat %s: %s", on ? "enabled" : "disabled", cheat_name(c));
  if (g_calls.hud_line) g_calls.hud_line(5.0f, line);
  std::fprintf(stderr, "mw3: %s\n", line);
  if (c == CheatCode::kLose && g_calls.lose) g_calls.lose();
  if (c == CheatCode::kWin && g_calls.win) g_calls.win(player);
  return true;
}

bool cheat_open(Mech& m) {
  if (g_calls.network && g_calls.network()) return false;
  delete m.chat;
  m.chat = new CheatPanel(&m);
  return true;
}

bool cheat_box_is(const void* panel) { return dynamic_cast<const CheatPanel*>(static_cast<const ChatPanel*>(panel)) != nullptr; }

void register_cheat_key() {
  if (zin::key_command(static_cast<int32_t>(kCheatKey)) == 0) zin::bind(kCheatCommand, nullptr, kCheatKey, 0, 0, 0);
  zin::set_callback(kCheatCommand, open_for_player);
}

}  // namespace mech3
