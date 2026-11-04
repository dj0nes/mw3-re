#include "frontend/pm_debug_world.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "gamez/zclass/camera.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zgame/zgame_table.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_clip2d.h"
#include "gamez/zmodel/gmod_clip3d.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_dd.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zvideo/zvid_format.h"
#include "platform/render3d.h"
#include "pm/gamez/zmodel/gmod_const.h"
#include "pm/gamez/zmodel/gmod_draw.h"

namespace frontend {
namespace {

zcls::Node* camera_node() {
  zcls::ListItem* item = zcls::list_head(zcls::kCameras);
  return item ? item->node : nullptr;
}

zvid::D3DTexture g_fallback{};

struct Vert {
  float pos[3], uv[2], color[3];
};

Vert cut(const Vert& a, const Vert& b, float near_z) {
  const float t = (near_z - a.pos[2]) / (b.pos[2] - a.pos[2]);
  Vert v;
  for (int i = 0; i < 3; ++i) v.pos[i] = a.pos[i] + (b.pos[i] - a.pos[i]) * t;
  for (int i = 0; i < 2; ++i) v.uv[i] = a.uv[i] + (b.uv[i] - a.uv[i]) * t;
  for (int i = 0; i < 3; ++i) v.color[i] = a.color[i] + (b.color[i] - a.color[i]) * t;
  return v;
}

// Dev only, deleted when 0x00543b10's submit lands: every polygon's first
// material and the file's vertex colours, near clip only (the GPU does the
// sides), no lights, fog, sets or priority.
void draw_piece(const pm::zmod::Polygon& p, const std::vector<float>& cam, const int* idx, int n) {
  const zmod::Material& mat = *p.materials[0];
  const zmth::Projection& pr = zmth::projection();
  std::vector<Vert> in(static_cast<size_t>(n)), out;
  for (int i = 0; i < n; ++i) {
    Vert& v = in[static_cast<size_t>(i)];
    const int k = idx[i];
    std::memcpy(v.pos, &cam[3 * static_cast<size_t>(p.vertex_indices[k])], 12);
    v.uv[0] = p.uvs ? p.uvs[0][2 * k] : 0, v.uv[1] = p.uvs ? p.uvs[0][2 * k + 1] : 0;
    std::memcpy(v.color, p.colors + 3 * k, 12);
  }
  for (int i = 0; i < n; ++i) {
    const Vert& a = in[static_cast<size_t>(i)];
    const Vert& b = in[static_cast<size_t>((i + 1) % n)];
    const bool ain = a.pos[2] >= pr.near_z, bin = b.pos[2] >= pr.near_z;
    if (ain) out.push_back(a);
    if (ain != bin) out.push_back(cut(a, b, pr.near_z));
  }
  const int count = static_cast<int>(out.size());
  if (count < 3) return;
  zmod::TLVertex* tl = zmod::clip2d().tl;
  for (int i = 0; i < count; ++i) {
    const Vert& v = out[static_cast<size_t>(i)];
    const float w = pr.near_z / v.pos[2];
    tl[i].sz = w;
    tl[i].sx = pr.fx_near * v.pos[0] * w + pr.cx;
    tl[i].sy = pr.fy_near * v.pos[1] * w + pr.cy;
    tl[i].tu = v.uv[0], tl[i].tv = v.uv[1];
    std::memcpy(zmod::clip2d().work[i].color, v.color, 12);
  }
  zmod::DrawTable& t = zmod::draw_table();
  if ((mat.flags & 1) && mat.texture && p.uvs)
    t.textured(t.ctx, count, mat.texture->surface, 1.0f, 0);
  else
    t.flat(t.ctx, mat.rgb, 255, count, p.priority, 0);
}

void draw_polygon(const pm::zmod::Polygon& p, const std::vector<float>& cam) {
  const int n = static_cast<int>(p.flags & 0x3ff);
  if (n < 3 || p.material_count < 1 || !p.materials || !p.materials[0]) return;
  if (p.flags & 0x2000) {  // a strip: its triangles, the device's primitive stays a fan
    for (int i = 0; i + 2 < n; ++i) {
      const int tri[3] = {i, i + 1, i + 2};
      draw_piece(p, cam, tri, 3);
    }
    return;
  }
  std::vector<int> all(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) all[static_cast<size_t>(i)] = i;
  draw_piece(p, cam, all.data(), n);
}

int32_t draw_unlit(zcls::Node* node, uint32_t) {
  static std::vector<float> cam;
  pm::zmod::Model& m = *node->pm_model;
  zmth::Mat34 local;
  zmth::push(&local);
  float centre[3] = {};
  if (!pm::zmod::prepare_model(m, centre)) return 0;
  const int32_t verts = m.count(0x14), polys = m.count(0x10);
  if (verts > 0 && polys > 0) {
    cam.resize(3 * static_cast<size_t>(verts));
    zmth::transform_points_to(reinterpret_cast<const zmth::Vec3*>(m.vertices), reinterpret_cast<zmth::Vec3*>(cam.data()),
                              static_cast<uint32_t>(verts));
    for (int32_t i = 0; i < polys; ++i) draw_polygon(m.polygons[i], cam);
  }
  zmth::pop();
  return 0;
}

}  // namespace

std::string pm_world_load(const std::string& mission, platform::Render3D* render, int width, int height, float eye[3]) {
  zvid::set_pixel_format(5, 6, 5);
  zvid::channel_masks() = {0xf800, 0x07e0, 0x001f};
  zrndr::device_fog().active = 1;
  const std::string path = "zbd\\" + mission + "\\gamez.zbd";
  if (zgame::game().zcls_read_zbd(path.c_str()) != 0) return "could not read " + path;
  if (!camera_node()) return "no camera in " + path;
  for (zcls::ListItem* i = zcls::list_head(zcls::kWindows); i; i = i->next)
    if (i->node && i->node->data) {
      auto* w = static_cast<zcls::Window*>(i->node->data);
      w->put<int32_t>(0x0, 0), w->put<int32_t>(0x4, 0), w->put<int32_t>(0x8, width), w->put<int32_t>(0xc, height);
    }
  render->set_viewport(0, 0, width, height);

  zimg::state() = zimg::State{};
  zimg::state().display_format = 6;
  const std::vector<std::string> roots = zutl::mounts().read;
  for (const std::string& r : roots) zutl::mount(r + "/zbd/" + mission);
  for (const std::string& r : roots) zutl::mount(r + "/zbd");
  for (int l = 0; l < 2; ++l) zimg::library_list(l).clear();
  zimg::new_library("texture", 0, true);
  zimg::new_library("mechtex", 0, true);
  zimg::register_image_library(1);
  zimg::texture_load() = zimg::TextureLoad{};
  zimg::texture_hooks() = zimg::TextureHooks{};
  zvid::set_render3d(render);
  zvid::default_texture() = &g_fallback;
  zimg::load_group(-1);
  zimg::default_entry()->surface = &g_fallback;

  zvid::install_model_draw_table();
  zmod::walk_state() = zmod::WalkState{};
  zmod::walk_state().draw = draw_unlit;
  zcls::WorldRenderHooks& h = zcls::world_render_hooks();
  h = zcls::WorldRenderHooks{};
  h.begin_scene = [](void*) { zvid::scene_begin(); };
  h.end_scene = [](void*) { zvid::scene_end(); };
  h.flush_transparent = [](void*) { zvid::flush_transparent(); };
  h.flush_overlay = [](void*) { zvid::flush_overlay(); };
  zmth::reset_stack();

  // The file's camera sees 800; the game sets its own clip later (not ported).
  {
    zcls::Node* c = camera_node();
    zcls::camera_set_clip(c, static_cast<zcls::Camera*>(c->data)->get<float>(0xb0), 4000.0f);
  }
  // Over the middle of the world's area (+0x34..+0x44, +0x38..+0x48).
  const auto* w = static_cast<const zcls::World*>(zcls::camera_world(camera_node())->data);
  eye[0] = (w->get<float>(0x34) + w->get<float>(0x44)) * 0.5f;
  eye[1] = 150.0f;
  eye[2] = (w->get<float>(0x38) + w->get<float>(0x48)) * 0.5f;
  int32_t with = 0;
  for (int32_t i = 0; i < zcls::nodes().capacity; ++i) with += zcls::node_at(i)->pm_model != nullptr;
  std::printf("mw3pm: %s: %d nodes with models, view from %.1f %.1f %.1f\n", path.c_str(), with, eye[0], eye[1], eye[2]);
  return {};
}

void pm_world_frame(const float eye[3], float yaw, float pitch, platform::Render3D* render) {
  zcls::Node* cam = camera_node();
  if (!cam) return;
  zcls::camera_set_rotation(cam, -pitch, yaw + 3.14159265f, 0.0f);
  zcls::camera_set_position(cam, eye[0], eye[1], eye[2]);
  ++zcls::frames().frame;
  render->clear(true, 89, 115, 140);
  zcls::render_world(cam, 0);
  render->present();
}

}  // namespace frontend
