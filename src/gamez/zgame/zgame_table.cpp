#include "gamez/zgame/zgame_table.h"

#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_zar.h"

namespace zgame {
namespace {
const Table kMw3 = {"MechWarrior 3", "Mech3.exe 1.2", zutl_zar::read_table, zutl_zar::write_table, zutl_zar::length_span,
                    zrdr::parse_text};
const Table* g_game = &kMw3;
}  // namespace

const Table& mw3_table() { return kMw3; }
const Table& game() { return *g_game; }
void set_game(const Table& t) { g_game = &t; }

}  // namespace zgame
