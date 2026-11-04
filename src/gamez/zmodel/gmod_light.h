#pragma once
#include <cstdint>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"

// Model lighting for the Direct3D model draw 0x0053f2e0 (docs/render.md
// section 5, docs/drafts/lighting.md): the per-frame light list, the lights
// reaching a model, the model's needs bits and the per-polygon shading into
// the work records.
// zModel\gmod_light.c holds 0x00543b60; 0x00543d90..0x005465c0 follow it
// before gmod_puff.cpp, so most likely the same file. The cloud pattern
// (0x00543910..0x00543ac0) sits just before it, after gmod_cookie.cpp, and
// 0x0053daa0 between gmod_const.c and gmod_cookie.cpp. Kept here until their
// files are ported: the light direction pair from zClass\Light.c (0x004fb0c0,
// 0x004fb310) and the world's two calls from zClass\cls_world.c.
namespace zmod {

// One active light, 0x14 bytes at 0x00749824. +8 and +0xc are rewritten for
// every model by lights_reaching.
struct LightEntry {
  zcls::Light* data = nullptr;     // +0x00
  zcls::Node* node = nullptr;      // +0x04
  int32_t full = 0;                // +0x08  no range falloff for this model (inside near, or no range)
  int32_t reach = 0;               // +0x0c  the light reaches the model
  int32_t unused = 0;              // +0x10
};

struct LightList {
  static constexpr int kMax = 128;  // "Not enough MAX_LIGHTS"
  zcls::Node** nodes = nullptr;    // 0x0074980c  the world's light nodes
  zcls::Light** data = nullptr;    // 0x00749810  and their data
  int32_t any = 0;                 // 0x00749814  count > 0
  int32_t in_count = 0;            // 0x00749818
  int32_t count = 0;               // 0x0074981c
  int32_t directed = -1;           // 0x00749820  the last directed light, or -1
  LightEntry list[kMax];           // 0x00749824
  // The software renderer's colours (only set without Direct3D).
  float sw_color[3] = {};          // 0x007497c8  the directed light's colour, else the fog's
  float sw_fog[3] = {};            // 0x007497d4
  float sw_dark = 0;               // 0x007497e0  1 - ambient, 0 without a directed light
  float sw_one = 0;                // 0x007497e4
  float sw_color2[3] = {};         // 0x007497e8
  float sw_color3[3] = {};         // 0x007497f4
  float sw_zero = 0;               // 0x00749800
  float sw_one2 = 0;               // 0x00749804
};
LightList& light_list();

// The cloud pattern ("fluctuate" in world.zrd; mission.cpp sets it).
struct Clouds {
  int32_t on = 0;                  // 0x005ef860
  float speed = 1.2566371f;        // 0x005ef864  2 pi / period
  float scale = 0.036959913f;      // 0x005ef868  2 pi / wavelength
  float base = 0.8f;               // 0x005ef86c
  float amplitude = 0.2f;          // 0x005ef870
  float heading_sin = 0;           // 0x005ef874
  float heading_cos = 1;           // 0x005ef878
  float u = 0, v = 0;              // 0x005ef87c / 0x005ef880  phase, radians
};
Clouds& clouds();

// What the model draw shares with the shading: the globals 0x0053f2e0 and the
// object3d pushes set, and the per-polygon buffers.
struct Lighting {
  uint32_t mask_base = 0;          // 0x0074a2c8  the GfxFlags when no settings entry holds them
  const uint32_t* mask = &mask_base;  // 0x0074a2cc  the GfxFlags (the _HW or _SW entry's value): bit 0 Lighting
  int32_t single = 0;              // 0x0074a2d4  1 in software (0x00526800, 0x005269e0)
  int32_t tint_on = 0;             // 0x0074a270  object3d colour pushed (0x00526a10)
  float tint[3] = {};              // 0x0074a274
  float tint_weight = 0;           // 0x0074a280
  float camera_offset[3] = {};     // 0x0073fbb0  set by the camera matrices 0x004f4370
  const float (*cam)[3] = nullptr;      // 0x007476a4  the polygon, camera space, reversed
  int32_t vertex_normals = 0;      // 0x00747ca4  normals[] holds the polygon's vertex normals
  const float (*normals)[3] = nullptr;  // 0x00747ca8
  float (*rec)[8] = nullptr;       // 0x007481a8  work records: +0 darkness, +4 point, +8 fog/saturated
  float stale[3] = {};             // 0x00545720's [ebp-0x1c]: see shade_polygon
};
Lighting& lighting();

// Calls into other modules. Unset ones are skipped.
struct LightHooks {
  void (*point_color)(const float rgb[3]) = nullptr;  // 0x005798a0  the device's point-light colour
  void (*send_mix_fog)() = nullptr;                   // 0x00579980  send the mixed fog colour if changed
  void (*mix_fog)(float rgb[3]) = nullptr;            // 0x005680c0  set the mixed fog colour
  // 0x004ed280 gwNodeBuildNodeToAncestorMatrix onto the stack top. Unset: the
  // part a light uses (light and sound nodes euler, worlds and lods nothing).
  int (*node_to_world)(zcls::Node* node, int mode) = nullptr;
};
LightHooks& light_hooks();
// The exe's own calls for the three outputs: the device's point-light colour
// (zvid::set_point_light_color, 0x005798a0), the fog mix (zrndr::set_mix_fog_color,
// 0x005680c0) and its send (zvid::send_mix_fog, 0x00579980). The program's
// render set-up installs them; the probes set their own.
void use_device_light_output();

// --- The light list (per frame) ---
// ECX nodes, EDX data, stack count: the active nodes (flag 4) in order, at most 128.
void build_light_list(zcls::Node** nodes, zcls::Light** data, int32_t count);  // 0x00543b60
// ECX world node, EDX 1 for no lights.
int32_t world_build_lights(zcls::Node* world, int32_t none);  // 0x004f2f30 (cls_world.c)
// ECX world node: light_update on each of its lights.
int32_t world_update_lights(zcls::Node* world);               // 0x004f2f60 (cls_world.c)
// ECX light node: world position, angles, the direction toward the light in
// camera space (+0x90) and the camera-space position (+0x78) from the view.
int32_t light_update(zcls::Node* node);                      // 0x004fb310 (Light.c)
void light_world_pose(zcls::Node* node, zcls::Light* data);  // 0x004fb0c0 (Light.c)

// The cloud shading at a camera-space point, 1 without clouds.
float clouds_at_camera(const float p[3]);

// --- Per model ---
// ECX camera-space bounding centre, stack radius. Fills each entry's full and
// reach and the per-light intensity into rec[i][0] (directed) or rec[i][1];
// returns how many lights reach.
int32_t lights_reaching(const float centre[3], float radius);  // 0x00543d90
// ECX model: model flags (+0x08), local bounding centre (+0x48) and radius
// (+0x54). Bits: 1 fog, 2 lights, 4 tint; with two or more of lights and tint
// it mixes their colours into the fog colour (mix_fog).
uint32_t model_needs(uint32_t model_flags, const float centre[3], float radius);  // 0x0053daa0

// --- Per polygon ---
// ECX baked: per-vertex light (polygon +0x18), last vertex first, or null;
// EDX normal: the polygon's camera-space normal, normalised in place when a
// light needs it; count vertices in lighting().cam / rec. draw_flags (the
// draw's local_8) in: bit 1 fogged; out: |8|1 saturated light or tint, |4
// coloured point light. vary_flags (local_c) in: bit 4 model flag 0x20; out:
// |1 per-vertex light, |2 per-vertex saturation. shadowed: polygon takes
// projected shadows (skips lights with flag 0x40). needs: model_needs bits.
// Returns 1 when the polygon is lit.
int32_t shade_polygon(const float* baked, float normal[3], int32_t count, uint32_t* draw_flags,
                      uint32_t* vary_flags, int32_t shadowed, uint32_t needs);  // 0x00545720

// --- Projected lights (lights with flags 0x10 and 0x40, docs/render-shadows.md) ---
// 0x00547060..0x00547740 sit after gmod_light.c, before gmod_puff.cpp. A
// textured polygon near one is cut into a fan around its centre, then split
// along its longest edges where the light's falloff crosses it, and each
// triangle is lit per vertex and drawn on its own.
struct ProjVertex {                // 0x84 bytes at 0x0065ddf8
  float pos[3];                    // +0x00  camera space (unprojected from the screen)
  float screen[3];                 // +0x0c  sx, sy, sz
  float uv[8][2];                  // +0x18  the texture's, then one per shadow set
  float fog;                       // +0x58
  float light[3];                  // +0x5c  1 - darkness, plus the coloured point light
  float color[3];                  // +0x68  vertex colour, 0..255
  float lit[3];                    // +0x74  light[] plus the projected lights
  int32_t outline;                 // +0x80  on the polygon's outline: pushed out a pixel
};
static_assert(sizeof(ProjVertex) == 0x84, "ProjVertex is 0x84 bytes");
struct ProjTriangle {              // 0x1c bytes at 0x0064fdf0
  int32_t v[3];                    // +0x00
  int32_t across[3];               // +0x0c  the triangle across edge i (v[i]..v[i+1]), -1 none
  int32_t active;                  // +0x18  may still be split
};
struct ProjectedLights {
  static constexpr int kMaxLights = 128, kMaxTriangles = 2048, kMaxVertices = 6144;
  zcls::Light* lights[kMaxLights] = {};       // 0x0064fbf0
  int32_t light_count = 0;                    // 0x0065ddf0
  int32_t vertex_count = 0;                   // 0x0064fbe8
  int32_t triangle_count = 0;                 // 0x0064fbec
  ProjTriangle triangles[kMaxTriangles] = {}; // 0x0064fdf0
  ProjVertex vertices[kMaxVertices] = {};     // 0x0065ddf8
};
ProjectedLights& projected_lights();

struct Material;
// ECX count: the polygon in the draw's TL buffer. 1 when a projected light's
// range reaches its bounding sphere (the model draw asks before splitting a strip).
int32_t projected_reach(int32_t count);  // 0x00547540
// ECX count, EDX per_vertex (each vertex's own darkness), stack flags (the
// draw's fog/point bits), material, priority. Draws the polygon as lit
// triangles through the draw table's projected entries; 0 when no projected
// light is near (the caller then draws it as usual).
int32_t draw_projected(int32_t count, uint32_t per_vertex, uint32_t flags, const Material* material,
                       int32_t priority);  // 0x00547740
void projected_light_vertex(int32_t k);   // 0x00547060  vertex k's lit[]
void projected_split(int32_t t, int32_t edge);  // 0x00547160  triangle t across edge

// --- Clouds ---
void clouds_set_on(int32_t on);                  // 0x00543910
void clouds_set_period(float period);            // 0x00543920
void clouds_set_wavelength(float wavelength);    // 0x00543940
void clouds_set_level(float base, float amplitude);  // 0x00543960  clamps to 0..1
void clouds_set_heading(float radians);          // 0x005439f0
void clouds_advance(const float dir[2], float dt);  // 0x00543a50
float clouds_at(float x, float z);               // 0x00543ac0  world x, z

}  // namespace zmod
