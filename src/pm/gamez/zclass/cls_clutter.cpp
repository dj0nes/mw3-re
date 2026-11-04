#include "pm/gamez/zclass/cls_clutter.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "crt/msvc_rand.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/camera.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_cull.h"
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

struct Laying {
  ::zcls::Node* world_node = nullptr;  // 0x00639c04
  ::zcls::World* world = nullptr;      // 0x00639c00
  uint8_t zone = 0xff;                 // 0x00639c08
  bool skip_ground = false;            // 0x00639c1c
} g_lay;
constexpr float kRand = 3.0518509e-05f;  // [0x005dfcb4], 1 / 32767

float picked(float low, float high) {
  const float r = static_cast<float>(crt::msvc_rand()) * kRand;
  return r * (high - low) + low;
}

// Pirate's Moon 1.0 @ 0x00512000
void place(const pm::zmod::Model* model, const pm::zmod::Polygon* poly, uint32_t a, uint32_t b, uint32_t c,
           const ClutterPiece& piece, int material) {
  ClutterTemplate* t = piece.settings;
  const float* uvs = poly->uvs[material];
  const float u0 = uvs[a * 2], v0 = uvs[a * 2 + 1], u1 = uvs[b * 2], v1 = uvs[b * 2 + 1], u2 = uvs[c * 2], v2 = uvs[c * 2 + 1];
  zmth::MatrixStack& stack = zmth::matrix_stack();
  float p[3][3];
  const uint32_t at[3] = {a, b, c};
  for (int i = 0; i < 3; ++i) {
    const float* s = &model->vertices[poly->vertex_indices[at[i]] * 3];
    if (stack.is_identity() == 0) {
      const float* m = stack.matrix().m;
      p[i][0] = s[2] * m[6] + s[1] * m[3] + s[0] * m[0] + m[9];
      p[i][1] = s[2] * m[7] + s[1] * m[4] + s[0] * m[1] + m[10];
      p[i][2] = s[2] * m[8] + s[1] * m[5] + s[0] * m[2] + m[11];
    } else {
      std::memcpy(p[i], s, 12);
    }
  }
  float normal[3];
  zmth::poly_normal(p[1], p[2], p[0], normal);
  if (t) {
    float up = normal[1];
    if (up > 1.0f) up = 1.0f;
    else if (up < -1.0f) up = -1.0f;
    if (up < t->max_slope || t->min_slope < up) return;
  }
  float low_u = FLT_MAX, high_u = -FLT_MAX, low_v = FLT_MAX, high_v = -FLT_MAX;
  for (const float* q : {uvs + a * 2, uvs + b * 2, uvs + c * 2}) {
    if (q[0] < low_u) low_u = q[0];
    if (high_u < q[0]) high_u = q[0];
    if (q[1] < low_v) low_v = q[1];
    if (high_v < q[1]) high_v = q[1];
  }
  const int first_u = static_cast<int>(std::floor(static_cast<double>(low_u)));
  const int first_v = static_cast<int>(std::floor(static_cast<double>(low_v)));
  const int last_u = static_cast<int>(std::floor(static_cast<double>(high_u)));
  const int last_v = static_cast<int>(std::floor(static_cast<double>(high_v)));
  const float dv21 = v2 - v1, du10 = u1 - u0, dv10 = v1 - v0;
  const float area = du10 * dv21 - (u2 - u1) * dv10;
  const float edge_a = area * dv10, edge_b = (u0 - u1) * area;
  for (int iu = first_u; iu <= last_u; ++iu) {
    for (int iv = first_v; iv <= last_v; ++iv) {
      float x = static_cast<float>(iu) + piece.u, y = static_cast<float>(iv) + piece.v;
      if (t) {
        x = picked(t->uv_low[0], t->uv_low[1]) + x;
        y = picked(t->uv_high[0], t->uv_high[1]) + y;
      }
      const float du12 = u1 - u2;
      if (!((x - u0) * edge_a + (y - v0) * edge_b < 0.0f)) continue;
      if (!((x - u1) * area * dv21 + (y - v1) * area * du12 < 0.0f)) continue;
      if (!((x - u2) * (v0 - v2) * area + (y - v2) * (u2 - u0) * area < 0.0f)) continue;
      float det = du10 * dv21 - du12 * (v0 - v1);
      if (det == 0.0f) continue;
      det = 1.0f / det;
      const float k_u = det * dv21, k_ov = det * (v0 - v1), k_du = det * du12, k_v = det * du10;
      const float gx = k_ov * (p[2][0] - p[1][0]) + k_u * (p[1][0] - p[0][0]);
      const float e_y = p[2][1] - p[1][1], f_y = p[1][1] - p[0][1];
      const float gy = k_ov * e_y + f_y * k_u;
      const float e_z = p[2][2] - p[1][2], f_z = p[1][2] - p[0][2];
      const float gz = e_z * k_ov + f_z * k_u;
      const float hx = k_v * (p[2][0] - p[1][0]) + k_du * (p[1][0] - p[0][0]);
      const float hy = k_v * e_y + k_du * f_y;
      const float hz = e_z * k_v + f_z * k_du;
      float point[3];
      point[0] = gx * x + hx * y + ((p[0][0] - gx * u0) - hx * v0);
      point[1] = gy * x + hy * y + ((p[0][1] - gy * u0) - hy * v0);
      point[2] = gz * x + hz * y + ((p[0][2] - gz * u0) - hz * v0);
      if (!g_lay.skip_ground && g_hooks.ground_heights) {
        float heights[32];
        const int hits = g_hooks.ground_heights(g_lay.world_node, point[0], point[1] + 1.0f, point[2], heights, 32);
        if (hits >= 0) {
          int above = 0;
          for (int i = 0; i < hits; ++i) above += point[1] - 1.0f <= heights[i];
          if (above > 1) continue;
        }
      }
      const ::zcls::World* w = g_lay.world;
      int cx = static_cast<int>(std::floor(static_cast<double>((point[0] - w->get<float>(0x34)) * w->get<float>(0x74))));
      int cz = static_cast<int>(std::floor(static_cast<double>((point[2] - w->get<float>(0x38)) * w->get<float>(0x78))));
      const int cols = w->get<int32_t>(0x88), rows = w->get<int32_t>(0x8c);
      if (cx < 0) cx = 0;
      else if (cx >= cols) cx = cols - 1;
      if (cz < 0) cz = 0;
      else if (cz >= rows) cz = rows - 1;
      ClutterRecord rec;
      rec.node = piece.node;
      if (t) {
        float left = static_cast<float>(crt::msvc_rand()) * kRand;
        for (const auto& s : t->substitutes) {
          left = left - s.weight;
          if (left < 0.0f) {
            rec.node = s.node;
            break;
          }
        }
      }
      if (!rec.node) continue;
      // The zone: the first set one from this polygon on through the model's array.
      rec.zone = g_lay.zone;
      const pm::zmod::Polygon* end = model->polygons + model->count(0x10);
      while (poly < end && (poly->zone_set & 0xff) != 0) {
        if (((poly->zone_set >> 8) & 0xff) != 0xff) {
          rec.zone = static_cast<uint8_t>(poly->zone_set >> 8);
          break;
        }
        ++poly;
      }
      zmth::push(&rec.matrix);
      zmth::load_identity();
      if (t) {
        if (t->align_normal) {
          static const float kUp[3] = {0.0f, 1.0f, 0.0f};  // [0x00611200]
          float q[4];
          zmth::quat_between(kUp, normal, q);
          zmth::Mat34 turn{};
          zmth::quat_matrix(q, turn.m);
          zmth::load(turn);
        } else {
          zmth::rotate_y(picked(t->rotation[2], t->rotation[3]));
          zmth::rotate_x(picked(t->rotation[0], t->rotation[1]));
          zmth::rotate_z(picked(t->rotation[4], t->rotation[5]));
        }
        const float s = picked(t->scale[0], t->scale[1]);
        zmth::scale(s, s, s);
      }
      zmth::pop();
      std::memcpy(rec.matrix.m + 9, point, 12);
      if (!t || t->fade_high[1] == 0.0f) {
        rec.near_sq = rec.far_sq = FLT_MAX;
        rec.fade_scale = 0.0f;
      } else {
        const float r = static_cast<float>(crt::msvc_rand()) * kRand;
        float far_at;
        if (t->fade_low[1] == t->fade_low[0]) {
          rec.near_sq = t->fade_low[0] * t->fade_low[0];
          far_at = (t->fade_high[1] - t->fade_high[0]) * r + t->fade_high[0];
        } else {
          const float span = t->fade_low[1] - t->fade_low[0];
          const float near_at = span * r + t->fade_low[0];
          rec.near_sq = near_at * near_at;
          const float slope_f = (t->fade_high[1] - t->fade_high[0]) / span;
          far_at = slope_f * near_at + (t->fade_high[0] - slope_f * t->fade_low[0]);
        }
        rec.far_sq = far_at * far_at;
        rec.fade_scale = rec.far_sq - rec.near_sq;
        if (rec.fade_scale != 0.0f) rec.fade_scale = 1.0f / rec.fade_scale;
      }
      if (t) {
        rec.weapon_health = t->weapon_hit.health;
        rec.flags = t->on_weapon_hit ? 2 : 0;
        rec.collide_health = t->collide.health;
        rec.settings = t;
      }
      // Pirate's Moon 1.0 @ 0x00505e10
      ::zcls::Partition& cell = g_lay.world->rows[cz][cx];
      cell_clutter(cell)->push_back(rec);
      cell.put<int32_t>(0x40, cell.get<int32_t>(0x40) + 1);
      const uint32_t flags = cell.get<uint32_t>(0);
      cell.put<uint32_t>(0, flags | 0x40000);
      if (!(flags & 1)) ::zcls::world_touch_cell(g_lay.world_node, cx, cz);  // 0x005043d0
    }
  }
}

