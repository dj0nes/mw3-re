#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gamez/zmath/zmth_matrix.h"

namespace zcls { struct Node; struct Partition; }
namespace zimg { struct TextureEntry; }

// C:\zipper\branchep1\gamez\zClass\cls_clutter.cpp, new in Pirate's Moon: scattered props laid on
// the ground by its material (docs/pm-map.md). So far: the settings file.
namespace pm::zcls {

// What a hit does to a piece: below `health` the anim runs and `model` replaces it.
struct ClutterEvent {
  float health = 0;
  void* anim = nullptr;
  ::zcls::Node* model = nullptr;
};

// One entry of templates.zrd, 0x88 bytes in the exe.
struct ClutterTemplate {
  std::string name;                  // +0x00
  bool named = false;
  struct Substitute { float weight; ::zcls::Node* node; };
  std::vector<Substitute> substitutes;  // +0x08..+0x10  weights scaled to sum to 1
  float scale[2] = {1, 1};           // +0x14
  float uv_low[2] = {};              // +0x1c  u, v
  float uv_high[2] = {};             // +0x24
  float fade_low[2] = {};            // +0x2c  far_fade_range's first and second pairs' firsts
  float fade_high[2] = {};           // +0x34
  bool align_normal = false;         // +0x3c
  float rotation[6] = {};            // +0x40  radians: low x, high x, low y, high y, low z, high z
  float max_slope = -1;              // +0x58  cosines
  float min_slope = 1;               // +0x5c
  bool on_weapon_hit = false;        // +0x60
  ClutterEvent weapon_hit;           // +0x64
  ClutterEvent crater;               // +0x70
  ClutterEvent collide;              // +0x7c
};

struct ClutterHooks {
  void* (*anim_find)(const char* name) = nullptr;  // 0x00524b50
  // 0x005a4d80 (0, node, 1, 0x00513c60) and 0x005a4dc0 (0, node, 0x00513cf0): a substitute's hit
  // and collision callbacks, not ported yet.
  void (*watch_hits)(::zcls::Node* node) = nullptr;
  // 0x004ff8d0 on PM's models, not ported yet: the ground's heights at a point, or -1 on failure.
  int (*ground_heights)(::zcls::Node* world, float x, float y, float z, float* heights, int max) = nullptr;
  // 0x005043d0, the cell's box, not ported yet.
  void (*cell_changed)(::zcls::Node* world, ::zcls::Partition* cell) = nullptr;
  // 0x005216e0 (anim, 0, x, y, z, 0 ...): an animation at a point.
  void (*anim_start)(void* anim, const float point[3]) = nullptr;
};
ClutterHooks& clutter_hooks();

std::vector<std::unique_ptr<ClutterTemplate>>& clutter_templates();  // 0x00639bf4

void clutter_read_settings(const char* file);  // 0x00513030, with 0x00513310 on each entry

// One child of a template node: where it sits on the template's polygon, in 0..1.
struct ClutterPiece {
  float u, v;
  ::zcls::Node* node;
  ClutterTemplate* settings;  // the entry named as the node, or null
};
// The pieces by the texture of the polygon they lie on (0x00639be0).
std::map<::zimg::TextureEntry*, std::vector<ClutterPiece>>& clutter_pieces();

// 0x005139d0: each script-named template (AddClutterTemplates), its node's tree walked to
// single-polygon textured models (0x00511ad0), their children projected on to it (0x00511b50).
void clutter_load_templates(const std::list<std::string>& names);

// One piece laid on the ground, 0x5c bytes in a cell's array (+0x44, count +0x40).
struct ClutterRecord {
  ::zcls::Node* node = nullptr;      // +0x00
  zmth::Mat34 matrix{};              // +0x04  scale and turn, then the ground's point
  float near_sq = 0, far_sq = 0;     // +0x34  the fade's squared distances
  float fade_scale = 0;              // +0x3c  1 / (far_sq - near_sq)
  uint8_t zone = 0;                  // +0x40
  uint32_t flags = 0;                // +0x48  2 takes weapon hits
  float weapon_health = 0;           // +0x50
  float collide_health = 0;          // +0x54
  ClutterTemplate* settings = nullptr;  // +0x58
};
std::vector<ClutterRecord>* cell_clutter(::zcls::Partition& cell);

// 0x00513a30, 0x00512d90: over every polygon in the world's grid and tree, each of its textures'
// pieces laid over each triangle where the triangle's u, v repeats reach them (0x00512000).
void clutter_lay(::zcls::Node* world);

// A shot (0x00513c60) or a mech walking in (0x00513cf0) on a piece: below its health it is gone (flags
// 1) or swapped for the model, the anim runs. Both take OnWeaponHit's anim and model (a quirk of the exe).
// The record is the hit's +0x28 in the exe; wired when PM's zWeapon lands.
void clutter_on_weapon_hit(ClutterRecord* record, float damage);
void clutter_on_collide(ClutterRecord* record, float damage);

// 0x005116a0: the laying skips its ground test; a mission's load sets it first (0x00492f9a).
void clutter_skip_ground_test(bool on);

// 0x00513d90: the records within `radius` of a point, across x and z, flagged 5 (gone).
void clutter_crater(::zcls::Node* world, const float point[3], float radius);

// 0x00513a40: the settings, the pieces and the template names, at a mission's end. The cells'
// records go with the world.
void clutter_free(std::list<std::string>& names);

}  // namespace pm::zcls
