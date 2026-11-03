#include "check.h"
#include "mech3/cheats.h"
#include "mech3/mech.h"

TEST(heat_sinks_rate_and_loss) {
  mech3::HeatSinks s{3, 10, false};  // tech, count, doubles
  EXPECT(s.rate() == 1.25f);
  s.doubles = true;
  EXPECT(s.rate() == 2.5f);
  EXPECT(!s.lose(3));
  EXPECT(s.count == 7);
  EXPECT(!s.lose(20));
  EXPECT(s.count == 0);
}

TEST(mech_add_heat_accumulates) {
  mech3::Mech m{};
  m.add_heat(0.25f);
  m.add_heat(0.5f);
  EXPECT(m.heat_added == 0.75f);
}

// The cheat codes (ours, docs/cheats.md): what the box takes as a code, and
// none taken in a network game; typed again, a toggle goes off.
TEST(cheat_codes_parse_and_refuse_in_a_network_game) {
  using mech3::CheatCode;
  EXPECT(mech3::cheat_code("black sheep wall") == CheatCode::kRadarAll);
  EXPECT(mech3::cheat_code("  Power   OVERWHELMING ") == CheatCode::kInvincible);
  EXPECT(mech3::cheat_code("There Is No Cow Level") == CheatCode::kWin);
  EXPECT(mech3::cheat_code("game\tover man") == CheatCode::kLose);
  EXPECT(mech3::cheat_code("FOOD FOR THOUGHT") == CheatCode::kMaxSalvage);
  EXPECT(mech3::cheat_code("operation cwal") == CheatCode::kFast);
  EXPECT(mech3::cheat_code("operationcwal") == CheatCode::kNone);
  EXPECT(mech3::cheat_code("black sheep") == CheatCode::kNone);
  EXPECT(mech3::cheat_code("") == CheatCode::kNone);
  mech3::cheats_clear();
  mech3::CheatCalls kept = mech3::cheat_calls();
  static bool network = true, invincible = false;
  mech3::cheat_calls() = mech3::CheatCalls{};
  mech3::cheat_calls().network = [] { return network; };
  mech3::cheat_calls().set_invincible = [](mech3::Mech&, bool on) { invincible = on; };
  mech3::Mech m{};
  EXPECT(!mech3::cheat_open(m) && m.chat == nullptr);
  EXPECT(!mech3::cheat_enter(m, "power overwhelming") && !mech3::cheats().invincible && !invincible);
  network = false;
  EXPECT(mech3::cheat_enter(m, "power overwhelming") && mech3::cheats().invincible && invincible);
  EXPECT(mech3::cheat_enter(m, "operation cwal") && mech3::cheat_rate(true) == 10.0f && mech3::cheat_rate(false) == 1.0f);
  EXPECT(mech3::cheat_enter(m, "power overwhelming") && !mech3::cheats().invincible && !invincible);
  EXPECT(!mech3::cheat_enter(m, "power"));
  mech3::cheats_clear();
  EXPECT(mech3::cheat_rate(true) == 1.0f);
  mech3::cheat_calls() = kept;
}

