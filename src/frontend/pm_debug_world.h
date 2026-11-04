#pragma once
#include <string>

namespace platform { class Render3D; }

// mw3pm's debug start (ours): one Pirate's Moon world on screen before the
// game around it. The models go through 0x00543b10 (docs/pm-map.md).
namespace frontend {

// Empty, or what went wrong. `eye` is set over the middle of the world.
std::string pm_world_load(const std::string& mission, platform::Render3D* render, int width, int height, float eye[3]);
// Debug only: one light map light at a world point, since the scripts that set them are not
// ported: the shipped `lightmap` texture and a white light of that range. The world goes dark.
void pm_world_map_light(const float pos[3], float range);
void pm_world_frame(const float eye[3], float yaw, float pitch, platform::Render3D* render);

}  // namespace frontend
