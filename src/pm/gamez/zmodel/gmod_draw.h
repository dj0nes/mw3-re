#pragma once
#include <cstdint>
#include <vector>

#include "gamez/zmodel/gmod_light.h"
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
  // 0x005989b0 and 0x00598aa0: a fogged polygon's fog colour mixes with the light's.
  void (*mix_fog)(const float rgb[3]) = nullptr;
  // 0x00588910: a model light on the lens flares' list: world point, screen point.
  void (*flare)(const float world[3], const float screen[3], uint32_t color, const uint8_t* light) = nullptr;
  // The lights by kind, null-ended (0x00a4da24, 0x00a4dc28, 0x00a4de2c).
  std::vector<::zmod::LightEntry*> directed, point, spot;
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
  float lit[3] = {};              // +0x28  the first vertex's light before its colour
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
  size_t at = 0, normal_at = 0, face_at = 0;  // the arenas' next free
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
// 0x00543b10's start and Pirate's Moon 1.0 @ 0x00547dd0: the model's lights (+0x20,
// 0x4c each) animated and drawn as points; those with +0x3c go on the lens flares'
// list. MW3's animation; the exe batches the opaque points (the same picture).
void draw_model_lights(Model& m);
// Pirate's Moon 1.0 @ 0x005602e0: the lights that reach the model into their kinds
// (directed, point, spot), every record's light set to white, fog 0 unless fogged.
void sort_lights(RefPass& ref);
// Pirate's Moon 1.0 @ 0x005603f0: a polygon's light in its work records: the static
// bake `baked` (null or a float a vertex) and the lights, then the tint. Non-zero when
// it did anything. Its flags: 0x10 not all white, 0x40 not all alike, 8 and 0x18 fogged
// by a saturating light or the tint, 0x20 that fog not alike.
int shade_polygon(Record& r, const float* baked, uint32_t needs);
// The rest of the ref loop's lighting (0x00544797..0x00544ad6), projected lights aside:
// each record shaded, then multiplied by its colour or the material's.
void light_records(const Model& m, const ModelPass& pass, RefPass& ref);
// Pirate's Moon 1.0 @ 0x0055fb30: each polygon's static light, from the lights with
// flag 0x200, into its scratch (not two-sided ones). The light-to-vertex vector carries
// over, as MW3's (zmod::bake_state): a spot's first vertex reads it unset.
void bake_static_light(Model& m);
// 0x00543b10 after the lighting: strips into triangles where something needs them
// apart, polygons with plane codes into a fan of them, the projected shadows' sets
// (MW3's collect_shadows 0x00540552), the light map's. Each is one of the exe's loops.
void split_strips(const ::zmod::Material& mat, const ModelPass& pass, RefPass& ref);
void split_fans(const ModelPass& pass, RefPass& ref);
void shadow_sets(const ::zmod::Material& mat, const ModelPass& pass, RefPass& ref);
void light_map_sets(RefPass& ref);
// 0x0055ebf0: which lights reach the sphere; their count.
int32_t lights_reaching(const float centre[3], float radius);

}  // namespace pm::zmod
