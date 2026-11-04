#include "pm/gamez/zclass/cls_clutter.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_hit.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zreader/zreader.h"
#include "pm/gamez/zmodel/gmod_const.h"

namespace pm::zcls {

namespace {
const char* const kFile = "C:\\zipper\\branchep1\\gamez\\zClass\\cls_clutter.cpp";
constexpr double kDegToRad = 0.017453292519943295;  // [0x005deee8]

ClutterHooks g_hooks;
std::vector<std::unique_ptr<ClutterTemplate>> g_templates;
std::map<::zimg::TextureEntry*, std::vector<ClutterPiece>> g_pieces;

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

// The u or v of a piece, wrapped into 0..1.
float wrap(float x) {
  if (x < 0.0f) {
    const float r = 1.0f - static_cast<float>(std::fmod(static_cast<double>(-x), 1.0));
    return r == 1.0f ? 0.0f : r;
  }
  return x >= 1.0f ? static_cast<float>(std::fmod(static_cast<double>(x), 1.0)) : x;
}

// Pirate's Moon 1.0 @ 0x00511b50
void project_children(::zcls::Node* node) {
  const pm::zmod::Model* model = node->pm_model;
  const pm::zmod::Polygon* poly = model->polygons;
  const int count = static_cast<int>(poly->flags & 0x3ff);
  std::vector<float> verts(static_cast<size_t>(count) * 3 + 9);
  for (int i = 0; i < count; ++i) std::memcpy(&verts[static_cast<size_t>(i) * 3], &model->vertices[poly->vertex_indices[i] * 3], 12);
  float n[3];
  zmth::poly_normal(&verts[3], &verts[6], &verts[0], n);
  auto& pieces = g_pieces[poly->materials[0]->texture];
  for (int c = 0; c < node->child_count(); ++c) {
    ::zcls::Node* child = node->children[c];
    float at[3];
    ::zcls::node_world_position(child, at);
    // Through the plane through the origin along the normal: the children lie about the origin.
    const float d = at[2] * n[2] + at[1] * n[1] + at[0] * n[0];
    for (int k = 0; k < 3; ++k) at[k] = at[k] + n[k] * -d;
    float a[3], b[3];
    for (int k = 0; k < 3; ++k) a[k] = n[k] * -5.0f + at[k], b[k] = n[k] * 5.0f + at[k];
    ::zcls::LineHit slot{};
    float uv[2];
    if (!::zmod::polygon_segment_uv(&slot, a, b, verts.data(), poly->uvs[0], uv, count, 1)) {
      char msg[240];
      std::snprintf(msg, sizeof msg, "LoadTemplate(): template %s clutter %s does not project to polygon.", node->name(), child->name());
      zerr::report(0x200, kFile, 0x178, msg);
      continue;
    }
    ClutterPiece piece{wrap(uv[0]), wrap(uv[1]), child, nullptr};
    for (const auto& t : g_templates) {
      if (t->name == child->name()) {
        piece.settings = t.get();
        break;
      }
    }
    pieces.push_back(piece);
  }
}

// Pirate's Moon 1.0 @ 0x00511ad0
void walk_template(::zcls::Node* node) {
  const pm::zmod::Model* model = node->pm_model;
  if (!model) {
    for (int i = 0; i < node->child_count(); ++i) walk_template(node->children[i]);
    return;
  }
  if (model->count(0x10) == 1 && model->polygons->material_count == 1 && (model->polygons->materials[0]->flags & 1)) {
    project_children(node);
    return;
  }
  char msg[200];
  std::snprintf(msg, sizeof msg, "LoadTemplates(): ignoring %s; its model has other than one textured polygon.", node->name());
  zerr::report(0x200, kFile, 0x189, msg);
}
}  // namespace

std::map<::zimg::TextureEntry*, std::vector<ClutterPiece>>& clutter_pieces() { return g_pieces; }

void clutter_load_templates(const std::list<std::string>& names) {
  for (const std::string& name : names) {
    if (::zcls::Node* node = ::zcls::list_find_by_name(::zcls::kAllNodes, name.c_str())) {
      walk_template(node);
    } else {
      char msg[200];
      std::snprintf(msg, sizeof msg, "ClutterLoadTemplates(): cannot find node for template %s", name.c_str());
      zerr::report(0x200, kFile, 0x399, msg);
    }
  }
}

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
