#include "mech3/mission_world.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "crt/msvc_math.h"
#include "gamez/zclass/camera.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zreader/zreader.h"
#include "pm/gamez/zclass/light.h"
#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zvideo/zvid_format.h"

namespace mech3 {

namespace {
constexpr double kDegToRad = 0.01745329251994;  // 0x005c60c0: pi cut to 3.1415926535892, / 180
Environment g_env;
WorldHooks g_hooks{};

bool hardware() { return zrndr::device_fog().active != 0; }  // 0x0072f348

using zcls::world_get_fog_altitude;
using zcls::world_get_fog_color;
using zcls::world_get_fog_range;
}  // namespace

Environment& environment() { return g_env; }

const char* soil_name(int32_t i);  // 0x0047f380, mech_data.cpp

namespace {
uint32_t g_soils[64];  // 0x00608c00
}

// Mech3.exe 1.2 @ 0x0047f400. A name zModel does not know gives -1, and the
// exe writes the word before the table. FIX (bug): skipped, as is an index
// past the table (fixes.md).
void build_soil_table() {
  g_soils[0] = 0;
  for (uint32_t soil = 1; soil < 11; ++soil) {
    const int32_t i = zmod::soil_index(soil_name(static_cast<int32_t>(soil)));
    if (i >= 0 && i < 64) g_soils[i] = soil;
  }
}

uint32_t game_soil(uint32_t zmodel_soil) { return zmodel_soil < 64 ? g_soils[zmodel_soil] : 0; }
WorldHooks& world_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x00491d70
void world_init(MissionWorld& m) {
  m.weather = nullptr;
  m.sun = nullptr;
  m.fluctuate = 0;
  m.fog = 0;
}

// Mech3.exe 1.2 @ 0x00491d80
bool world_clear(MissionWorld& m) {
  const WorldHooks& h = g_hooks;
  if (m.weather) {
    h.weather_remove(m.weather);
    h.weather_destroy(m.weather);
  }
  m.weather = nullptr;
  if (m.sun) {
    delete m.sun;
    m.sun = nullptr;
  }
  if (m.fluctuate) {
    h.fluctuate_on(0);
    m.fluctuate = 0;
  }
  m.fog = 0;
  return true;
}

// Mech3.exe 1.2 @ 0x004926d0, with zMath's 0x00522170 (pitch and yaw to the
// direction they face) and 0x004020d0 on its level part.
void world_update(const MissionWorld& m) {
  const WorldHooks& h = g_hooks;
  if (!m.fluctuate) return;
  const float* rot = h.camera_angles();
  const float pitch = rot[0], yaw = rot[1];
  const float cp = static_cast<float>(std::cos(double{pitch}));
  zmth::Vec3 dir{-cp * static_cast<float>(std::sin(double{yaw})), 0.0f,
                 -cp * static_cast<float>(std::cos(double{yaw}))};
  zmth::normalize(dir);
  const float d[2] = {dir.z, -dir.x};
  h.clouds_advance(d, h.frame_dt());
}

// Mech3.exe 1.2 @ 0x00492660. The sun is lit only on the Direct3D renderer.
void world_set_enabled(MissionWorld& m, bool on) {
  const WorldHooks& h = g_hooks;
  if (m.weather) {
    if (on) h.weather_start(m.weather, -1.0f);
    else h.weather_stop(m.weather);
  }
  if (m.sun) zcls::node_set_active(m.sun->node, on && hardware() ? 1 : 0);
  if (m.fluctuate && h.fluctuate_on) h.fluctuate_on(on ? 1 : 0);
}

namespace {
// Pirate's Moon 1.0 @ 0x004951e0: a read starts from these.
void reset_params(MissionWorld& m) {
  const WorldHooks& h = g_hooks;
  m.fog = 0, m.ai_headlights = 0;
  g_env.jet_ceiling = 150.0f, g_env.ambient_temperature = 20.0f, g_env.inv_temperature = 0.05f;
  m.water_range[0] = 0.0f, m.water_range[1] = 250.0f;
  m.water_altitude[0] = 0.0f, m.water_altitude[1] = 100.0f;
  m.water_color[0] = 50.0f, m.water_color[1] = 50.0f, m.water_color[2] = 80.0f;
  if (h.puffer_tick_reset) h.puffer_tick_reset();
  m.cloud_on = 0, m.fluctuate = 0;
}

// Pirate's Moon 1.0 @ 0x00495250: one block of world.zrd over what is there.
void read_block(MissionWorld& m, zrdr::Node* params) {
  if (!params) return;
  const WorldHooks& h = g_hooks;
  const auto* cam_data = h.camera ? static_cast<const zcls::Camera*>(h.camera->data) : nullptr;
  float clip[2] = {cam_data ? cam_data->get<float>(0xb0) : 0.0f, cam_data ? cam_data->get<float>(0xb4) : 0.0f};  // 0x00506f80
  if (zrdr::Node* fog = zrdr::find(params, "fog")) {
    m.sky = nullptr;
    m.fog = 1;
    zrdr::get_vec2(fog, "range", m.fog_range);
    if (hardware()) zrdr::get_vec2(fog, "hwrange", m.fog_range);
    zrdr::get_vec3(fog, "color", m.fog_color);
    zrdr::get_vec2(fog, "altitude", m.fog_altitude);
    zrdr::get_int(fog, "state", &m.fog_state);
    const char* node = zrdr::get_string(fog, "node");  // 0x0050b920 takes a null name, MW3's not
    m.sky = node ? zcls::list_find_by_name(zcls::kAllNodes, node) : nullptr;
  }
  if (zrdr::Node* water = zrdr::find(params, "underwater")) {
    zrdr::get_vec2(water, "range", m.water_range);
    if (hardware()) zrdr::get_vec2(water, "hwrange", m.water_range);
    zrdr::get_vec3(water, "color", m.water_color);
    zrdr::get_vec2(water, "altitude", m.water_altitude);
  }
  zrdr::get_float(params, "jet_ceiling", &g_env.jet_ceiling);
  bool headlights;
  if (zrdr::get_bool(params, "ai_headlights", &headlights)) m.ai_headlights = headlights;
  if (zrdr::Node* cam = zrdr::find(params, "camera")) zrdr::get_vec2(cam, "nearfar_clip", clip);
  if (zrdr::Node* wind = zrdr::find(params, "global_wind")) {
    float v;
    if (zrdr::get_float(wind, "max_speed", &v) && h.wind_max_speed) h.wind_max_speed(v);
    if (zrdr::get_float(wind, "random_accel", &v) && h.wind_random_accel) h.wind_random_accel(v);
    if (zrdr::get_float(wind, "random_angvel", &v) && h.wind_random_angvel) h.wind_random_angvel(static_cast<float>(v * kDegToRad));
    float vel[3];
    if (zrdr::get_vec3(wind, "velocity", vel) && h.wind_velocity) h.wind_velocity(vel);
  }
  if (zrdr::Node* sun = m.sun ? zrdr::find(params, "sun") : nullptr) {
    Node* light = m.sun->node;
    float v[3], k = 0.0f;
    if (zrdr::get_vec3(sun, "orientation", v)) {
      pm::zcls::light_set_orientation(light, static_cast<float>(v[0] * kDegToRad), static_cast<float>(v[1] * kDegToRad),
                                      static_cast<float>(v[2] * kDegToRad));
    }
    if (zrdr::get_float(sun, "diffuse", &k)) pm::zcls::light_set_diffuse(light, k);
    if (zrdr::get_float(sun, "ambient", &k)) pm::zcls::light_set_ambient(light, k);
    bool diffuse = false;
    if (zrdr::get_vec3(sun, "diffuse_color", v) || zrdr::get_vec3(sun, "color", v)) {
      pm::zcls::light_set_diffuse_color(light, v[0], v[1], v[2]);
      diffuse = true;
    }
    if (zrdr::get_vec3(sun, "ambient_color", v)) {
      pm::zcls::light_set_ambient_color(light, v[0], v[1], v[2]);
      if (diffuse) pm::zcls::light_set_own_colors(light, 1);
    }
  }
  if (zrdr::Node* cloud = zrdr::find(params, "cloud_cover")) {
    float depth = 0.0f, none = 0.0f;
    m.cloud_on = 1;
    zrdr::get_float(cloud, "top", &m.cloud_top);
    zrdr::get_float(cloud, "bottom", &m.cloud_bottom);
    zrdr::get_float(cloud, "depth", &depth);
    m.cloud_death_on = zrdr::get_vec2(cloud, "death", m.cloud_death) ? 1 : 0;
    zrdr::get_vec3(cloud, "color", m.cloud_color);
    const float limit = (m.cloud_top - m.cloud_bottom) * 0.9f;
    m.cloud_depth = !(depth <= limit) ? limit : (depth < 0.0f ? none : depth);
    m.cloud_packed = zvid::pack_bytes(static_cast<uint8_t>(crt::msvc_ftol(m.cloud_color[0] * 255.0f)),
                                      static_cast<uint8_t>(crt::msvc_ftol(m.cloud_color[1] * 255.0f)),
                                      static_cast<uint8_t>(crt::msvc_ftol(m.cloud_color[2] * 255.0f)));
    const float middle = (m.cloud_top - m.cloud_bottom) * 0.5f + m.cloud_bottom;
    m.cloud_high = middle + m.cloud_depth;
    m.cloud_low = middle - m.cloud_depth;
  }
  float temperature = 0.0f;
  if (zrdr::get_float(params, "ambient_temperature", &temperature)) {
    if (temperature <= 0.0f) temperature = 20.0f;  // the heat code divides by it
    g_env.inv_temperature = 1.0f / temperature;
    g_env.ambient_temperature = temperature;
  }
  if (zrdr::Node* weather = zrdr::find(params, "weather")) {
    int32_t particles = 100;
    if (m.weather) {
      if (h.weather_destroy) h.weather_destroy(m.weather);
      m.weather = nullptr;
    }
    zrdr::get_int(weather, "particles", &particles);
    if (const char* type = zrdr::get_string(weather, "type")) {
      if (std::strcmp(type, "SNOW") == 0 && h.create_snow) m.weather = h.create_snow(particles);
      else if (std::strcmp(type, "RAIN") == 0 && h.create_rain) m.weather = h.create_rain(particles);
    }
    if (m.weather) {
      Weather& w = *m.weather;
      int32_t c[3] = {0, 0, 0};
      if (zrdr::get_ivec3(weather, "color", c)) {
        w.put<uint16_t>(0x44, zvid::pack_bytes(static_cast<uint8_t>(c[0]), static_cast<uint8_t>(c[1]), static_cast<uint8_t>(c[2])));
      }
      if (zrdr::Node* wind = zrdr::find(weather, "wind")) {
        float dir = 0.0f, vel = 0.0f;
        zrdr::get_float(wind, "dir", &dir);
        zrdr::get_float(wind, "vel", &vel);
        w.put(0x68, dir), w.put(0x6c, vel);
      }
      float gravity = 0.0f;
      zrdr::get_float(weather, "gravity", &gravity);
      w.put(0x70, gravity);
      float gradient[2] = {0.0f, 0.0f};
      if (zrdr::get_vec2(weather, "alpha_gradient", gradient)) w.put(0x48, gradient[0]), w.put(0x4c, gradient[1]);
      if (h.weather_add) h.weather_add(m.weather);
    }
  }
  if (h.camera) zcls::camera_set_clip(h.camera, clip[0], clip[1]);
  if (zrdr::Node* fl = zrdr::find(params, "fluctuate")) {
    float v, amplitude;
    m.fluctuate = 1;
    const float period = zrdr::get_float(fl, "period", &v) ? v : 5.0f;
    if (h.fluctuate_period) h.fluctuate_period(period);
    const float wave = zrdr::get_float(fl, "wavelength", &v) ? v : 500.0f;
    if (h.fluctuate_wavelength) h.fluctuate_wavelength(wave);
    const float base = zrdr::get_float(fl, "base", &v) ? v : 0.7f;
    if (!zrdr::get_float(fl, "amplitude", &amplitude)) amplitude = 0.3f;
    if (h.fluctuate_base_amp) h.fluctuate_base_amp(base, amplitude);
    if (amplitude == 0.0f) m.fluctuate = 0;  // flat: the pattern stays off
    const float heading = zrdr::get_float(fl, "heading", &v) ? v : 0.0f;
    if (h.fluctuate_heading) h.fluctuate_heading(static_cast<float>(heading * kDegToRad));
  }
}
}  // namespace

// Pirate's Moon 1.0 @ 0x00495b30 and 0x00495ba0: `defaults`, then the game's block (multiplayer's, in place
// of MW3's mpinfo.zrd) or else `key` over them.
bool world_read_params(MissionWorld& m, const char* file, const char* key) {
  const WorldHooks& h = g_hooks;
  // A second read leaks the first handle, as the original does.
  if (Node* sunlight = zcls::list_find_by_name(zcls::kAllNodes, "sunlight")) m.sun = new NodeHandle{sunlight};
  zrdr::Node* root = zrdr::load(file);
  reset_params(m);
  read_block(m, zrdr::find(root, "defaults"));
  zrdr::Node* block = h.game_block ? h.game_block() : nullptr;
  if (!block && key) block = zrdr::find(root, key);
  read_block(m, block);
  world_set_enabled(m, true);
  if (m.sun && h.hw_card_flag && !h.hw_card_flag()) zcls::node_set_active(m.sun->node, 0);
  if (m.weather) m.weather->camera = h.camera;
  if (m.fog && h.world) {
    zcls::world_set_fog_type(h.world, 1);
    zcls::world_set_fog_range(h.world, m.fog_range[0], m.fog_range[1]);
    zcls::world_set_fog_color(h.world, m.fog_color[0], m.fog_color[1], m.fog_color[2]);
    zcls::world_set_fog_altitude(h.world, m.fog_altitude[0], m.fog_altitude[1]);
    // The named sky is shown and the horizon's other children hidden.
    if (m.sky && h.camera) {
      if (Node* horizon = static_cast<zcls::Camera*>(h.camera->data)->nodes[2]) {
        for (int i = 0; i < horizon->child_count(); ++i) {
          Node* child = horizon->children[i];
          zcls::node_set_active(child, child == m.sky ? 1 : 0);
        }
      }
    }
  } else if (!m.fog && h.world) {
    world_get_fog_range(h.world, &m.fog_range[0], &m.fog_range[1]);
    world_get_fog_color(h.world, &m.fog_color[0], &m.fog_color[1], &m.fog_color[2]);
    world_get_fog_altitude(h.world, &m.fog_altitude[0], &m.fog_altitude[1]);
  }
  zrdr::free_node(root);
  return true;
}

}  // namespace mech3
