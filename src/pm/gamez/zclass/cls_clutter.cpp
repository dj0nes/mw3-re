#include "pm/gamez/zclass/cls_clutter.h"

#include <cmath>
#include <cstdio>

#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zreader/zreader.h"

namespace pm::zcls {

namespace {
const char* const kFile = "C:\\zipper\\branchep1\\gamez\\zClass\\cls_clutter.cpp";
constexpr double kDegToRad = 0.017453292519943295;  // [0x005deee8]

ClutterHooks g_hooks;
std::vector<std::unique_ptr<ClutterTemplate>> g_templates;

// Pirate's Moon 1.0 @ 0x005116b0: 0 when it is no number. FIX (bug): the exe reads past a short list;
// here that is 0 too (fixes.md).
float number(zrdr::Node* list, int i) {
  float v = 0.0f;
  if (list && list->type == zrdr::List && i < zrdr::list_count(list)) zrdr::get_float_value(&list->list[i], &v);
  return v;
}
zrdr::Node* item(zrdr::Node* list, int i) {
  return list && list->type == zrdr::List && i < zrdr::list_count(list) ? &list->list[i] : nullptr;
}
float radians(float degrees) { return static_cast<float>(degrees * kDegToRad); }
float slope(float degrees) { return static_cast<float>(std::cos(static_cast<double>(radians(degrees)))); }
::zcls::Node* node_named(const char* name) {  // 0x0050b920 takes a null name, MW3's not
  return name ? ::zcls::list_find_by_name(::zcls::kAllNodes, name) : nullptr;
}

// Pirate's Moon 1.0 @ 0x00513990
void mark_substitute(::zcls::Node* node) {
  if (!node) return;
  node->set_flags(node->flags() | 0x200000);
  for (int i = 0; i < node->child_count(); ++i) mark_substitute(node->children[i]);
}

bool read_event(zrdr::Node* entry, const char* key, ClutterEvent* event) {
  zrdr::Node* block = zrdr::find(entry, key);
  if (!zrdr::get_float(block, "health", &event->health)) return false;
  const char* anim = zrdr::get_string(block, "anim");
  event->anim = g_hooks.anim_find ? g_hooks.anim_find(anim) : nullptr;
  event->model = node_named(zrdr::get_string(block, "model"));
  return true;
}

// Pirate's Moon 1.0 @ 0x00513310
void read_entry(ClutterTemplate& t, zrdr::Node* entry) {
  if (zrdr::Node* n = zrdr::find(entry, "scale_range")) t.scale[0] = number(n, 1), t.scale[1] = number(n, 2);
  if (zrdr::Node* n = zrdr::find(entry, "translate_uv_range")) {
    zrdr::Node* low = item(n, 1), *high = item(n, 2);
    t.uv_low[0] = number(low, 1), t.uv_high[0] = number(low, 2);
    t.uv_low[1] = number(high, 1), t.uv_high[1] = number(high, 2);
  }
  if (zrdr::Node* n = zrdr::find(entry, "far_fade_range")) {
    zrdr::Node* low = item(n, 1), *high = item(n, 2);
    t.fade_low[0] = number(low, 1), t.fade_high[0] = number(low, 2);
    t.fade_low[1] = number(high, 1), t.fade_high[1] = number(high, 2);
  }
  if (zrdr::Node* n = zrdr::find(entry, "rotation_range")) {
    zrdr::Node* low = item(n, 1), *high = item(n, 2);
    for (int axis = 0; axis < 3; ++axis) {
      t.rotation[axis * 2] = radians(number(low, axis + 1));
      t.rotation[axis * 2 + 1] = radians(number(high, axis + 1));
    }
  }
  if (zrdr::Node* n = zrdr::find(entry, "min_slope")) t.min_slope = slope(number(n, 1));
  if (zrdr::Node* n = zrdr::find(entry, "max_slope")) t.max_slope = slope(number(n, 1));
  t.on_weapon_hit = read_event(entry, "OnWeaponHit", &t.weapon_hit);
  read_event(entry, "OnCrater", &t.crater);
  read_event(entry, "OnCollide", &t.collide);
  if (zrdr::find(entry, "align_normal")) t.align_normal = true;
  zrdr::Node* subs = zrdr::find(entry, "substitute");
  if (!subs) return;
  const int count = zrdr::list_count(subs);
  float sum = 0.0f;
  for (int i = 1; i < count; ++i) sum = sum + number(&subs->list[i], 1);
  if (sum == 0.0f) return;
  const float scale = 1.0f / sum;
  for (int i = 1; i < count; ++i) {
    zrdr::Node* s = &subs->list[i];
    ClutterTemplate::Substitute out{number(s, 1) * scale, nullptr};
    if (const char* name = zrdr::list_count(s) < 3 ? nullptr : s->list[2].s) {
      out.node = node_named(name);
      if (!out.node) {
        char msg[200];
        std::snprintf(msg, sizeof msg, "%s: cannot find clutter substitution node, interpreting it as nothing.", name);
        zerr::report(0x200, kFile, 0xc2, msg);
      } else if (!(out.node->flags() & 0x200000)) {
        mark_substitute(out.node);
        if (g_hooks.watch_hits) g_hooks.watch_hits(out.node);
      }
    }
    t.substitutes.push_back(out);
  }
}
}  // namespace

ClutterHooks& clutter_hooks() { return g_hooks; }
std::vector<std::unique_ptr<ClutterTemplate>>& clutter_templates() { return g_templates; }

void clutter_read_settings(const char* file) {
  zrdr::Node* root = zrdr::load(file);
  if (!root) return;
  for (int i = 1; i < zrdr::list_count(root); ++i) {
    zrdr::Node* entry = &root->list[i];
    zrdr::Node* name = zrdr::find(entry, "node");
    if (!name) continue;
    auto t = std::make_unique<ClutterTemplate>();
    if (zrdr::Node* first = item(name, 1); first && first->type == zrdr::String && first->s) t->name = first->s, t->named = true;
    read_entry(*t, entry);
    g_templates.push_back(std::move(t));
  }
  zrdr::free_node(root);
}

}  // namespace pm::zcls
