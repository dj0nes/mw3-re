#include "gamez/zgame/zgame_table.h"

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
                    zcls::mw3_node_set_active, zcls::mw3_light_create, zeff::mw3_anim_read_zbd};
const Table* g_game = &kMw3;
}  // namespace

const Table& mw3_table() { return kMw3; }
const Table& game() { return *g_game; }
void set_game(const Table& t) { g_game = &t; }

}  // namespace zgame
