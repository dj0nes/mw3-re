#pragma once
#include <string>

namespace platform { class Render3D; }

// mw3pm's debug start (ours): one Pirate's Moon world on screen before the
// game around it. The polygons go through the real seam (zmod::DrawTable) by a
// dev-only unlit draw, which 0x00543b10's submit replaces (docs/pm-map.md).
namespace frontend {

// Empty, or what went wrong. `eye` is set over the middle of the world.
std::string pm_world_load(const std::string& mission, platform::Render3D* render, int width, int height, float eye[3]);
void pm_world_frame(const float eye[3], float yaw, float pitch, platform::Render3D* render);

}  // namespace frontend
