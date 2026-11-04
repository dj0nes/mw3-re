#pragma once
#include <cstdint>

#include "pm/gamez/zmodel/gmod_clip3d.h"

// C:\zipper\branchep1\gamez\zModel\gmod_matl.c: the Direct3D model draw's plane
// tests and its vertex colours (docs/pm-map.md, gmod_matl.c).
namespace pm::zmod {

// The view's planes, camera space. near is the clippers' (Clip3dState).
struct CullState {
  uint32_t planes = 0;   // 0x008d4b08  0x10 near, 0x20 far, 1, 2, 4, 8 the sides
  float far_z = 0;       // 0x008d4b20  from the view (0x00548990)
  float slope_x = 0;     // 0x008d43a4  from the projection (0x0053b730)
  float slope_y = 0;     // 0x008d43a8
  uint32_t codes[1024] = {};  // 0x008dbbb4  a code per model vertex
  float fade = 1.0f;     // 0x00a4e078  the vertices' alpha, 1 opaque
};
CullState& cull_state();

// 0x005403d0
void set_fade(float fade);

// 0x0054faa0: each of n positions' code, for the planes asked. 0 when every
// vertex is out of one plane (the rest of the codes then unset). Plane 8 never
// rejects: its count runs on from the plane before instead of starting at 0.
int plane_codes(const float* pos, int n, uint32_t* codes);

// 0x0054fc60: a polygon's n vertices' codes, ORed into *out. 0 when all n are
// out of one plane.
int polygon_codes(int n, const int32_t* vertices, uint32_t* out);

// 0x005439e0: each vertex's D3D colour (the TL vertex's +0x10) from its work
// record's +0..+8, with the fade's alpha.
void tl_colours(ClipPoly& p);

}  // namespace pm::zmod
