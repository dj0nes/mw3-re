#pragma once
#include <cstdint>
#include <vector>

// C:\zipper\branchep1\gamez\zModel\gmod_matl.c: PM's camera-space near clippers
// for the Direct3D model draw 0x00543b10 (docs/pm-map.md, gmod_matl.c). Not
// MW3's six (gamez/zmodel/gmod_clip3d): no far reject, the near plane only.
namespace pm::zmod {

// The projected-shadow sets of a polygon. FIX (bug): the exe's record is 0x2358
// bytes from the draw's arena [0x00a4cfa4], 64 vertices a set (count +0x00, kind
// +0x20, xyz +0x3c, uv +0x1558), which a shipped 313-vertex polygon overruns; 1,536
// a set would be 220 MB of arena, so each set grows as it needs (fixes.md).
struct ClipShadows {
  int32_t count = 0;
  int32_t kind[7] = {};  // 2: the set has xyz as well
  std::vector<float> xyz[7];  // 3 a vertex
  std::vector<float> uv[7];   // 2 a vertex
};

// What the clippers read of the draw's polygon record (0x38 bytes in the exe).
struct ClipPoly {
  float* pos = nullptr;         // +0x04  camera space, 12 bytes a vertex
  float* rec = nullptr;         // +0x10  work records, 0x20 bytes
  float* tl = nullptr;          // +0x14  TL vertices, 0x20 bytes: tu, tv at +0x18
  ClipShadows* shadows = nullptr;  // +0x18
  uint32_t flags = 0;           // +0x1c  bit 8: the record's +0x10 too
  int32_t count = 0;            // +0x20
  uint8_t* extra = nullptr;     // +0x24  a byte a vertex: moved with the rest, never read here
};

// The globals they read and rewrite.
struct Clip3dState {
  float near_z = 0;             // 0x008d4b14  from the view (0x00548990)
  float* arena_pos = nullptr;   // 0x00a4cfb0  where a polygon that grew goes
  float* arena_rec = nullptr;   // 0x00a4cfac
  float* arena_tl = nullptr;    // 0x00a4cfb4
  uint8_t* arena_extra = nullptr;  // 0x00a4cfbc
};
Clip3dState& clip3d_state();

// Each: 1 to draw the polygon, 0 (count 0) with fewer than 3 vertices left.
int clip3d_plain(ClipPoly& p);            // 0x00549440  positions
int clip3d_lit(ClipPoly& p);              // 0x00549630  and the record's +0..+8 (+0x10 by flag 8)
int clip3d_textured(ClipPoly& p);         // 0x00549ab0  UVs, the record's +0x14..+0x1c (+0x10 by flag 8)
int clip3d_textured_lit(ClipPoly& p);     // 0x0054a450  UVs and the whole record
int clip3d_textured_shadow(ClipPoly& p);  // 0x0054a6f0  as 0x00549ab0 without +0x10, with shadow sets
int clip3d_textured_lit_shadow(ClipPoly& p);  // 0x0054aa50  everything

}  // namespace pm::zmod
