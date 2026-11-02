#pragma once
#include <string>

#include "gamez/zgame/zgame_table.h"

// Pirate's Moon's side of the shared engine (docs/games-layout.md): its table
// of the engine functions it changed, and what tells its install from MW3's.
// MW3's code never includes anything from src/pm/.
namespace pm {

const zgame::Table& table();

// What a folder holds, by its zbd/reader.zbd: the zArchive's version, the
// table's last word (MW3 1, PM 2; docs/pm-map.md, The data formats).
enum class Install { kNone, kMw3, kPm, kUnknown };
Install install_kind(const std::string& dir);
const char* install_words(Install k);  // for the player: what the folder is

}  // namespace pm
