#pragma once
#include "gamez/zclass/cls_node.h"

// zClass\Light.c
namespace zcls {

Node* light_create();              // the running game's (zgame::Table): MW3's 0x004fa720
Node* mw3_light_create();          // 0x004fa720
// 1 while a world still lists it; a static light (0x100) bumps the frame stamp.
int light_delete(Node* node);      // 0x004fa850

// The setters the mission's world.zrd reader calls on the sun (mission_world).
// Each marks the light changed (+0xb0 |= 1) and, for a static light (0x100),
// bumps the frame stamp so cached models bake again (gmod_bake). 0 or 5.
int light_set_orientation(Node* node, float x, float y, float z);       // 0x004faff0  data +0..+8
int light_get_orientation(Node* node, float* x, float* y, float* z);    // 0x004fb060
int light_set_ambient(Node* node, float v);                             // 0x004fa940  data +0xa0
int light_set_diffuse(Node* node, float v);                             // 0x004fa9b0  data +0x9c

// The light state event's setters (zeff_anim_run 0x00503390). Flags +0xb0
// (mech3ax): 0x20 ambient only, 0x40 diffuse only, 0x80 directional, 0x100
// saturated (recomputed: directional, neither "only" bit, white).
int light_set_bit2(Node* node, int32_t on);                             // 0x004faa20
int light_set_bit3(Node* node);                                         // 0x004faaa0  and clears bit 4
int light_set_bit4(Node* node);                                         // 0x004fab20  and clears bit 3
int light_set_ambient_only(Node* node, int on);                         // 0x004faba0
int light_set_diffuse_only(Node* node, int on);                         // 0x004fac70
int light_set_directional(Node* node, int on);                          // 0x004fad30
int light_set_range(Node* node, float a, float b);                      // 0x004fadf0  +0xb4..+0xc4
int light_get_range(Node* node, float* near_range, float* far_range);   // 0x004faf20
int light_set_position(Node* node, float x, float y, float z);          // 0x004faf80  +0x0c
int light_get_color(Node* node, float* r, float* g, float* b);          // 0x004fb4b0  +0xa4
int light_set_color(Node* node, float r, float g, float b);             // 0x004fb510

}  // namespace zcls
