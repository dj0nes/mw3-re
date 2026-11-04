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
#include "gamez/zrender/zrndr_flare.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_dd.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zvideo/zvid_format.h"
#include "platform/render3d.h"
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

// The view's clip and the cull's, as 0x00548990 and 0x0053b730 leave them, then the draw.
int32_t draw_pm_model(zcls::Node* node, uint32_t mask) {
  const zmth::Projection& v = zmth::projection();
  pm::zmod::Clip3dState& c = pm::zmod::clip3d_state();
  c.near_z = v.near_z;
  c.left = v.origin_x, c.top = v.origin_y;
  c.right = v.origin_x + v.half_w * 2.0f - 0.001f, c.bottom = v.origin_y + v.half_h * 2.0f - 0.001f;
  pm::zmod::CullState& k = pm::zmod::cull_state();
  k.far_z = v.far_z, k.slope_x = v.cot_h, k.slope_y = v.cot_v;
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
  zvid::overlay_list().limit = zvid::OverlayList::kMaxPm;
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