// Pirate's Moon 1.0 @ 0x00512a80: a strip's triangles by the same rule as the draw's, a fan's from 0.
void lay_polygon(const pm::zmod::Model* model, const pm::zmod::Polygon* poly) {
  for (int m = 0; m < poly->material_count; ++m) {
    const auto found = g_pieces.find(poly->materials[m]->texture);
    if (found == g_pieces.end()) return;
    for (const ClutterPiece& piece : found->second) {
      const uint32_t n = poly->flags & 0x3ff;
      if (poly->flags & 0x2000) {
        if (n == 2) continue;
        for (uint32_t i = 0; i < (poly->flags & 0x3ff) - 2; ++i) {
          const uint32_t b = (i + 1) | 1, c = (i + 2) & ~1u;
          const uint32_t* v = poly->vertex_indices;
          if (v[i] != v[b] && v[i] != v[c] && v[b] != v[c]) place(model, poly, i, b, c, piece, m);
        }
      } else {
        for (uint32_t i = 2; i < n; ++i) place(model, poly, 0, i - 1, i, piece, m);
      }
    }
  }
}

// Pirate's Moon 1.0 @ 0x00512bb0: only type 0 models' textured polygons, not 0x800.
void lay_model(const ::zcls::Node* node) {
  const pm::zmod::Model* model = node->pm_model;
  if (!model || model->count(0) != 0) return;
  for (int i = 0; i < model->count(0x10); ++i) {
    const pm::zmod::Polygon* poly = &model->polygons[i];
    if (!(poly->flags & 0x800) && poly->file_uvs != 0) lay_polygon(model, poly);
  }
}

