#pragma once
#include <cstdint>
#include <vector>

#include "pm/gamez/zmodel/gmod_clip3d.h"
#include "pm/gamez/zmodel/gmod_const.h"

namespace zcls { struct Node; }

// C:\zipper\branchep1\gamez\zModel\gmod_matl.c: Pirate's Moon's Direct3D model
// draw 0x00543b10, a rewrite of MW3's 0x0053f2e0 (docs/pm-map.md, gmod_matl.c).
// It stops at the same seam, zmod::DrawTable. In progress.
namespace pm::zmod {

struct DrawState {
  // The model's camera-space vertices. FIX (A): the exe's buffer at 0x008d4bac
  // holds 2,048 unchecked; this one grows.
  std::vector<float> cam;   // [0x008dabac]
  std::vector<float> normals;  // [0x008dabb0]  the model's normals in camera space
  int32_t default_model = 0;  // 0x00a4e22c  1 while a type-0 model draws
  // The light map's gate. FIX (A): the device's caps ([0x008ccc68], [0x008ccc6c]) are
  // what a one-shader draw always has (docs/fixes.md); the option stays: 0 auto, 1 and
  // 2 the exe's three and two stages, -1 off, from LIGHT MAPS (0x0040b270).
  int32_t light_map_option = 0;  // 0x008ccc64
  void (*bake)(Model& m, ::zcls::Node& node) = nullptr;  // 0x0055fb30, phase 4
};
DrawState& draw_state();

// 0x005416e0: a model's mean polygon vertex, shared ones counted each time.
void facade_centre(const Model& m, float out[3]);

// 0x00542a70: the top of the matrix stack by the model's type (0 default, 1 a
// facade, 2 points, drawn here). The caller has pushed; on 0 (points) the push
// is popped here and nothing more is to draw. `centre` is a facade's, with
// model flag 0x40.
int prepare_model(Model& m, float centre[3]);

// One polygon's record (0x38 bytes in the exe; the clippers' part is ClipPoly).
struct Record : ClipPoly {
  Polygon* polygon = nullptr;     // +0x00
  float* normal = nullptr;        // +0x08  the face's, null on a strip
  float* normals = nullptr;       // +0x0c  a vertex's, with flag 0x1000 and vertex shading
};

// What 0x00543b10 works out for a model before its polygons.
struct ModelPass {
  uint32_t needs = 0;      // local_58: 1 fog, 2 lights, 4 tint
  uint32_t mask = 0;       // [0x008d4b08]
  bool shadows = false;    // local_7c: receives projected shadows
  bool light_maps = false; // local_94
  bool projected = false;  // local_a4: a reaching light with flags 0x10 and 0x40
  bool bake = false;       // local_8c: the static bake is good
  bool cull = false;       // local_a8: back faces tested
};

// One material ref's polygons: the arenas at 0x0091cbc0.., the records from
// [0x008dcbb4]. FIX (bug): the exe's arenas are fixed and unchecked; these are
// sized for the ref, clip growth included (fixes.md).
struct RefPass {
  std::vector<Record> records;
  std::vector<float> pos, normals, rec, tl, faces;
  std::vector<uint8_t> extra;
  std::vector<ClipShadows> shadows;
  int32_t strips = 0;      // local_c
  bool projected = false;  // local_80
  bool light_maps = false; // local_88
};

// 0x00543b10's model part, after prepare_model, up to the refs. 0 when the model
// draws nothing (no polygons, or every vertex out of one plane). The model's own
// lights (0x00547dd0) are not ported yet: no shipped world model has any.
int begin_model(::zcls::Node& node, Model& m, uint32_t mask, ModelPass& pass);
// The ref loop's first half: back faces, plane codes, the records and what they
// point at. 0 when the ref's material or the fade rules it out.
int build_records(Model& m, const MaterialRef& ref, const ModelPass& pass, RefPass& out);
// 0x0055ebf0: which lights reach the sphere; their count.
int32_t lights_reaching(const float centre[3], float radius);

}  // namespace pm::zmod
