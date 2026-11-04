#pragma once
#include "gamez/zclass/cls_node.h"

// mech3ep1\\mission.cpp: the world's parameters from world.zrd, the part that makes the picture
// (docs/pm-map.md, The world's parameters). Left, waiting for the mission: wind, weather,
// temperature, jet ceiling, ai_headlights, underwater, cloud_cover.
namespace pm::mech3 {

// Pirate's Moon 1.0 @ 0x00495ba0: `defaults`, then `key` over them (null: none), onto the world
// and camera: fog, the camera's clip, the sun and the sky. `sun_option` the player's (0x0040b360):
// off, the sun's own light is out.
void world_read_params(::zcls::Node* world, ::zcls::Node* camera, const char* key, bool sun_option);

}  // namespace pm::mech3