void lay_node(::zcls::Node* node);

void lay_children(::zcls::Node* node) {
  lay_model(node);
  for (int i = 0; i < node->child_count(); ++i) lay_node(node->children[i]);
}

// Pirate's Moon 1.0 @ 0x00512c60: the world matrix as the walk's, cached or made.
void lay_object(::zcls::Node* node) {
  auto* data = static_cast<::zcls::Object3d*>(node->data);
  if (!data) return;
  auto* world = reinterpret_cast<zmth::Mat34*>(data->raw + 0x60);  // the walk fills the node's cache
  const uint32_t f = data->get<uint32_t>(0);
  const bool pushed = !(f & 8);
  if (pushed) {
    if (!(node->flags() & 0x80000)) {
      zmth::push_copy(world);
      zmth::multiply(*reinterpret_cast<const zmth::Mat34*>(data->raw + 0x30), 1);
    } else if (!(f & 0x20)) {
      zmth::push(world);
    } else {
      zmth::push_copy(world);
      zmth::multiply(*reinterpret_cast<const zmth::Mat34*>(data->raw + 0x30), 1);
      if (!(f & 1)) data->put<uint32_t>(0, f & ~0x20u);
    }
  }
  lay_children(node);
  if (pushed) zmth::pop();
}

// Pirate's Moon 1.0 @ 0x00512d20: active, not 0x4000000.
void lay_node(::zcls::Node* node) {
  if (!node || !(node->flags() & 4) || (node->get<uint32_t>(0x2c) & 0x4000000)) return;
  bool zone_set = false;
  if (g_lay.zone == 0xff && node->raw[0x30] != 0xff) {
    zone_set = true;
    g_lay.zone = node->raw[0x30];
  }
  node->set_flags(node->flags() & ~0x2000000u);
  if (node->cls() == 5) {
    lay_object(node);
  } else if (node->cls() == 6) {  // 0x00512c00: a LoD in range
    if (static_cast<const ::zcls::Lod*>(node->data)->get<float>(4) <= 0.0f) lay_children(node);
  }
  if (zone_set) g_lay.zone = 0xff;
}
}  // namespace

