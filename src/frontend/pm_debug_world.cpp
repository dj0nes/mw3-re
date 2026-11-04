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
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zrender/zrndr_flare.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_dd.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zvideo/zvid_format.h"
#include "platform/render3d.h"
#include "pm/gamez/zimage/zimg_texture.h"
#include "pm/gamez/zmodel/gmod_clip3d.h"
#include "pm/gamez/zmodel/gmod_const.h"
#include "pm/gamez/zmodel/gmod_cull.h"
#include "pm/gamez/zmodel/gmod_draw.h"
#include "pm/gamez/zvideo/zvid_tl.h"

namespace frontend {
namespace {

zcls::Node* camera_node() {
  zcls::ListItem* item = zcls::list_head(zcls::kCameras);
  return item ? item->node : nullptr;
}

zvid::D3DTexture g_fallback{};

// --map-light: on the frame's light list, in camera space, once a frame.
zcls::Light g_map_light;
float g_map_light_at[3];
bool g_map_light_on = false;

void add_map_light() {
  zmod::LightList& list = zmod::light_list();
  if (list.count > 0 && list.list[list.count - 1].data == &g_map_light) return;
  if (list.count >= zmod::LightList::kMax) return;
  const float* t = zmth::view().view.m;
  const float* p = g_map_light_at;
  float* to = g_map_light.floats(0x78);
  to[0] = p[0] * t[0] + p[1] * t[3] + p[2] * t[6] + t[9];
  to[1] = p[0] * t[1] + p[1] * t[4] + p[2] * t[7] + t[10];
  to[2] = p[0] * t[2] + p[1] * t[5] + p[2] * t[8] + t[11];
  zmod::LightEntry& e = list.list[list.count++];
  e = zmod::LightEntry{};
  e.data = &g_map_light, e.reach = 1;
  list.any = 1;
}

// The view's clip and the cull's, from what the shared setter left: PM's 0x00548990 and
// 0x0053b730 are MW3's 0x00526e10 and 0x00521c60 (camera +0xb0, +0xb4, +0x1d4, +0x1d8; the window's
// origin and size; D3D's sides x, y, x + w + 0.001, y + h + 0.001).
int32_t draw_pm_model(zcls::Node* node, uint32_t mask) {
  const zmth::Projection& v = zmth::projection();
  const zmod::ClipPlanes& from = zmod::draw_state().clip;
  pm::zmod::Clip3dState& c = pm::zmod::clip3d_state();
  c.near_z = from.near_z;
  std::memcpy(&c.left, &from.side_04, 4), std::memcpy(&c.top, &from.side_08, 4);
  std::memcpy(&c.right, &from.side_1c, 4), std::memcpy(&c.bottom, &from.side_20, 4);
  pm::zmod::CullState& k = pm::zmod::cull_state();
  k.far_z = from.far_z, k.slope_x = v.cot_h, k.slope_y = v.cot_v;
  if (g_map_light_on) add_map_light();
  return pm::zmod::draw_model(node, mask, pm::zvid::device_call);
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
  zmod::walk_state().draw = draw_pm_model;
  zvid::overlay_list().set_limit(zvid::OverlayList::kMaxPm);
  pm::zmod::draw_state().flare = zrndr::flare_add;  // 0x00588910
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

void pm_world_map_light(const float pos[3], float range) {
  zimg::TextureEntry* map = zimg::load_named("lightmap", zimg::textures().group, 0);
  if (!map) {
    std::printf("mw3pm: --map-light: no lightmap texture\n");
    return;
  }
  pm::zimg::set_light_map(map);
  std::memcpy(g_map_light_at, pos, 12);
  // PM's light by its PM offsets: flags at +0xb0 here; +0xd4.. the colours; +0xe4.. off 0x30.
  g_map_light.put<uint32_t>(0xb0, 0x10 | 0x80 | 2 | 0x800);
  for (int k = 0; k < 3; ++k) g_map_light.pm_colors[(0xd4 - 0xb0) / 4 + k] = 1.0f;
  const float near_range = range * 0.5f;
  g_map_light.put<float>(0xe4 - 0x30, near_range), g_map_light.put<float>(0xe8 - 0x30, range);
  g_map_light.put<float>(0xec - 0x30, near_range * near_range), g_map_light.put<float>(0xf0 - 0x30, range * range);
  g_map_light.put<float>(0xf4 - 0x30, 1.0f / (range - near_range));
  g_map_light_on = true;
  // GfxFlags' lighting, off in --fly: the scripts that colour the world's lights are not ported, so
  // with it on the world goes dark and polygons no light reaches stay bright.
  zmod::lighting().mask_base |= 1;
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
