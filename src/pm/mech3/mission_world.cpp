#include "pm/mech3/mission_world.h"

#include "gamez/zclass/camera.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/list.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zreader/zreader.h"
#include "pm/gamez/zclass/light.h"

namespace pm::mech3 {

namespace {
constexpr float kDegToRad = 0.017453292f;  // [0x005deee8]

struct Params {
  bool fog = false;
  float range[2] = {}, altitude[2] = {}, color[3] = {};
  ::zcls::Node* sky = nullptr;
  bool fluctuate = false;  // [this+8]
};

// Pirate's Moon 1.0 @ 0x00495250
void read_block(zrdr::Node* params, ::zcls::Node* sun, ::zcls::Node* camera, Params& p) {
  if (!params) return;
  const auto* cam_data = static_cast<const ::zcls::Camera*>(camera->data);
  float clip[2] = {cam_data->get<float>(0xb0), cam_data->get<float>(0xb4)};  // 0x00506f80
  if (zrdr::Node* fog = zrdr::find(params, "fog")) {
    p.sky = nullptr;
    p.fog = true;
    zrdr::get_vec2(fog, "range", p.range);
    zrdr::get_vec2(fog, "hwrange", p.range);  // Direct3D's, over it
    zrdr::get_vec3(fog, "color", p.color);
    zrdr::get_vec2(fog, "altitude", p.altitude);
    const char* node = zrdr::get_string(fog, "node");  // 0x0050b920 takes a null name, MW3's not
    p.sky = node ? ::zcls::list_find_by_name(::zcls::kAllNodes, node) : nullptr;
  }
  if (zrdr::Node* cam = zrdr::find(params, "camera")) zrdr::get_vec2(cam, "nearfar_clip", clip);
  if (zrdr::Node* s = sun ? zrdr::find(params, "sun") : nullptr) {
    float v[3], k = 0.0f;
    if (zrdr::get_vec3(s, "orientation", v)) {
      pm::zcls::light_set_orientation(sun, v[0] * kDegToRad, v[1] * kDegToRad, v[2] * kDegToRad);
    }
    if (zrdr::get_float(s, "diffuse", &k)) pm::zcls::light_set_diffuse(sun, k);
    if (zrdr::get_float(s, "ambient", &k)) pm::zcls::light_set_ambient(sun, k);
    bool diffuse = false;
    if (zrdr::get_vec3(s, "diffuse_color", v) || zrdr::get_vec3(s, "color", v)) {
      pm::zcls::light_set_diffuse_color(sun, v[0], v[1], v[2]);
      diffuse = true;
    }
    if (zrdr::get_vec3(s, "ambient_color", v)) {
      pm::zcls::light_set_ambient_color(sun, v[0], v[1], v[2]);
      if (diffuse) pm::zcls::light_set_own_colors(sun, 1);
    }
  }
  ::zcls::camera_set_clip(camera, clip[0], clip[1]);
  if (zrdr::Node* fl = zrdr::find(params, "fluctuate")) {
    float v, amplitude;
    ::zmod::clouds_set_period(zrdr::get_float(fl, "period", &v) ? v : 5.0f);
    ::zmod::clouds_set_wavelength(zrdr::get_float(fl, "wavelength", &v) ? v : 500.0f);
    const float base = zrdr::get_float(fl, "base", &v) ? v : 0.7f;
    if (!zrdr::get_float(fl, "amplitude", &amplitude)) amplitude = 0.3f;
    ::zmod::clouds_set_level(base, amplitude);
    p.fluctuate = amplitude != 0.0f;  // flat: left off
    ::zmod::clouds_set_heading((zrdr::get_float(fl, "heading", &v) ? v : 0.0f) * kDegToRad);
  }
}
}  // namespace

void world_read_params(::zcls::Node* world, ::zcls::Node* camera, const char* key, bool sun_option) {
  zrdr::Node* root = zrdr::load("world.zrd");
  if (!root) return;
  ::zcls::Node* sun = ::zcls::list_find_by_name(::zcls::kAllNodes, "sunlight");
  Params p;
  read_block(zrdr::find(root, "defaults"), sun, camera, p);
  if (key) read_block(zrdr::find(root, key), sun, camera, p);
  // 0x00495dd0(1), on hardware: the sun on, then off without its option; the clouds on if read.
  if (sun) {
    ::zcls::node_set_active(sun, 1);
    if (!sun_option) ::zcls::node_set_active(sun, 0);
  }
  if (p.fluctuate) ::zmod::clouds_set_on(1);
  if (p.fog) {
    ::zcls::world_set_fog_type(world, 1);
    ::zcls::world_set_fog_range(world, p.range[0], p.range[1]);
    ::zcls::world_set_fog_color(world, p.color[0], p.color[1], p.color[2]);
    ::zcls::world_set_fog_altitude(world, p.altitude[0], p.altitude[1]);
    // The named sky is shown, the horizon's other children hidden.
    if (::zcls::Node* horizon = p.sky ? static_cast<::zcls::Camera*>(camera->data)->nodes[2] : nullptr) {
      for (int i = 0; i < horizon->child_count(); ++i) {
        ::zcls::node_set_active(horizon->children[i], horizon->children[i] == p.sky ? 1 : 0);
      }
    }
  }
  zrdr::free_node(root);
}

}  // namespace pm::mech3