namespace {
void piece_destroyed(ClutterRecord* r, float* health) {
  *health = 0.0f;
  r->flags |= 1;
  if (const ClutterTemplate* t = r->settings) {
    if (g_hooks.anim_start) g_hooks.anim_start(t->weapon_hit.anim, r->matrix.m + 9);
    if (t->weapon_hit.model) {
      r->flags &= ~1u;
      r->node = t->weapon_hit.model;
    }
  }
  r->flags = (r->flags & ~2u) | 4;
}
}  // namespace

void clutter_on_weapon_hit(ClutterRecord* r, float damage) {
  if (!r || !(r->flags & 2)) return;
  r->weapon_health = r->weapon_health - damage;
  if (r->weapon_health <= 0.0f) piece_destroyed(r, &r->weapon_health);
}

void clutter_on_collide(ClutterRecord* r, float damage) {
  if (!r || !(r->flags & 2)) return;
  r->collide_health = r->collide_health - damage;
  if (r->collide_health <= 0.0f) piece_destroyed(r, &r->collide_health);
}

namespace {
// Pirate's Moon 1.0 @ 0x0053c510: the eight corners of a box through a matrix, in the exe's order.
void box_corners(const float* m, const float* b, float out[8][3]) {
  const float* mm = m;
  const float x0[3] = {mm[3] * b[4] + mm[0] * b[0], mm[4] * b[4] + mm[1] * b[0], mm[5] * b[4] + mm[2] * b[0]};
  const float x1[3] = {mm[3] * b[4] + mm[0] * b[3], mm[4] * b[4] + mm[1] * b[3], mm[5] * b[4] + mm[2] * b[3]};
  const float y0[3] = {mm[0] * b[0] + mm[3] * b[1], mm[4] * b[1] + mm[1] * b[0], mm[5] * b[1] + mm[2] * b[0]};
  const float y1[3] = {mm[3] * b[1] + mm[0] * b[3], mm[4] * b[1] + mm[1] * b[3], mm[5] * b[1] + mm[2] * b[3]};
  const float z2[3] = {b[2] * mm[6] + mm[9], b[2] * mm[7] + mm[10], b[2] * mm[8] + mm[11]};
  const float z5[3] = {b[5] * mm[6] + mm[9], b[5] * mm[7] + mm[10], b[5] * mm[8] + mm[11]};
  for (int k = 0; k < 3; ++k) {
    out[0][k] = z5[k] + y0[k];
    out[1][k] = z5[k] + y1[k];
    out[2][k] = z2[k] + y1[k];
    out[3][k] = z2[k] + y0[k];
    out[4][k] = x0[k] + z5[k];
    out[5][k] = x1[k] + z5[k];
    out[6][k] = x1[k] + z2[k];
    out[7][k] = x0[k] + z2[k];
  }
}

// Pirate's Moon 1.0 @ 0x0050a230
void draw_record(::zcls::Partition& cell, ClutterRecord& r) {
  if (r.flags & 1) return;
  ::zcls::SceneWalk& walk = ::zcls::scene_walk();
  const float* eye = zmth::view().camera.m + 9;  // 0x008d4424
  const float dx = eye[0] - r.matrix.m[9], dy = eye[1] - r.matrix.m[10], dz = eye[2] - r.matrix.m[11];
  const float d2 = (dx * dx + dy * dy) + dz * dz;
  float opacity = 1.0f;
  if (r.near_sq < d2) {
    if (r.far_sq <= d2) return;
    opacity = (r.far_sq - d2) * r.fade_scale;
    if (opacity < 0.004f) return;
  }
  const bool faded = opacity < 0.996f;
  if (faded) {
    walk.opacity[++walk.opacity_top] = opacity;
    ::zmod::set_opacity(opacity);
  }
  ::zcls::Node* n = r.node;
  std::memcpy(n->raw + 0x64, r.matrix.m + 9, 12);
  if (n->cls() == 5) {
    auto* od = static_cast<::zcls::Object3d*>(n->data);
    od->put<uint32_t>(0, od->get<uint32_t>(0) & ~8u);
    std::memcpy(od->floats(0x30), r.matrix.m, 48);
    std::memcpy(od->floats(0x60), r.matrix.m, 48);
    if (n->pm_box) {
      float corners[8][3];
      box_corners(r.matrix.m, n->pm_box, corners);
      ::zcls::corners_sphere(corners, reinterpret_cast<float*>(n->raw + 0x64), reinterpret_cast<float*>(n->raw + 0x70));
    }
  }
  n->raw[0x30] = r.zone;
  const uint32_t held = n->get<uint32_t>(0xc8);
  n->put<uint32_t>(0xc8, r.link);
  ::zcls::render_node(n, cell.get<int32_t>(0x40));
  r.link = n->get<uint32_t>(0xc8);
  n->put<uint32_t>(0xc8, held);
  if (faded) {
    --walk.opacity_top;
    ::zmod::set_opacity(walk.opacity_top < 0 ? 1.0f : walk.opacity[walk.opacity_top]);
  }
}
}  // namespace

