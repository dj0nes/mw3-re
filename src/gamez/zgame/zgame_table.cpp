#include "gamez/zgame/zgame_table.h"

namespace zgame {
namespace {
const Table kMw3 = {"MechWarrior 3", "Mech3.exe 1.2"};
const Table* g_game = &kMw3;
}  // namespace

const Table& mw3_table() { return kMw3; }
const Table& game() { return *g_game; }
void set_game(const Table& t) { g_game = &t; }

}  // namespace zgame
