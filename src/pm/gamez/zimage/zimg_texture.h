#pragma once
#include <cstdio>

#include "gamez/zimage/zimg_texture.h"

// C:\zipper\branchep1\gamez\zImage\zimg_texture.cpp: what Pirate's Moon
// changed in the texture table (docs/pm-map.md, gamez.zbd 41). The table is
// zimg's; PM's file entries are 0x2c bytes and carry no mip chain.
namespace pm::zimg {

// Pirate's Moon 1.0 @ 0x00531920: `count` entries of 0x2c (the name at +0xc),
// each set to load (state 2) in the current group, its image, surface and mip
// cleared. The count, or 0 for none, -1 on an error.
int read_texture_directory(int count, std::FILE* f);

}  // namespace pm::zimg