namespace {
// Pirate's Moon 1.0 @ 0x005045e0
int32_t tree_add(CellClutter& c, float x0, float x1, float z0, float z1) {
  ClutterTreeNode node;
  node.box[0] = x0, node.box[3] = x1, node.box[2] = z0, node.box[5] = z1;
  c.tree.push_back(node);
  return static_cast<int32_t>(c.tree.size()) - 1;
}

// Pirate's Moon 1.0 @ 0x00504530
void tree_insert(CellClutter& c, int32_t at, int32_t record) {
  ClutterTreeNode& node = c.tree[static_cast<size_t>(at)];
  const ClutterRecord& r = c[static_cast<size_t>(record)];
  if (node.records.empty()) {
    node.near_sq = r.near_sq, node.far_sq = r.far_sq;
  } else {
    if (r.near_sq < node.near_sq) node.near_sq = r.near_sq;
    if (node.far_sq < r.far_sq) node.far_sq = r.far_sq;
  }
  node.records.push_back(record);
}

// Pirate's Moon 1.0 @ 0x005047e0: the box over the records' corners or the children's boxes, its
// sphere. A node with neither is left as it is.
void tree_finish(CellClutter& c, int32_t at) {
  ClutterTreeNode& node = c.tree[static_cast<size_t>(at)];
  bool first = true;
  if (node.records.empty()) {
    if (node.first_child < 0) return;
    for (int32_t k = node.first_child; k < node.first_child + 4; ++k) {
      const ClutterTreeNode& child = c.tree[static_cast<size_t>(k)];
      if (child.records.empty() && child.first_child < 0) continue;
      if (first) {
        std::memcpy(node.box, child.box, sizeof node.box);
        first = false;
      } else {
        for (int a = 0; a < 3; ++a) {
          if (child.box[a] < node.box[a]) node.box[a] = child.box[a];
          if (node.box[a + 3] < child.box[a + 3]) node.box[a + 3] = child.box[a + 3];
        }
      }
    }
  } else {
    for (int32_t i : node.records) {
      const ClutterRecord& r = c[static_cast<size_t>(i)];
      if ((r.flags & 1) || !(r.node->flags() & 0x100) || !r.node->pm_box) continue;
      float corners[8][3];
      box_corners(r.matrix.m, r.node->pm_box, corners);
      if (first) {
        node.box[0] = node.box[3] = corners[0][0];
        node.box[1] = node.box[4] = corners[0][1];
        node.box[2] = node.box[5] = corners[0][2];
        first = false;
      }
      for (const auto& q : corners) {
        for (int a = 0; a < 3; ++a) {
          if (q[a] < node.box[a]) node.box[a] = q[a];
          else if (node.box[a + 3] < q[a]) node.box[a + 3] = q[a];
        }
      }
    }
  }
  ::zcls::box_sphere(node.box, node.centre, &node.radius);  // 0x0050d8b0
  node.fade_scale = 1.0f / (node.far_sq - node.near_sq);
}

// Pirate's Moon 1.0 @ 0x00504640
void tree_split(CellClutter& c, int32_t at) {
  if (c.tree[static_cast<size_t>(at)].records.size() < 0x15) {
    tree_finish(c, at);
    return;
  }
  const float x0 = c.tree[static_cast<size_t>(at)].box[0], x1 = c.tree[static_cast<size_t>(at)].box[3];
  const float z0 = c.tree[static_cast<size_t>(at)].box[2], z1 = c.tree[static_cast<size_t>(at)].box[5];
  const float mid_x = (x1 - x0) * 0.5f + x0, mid_z = (z1 - z0) * 0.5f + z0;
  const int32_t first = tree_add(c, x0, mid_x, z0, mid_z);
  tree_add(c, mid_x, x1, z0, mid_z);
  tree_add(c, x0, mid_x, mid_z, z1);
  tree_add(c, mid_x, x1, mid_z, z1);
  const std::vector<int32_t> records = std::move(c.tree[static_cast<size_t>(at)].records);
  c.tree[static_cast<size_t>(at)].records.clear();
  c.tree[static_cast<size_t>(at)].first_child = first;
  for (int32_t i : records) {
    const ClutterRecord& r = c[static_cast<size_t>(i)];
    int32_t to = first;
    if (mid_x < r.matrix.m[9]) to = first + 1;
    if (mid_z < r.matrix.m[11]) to = to + 2;
    tree_insert(c, to, i);
  }
  for (int k = 0; k < 4; ++k) tree_split(c, first + k);
  tree_finish(c, at);
}

// Pirate's Moon 1.0 @ 0x00504450
void tree_build(const ::zcls::World* w, ::zcls::Partition& cell, CellClutter& c) {
  c.tree.clear();
  const float x = cell.get<float>(8), z = cell.get<float>(0xc);
  tree_add(c, x, x + w->get<float>(0x64), w->get<float>(0x68) + z, z);
  for (int32_t i = 0; i < static_cast<int32_t>(c.size()); ++i) tree_insert(c, 0, i);
  tree_split(c, 0);
}

// Pirate's Moon 1.0 @ 0x0050a3e0
void draw_tree(::zcls::Partition& cell, CellClutter& c, int32_t at) {
  const ClutterTreeNode& node = c.tree[static_cast<size_t>(at)];
  if (node.records.empty() && node.first_child < 0) return;
  ::zmod::WalkState& walk = ::zmod::walk_state();
  uint32_t mask = walk.mask[walk.top];
  if (mask != 0 && ::zmod::cull_sphere(node.centre, &mask, node.radius) != 0) return;
  walk.mask[++walk.top] = mask;
  const float* eye = zmth::view().camera.m + 9;
  float d = zmth::distance(zmth::Vec3{eye[0], eye[1], eye[2]}, zmth::Vec3{node.centre[0], node.centre[1], node.centre[2]}) - node.radius;  // 0x00537a50
  if (d < 0.0f) d = 0.0f;
  const float d2 = d * d;
  if (!(node.near_sq < d2 && (node.far_sq <= d2 || (node.far_sq - d2) * node.fade_scale < 0.004f))) {
    if (node.first_child >= 0) {
      const int32_t first = node.first_child;
      for (int k = 0; k < 4; ++k) draw_tree(cell, c, first + k);
    } else {
      for (int32_t i : node.records) draw_record(cell, c[static_cast<size_t>(i)]);
    }
  }
  --walk.top;
}
}  // namespace

