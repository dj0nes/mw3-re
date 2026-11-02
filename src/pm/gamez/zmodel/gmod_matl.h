#pragma once
#include <cstdio>

#include "gamez/zmodel/gmod_matl.h"

// C:\zipper\branchep1\gamez\zModel\gmod_matl.c: what Pirate's Moon changed in
// the material table (docs/pm-map.md, gamez.zbd 41). The table is zmod's.
namespace pm::zmod {

// Pirate's Moon 1.0 @ 0x00555e00: a gamez.zbd's materials appended to the
// table in new blocks. The file's count and list heads are taken on the first
// load only; later ones put each material on the used or free list. Returns
// the first one's index (the file's material indices are offset by it), 0
// for an empty table in the file, -1 on a read error.
int read_materials(std::FILE* f);

}  // namespace pm::zmod
