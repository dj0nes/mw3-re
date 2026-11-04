#pragma once
#include <cstdint>

#include "gamez/zclass/cls_node.h"

// zClass\Camera.c: the camera's setters, its matrices, and the scene walk
// that draws a world through it (docs/render.md section 1,
// docs/drafts/scenewalk.md). The world render 0x004f7030 and the horizon
// follow 0x004f6d40 come right after the file's last named function, before
// list.c: kept here.
namespace zcls {

struct Camera;

Node* camera_create();                                             // 0x004f32e0
int camera_set_world(Node* node, Node* world);                     // 0x004f3560  data +0
// The camera's zone list (+0x1e4) from `zone` (count, then up to three ids),
// +0x1e0 set: only when it has a count and no 0xff ("all"); else as it was.
int camera_set_zone(Node* node, const uint8_t zone[4]);            // 0x004f6c80
int camera_set_window(Node* node, Node* window);                   // 0x004f3620  data +4
int camera_set_fov_angles(Node* node, float h, float v);           // 0x004f3d90
int camera_get_fov_angles(Node* node, float* h, float* v);         // 0x004f3ee0
int camera_add_child(Node* node, Node* child);                     // 0x004f33a0
int camera_set_clip(Node* node, float near_clip, float far_clip);  // 0x004f3a70
int camera_set_fov(Node* node, float h, float v);                  // 0x004f3b90

Node* camera_world(Node* node);                                    // 0x004f3610  data +0
int camera_set_rotation(Node* node, float x, float y, float z);    // 0x004f36b0
int camera_set_position(Node* node, float x, float y, float z);    // 0x004f3870
int camera_add_rotation(Node* node, float x, float y, float z);    // 0x004f37c0
int camera_add_position(Node* node, float x, float y, float z);    // 0x004f3920
int camera_set_lod_scale(Node* node, float f);                     // 0x004f3ff0
// The camera as zSound's listener (data +0x10 bit 0; on, the next walk hands
// it the place at once: [0x0060be28] 0).
int camera_set_listener(Node* node, int32_t on);                   // 0x004f3430 ECX node, EDX on
// Data +0x10 bit 3 (render_world passes it to world_build_lights).
int camera_set_flag8(Node* node, bool on);                         // 0x004f34c0
int camera_set_horizon(Node* node, Node* horizon);                 // 0x004f4090  EDX horizon
int camera_set_horizon_xz(Node* node, Node* horizon);              // 0x004f4100

// The camera's world matrix, position, view direction and euler angles, the
// view matrix (0x00520b00), its planes and frustum points. `offset` moves it
// (a wrapped partition is drawn by shifting the camera the other way).
int camera_update(Node* node);                                     // 0x004f4170
int camera_update_offset(Node* node, const float* offset);         // 0x004f41b0 ECX node, EDX offset
int camera_matrices(Node* node, Camera* data, const float* offset);  // 0x004f4370

// The walk's own state (camera.c globals).
struct WalkLod {                   // 0x0060bba0, 0x10 bytes
  float centre[3];                 // the LoD node's sphere centre
  float d2;                        // its squared camera distance times camera +0xd4
};
struct WalkColor {                 // 0x0060bc28, 0x10 bytes
  float rgb[3];
  float weight;
};
struct ScreenRing {                // 0x00736640 + ring * 0x2d4
  struct Cell {                    // 0x18 bytes
    int32_t x, z;                  // +0x00 / +0x04  the stored cell
    int32_t wrapped;               // +0x08  outside the grid: drawn by moving the camera
    float dx, dz;                  // +0x0c / +0x10  that move (only when wrapped)
    uint32_t mask;                 // +0x14  planes the cell's sphere crosses
  } cell[30];
  int32_t count;                   // +0x2d0
};
struct SceneWalk {
  Camera* camera = nullptr;        // 0x0060bb9c  the camera being drawn
  uint32_t zone = 0;               // 0x0060bb98  its zone list, as drawn
  int32_t lod_dynamic = 0;         // 0x005ea51c  CameraSetDynamicLOD
  float lod_target = 0.04f;        // 0x005ea520  frame time to hold
  float lod_factor = 1.0f;         // 0x005ea524
  float lod_step = 0.05f;          // 0x005ea528
  float lod_min = 0.6f;            // 0x005ea52c
  int32_t hidden_surface = 1;      // 0x005ea518  CameraSetObjectHSETest
  int32_t recompute = 0;           // 0x005ea530  a node above moved: children redo their spheres
  int32_t ring = 0;                // 0x005ea534  the partition ring being drawn
  int32_t lod_level = 0;           // 0x005ea538
  // 0x0060bba0: 8 entries in the exe (up to 0x0060bc20), unchecked.
  WalkLod lod[32] = {};
  int32_t morph = 0;               // 0x0060bc20  inside a morphing LoD
  float morph_factor = 0;          // 0x0060bac8
  int32_t opacity_top = -1;        // 0x005ea544
  bool clutter_tree_built = false;  // PM: 0x00509dd0's local: at most one cell's tree a call
  float opacity[50] = {};          // 0x0060bad0
  int32_t color_top = -1;          // 0x005ea548
  WalkColor color[32] = {};        // 0x0060bc28
  int32_t overlay = 0;             // 0x0060be3c  an override (0x800000) node is open
  Node* second_pass_node = nullptr;  // 0x005ea540  drawn without the second pass
  void (*pre)(Node* node) = nullptr;   // [0x005ea510]  node +0x2c bit 0x20 (default empty)
  void (*post)(Node* node) = nullptr;  // [0x005ea514]  node +0x2c bit 0x40
  uint32_t listener_frame = 0;     // 0x0060be28
  float listener_last[3] = {};     // 0x0060be30
  ScreenRing rings[50] = {};       // 0x00736640
  float view_points[6][3] = {};    // 0x007365c0  the camera's footprint (the hull reuses one more)
  int32_t view_count = 0;          // 0x0073662c
};
SceneWalk& scene_walk();

// Calls the world render makes into code that is not ported yet (or into
// the video layer). Null hooks are skipped.
struct WorldRenderHooks {
  void* ctx = nullptr;
  void (*begin_scene)(void* ctx) = nullptr;       // 0x00579af0
  void (*end_scene)(void* ctx) = nullptr;         // 0x00579b10
  void (*flush_transparent)(void* ctx) = nullptr;  // [0x0072f3e4] = 0x0057e110
  void (*flush_overlay)(void* ctx) = nullptr;     // [0x0072f3e8] = 0x0057e4c0
  void (*draw_quad)(void* ctx) = nullptr;         // [0x0072f3ec] = 0x0057e380, the colour quad
  void (*puffers)(void* ctx) = nullptr;           // 0x005496c0
  void (*after_scene)(void* ctx, float dt) = nullptr;  // 0x005a67c0, when EDX != 0
  void (*world_sounds)(void* ctx, Node* world) = nullptr;  // 0x004f3170
  // 0x004eda10(ECX world, EDX position): the zone under the camera (a ray
  // down through the world). Returns 1 with the zone dword, 0 for none.
  int32_t (*zone_probe)(void* ctx, Node* world, const float pos[3], uint32_t* zone) = nullptr;
  void (*dirty_rect)(void* ctx, const int32_t rect[4]) = nullptr;  // 0x00576ce0
  // 0x00572360: the fastest the sound listener may move; 0x00571e50(ECX
  // camera world matrix, EDX velocity) sets it.
  float (*listener_speed_limit)(void* ctx) = nullptr;
  void (*listener)(void* ctx, const float* matrix, const float velocity[3]) = nullptr;
};
WorldRenderHooks& world_render_hooks();

void render_node(Node* node, int32_t siblings);    // 0x004f5910 ECX node, EDX (see the doc)
// The shadow-image walk (zmod::shadow_draw, docs/render-shadows.md): an
// active object3d with its matrix, or a LoD whose +4 is not above 0, casts
// its model and children.
void shadow_object3d(Node* node);                  // 0x004f7670
void shadow_lod(Node* node);                       // 0x004f7750
int32_t view_hull(float (*points)[3], int32_t count);  // 0x004f5a90  XZ convex hull in place
int pick_cells(Node* world, Camera* camera);       // 0x004f5c80 ECX world node, EDX its data
int pick_cells_virtual(Node* world, Camera* camera);  // 0x004f61b0
int render_cells(Node* world, Node* camera_node, Camera* camera);  // 0x004f6720
int render_world_children(Node* world);            // 0x004f6c00
int render_scene(Node* world, Node* camera_node, Camera* camera);  // 0x004f6c50
void horizon_follow();                             // 0x004f6d40
// 0x004f73f0 (ECX camera node, EDX node; hw): one subtree through the camera,
// in a scene of its own (the cockpit and the arms over the world). The
// Direct3D path; the software one (hw 0) is not ported and draws nothing.
int render_subtree(Node* camera_node, Node* node, int32_t hw);
// Mech3.exe 1.2 @ 0x004f7350 (ECX the camera's data, EDX the frame's first
// flare): the lens flares in sight, between a BeginScene and an EndScene.
// Each picked flare (zrndr) is drawn unless the segment from the camera to
// its light (first hit, kind 0x40000 left out) meets something.
int camera_draw_flares(Camera* camera, int32_t from);
int render_world(Node* camera_node, int32_t after);  // 0x004f7030 ECX camera node, EDX

}  // namespace zcls