void clutter_draw_cell(const ::zcls::World* world, ::zcls::Partition& cell) {
  if (!cell.pm_clutter) return;
  CellClutter& c = *cell_clutter(cell);
  // One cell's tree a walk: the rest stay flat until a later frame reaches them first.
  ::zcls::SceneWalk& walk = ::zcls::scene_walk();
  if (c.tree.empty() && c.size() > 0x14 && !walk.clutter_tree_built) {
    tree_build(world, cell, c);
    walk.clutter_tree_built = true;
  }
  if (c.tree.empty()) {
    for (ClutterRecord& r : c) draw_record(cell, r);
  } else {
    draw_tree(cell, c, 0);
  }
}

void clutter_skip_ground_test(bool on) { g_lay.skip_ground = on; }

void clutter_free(std::list<std::string>& names) {
  g_templates.clear();
  g_pieces.clear();
  names.clear();
}

void clutter_crater(::zcls::Node* world, const float point[3], float radius) {
  auto* w = static_cast<::zcls::World*>(world->data);
  int32_t x0, z0, x1, z1;
  ::zcls::world_cell_clamped(world, &x0, point[0] - radius, radius + point[2], &z0);  // 0x00505230
  ::zcls::world_cell_clamped(world, &x1, radius + point[0], point[2] - radius, &z1);
  const float limit = radius * radius;
  for (int x = x0; x <= x1; ++x) {
    for (int z = z0; z <= z1; ++z) {
      ::zcls::Partition& cell = w->rows[z][x];
      if (!cell.pm_clutter) continue;
      for (ClutterRecord& r : *cell_clutter(cell)) {
        const float dx = point[0] - r.matrix.m[9], dz = point[2] - r.matrix.m[11];
        if (dz * dz + dx * dx < limit) r.flags |= 5;
      }
    }
  }
}

CellClutter* cell_clutter(::zcls::Partition& cell) {
  if (!cell.pm_clutter) cell.pm_clutter = new CellClutter;
  return static_cast<CellClutter*>(cell.pm_clutter);
}

void clutter_lay(::zcls::Node* world) {
  if (::zcls::update_hooks().pending) ::zcls::update_hooks().pending(::zcls::update_hooks().ctx);
  g_lay.world_node = world;
  g_lay.world = static_cast<::zcls::World*>(world->data);
  g_lay.zone = 0xff;
  zmth::Mat34 base;
  zmth::push(&base);
  zmth::load_identity();
  lay_model(world);
  const ::zcls::World* w = g_lay.world;
  for (int x = 0; x < w->get<int32_t>(0x88); ++x) {
    for (int z = 0; z < w->get<int32_t>(0x8c); ++z) {
      const ::zcls::Partition& cell = w->rows[z][x];
      for (int i = 0; i < cell.get<int16_t>(0x3a); ++i) lay_node(cell.nodes[i]);
    }
  }
  for (int i = 0; i < world->child_count(); ++i) lay_node(world->children[i]);
  zmth::pop();
}

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
