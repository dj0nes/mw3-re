#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zcls { struct Node; }
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

}  // namespace pm::zcls
