#include "gamez/zrender/zrndr_flare.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "pm/pm_game.h"
#include "pm/gamez/zclass/cls_clutter.h"
#include "pm/gamez/zclass/cls_world.h"
#include "pm/gamez/zmodel/gmod_draw.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "pm/gamez/zclass/cls_mechlib.h"
#include "pm/gamez/zclass/cls_node.h"
#include "pm/gamez/zclass/cls_zbd.h"
#include "pm/gamez/zclass/light.h"
#include "pm/gamez/zeffect/zeff_anim_init.h"
#include "pm/gamez/zimage/zimg_texture.h"
#include "pm/gamez/zreader/zrdr_main.h"
#include "pm/gamez/zutil/zutl_zar.h"

namespace pm {
namespace {
void render_setup() {
  ::zvid::overlay_list().set_limit(::zvid::OverlayList::kMaxPm);
  zmod::draw_state().flare = ::zrndr::flare_add;  // 0x00588910
}

const zgame::Table kPm = {"Pirate's Moon", "Pirate's Moon 1.0", zar::read_table, zar::write_table, zar::span,
                         zrdr::parse_text, zcls::read_zbd, zcls::node_alloc, zmod::walk_draw, zcls::world_cell_update, zcls::world_add_child, zcls::world_remove_node, zcls::world_make_grid, zcls::node_bound_box, zcls::node_set_active,
                         zcls::node_model_box, zcls::node_model_header, zcls::light_create, zeff::anim_read_zbd,
                         // mechlib (pm-map step 1e)
                         zcls::mechlib_open, zcls::mechlib_load,
                         zimg::set_light_map, zimg::set_additive, 0x800, true, false, zcls::clutter_draw_cell, zcls::clutter_free_cell, render_setup};
}  // namespace

const zgame::Table& table() { return kPm; }

// MW3's archive ends "version (1), count"; PM's "count, checksum" after a
// version word of 2 (docs/pm-map.md, zArchive). Only the last 12 bytes are read.
Install install_kind(const std::string& dir) {
  const std::filesystem::path p = std::filesystem::path(dir) / "zbd" / "reader.zbd";
  std::FILE* f = std::fopen(p.string().c_str(), "rb");
  if (!f) return Install::kNone;
  uint8_t tail[12] = {};
  const bool ok = std::fseek(f, -12, SEEK_END) == 0 && std::fread(tail, 1, 12, f) == 12;
  std::fclose(f);
  if (!ok) return Install::kUnknown;
  uint32_t v8, v12;
  std::memcpy(&v8, tail + 4, 4);
  std::memcpy(&v12, tail, 4);
  if (v8 == 1) return Install::kMw3;
  if (v12 == 2) return Install::kPm;
  return Install::kUnknown;
}

const char* install_words(Install k) {
  switch (k) {
    case Install::kNone: return "no zbd/reader.zbd: not a game install";
    case Install::kMw3: return "MechWarrior 3, not Pirate's Moon";
    case Install::kPm: return "Pirate's Moon";
    case Install::kUnknown: return "a zbd/reader.zbd neither game's";
  }
  return "";
}

}  // namespace pm
