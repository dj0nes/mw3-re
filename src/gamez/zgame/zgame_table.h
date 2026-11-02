#pragma once

#include <cstdint>
#include <cstdio>

namespace zutl_zar {
struct Archive;
}
namespace zrdr {
struct Node;
}

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
  // zArchive (zutl_zar.h): MW3's version 1, PM's version 2 with its checksum.
  bool (*zar_read_table)(zutl_zar::Archive& a);   // 0x005767b0; PM 0x00597610
  bool (*zar_write_table)(zutl_zar::Archive& a);  // 0x00576720; PM 0x00597570
  // Ours: how many bytes entry `index`'s data may take, for the bounded
  // readers (MW3: its length; PM: up to the next entry, its motion.zbd's
  // lengths being 1).
  uint32_t (*zar_span)(const zutl_zar::Archive& a, int index);
  // zReader's loose text files (zreader.h): PM's adds #include.
  zrdr::Node* (*zrdr_parse_text)(std::FILE* file);  // 0x005563d0; PM 0x005782f0
};

const Table& mw3_table();
const Table& game();          // the running game's
void set_game(const Table& t);  // once, at start

}  // namespace zgame
