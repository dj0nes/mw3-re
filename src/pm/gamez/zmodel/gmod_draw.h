#pragma once
#include <cstdint>
#include <vector>

#include "pm/gamez/zmodel/gmod_const.h"

// C:\zipper\branchep1\gamez\zModel\gmod_matl.c: Pirate's Moon's Direct3D model
// draw 0x00543b10, a rewrite of MW3's 0x0053f2e0 (docs/pm-map.md, gmod_matl.c).
// It stops at the same seam, zmod::DrawTable. In progress.
namespace pm::zmod {

struct DrawState {
  // The model's camera-space vertices. FIX (A): the exe's buffer at 0x008d4bac
  // holds 2,048 unchecked; this one grows.
  std::vector<float> cam;   // [0x008dabac]
  int32_t default_model = 0;  // 0x00a4e22c  1 while a type-0 model draws
};
DrawState& draw_state();

// 0x005416e0: a model's mean polygon vertex, shared ones counted each time.
void facade_centre(const Model& m, float out[3]);

// 0x00542a70: the top of the matrix stack by the model's type (0 default, 1 a
// facade, 2 points, drawn here). 0 when the model has no polygons to draw.
// `centre` is a facade's, with model flag 0x40.
int prepare_model(Model& m, float centre[3]);

}  // namespace pm::zmod
