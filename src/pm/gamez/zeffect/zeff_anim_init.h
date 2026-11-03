#pragma once

namespace zcls { struct Node; }
namespace zeff { struct AnimDef; }

// C:\zipper\branchep1\gamez\zEffect\zeff_anim_init.c: Pirate's Moon's
// anim.zbd (version 50) into the shared zeff::AnimState and AnimDef, in
// MW3's layout with PM's own fields beside it (docs/pm-map.md, anim.zbd 50).
// Shared code reaches the reader as zeff::anim_read_zbd.
namespace pm::zeff {

// Pirate's Moon 1.0 @ 0x00523140. Against MW3's 0x0050a0e0: a 16-byte
// header (a time stamp), a 0x6c block, 0x10c defs whose node may come by a
// path of names, optional reset and second sequences, objects and nodes
// found under a parent and optionally only in the def's own nodes, a def
// whose node or root is missing marked corrupt (5) and skipped where MW3
// stops the read, a failed puffer undone, and the file's SI scripts.
int anim_read_zbd();

// Pirate's Moon 1.0 @ 0x00522550: MW3's 0x00509640 with the list search
// left out when `local` (MW3 reads the def's flag 0x80000 itself).
::zcls::Node* anim_find_node(::zeff::AnimDef* def, const char* name, int local);

}  // namespace pm::zeff
