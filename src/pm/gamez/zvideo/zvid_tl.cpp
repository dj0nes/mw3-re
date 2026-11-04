#include "pm/gamez/zvideo/zvid_tl.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "crt/msvc_math.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "platform/render3d.h"
#include "pm/gamez/zmodel/gmod_clip3d.h"
#include "pm/gamez/zmodel/gmod_cull.h"

namespace pm::zvid {

namespace {
using ::zvid::D3DTexture;
using ::zvid::TLVertex;

const D3DTexture* surface_of(const ::zmod::Material& m) {
  return m.texture ? static_cast<const D3DTexture*>(m.texture->surface) : nullptr;
}

// A texture drawn additively: the image's +0x0c bit 2 (0x0059e7c8).
bool adds(const ::zmod::Material& m) {
  return m.texture && m.texture->image && (static_cast<const ::zimg::Texture*>(m.texture->image)->stretch & 4);
}

// FIX (A): CULLMODE CW on a strip (0x0059b870; only strips ask for it) is done here,
// so no backend's winding can change which triangles survive.
void draw(const pm::zmod::DeviceCall& d, const TLVertex* v, int32_t count) {
  if (d.cull != 2 || d.primitive != 5) {
    ::zvid::draw_primitive(d.primitive, v, count);
    return;
  }
  static std::vector<TLVertex> tris;
  tris.clear();
  for (int32_t i = 0; i + 2 < count; ++i) {
    const TLVertex& a = v[i], &b = v[i + 1 + (i & 1)], &c = v[i + 2 - (i & 1)];
    // Screen y runs down: clockwise on screen is a positive cross product.
    if ((b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx) <= 0.0f) tris.insert(tris.end(), {a, b, c});
  }
  if (!tris.empty()) ::zvid::draw_primitive(4, tris.data(), static_cast<int32_t>(tris.size()));
}

// The overlay list holds 64 corners a polygon, and the exe writes past that. FIX (bug):
// a larger one goes on as fans of its first corner (fixes.md).
void overlay_add(int32_t type, const TLVertex* v, int32_t count, const D3DTexture* t, int line) {
  for (int32_t at = 1; at + 1 < count;) {
    const int32_t take = std::min(count - at, 63);
    ::zvid::OverlayPoly* p = ::zvid::overlay_slot(type, take + 1, t, line);
    if (!p) return;
    p->verts[0] = v[0];
    std::memcpy(p->verts + 1, v + at, sizeof(TLVertex) * static_cast<size_t>(take));
    at += take - 1;
  }
}

void queue(const pm::zmod::DeviceCall& d, const TLVertex* v, const D3DTexture* t, bool additive) {
  TLVertex* to = ::zvid::queue_transparent(d.primitive, d.count, t);
  if (!to) return;
  std::memcpy(to, v, sizeof(TLVertex) * static_cast<size_t>(d.count));
  ::zvid::TransparentQueue& q = ::zvid::transparent_queue();
  ::zvid::TransparentPoly& p = q.polys[q.count - 1];
  p.additive = additive, p.own_fog = true;
  std::memcpy(p.fog, zrndr::device_fog().sent, sizeof p.fog);
}

// 0x0059d450: the material's colour on every vertex, flat.
void flat(const pm::zmod::DeviceCall& d, const TLVertex* v) {
  const int32_t alpha = crt::msvc_ftol(static_cast<float>(d.material->alpha) * d.fade);
  const uint32_t c = ::zvid::unpack(d.material->rgb, alpha);
  static std::vector<TLVertex> own;
  own.assign(v, v + d.count);
  for (TLVertex& o : own) o.rhw = o.sz, o.diffuse = c, o.specular = 0xff000000u;
  if (alpha < 0xff) {
    if (d.overlay) overlay_add(0, own.data(), d.count, nullptr, 0x79c);
    else queue(d, own.data(), nullptr, false);
    return;
  }
  if (d.overlay) {
    overlay_add(1, own.data(), d.count, nullptr, 0x76e);
    return;
  }
  ::zvid::set_texture(nullptr);
  ::zvid::set_shade(1);
  draw(d, own.data(), d.count);
}

// 0x0059de30. Below full alpha it draws nothing, where MW3's 0x0057c700 queued the
// polygon (fixes.md, candidates).
void lit(const pm::zmod::DeviceCall& d, const TLVertex* v) {
  if (crt::msvc_ftol(static_cast<float>(d.material->alpha) * d.fade) < 0xff) return;
  if (d.overlay) {
    overlay_add(3, v, d.count, nullptr, 0x8d);
    return;
  }
  ::zvid::set_texture(nullptr);
  ::zvid::set_shade(2);
  draw(d, v, d.count);
}

void opaque_texture(const D3DTexture* t, int32_t shade) {
  ::zvid::DeviceState& s = ::zvid::device_state();
  ::zvid::set_shade(shade);
  if (s.alpha_op != 2) s.alpha_op = 2, ::zvid::set_texture_stage_state(4, 2);
  ::zvid::set_texture(t);
  ::zvid::set_address(t);
}

// 0x0059e500, 0x0059e830, 0x0059eb60, 0x005a0060: alike but for the overlay's type.
void textured(const pm::zmod::DeviceCall& d, const TLVertex* v, int32_t type) {
  const D3DTexture* t = surface_of(*d.material);
  if (!t) return;  // FIX (bug): the exe reads through it
  if (t->type == 4 || !(d.fade >= 1.0f)) {
    if (d.overlay) overlay_add(0, v, d.count, t, 0x8d);
    else queue(d, v, t, adds(*d.material));
    return;
  }
  if (d.overlay) {
    overlay_add(type, v, d.count, t, 0x8d);
    return;
  }
  opaque_texture(t, 2);
  draw(d, v, d.count);
}

// 0x005a0f30: the ref's opaque polygons, as triangles; an alpha plane's cannot reach here.
void indexed_textured(const pm::zmod::DeviceCall& d, const TLVertex* v) {
  const D3DTexture* t = surface_of(*d.material);
  if (!t || t->type == 4) return;
  opaque_texture(t, d.shade);
  ::zvid::draw_primitive(4, v, d.count);
}

// 0x005a0ea0
void indexed_flat(const pm::zmod::DeviceCall& d, const TLVertex* v) {
  ::zvid::set_shade(d.shade);
  ::zvid::set_texture(nullptr);
  ::zvid::draw_primitive(4, v, d.count);
}

// A light map over the polygon, added: texture * map * colour, the fade as alpha, and fog
// going to black so that it dims what is added (0x005a0539..0x005a0d82). FIX (A): the exe's
// two texture stages as one shader, on every setting: its three-stage path, where the device
// offers it, reads as losing the fade (fixes.md).
void light_map(const pm::zmod::DeviceCall& d, const pm::zmod::ClipShadows& c, int32_t k, const TLVertex* uv, const D3DTexture* base) {
  const auto* map = static_cast<const D3DTexture*>(c.texture[k]);
  if (!map) return;  // FIX (bug): the exe reads through it
  static std::vector<platform::LightMapVertex> own;
  own.resize(static_cast<size_t>(d.count));
  const auto channel = [](float x) {
    const int32_t w = static_cast<int32_t>(pm::zmod::rounded_sum(x, 0.5f));
    return static_cast<uint32_t>(w >= 0x100 ? 0xff : w < 0 ? 0 : w);
  };
  const auto* v = reinterpret_cast<const TLVertex*>(d.tl);
  for (int32_t i = 0; i < d.count; ++i) {
    platform::LightMapVertex& o = own[static_cast<size_t>(i)];
    const float* rgb = &c.xyz[k][static_cast<size_t>(i) * 3];
    o.sx = v[i].sx, o.sy = v[i].sy, o.sz = v[i].sz, o.rhw = v[i].sz;
    o.diffuse = (channel(rgb[0]) << 16) | (channel(rgb[1]) << 8) | channel(rgb[2]) | c.fade[k];
    o.specular = v[i].specular;
    o.u0 = uv[i].tu, o.v0 = uv[i].tv;
    o.u1 = c.uv[k][static_cast<size_t>(i) * 2], o.v1 = c.uv[k][static_cast<size_t>(i) * 2 + 1];
  }
  ::zvid::set_render_state(19, 5);
  ::zvid::set_render_state(20, 2);
  ::zvid::set_render_state(28, 1);
  ::zvid::set_texture(base);
  ::zvid::set_address(base);
  ::zvid::set_texture_stage_state(1, 4);
  ::zvid::set_texture_stage_state(4, 4);
  const float* fog = zrndr::device_fog().sent;
  const bool black = fog[0] == 0.0f && fog[1] == 0.0f && fog[2] == 0.0f;
  if (!black) ::zvid::set_render_state(34, 0);
  if (platform::Render3D* r = ::zvid::render3d())
    r->draw_light_map(static_cast<platform::Primitive>(d.primitive), own.data(), d.count, map->handle, ::zvid::draw_state());
  if (!black) ::zvid::send_fog_color();
  ::zvid::set_texture_stage_state(4, 2);
}

// 0x005a03b0: the polygon, then each set over it with its own u, v: a shadow multiplied
// (kind 1, clamped, the texture's colour alone, no fog), other materials blended (kind 0),
// a light map added (kind 2).
void sets(const pm::zmod::DeviceCall& d, const TLVertex* v) {
  const D3DTexture* base = surface_of(*d.material);
  if (!base) return;  // FIX (bug): the exe reads through it
  ::zvid::DeviceState& s = ::zvid::device_state();
  opaque_texture(base, 2);
  draw(d, v, d.count);
  ::zvid::set_render_state(27, 1);
  static std::vector<TLVertex> own;
  own.assign(v, v + d.count);
  const pm::zmod::ClipShadows& c = *d.sets;
  const auto address = [&](int32_t a) {
    if (s.address_u != a) ::zvid::set_texture_stage_state(13, a), s.address_u = a;
    if (s.address_v != a) ::zvid::set_texture_stage_state(14, a), s.address_v = a;
  };
  // The polygon's own u, v are kept for a light map only when the last set is one and the first
  // is not (0x005a04bb); otherwise it takes what earlier sets left in the vertices.
  const bool keep_uv = c.count > 0 && c.kind[0] != 2 && c.kind[c.count - 1] == 2;
  for (int32_t k = 0; k < c.count; ++k) {
    if (c.kind[k] == 2) {
      light_map(d, c, k, keep_uv ? v : own.data(), base);
      continue;
    }
    const bool shadow = c.kind[k] == 1;
    ::zvid::set_render_state(19, shadow ? 1 : 5);
    ::zvid::set_render_state(20, shadow ? 3 : 6);
    ::zvid::set_render_state(28, shadow ? 0 : 1);
    address(shadow ? 3 : 1);
    ::zvid::set_texture_stage_state(1, shadow ? 2 : 4);
    for (int32_t i = 0; i < d.count; ++i) std::memcpy(&own[static_cast<size_t>(i)].tu, &c.uv[k][static_cast<size_t>(i) * 2], 8);
    ::zvid::set_texture(static_cast<const D3DTexture*>(c.texture[k]));
    draw(d, own.data(), d.count);
  }
  ::zvid::set_render_state(19, 5);
  ::zvid::set_render_state(20, 6);
  ::zvid::set_render_state(27, 0);
  ::zvid::set_texture_stage_state(1, 4);
  ::zvid::set_render_state(28, 1);
}
}  // namespace

void device_call(const pm::zmod::DeviceCall& d) {
  ::zvid::DeviceState& s = ::zvid::device_state();
  const int32_t held = s.primitive;
  s.primitive = d.primitive;  // [0x008c4c40]
  const auto* v = reinterpret_cast<const TLVertex*>(d.tl);
  switch (d.kind) {
    case pm::zmod::DeviceCall::kFlat: flat(d, v); break;
    case pm::zmod::DeviceCall::kLit: lit(d, v); break;
    case pm::zmod::DeviceCall::kTextured: textured(d, v, 4); break;
    case pm::zmod::DeviceCall::kTexturedLit: textured(d, v, 5); break;
    case pm::zmod::DeviceCall::kTexturedVertex: textured(d, v, 6); break;
    case pm::zmod::DeviceCall::kProjected: textured(d, v, 5); break;
    case pm::zmod::DeviceCall::kSets: sets(d, v); break;
    case pm::zmod::DeviceCall::kIndexedTextured: indexed_textured(d, v); break;
    case pm::zmod::DeviceCall::kIndexedFlat: indexed_flat(d, v); break;
  }
  s.primitive = held;
}

}  // namespace pm::zvid
