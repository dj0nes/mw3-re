// Pirate's Moon's side of the engine (src/pm/pm_game, docs/games-layout.md):
// the game table and telling PM's install from MW3's.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"
#include "gamez/zgame/zgame_table.h"
#include "pm/pm_game.h"

namespace {
bool have(const char* dir) { return std::filesystem::exists(std::filesystem::path(dir) / "zbd" / "reader.zbd"); }
}  // namespace

TEST(pm_table_is_picked_at_run_time) {
  EXPECT(&zgame::game() == &zgame::mw3_table());
  zgame::set_game(pm::table());
  EXPECT(std::string(zgame::game().name) == "Pirate's Moon");
  EXPECT(std::string(zgame::game().exe) == "Pirate's Moon 1.0");
  zgame::set_game(zgame::mw3_table());
  EXPECT(std::string(zgame::game().name) == "MechWarrior 3");
}

TEST(pm_install_told_from_mw3s) {
  if (!have(MW3PM_GAME_DIR) || !have(MW3_GAME_DIR)) {
    std::printf("  skipped: no PM or MW3 install\n");
    return;
  }
  EXPECT(pm::install_kind(MW3PM_GAME_DIR) == pm::Install::kPm);
  EXPECT(pm::install_kind(MW3_GAME_DIR) == pm::Install::kMw3);
  const std::string tmp = MW3_TEST_TMP;
  EXPECT(pm::install_kind(tmp) == pm::Install::kNone);
  std::filesystem::create_directories(tmp + "/zbd");
  { std::ofstream(tmp + "/zbd/reader.zbd", std::ios::binary) << "short"; }
  EXPECT(pm::install_kind(tmp) == pm::Install::kUnknown);
  { std::ofstream(tmp + "/zbd/reader.zbd", std::ios::binary) << std::string(12, '\0'); }
  EXPECT(pm::install_kind(tmp) == pm::Install::kUnknown);
}
