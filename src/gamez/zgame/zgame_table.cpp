#include "gamez/zgame/zgame_table.h"

#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_zbd.h"
#include "gamez/zclass/light.h"
#include "gamez/zeffect/zeff_anim_init.h"

#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_zar.h"

namespace zgame {
namespace {
const Table kMw3 = {"MechWarrior 3", "Mech3.exe 1.2", zutl_zar::read_table, zutl_zar::write_table, zutl_zar::length_span,
                    zrdr::parse_text, zcls::read_zbd, zcls::mw3_node_alloc,
                    zmod::draw_model, zcls::mw3_area_update, zcls::mw3_world_add_child, zcls::mw3_world_remove_child, zcls::mw3_world_make_grid, zcls::mw3_node_bound_box, zcls::mw3_node_set_active, zcls::mw3_node_model_box, zcls::mw3_node_model_header,
                    zcls::mw3_light_create, zeff::mw3_anim_read_zbd,
                    // mechlib (pm-map step 1e)
                    zcls::mw3_mechlib_open, zcls::mw3_mechlib_load,
                    nullptr, nullptr, 0x200, false, true, nullptr, nullptr, nullptr};
const Table* g_game = &kMw3;
}  // namespace

const Table& mw3_table() { return kMw3; }
const Table& game() { return *g_game; }
void set_game(const Table& t) { g_game = &t; }

}  // namespace zgame
