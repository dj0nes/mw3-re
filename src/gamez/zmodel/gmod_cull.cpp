#include "gamez/zmodel/gmod_cull.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma STDC FP_CONTRACT OFF

#include "gamez/zclass/camera.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zvideo/zvid_format.h"
#include "pm/gamez/zmodel/gmod_const.h"

namespace zmod {

namespace {
Zones g_zones;
ViewPolygon g_poly;
ShadowCasters g_casters;
ShadowCaster* g_caster_list = nullptr;
CullHooks g_hooks;

float dot_from(const float* c, const float* p, const float* n) {
  const float d0 = c[0] - p[0], d1 = c[1] - p[1], d2 = c[2] - p[2];
  return d0 * n[0] + d1 * n[1] + d2 * n[2];
}
}  // namespace

Zones& zones() { return g_zones; }
ViewPolygon& view_polygon() { return g_poly; }
ShadowCasters& shadow_casters() { return g_casters; }
CullHooks& cull_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x0053cdc0. The exe reads as many ids as the count byte
// says, past the four bytes; the lists hold at most three.
int32_t zone_visible(uint8_t zone) {
  if (g_zones.mode == 0 || zone == 0xff) return 1;
  const int n = g_zones.list[0];
  if (n == 0) return 1;
  for (int i = 0; i < n && i < 3; ++i) {
    const uint8_t id = g_zones.list[1 + i];
    if (id == 0xff || id == zone) return 1;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0053cc40
void zone_reset(uint8_t list[4]) {
  if (!list) return;
  list[0] = 0, list[1] = list[2] = list[3] = 0xff;
}

// Mech3.exe 1.2 @ 0x0053cd20
int32_t zone_common(const uint8_t* a, const uint8_t* b) {
  if (!zones().mode || a[0] == 0 || b[0] == 0) return 0xff;
  for (int i = 0; i < a[0]; ++i) {
    const uint8_t za = a[1 + i];
    if (za == 0xff) return 0xff;
    for (int j = 0; j < b[0]; ++j) {
      if (b[1 + j] == 0xff) return 0xff;
      if (za == b[1 + j]) return za;
    }
  }
  return -1;
}

// Mech3.exe 1.2 @ 0x0053cc90: two lists share a zone, or either is empty or
// all (0xff), or the mode is off. As in 0x0053cdc0, at most three ids.
int32_t zones_overlap(const uint8_t a[4], const uint8_t b[4]) {
  if (g_zones.mode == 0 || a[0] == 0 || b[0] == 0) return 1;
  for (int i = 0; i < a[0] && i < 3; ++i) {
    if (a[1 + i] == 0xff) return 1;
    for (int j = 0; j < b[0] && j < 3; ++j)
      if (b[1 + j] == 0xff || a[1 + i] == b[1 + j]) return 1;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x00541760. Order: near, the four sides, far. A plane the
// sphere is wholly behind (d <= -r) rejects it; one it straddles (d < r)
// stays in the mask for the children.
uint32_t cull_sphere(const float centre[3], uint32_t* mask, float radius) {
  zcls::Camera* cam = walk_state().camera;
  const uint32_t test = *mask;
  *mask = 0;
  if (test & 0x10) {
    const float d = dot_from(centre, cam->floats(0xb8), cam->floats(0x1b8));
    if (d < radius) {
      if (!(-radius < d)) return 0x10;
      *mask = 0x10;
    }
  }
  const float* pos = cam->floats(0x2c);
  for (int i = 0; i < 4; ++i) {
    const uint32_t bit = 1u << i;
    if (!(test & bit)) continue;
    const float d = dot_from(centre, pos, cam->floats(0x188 + 12 * i));
    if (!(-radius < d)) return bit;
    if (d < radius) *mask |= bit;
  }
  if (test & 0x20) {
    const float d = dot_from(centre, cam->floats(0xc4), cam->floats(0x1c4));
    if (!(-radius < d)) return 0x20;
    if (d < radius) *mask |= 0x20;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0054fe80. Edge i runs from point i to point i+1 (the
// last back to 0); its normal (dz, 0, -dx) is normalised.
int32_t set_view_polygon(const float (*points)[3], int32_t count) {
  g_poly.count = 0;
  if (count > 64) return 0;
  for (int32_t i = count - 1; i >= 0; --i) {
    const float* next = points[i == count - 1 ? 0 : i + 1];
    for (int k = 0; k < 3; ++k) g_poly.points[i][k] = points[i][k];
    float* n = g_poly.normals[i];
    n[2] = points[i][0] - next[0];
    n[0] = next[2] - points[i][2];
    n[1] = 0.0f;
    normalize(n);
  }
  g_poly.count = count;
  return 1;
}

// Mech3.exe 1.2 @ 0x0054ff40. Inside (1) unless the point is more than
// -tolerance outside some edge.
int32_t inside_view_polygon(const float point[3], float tolerance) {
  for (int32_t i = 0; i < g_poly.count; ++i) {
    const float dx = point[0] - g_poly.points[i][0];
    const float dz = point[2] - g_poly.points[i][2];
    const float a = dx * g_poly.normals[i][0];
    const float b = dz * g_poly.normals[i][2];
    if (a + b < tolerance) return 0;
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x005438d0
void use_all_casters() {
  g_casters.list = g_casters.all;
  g_casters.count = g_casters.all_count;
}

// Mech3.exe 1.2 @ 0x005438f0
void use_cell_casters() {
  g_casters.list = g_casters.cell;
  g_casters.count = g_casters.cell_count;
}

ShadowCaster*& caster_list() { return g_caster_list; }

// Mech3.exe 1.2 @ 0x00543720
void collect_casters() {
  ShadowCasters& c = g_casters;
  c.all_count = 0;
  for (ShadowCaster* s = g_caster_list; s; s = s->next) {
    if (!s->enabled) continue;
    c.all[c.all_count++] = s;
    if (c.all_count >= 200) return;
  }
}

// Mech3.exe 1.2 @ 0x00543760. (slope x, 1, slope z) turned by the view's
// 3x3, last caster first.
void caster_directions() {
  const float* v = zmth::view().view.m;  // 0x0073fbc0
  for (int32_t i = g_casters.all_count - 1; i >= 0; --i) {
    ShadowCaster& s = *g_casters.all[i];
    const float x = s.slope[0], y = 1.0f, z = s.slope[1];
    for (int j = 0; j < 3; ++j) {
      const float a = x * v[j], b = y * v[3 + j], c = z * v[6 + j];
      const float ab = a + b;
      s.slope_cam[j] = ab + c;
    }
  }
}

// Mech3.exe 1.2 @ 0x00543810. ECX = min x, y, z, max x, y, z of the
// partition, shifted back by a wrapped cell's camera offset (0x0073fbb0).
void cell_casters(const float* box) {
  const float* off = lighting().camera_offset;
  const float x0 = box[0] - off[0], z0 = box[2] - off[2];
  const float x1 = box[3] - off[0], z1 = box[5] - off[2];
  ShadowCasters& c = g_casters;
  c.cell_count = 0;
  for (int32_t i = 0; i < c.all_count; ++i) {
    ShadowCaster* s = c.all[i];
    // x87 compares: "above" fails on NaN, "below" holds on it.
    if (!(s->box_max[0] > x0) || !(s->box_max[2] > z0)) continue;
    if (s->box_min[0] >= x1 || s->box_min[2] >= z1) continue;
    c.cell[c.cell_count++] = s;
    if (c.cell_count >= 200) return;
  }
}

int32_t hidden_surface(const float centre[3], float radius) {
  return g_hooks.hidden_surface ? g_hooks.hidden_surface(g_hooks.ctx, centre, radius) : 1;
}

// --- The casters and their images ---

namespace {
ShadowState g_shadow;

zimg::Texture* image_of(const ShadowCaster* c) { return static_cast<zimg::Texture*>(c->image); }
zvid::D3DTexture* texture_of(const ShadowCaster* c) { return static_cast<zvid::D3DTexture*>(c->texture); }

template <typename T>
T header(const uint8_t* h, int off) {
  T v;
  std::memcpy(&v, h + off, sizeof v);
  return v;
}

// 0x00543330's body.
// Pirate's Moon 1.0 @ 0x0055dec0 does the same with its own model (the node's +0x3c): a polygon's
// corner count in bits 0-9 and its strip flag 0x2000 (MW3: 0-7 and 0x400). `count` and `strip` give them.
template <class M, class Count, class Strip>
void shadow_model_with(const M& m, Count count, Strip strip) {
  const uint8_t* h = m.header;
  if (header<uint32_t>(h, 0x10) == 0) return;
  float (*cam)[3] = draw_buffers().cam;  // [0x0074639c]
  const uint32_t nverts = header<uint32_t>(h, 0x14);
  const float f = header<float>(h, 0x24);
  if ((header<uint32_t>(h, 8) & 8) && (f < 0.0f || f > 0.0f) && header<uint32_t>(h, 0x1c) != 0) {
    zmth::morph(reinterpret_cast<zmth::Vec3*>(cam), reinterpret_cast<const zmth::Vec3*>(m.vertices),
                reinterpret_cast<const zmth::Vec3*>(m.morphs), header<uint32_t>(h, 0x1c), f);
    zmth::transform_points(reinterpret_cast<zmth::Vec3*>(cam), nverts);
  } else if (nverts != 0) {
    zmth::transform_points_to(reinterpret_cast<const zmth::Vec3*>(m.vertices), reinterpret_cast<zmth::Vec3*>(cam),
                              nverts);
  }
  // Down the light onto the ground, then into the image's pixels: the box
  // maps to 0.6 .. size - 0.6. A vertex outside 0 .. size - 0.5 drops the
  // whole model (also NaN).
  const ShadowCaster& c = *g_shadow.current;
  const zimg::Texture& img = *image_of(&c);
  const float sx = (static_cast<float>(img.width) - 1.2f) * c.scale[0];  // 0x005ce250
  const float ox = 0.6f - c.box_min[0] * sx;                           // 0x005ce254
  const float sy = (static_cast<float>(img.height) - 1.2f) * c.scale[1];
  const float oy = 0.6f - c.box_min[2] * sy;
  for (int32_t i = static_cast<int32_t>(nverts) - 1; i >= 0; --i) {
    float* v = cam[i];
    const float d = c.ground_y - v[1];
    v[0] = (c.slope[0] * d + v[0]) * sx + ox;
    v[1] = (c.slope[1] * d + v[2]) * sy + oy;
    if (!(v[0] >= 0.0f) || !(static_cast<float>(img.width) - 0.5f >= v[0])) return;  // 0x005ce258
    if (!(v[1] >= 0.0f) || !(static_cast<float>(img.height) - 0.5f >= v[1])) return;
  }
  float (*poly)[3] = clip3d_buffers().cam;  // 0x007476a4, only x and y
  // Drawn when (y1 - y0)(x2 - x1) > (x1 - x0)(y2 - y1).
  auto facing = [poly]() {
    const float a = (poly[1][0] - poly[0][0]) * (poly[2][1] - poly[1][1]);
    const float b = (poly[1][1] - poly[0][1]) * (poly[2][0] - poly[1][0]);
    return b > a;
  };
  const uint32_t npolys = header<uint32_t>(h, 0x10);
  for (uint32_t p = 0; p < npolys; ++p) {
    const auto& pg = m.polygons[p];
    const uint32_t* idx = pg.vertex_indices;
    const int32_t k = count(pg);
    if (strip(pg)) {
      // A strip, last triangle first: (i, (i + 1) | 1, (i + 2) & ~1) keeps
      // every triangle's winding.
      for (int32_t i = k - 3; i >= 0; --i) {
        const int32_t t[3] = {i, (i + 1) | 1, (i + 2) & ~1};
        for (int j = 0; j < 3; ++j) poly[j][0] = cam[idx[t[j]]][0], poly[j][1] = cam[idx[t[j]]][1];
        if (facing()) zrndr::fill_polygon(&poly[0][0], 3, 1, g_shadow.precision);
      }
    } else {
      // The exe copies the count byte's vertices into a 64-slot buffer
      // unchecked, and tests the first three even when there are fewer.
      for (int32_t j = k - 1; j >= 0; --j) poly[j][0] = cam[idx[j]][0], poly[j][1] = cam[idx[j]][1];
      if (facing()) zrndr::fill_polygon(&poly[0][0], k, 1, g_shadow.precision);
    }
  }
}

void shadow_model_of(const Model& m) {
  shadow_model_with(m, [](const auto& pg) { return static_cast<int32_t>(pg.flags & 0xff); },
                    [](const auto& pg) { return (pg.flags & 0x400) != 0; });
}
void shadow_model_of(const pm::zmod::Model& m) {
  shadow_model_with(m, [](const auto& pg) { return static_cast<int32_t>(pg.flags & 0x3ff); },
                    [](const auto& pg) { return (pg.flags & 0x2000) != 0; });
}
}  // namespace

ShadowState& shadow_state() { return g_shadow; }

// Mech3.exe 1.2 @ 0x00542bb0. The list and colours are reset only when
// shadows come on.
void shadows_init() {
  set_shadows(0);
  if (zrndr::device_fog().active != 1 || !zvid::has_caps(3)) return;
  set_shadows(1);
  g_caster_list = nullptr;
  g_shadow.black = zvid::pack_bytes(0, 0, 0);
  g_shadow.white = zvid::pack_bytes(0xff, 0xff, 0xff);
}

// Mech3.exe 1.2 @ 0x00542c20. The rest of the record is malloc's (zero here).
ShadowCaster* shadow_new() {
  auto* c = new ShadowCaster;
  c->prev = g_caster_list;
  c->next = g_caster_list ? g_caster_list->next : nullptr;
  if (c->prev) c->prev->next = c;
  else g_caster_list = c;
  if (c->next) c->next->prev = c;
  c->node = nullptr;
  c->color = g_shadow.black;
  c->image = nullptr;
  c->texture = nullptr;
  c->enabled = 0;
  return c;
}

// Mech3.exe 1.2 @ 0x00542ca0
void shadow_delete(ShadowCaster* c) {
  if (c->next) c->next->prev = c->prev;
  if (c->prev) c->prev->next = c->next;
  else g_caster_list = c->next;
  if (zimg::Texture* img = image_of(c)) {
    std::free(img->pixels);
    img->pixels = nullptr;
    zimg::free_texture(img);
  }
  if (c->texture) {
    zvid::release_texture(texture_of(c));
    c->texture = nullptr;
  }
  delete c;
}

// Mech3.exe 1.2 @ 0x00542d30. A new 16-bit image unless it already has this
// size; the old texture record is dropped, not released.
void shadow_set_size(ShadowCaster* c, int32_t w, int32_t h) {
  zimg::Texture* img = image_of(c);
  if (img && img->width == w && img->height == h) return;
  if (img) {
    // FIX (bug): the exe clears +0x5c before freeing through it, so it reads
    // address 0x10 and crashes. Never reached in 1.2 (only a new caster gets
    // a size); the port frees the old image.
    std::free(img->pixels);
    img->pixels = nullptr;
    zimg::free_texture(img);
    c->image = nullptr;
  }
  img = static_cast<zimg::Texture*>(std::calloc(1, sizeof(zimg::Texture)));  // 0x00517c10
  c->image = img;
  img->flags = 1;                                                            // 0x00517c60
  img->pixels = std::calloc(static_cast<size_t>(w) * h, 2);  // 0x00517c70; malloc (garbage) there
  img->alpha = nullptr;
  img->width = static_cast<int16_t>(w);                                      // 0x00517c90
  img->height = static_cast<int16_t>(h);
  img->width_again = img->width;
  img->pixel_count = img->width * img->height;
  c->texture = nullptr;
}

// Mech3.exe 1.2 @ 0x00542dd0
void shadow_set_node(ShadowCaster* c, zcls::Node* node) { c->node = node; }

// Mech3.exe 1.2 @ 0x00542de0
void shadow_set_position(ShadowCaster* c, const float p[3]) {
  c->position[0] = p[0], c->position[1] = p[1], c->position[2] = p[2];
}

// Mech3.exe 1.2 @ 0x00542e00
void shadow_set_color(ShadowCaster* c, uint32_t color) { c->color = color; }

// Mech3.exe 1.2 @ 0x00542e10: [0x0072f364] (alpha 0, clamped u and v) the
// first time, [0x0072f374] after.
void shadow_upload(ShadowCaster* c) {
  if (!c->texture) {
    c->texture = zvid::create_texture("gModShadow", *image_of(c), 0, 1, 1);  // 0x005ef82c
    return;
  }
  zvid::reload_texture(texture_of(c), image_of(c));
}

// Mech3.exe 1.2 @ 0x00542e40: [0x0072f378]
void shadow_release_texture(ShadowCaster* c) {
  if (!c->texture) return;
  zvid::release_texture(texture_of(c));
  c->texture = nullptr;
}

// Mech3.exe 1.2 @ 0x00542e60
void shadow_enable(ShadowCaster* c, int32_t on) { c->enabled = on; }

// Mech3.exe 1.2 @ 0x00542e70. The box: the node's own box (+0x74) through
// its world matrix, each corner slid down the light to the ground; its top
// is the highest corner + 2 (before the slide). Then the subtree's polygons
// fill the cleared image with 1s, a 3x3 sum makes 0..9 of each pixel and a
// ramp of 10 greys from white down to the caster's colour replaces it.
int32_t shadow_draw(ShadowCaster* c, const float light[3], float ground) {
  if (!c->enabled || !c->image) return 1;
  if (light[1] >= 0.0f) return 1;  // x87 "below": NaN goes on
  const float inv = 1.0f / light[1];
  c->slope[0] = light[0] * inv;
  const float d = ground - c->position[1];
  c->slope[1] = light[2] * inv;
  const float sx = c->slope[0], sz = c->slope[1];
  const float xx = sx * sx, zz = sz * sz;
  c->slope_len = std::sqrt(xx + zz);
  c->ground_at[0] = d * sx + c->position[0];
  c->ground_at[1] = d * sz + c->position[2];

  zmth::Mat34 m;
  zmth::push(&m);
  zmth::load_identity();
  if (zcls::node_to_world(c->node, 3) != 0) {
    zmth::pop();
    return 1;
  }
  float box[6];
  std::memcpy(box, c->node->raw + 0x74, sizeof box);  // 0x004ebd50, its result unused
  float corners[8][3];
  zmth::box_corners(m.m, box, corners);
  zmth::pop();
  for (auto& p : corners) {
    const float t = ground - p[1];
    const float dx = t * c->slope[0], dz = t * c->slope[1];
    p[0] = dx + p[0];
    p[2] = dz + p[2];
  }
  c->box_min[0] = c->box_max[0] = corners[0][0];
  c->box_max[1] = corners[0][1];
  c->box_min[2] = c->box_max[2] = corners[0][2];
  // x87 compares: "above" fails on NaN, "below" holds on it.
  for (int i = 1; i < 8; ++i) {
    const float* p = corners[i];
    if (c->box_min[0] > p[0]) c->box_min[0] = p[0];
    else if (!(c->box_max[0] >= p[0])) c->box_max[0] = p[0];
    if (!(c->box_max[1] >= p[1])) c->box_max[1] = p[1];
    if (c->box_min[2] > p[2]) c->box_min[2] = p[2];
    else if (!(c->box_max[2] >= p[2])) c->box_max[2] = p[2];
  }
  c->ground_y = ground;
  c->box_max[1] = c->box_max[1] - -2.0f;  // 0x005ce240
  c->scale[0] = 1.0f / (c->box_max[0] - c->box_min[0]);
  c->scale[1] = 1.0f / (c->box_max[2] - c->box_min[2]);

  zimg::Texture& img = *image_of(c);
  const int32_t w = img.width, h = img.height;
  const int32_t rect[4] = {0, 0, w - 1, h - 1};
  zrndr::set_target(static_cast<uint8_t*>(img.pixels), rect, 0x10, w * 2);
  std::memset(img.pixels, 0, static_cast<size_t>(w) * h * 2);
  zmth::push(&m);
  zcls::Node* parent = c->node->parents ? c->node->parents[0] : nullptr;
  if (parent) zcls::node_to_world(parent, 3);
  else zmth::load_identity();
  g_shadow.current = c;
  zrndr::soft_target().winding = 1;
  shadow_node(c->node);
  zmth::pop();

  uint16_t ramp[10];
  const uint32_t step = ~c->color & 0x1f;
  for (uint32_t i = 0, acc = 0; i < 10; ++i, acc += step) {
    const uint8_t g = static_cast<uint8_t>(0xff - static_cast<uint8_t>((acc / 9) << 3));
    ramp[i] = zvid::pack_bytes(g, g, g);
  }
  // In place, left to right and down: the neighbours already done count by
  // their low bit (the fill's 1), the others whole.
  auto* px = static_cast<uint16_t*>(img.pixels);
  for (int32_t y = 1; y < h - 1; ++y) {
    for (int32_t x = 1; x < w - 1; ++x) {
      uint16_t* p = px + y * w + x;
      const int32_t done = (p[-w - 1] & 1) + (p[-w] & 1) + (p[-w + 1] & 1) + (p[-1] & 1);
      const int32_t ahead = static_cast<int16_t>(p[1]) + static_cast<int16_t>(p[w - 1]) +
                            static_cast<int16_t>(p[w]) + static_cast<int16_t>(p[w + 1]);
      *p = static_cast<uint16_t>(*p + 2 * (done + ahead + static_cast<int16_t>(*p)));
    }
  }
  for (int32_t i = w * h - 1; i >= 0; --i) px[i] = ramp[(static_cast<int16_t>(px[i]) & ~1) >> 1];
  return 0;
}

// Mech3.exe 1.2 @ 0x005432d0. Classes 1 and 7..11 cast nothing; 2..4 and
// unknown ones are reported (the node pointer goes to %s: its name).
void shadow_node(zcls::Node* node) {
  switch (node->cls()) {
    case 5: zcls::shadow_object3d(node); break;
    case 6: zcls::shadow_lod(node); break;
    case 1: case 7: case 8: case 9: case 10: case 11: break;
    default: std::fprintf(stderr, "Unrecognized node rendering type: %s\n", node->name()); break;
  }
}

// Mech3.exe 1.2 @ 0x00543330
void shadow_model(zcls::Node* node) {
  if (node->model) shadow_model_of(*node->model);
  else if (node->pm_model) shadow_model_of(*node->pm_model);  // Pirate's Moon 0x0055dec0
}

}  // namespace zmod
