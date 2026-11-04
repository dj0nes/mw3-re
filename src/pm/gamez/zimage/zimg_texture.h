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

// [0x00683cb0]: the one light map's texture (docs/pm-map.md, gmod_matl.c), set
// by the script's LightMapTexture and read by the Direct3D model draw.
::zimg::TextureEntry* light_map();
void set_light_map(::zimg::TextureEntry* texture);
void set_additive(::zimg::TextureEntry* texture);  // 0x005b6491: drawn adding light (zvid_tl)

// Pirate's Moon 1.0 @ 0x00530810: MW3's 0x00516f30, and the light map gone.
void textures_free();

}  // namespace pm::zimg
