#pragma once
#include <cstdint>

#include "gamez/zmodel/gmod_clip2d.h"
#include "gamez/zmodel/gmod_clip3d.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_light.h"

// The Direct3D model draw ([0x74a2e0] = 0x53f2e0) and what it needs from the
// model (0x53daa0): camera-space transform, backface test, clip, projection
// and the call into the draw table (docs/render.md section 3,
// docs/drafts/modeldraw.md). Original file unknown: between gmod_const.c and
// gmod_cookie.cpp.
namespace zcls { struct Node; }

namespace zmod {

constexpr int kModelVerts = 1024;  // 0x0074039c .. 0x0074339c (0x526510)
constexpr int kPolyVerts = 64;     // the per-polygon buffers (gmod_clip3d.h)

// The model draw's own buffers. cam/normals are the targets of [0x74639c] and
// [0x7463a0]; the polygon's camera-space vertices, UVs and work records live
// in clip3d_buffers().cam and clip2d().tl / .work (0x7476a4, 0x72f41c, 0x7481a8).
struct DrawBuffers {
  float cam[kModelVerts][3] = {};         // 0x0074039c  the model's vertices in camera space
  float normals[kModelVerts][3] = {};     // 0x0074339c  its normals, rotated and normalised
  int32_t poly_normals = 0;               // 0x00747ca4  1: poly_normal holds this polygon's normals
  float poly_normal[kPolyVerts][3] = {};  // 0x00747ca8  reversed like the vertices
  // A strip split into triangles keeps the whole strip here (0x540038..).
  float save_cam[kPolyVerts][3] = {};     // 0x0064cbd8
  float save_uv[kPolyVerts][2] = {};      // 0x0063fbd8
  VertexWork save_work[kPolyVerts] = {};  // 0x00641bd8
  float save_normal[kPolyVerts][3] = {};  // 0x00649bd8
};
DrawBuffers& draw_buffers();

// Render state the draw reads, owned by the model renderer (0x526510 sets the
// defaults kept here).
struct DrawState {
  float opacity = 1.0f;          // 0x0074a26c  the pushed node opacity
  int32_t overlay = 0;           // 0x0074a268  node flag 0x800000: drawn last, no depth test
  int32_t shadows = 0;           // 0x0074a264  projected shadows on
  float priority_scale = 0.02f;  // 0x0074a284  0.02 with a device, 0.01 without
  int32_t vertex_shading = 0;    // 0x00749808  SetVertexShading
  int32_t second_pass = 0;       // 0x0074a32c  draw untextured polygons again into `second`
  int32_t default_model = 0;     // 0x0074a3cc  1 while a type-0 model draws
  int32_t static_lights = 1;     // 0x005ef740
  float backface = 0.005f;       // 0x005ef73c  unnormalised threshold
  ClipPlanes clip;               // 0x007402f8  plane mask, screen rect, near, far (0x526e10)
  float point_rect[4] = {};      // 0x0074031c  left, top, right, bottom (inclusive) for points
  ClipPlanes second;             // 0x00740338  rect of the second pass (+0 its side mask)
  float second_scale[2] = {};    // 0x0074038c  x, y
  float second_offset[2] = {};   // 0x00740394  x, y
  const float* camera_data = nullptr;  // [0x007402f4]  the camera data (+0x3c its yaw, facades)
};
DrawState& draw_state();
// The scripts' setters (common.gw, tolerances.gw, c<n>.gw): BFE 0.00005,
// InverseZ 0.01, vertex shading on. The exe returns what EAX holds (0 here).
void set_bfe_tolerance(float v);               // 0x0053cef0 (backface)
float bfe_tolerance();                         // 0x0053cf00
int set_inverse_z_tolerance(float v);          // 0x00526df0 (priority_scale; its copy 0x0072f350 is never read)
int set_vertex_shading(int on);                // 0x00526a00 ECX on

// FIX (A): the Direct3D draw table [0x72f3b0].. (zvid_ddd3d, filled by
// 0x579f50) as function pointers with a context. Arguments as the original
// passes them (ECX, EDX, then the stack); the polygon is in clip2d().tl /
// .work, the primitive in zvid::device_state().primitive.
struct DrawTable {
  void* ctx = nullptr;
  // [0x72f3b0] 0x57bf80: untextured, unlit, unfogged.
  void (*flat)(void* ctx, uint32_t color, int32_t alpha, int32_t count, int32_t priority,
               int32_t overlay) = nullptr;
  // [0x72f3b8] 0x57c700: untextured, lit or fogged; rgb = &material.r.
  void (*lit)(void* ctx, uint32_t color, const float* rgb, uint32_t fogged, int32_t alpha,
              int32_t count, int32_t priority, int32_t overlay) = nullptr;
  // [0x72f3c0] 0x57ce20: textured, plain.
  void (*textured)(void* ctx, int32_t count, void* texture, float opacity, int32_t overlay) = nullptr;
  // [0x72f3bc] 0x57caa0: textured, white vertices (gmod_puff, weather).
  void (*textured_white)(void* ctx, int32_t count, void* texture, int32_t priority, float opacity,
                         int32_t overlay) = nullptr;
  // [0x72f3c4] 0x57d160: textured, light/fog the same for all vertices.
  void (*textured_lit)(void* ctx, uint32_t fogged, int32_t count, void* texture, int32_t priority,
                       float opacity, int32_t overlay) = nullptr;
  // [0x72f3c8] 0x57d690: textured, per-vertex light/fog.
  void (*textured_vertex)(void* ctx, uint32_t fogged, int32_t count, void* texture, float opacity,
                          int32_t overlay) = nullptr;
  // [0x72f3d0] / [0x72f3d4] / [0x72f3d8]: the same three with projected shadows.
  void (*shadowed)(void* ctx, int32_t count, void* texture) = nullptr;
  void (*shadowed_lit)(void* ctx, uint32_t fogged, int32_t count, void* texture) = nullptr;
  void (*shadowed_vertex)(void* ctx, uint32_t fogged, int32_t count, void* texture) = nullptr;
  // [0x72f3cc] 0x57ea20 / [0x72f3dc] 0x57fe20: one triangle of a polygon under
  // projected lights (0x547740), light = 3 rgb multipliers per vertex (or null).
  void (*projected)(void* ctx, const float* light, uint32_t fogged, int32_t count, void* texture,
                    int32_t priority, float opacity, int32_t overlay) = nullptr;
  void (*projected_shadowed)(void* ctx, const float* light, uint32_t fogged, int32_t count,
                             void* texture) = nullptr;
  // [0x72f3e0]: one point (model type 2), screen x, y, rhw.
  void (*point)(void* ctx, const float screen[3], uint32_t color, int32_t alpha) = nullptr;
};
DrawTable& draw_table();

// The receiver test inlined twice in 0x53f2e0 (docs/render-shadows.md): the
// casters over this polygon into clip2d().shadow (its count is 0 before).
void collect_shadows(int32_t count, const float normal[3]);  // 0x00540552..0x005408d8

// The calls the model draw makes into other code (static light bake,
// projected lights and shadows, lens flares). The lighting itself is
// gmod_light. Null hooks do nothing and return 0; the projected lights and
// the shadow receivers default to their ports (tests swap them).
struct DrawHooks {
  void* ctx = nullptr;
  // 0x544ed0(model) then 0x4ed980(node): the static light cache is due.
  // Unset: the ported bake and stamp (the tests record it instead).
  void (*refresh_static)(void* ctx, Model* model, zcls::Node* node) = nullptr;
  // 0x547540(ECX count): a strip's projected-light set.
  int32_t (*projected_strip)(void* ctx, int32_t count) = [](void*, int32_t n) { return projected_reach(n); };
  // 0x547740(ECX count, EDX per_vertex, fogged, material, priority): draws a
  // polygon under projected lights; non-zero when it did.
  int32_t (*projected_draw)(void* ctx, int32_t count, uint32_t per_vertex, uint32_t fogged,
                            Material* material, int32_t priority) =
      [](void*, int32_t n, uint32_t pv, uint32_t f, Material* m, int32_t p) { return draw_projected(n, pv, f, m, p); };
  // 0x541df0(ECX vertex, EDX colour, light): one model light; unset, the
  // port's (a point). Set by the tests to log it.
  void (*model_light)(void* ctx, const float* vertex, uint32_t color, Light* light) = nullptr;
  // 0x00567420 from 0x541df0: a light with +0x3c onto the lens-flare list
  // (zrndr::flare_add when unset): its world point, screen point, colour and
  // the light's +0x30.
  void (*flare)(void* ctx, const float world[3], const float screen[3], uint32_t color, Light* light) = nullptr;
  // 0x540552..0x5408d8 / 0x540ef6..0x541260: the projected-shadow casters
  // over this polygon into clip2d().shadow (count reset to 0 before).
  void (*collect_shadows)(void* ctx, int32_t count, const float normal[3]) =
      [](void*, int32_t n, const float* nrm) { zmod::collect_shadows(n, nrm); };
};
DrawHooks& draw_hooks();

void material_cycle(Material& material);      // 0x005376c0  (gmod_matl.c)
float normalize(float v[3]);                  // 0x004020d0
int32_t screen_reject(const ClipPlanes& rect, int32_t count);  // 0x00535d20
// The stack's top for a facade (0x0053f2e0's type 1), about its centre with
// model flag 0x40. Shared with Pirate's Moon's draw, whose model types differ.
void facade_turn(int32_t mode, bool about_centre, const float centre[3]);
int32_t draw_model(zcls::Node* node, uint32_t mask);  // 0x0053f2e0, ECX node, EDX plane mask

}  // namespace zmod
