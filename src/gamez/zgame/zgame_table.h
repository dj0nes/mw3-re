#pragma once

#include <cstdint>
#include <cstdio>

namespace zutl_zar {
struct Archive;
}
namespace zrdr {
struct Node;
}
namespace zcls {
struct Node;
struct MechLib;
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
  // A world file, gamez.zbd (zcls/cls_zbd.h): PM's stacks onto what is loaded.
  int (*zcls_read_zbd)(const char* path);  // 0x004ff520; PM 0x00516bc0
  // A new node, light and the active switch (zcls/cls_node.h, light.h):
  // PM's node points at its box, its light data is 0x100 bytes.
  zcls::Node* (*zcls_node_alloc)();                      // 0x004eacd0; PM 0x004fc550
  int (*zcls_node_set_active)(zcls::Node* node, int on);  // 0x004eb170; PM 0x004fca50
  zcls::Node* (*zcls_light_create)();                    // 0x004fa720; PM 0x0050e470
  // A mission's anim.zbd (zeff/zeff_anim_init.h): PM's is version 50.
  int (*zeff_anim_read_zbd)();  // 0x0050a0e0; PM 0x00523140
  // mechlib (pm-map step 1e): mechlib.zbd (zcls/cls_mechlib.h), PM's version
  // 41 with PM's nodes and models.
  bool (*zcls_mechlib_open)(zcls::MechLib& lib, const char* path);  // 0x004fc6d0; PM 0x00510910
  zcls::Node* (*zcls_mechlib_load)(zcls::MechLib& lib, const char* name);  // 0x004fc8f0; PM 0x00510b90
};

const Table& mw3_table();
const Table& game();          // the running game's
void set_game(const Table& t);  // once, at start

}  // namespace zgame
