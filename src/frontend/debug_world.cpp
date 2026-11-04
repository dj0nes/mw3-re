#include "frontend/debug_world.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "gamez/zclass/camera.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_zbd.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zclass/sound.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zmodel/gmod_puff.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zvideo/zvid_dd.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "gamez/zvideo/zvid_format.h"
#include "mech3/mission_world.h"
#include "platform/render3d.h"

namespace frontend {
namespace {

zcls::Node* camera_node() {
  zcls::ListItem* item = zcls::list_head(zcls::kCameras);
  return item ? item->node : nullptr;
}

zvid::D3DTexture g_fallback{};  // the default record (0x0080931c) for textures that fail

// The mission's world.zrd block through the ported reader (0x491de0), the
// way mission load calls it: hardware, a campaign game, the sun option on.
// The wind and the puffer tick go to gmod_puff; weather (rain, snow) is not
// ported: its hooks do nothing.
std::string read_world_params(const std::string& mission, const std::string& variant) {
  zrdr::close_archives(true);
  zrdr::init();
  const std::string reader = "zbd\\" + mission + "\\reader.zbd";
  if (!zrdr::open_archive(reader.c_str(), true)) return "could not open " + reader;
  std::string key = variant;
  if (key.empty()) {
    // ((name block name block ...)): the first block's name.
    zrdr::Node* top = zrdr::load("world.zrd");
    zrdr::Node* blocks = top && top->type == zrdr::List && zrdr::list_count(top) >= 2 ? &top->list[1] : nullptr;
    if (!blocks || blocks->type != zrdr::List || zrdr::list_count(blocks) < 2 || blocks->list[1].type != zrdr::String)
      return "no world.zrd blocks in " + reader;
    key = blocks->list[1].s;
    for (int32_t i = 1; i < blocks->list[0].i; i += 2)  // the campaign's first mission when there is one
      if (blocks->list[i].type == zrdr::String && std::strcmp(blocks->list[i].s, "m1") == 0) key = "m1";
  }
  mech3::WorldHooks& h = mech3::world_hooks();
  h = mech3::WorldHooks{};
  h.hw_card_flag = [] { return 1; };
  h.world = zcls::camera_world(camera_node());
  h.camera = camera_node();
  zmod::puff_hooks().create_node = [](void*) { return zcls::object3d_create(); };
  h.puffer_tick_reset = zmod::puff_tick_reset;
  h.wind_velocity = zmod::wind_set_velocity;
  h.wind_max_speed = zmod::wind_set_max_speed;
  h.wind_random_accel = zmod::wind_set_random_accel;
  h.wind_random_angvel = zmod::wind_set_random_angvel;
  h.create_snow = [](int32_t) -> mech3::Weather* { return nullptr; };
  h.create_rain = [](int32_t) -> mech3::Weather* { return nullptr; };
  h.weather_add = [](mech3::Weather*) {};
  h.weather_remove = [](mech3::Weather*) {};
  h.weather_destroy = [](mech3::Weather*) {};
  h.weather_start = [](mech3::Weather*, float) {};
  h.weather_stop = [](mech3::Weather*) {};
  h.fluctuate_on = zmod::clouds_set_on;
  h.fluctuate_period = zmod::clouds_set_period;
  h.fluctuate_wavelength = zmod::clouds_set_wavelength;
  h.fluctuate_base_amp = zmod::clouds_set_level;
  h.fluctuate_heading = zmod::clouds_set_heading;
  static mech3::MissionWorld m;
  m = mech3::MissionWorld{};
  mech3::world_init(m);
  if (!mech3::world_read_params(m, "world.zrd", key.c_str())) return "no block " + key + " in world.zrd";
  std::fprintf(stderr, "world.zrd %s: fog %s, range %g..%g, altitude %g..%g, sky %s\n", key.c_str(),
               m.fog ? "on" : "off", m.fog_range[0], m.fog_range[1], m.fog_altitude[0], m.fog_altitude[1],
               m.sky ? m.sky->name() : "-");
  return {};
}

}  // namespace

std::string world_load(const std::string& mission, const std::string& variant, platform::Render3D* render, int width,
                       int height) {
  // Fresh pools, as a mission load starts (0x4ff520's callers).
  zimg::TextureTable& t = zimg::textures();
  std::memset(t.entries, 0, sizeof t.entries);
  t.count = 0, t.group = 0;
  zmod::materials() = zmod::MaterialTable{};
  zmod::models() = zmod::ModelPool{};
  zcls::nodes() = zcls::NodePool{};
  zcls::nodes().free_head = -1;
  zcls::lists() = zcls::Lists{};
  zmod::fog() = zmod::Fog{};
  zrndr::fog_color() = zrndr::FogColor{};
  zvid::set_pixel_format(5, 6, 5);
  zvid::channel_masks() = {0xf800, 0x07e0, 0x001f};
  zrndr::device_fog().active = 1;  // the Direct3D path
  const std::string path = "zbd\\" + mission + "\\gamez.zbd";
  if (zcls::read_zbd(path.c_str()) != 0) return "could not read " + path;
  if (!camera_node()) return "no camera in " + path;
  // The world files keep a 320x200 window; the game resizes it to the display
  // mode at startup (the window setter is not ported yet): the mode asked for.
  for (zcls::ListItem* i = zcls::list_head(zcls::kWindows); i; i = i->next)
    if (i->node && i->node->data) {
      auto* w = static_cast<zcls::Window*>(i->node->data);
      w->put<int32_t>(0x0, 0), w->put<int32_t>(0x4, 0), w->put<int32_t>(0x8, width), w->put<int32_t>(0xc, height);
    }
  render->set_viewport(0, 0, width, height);

  // The libraries as mission load registers them (0x48fcfe): the mission's
  // "texture", "mechtex", then "image", r-prefixed on hardware, found through
  // the mounts: each asset root's zbd/<mission>/ and zbd/ are added.
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

  // The draw table and the walk (0x579f50; [0x74a2e0] = 0x53f2e0).
  zvid::install_model_draw_table();
  zmod::use_device_light_output();  // the lighting's point colour and fog mix to the device
  zcls::scene_walk() = zcls::SceneWalk{};
  zmod::walk_state() = zmod::WalkState{};
  zmod::walk_state().draw = zmod::draw_model;
  zmod::zones() = zmod::Zones{};
  zmod::shadow_casters() = zmod::ShadowCasters{};
  zmod::draw_state() = zmod::DrawState{};
  zmod::cull_hooks() = zmod::CullHooks{};
  zcls::update_hooks() = zcls::UpdateHooks{};
  zcls::WorldRenderHooks& h = zcls::world_render_hooks();
  h = zcls::WorldRenderHooks{};
  h.begin_scene = [](void*) { zvid::scene_begin(); };
  h.end_scene = [](void*) { zvid::scene_end(); };
  h.flush_transparent = [](void*) { zvid::flush_transparent(); };
  h.flush_overlay = [](void*) { zvid::flush_overlay(); };
  h.draw_quad = [](void*) { zvid::draw_tint_quad(); };
  h.puffers = [](void*) { zmod::puff_draw(); };  // none yet: the animations make them (anim run, not ported)
  zimg::default_entry()->surface = &g_fallback;
  zmth::reset_stack();
  return read_world_params(mission, variant);
}

void world_sounds_on() {
  zcls::WorldRenderHooks& h = zcls::world_render_hooks();
  h.world_sounds = [](void*, zcls::Node* world) { zcls::world_sounds(world); };  // 0x004f3170
  h.listener_speed_limit = [](void*) { return zsnd::speed_of_sound(); };       // 0x00572360
  h.listener = [](void*, const float* m, const float v[3]) { zsnd::set_listener(m, v); };  // 0x00571e50
}

void world_frame(const float eye[3], float yaw, float pitch, platform::Render3D* render, float dt) {
  zcls::Node* cam = camera_node();
  if (!cam) return;
  // The camera looks along -z at zero rotation (render.md section 0); the
  // viewer's yaw 0 looks along +z, hence the half turn. Pitch about x, up
  // positive for the viewer, so negated.
  zcls::camera_set_rotation(cam, -pitch, yaw + 3.14159265f, 0.0f);
  zcls::camera_set_position(cam, eye[0], eye[1], eye[2]);
  ++zcls::frames().frame;
  if (dt >= 0) ztimer::state().dt = dt;
  render->clear(true, 89, 115, 140);
  zcls::render_world(cam, 0);
  render->present();
}

}  // namespace frontend
