// Probes for test_view.py (docs/view.md): the player's view, its camera modes
// and their set-up (mech3/player_view) with every call outside recorded. The
// test gives each object the emulator's address, so the records match.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "gamez/zgame/unported.h"
#include "mech3/mech.h"
#include "mech3/mech_parts.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zmodel/gmod_matl.h"
#include "mech3/player_view.h"
#include "probe_app.h"

namespace {
using probe_app::call;
using zgame::Arg;

// The world as the test gives it: values by the emulator's addresses.
struct World {
  uint32_t mech = 0, torso = 0, torso_node = 0, centroid = 0, controls = 0, pilot = 0, camera = 0;
  uint32_t world_node = 0, camera_parent = 0, horizon = 0;
  uint32_t centroid_flags = 0;
  float centroid_pos[3] = {};
  float torso_node_pos[3] = {};
  float camera_world_pos[3] = {};
  float camera_local_pos[3] = {};
  float aim[4] = {1, 0, 0, 0};
  float entity_pos[3] = {};
  float fog[2][7] = {};
  uint32_t controls_flags = 0;
  int32_t controls_kind = 0;
  uint32_t option = 0;
  uint8_t head_bob = 1;
  float head_bob_scale = 0.15f;
  uint8_t water_flag = 1;
  uint32_t weather = 0, weather_flags = 0;
  uint8_t game_8e = 0;
  float fov = 0;
  int32_t has_view = 1;
};
World g_w;
mech3::Mech* g_mech = nullptr;
mech3::Torso* g_torso = nullptr;
mech3::PlayerView* g_view = nullptr;
mech3::CameraList* g_cams = nullptr;
std::map<uint32_t, mech3::CameraMode*> g_mode_at;  // emulator address -> port object
std::map<const mech3::CameraMode*, uint32_t> g_addr_of;

void* P(uint32_t a) { return reinterpret_cast<void*>(static_cast<uintptr_t>(a)); }
uint32_t A(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t fb(float f) { return zgame::bits(f); }
float bf(uint32_t u) { return zgame::from_bits(u); }

struct Recording : mech3::ViewHooks {
  uint32_t unported(const char* key, std::initializer_list<Arg> args) override { return call(key, args); }
  uint32_t id(const void* p) override {
    if (p == g_mech) return g_w.mech;
    if (p == g_torso) return g_w.torso;
    return A(p);
  }
  mech3::Node3 find_node(int32_t list, const char* name) override { return P(call("0x004f7fe0", {list, name})); }
  uint32_t node_flags(mech3::Node3) override { return g_w.centroid_flags; }
  const float* node_position(mech3::Node3) override { return g_w.centroid_pos; }
  void node_refresh(mech3::Node3 node) override {
    call("0x0051e9d0", {});
    call("0x0051fde0", {});
    call("0x004ed280", {A(node), 1});
    call("0x0051ea00", {});
  }
  void node_world_position(mech3::Node3 node, float out[3]) override {
    call("0x004ed530", {A(node)});
    std::memcpy(out, A(node) == g_w.camera ? g_w.camera_world_pos : g_w.torso_node_pos, 12);
  }
  mech3::Node3 node_parent(mech3::Node3) override { return P(g_w.camera_parent); }
  mech3::Node3 world_node() override { return P(g_w.world_node); }
  int32_t segment_query(const float a[3], const float b[3], uint8_t* found) override {
    const uint32_t r = call("0x004eed90", {fb(a[0]), fb(a[1]), fb(a[2]), fb(b[0]), fb(b[1]), fb(b[2])});
    *found = static_cast<uint8_t>(call("@found", {}));
    return static_cast<int32_t>(r);
  }
  const float* camera_position(mech3::Node3) override { return g_w.camera_local_pos; }
  bool ground_query(float x, float y, float z) override { return call("0x004edab0", {fb(x), fb(y), fb(z)}) != 0; }
  float water_query(float y, float height, int32_t* index, uint8_t flag, int32_t* kind, float* level) override {
    const float g = bf(call("0x004abd90", {fb(y), fb(height), flag}));
    *index = static_cast<int32_t>(call("@index", {}));
    *kind = static_cast<int32_t>(call("@kind", {}));
    *level = bf(call("@level", {}));
    return g;
  }
  uint8_t water_flag() override { return g_w.water_flag; }
  void camera_zone(mech3::Node3 cam, int32_t index, mech3::Vehicle* t) override {
    call("0x004f6c80", {A(cam), index});
    call("0x005a80d0", {t ? id(t) : 0u});
    call("0x00502d70", {});
  }
  void world_fog_range(float a, float b) override { call("0x004f2440", {g_w.world_node, fb(a), fb(b)}); }
  void world_fog_color(float r, float g, float b) override {
    call("0x004f23f0", {g_w.world_node, fb(r), fb(g), fb(b)});
  }
  void world_fog_altitude(float a, float b) override { call("0x004f2420", {g_w.world_node, fb(a), fb(b)}); }
  bool has_weather() override { return g_w.weather != 0; }
  uint32_t weather_flags() override { return g_w.weather_flags; }
  void world_segments(float*, int32_t, zcls::SegmentHits* out) override {  // not reached: pull_in stands in
    out[0].count = out[1].count = 0;
  }
  void pull_in(float pos[3], const float at[3], const float back[3]) override {
    call("0x0040b310", {fb(pos[0]), fb(pos[1]), fb(pos[2]), fb(at[0]), fb(at[1]), fb(at[2]), fb(back[0]), fb(back[1]),
                        fb(back[2])});
  }
  const float* entity_position(mech3::Vehicle&) override {
    call("0x005a85e0", {g_w.mech});
    return g_w.entity_pos;
  }
  mech3::CameraMode* target_camera(mech3::Vehicle&, int32_t i) override {
    return g_mode_at[call("mech.vt+0x10c", {g_w.mech, i})];
  }
  void target_attached(mech3::Vehicle& t) override { unported("mech.vt+0x100", {v(t)}); }
  void target_detached(mech3::Vehicle& t) override { unported("mech.vt+0x104", {v(t)}); }
  const float* torso_aim(mech3::Mech&) override {
    call("0x0041ce00", {g_w.mech});
    return g_w.aim;
  }
  void torso_464ac0(mech3::Mech&, int32_t on) override { call("0x00464ac0", {g_w.torso, on}); }
  void torso_465a00(mech3::Mech&, int32_t on) override { call("0x00465a00", {g_w.torso, on}); }
  void torso_466190(mech3::Mech&, int32_t on) override { call("0x00466190", {g_w.torso, on}); }
  float torso_pitch_rate(mech3::Mech&) override { return bf(call("0x00465900", {g_w.torso})); }
  float torso_yaw_rate(mech3::Mech&) override { return bf(call("0x004658c0", {g_w.torso})); }
  uint32_t controls_flags(mech3::Mech&) override { return g_w.controls_flags; }
  int32_t controls_kind(mech3::Mech&) override { return g_w.controls_kind; }
  void controls_426780(mech3::Mech&, uint32_t arg) override { call("0x00426780", {g_w.controls, arg}); }
  uint32_t option_5f4d44() override { return g_w.option; }
  uint8_t head_bob() override { return g_w.head_bob; }
  float head_bob_scale() override { return g_w.head_bob_scale; }
  uint32_t mech_screen(mech3::Mech&) override { return call("0x00430aa0", {g_w.pilot}); }
  mech3::Vehicle* player_target() override { return g_view ? g_view->target : nullptr; }
  uint8_t game_byte_8e(uint32_t) override { return g_w.game_8e; }
  const float* mission_fog(bool water) override { return g_w.fog[water ? 1 : 0]; }
  void* dfn_reader(int32_t roster) override {
    call("0x004846a0", {roster});
    return P(0x2b000100);  // the test's definition keeps its reader there
  }
  void* reader_find(void* node, const char* key) override { return P(call("0x005569e0", {A(node), key})); }
  bool reader_float(void* node, const char* key, float* out) override {
    const uint32_t r = call("0x00556a60", {A(node), key});
    if (r) *out = bf(call("@float", {}));
    return r != 0;
  }
  bool reader_vec3(void* node, const char* key, float out[3]) override {
    const uint32_t r = call("0x005570b0", {A(node), key});
    if (r)
      for (int i = 0; i < 3; ++i) out[i] = bf(call("@float", {}));
    return r != 0;
  }
  bool view_fov(float* fov) override {
    *fov = g_w.fov;
    return g_w.has_view != 0;
  }
};
Recording g_hooks;

}  // namespace

extern "C" {

// The world's addresses and read values: words (u32) then floats, as listed.
void probe_view_world(const uint32_t* u, const float* f) {
  mech3::set_view_hooks(&g_hooks);
  World& w = g_w;
  int i = 0;
  for (uint32_t* p : {&w.mech, &w.torso, &w.torso_node, &w.centroid, &w.controls, &w.pilot, &w.camera, &w.world_node,
                      &w.camera_parent, &w.horizon, &w.centroid_flags, &w.controls_flags})
    *p = u[i++];
  w.controls_kind = static_cast<int32_t>(u[i++]);
  w.option = u[i++];
  w.head_bob = static_cast<uint8_t>(u[i++]);
  w.water_flag = static_cast<uint8_t>(u[i++]);
  w.weather = u[i++];
  w.weather_flags = u[i++];
  w.game_8e = static_cast<uint8_t>(u[i++]);
  w.has_view = static_cast<int32_t>(u[i++]);
  int k = 0;
  for (float* a : {w.centroid_pos, w.torso_node_pos, w.camera_world_pos, w.camera_local_pos, w.entity_pos})
    for (int j = 0; j < 3; ++j) a[j] = f[k++];
  for (int j = 0; j < 4; ++j) w.aim[j] = f[k++];
  for (int a = 0; a < 2; ++a)
    for (int j = 0; j < 7; ++j) w.fog[a][j] = f[k++];
  w.head_bob_scale = f[k++];
  w.fov = f[k++];
}

// The mech's fields: active, anim_state, velocity 3, vec_3c 3, orientation
// 4, vec_58 3, height, torso view_aim, view_q 4.
void probe_view_mech(const float* f, int32_t active, int32_t anim_state, int32_t view_aim) {
  if (!g_mech) {
    g_mech = new mech3::Mech;
    g_torso = new mech3::Torso;
  }
  mech3::Mech& m = *g_mech;
  m.active = active != 0;
  m.anim_state = anim_state;
  int k = 0;
  for (float* p : {&m.velocity.x, &m.velocity.y, &m.velocity.z, &m.vec_3c.x, &m.vec_3c.y, &m.vec_3c.z}) *p = f[k++];
  for (float& q : m.orientation) q = f[k++];
  for (float* p : {&m.vec_58.x, &m.vec_58.y, &m.vec_58.z}) *p = f[k++];
  m.height = f[k++];
  m.torso = g_torso;
  m.centroid = P(g_w.centroid);
  m.node = P(g_w.mech + 0x1000);  // the mech's node, named apart
  g_torso->node = P(g_w.torso_node);
  g_torso->view_aim = view_aim != 0;
  for (float& q : g_torso->view_q) q = f[k++];
}

// A camera mode at `addr`: kind 0 base, 1 mech, 2 cockpit, 3 area; its
// fields as the 0x78 bytes (+0x04..+0x77) the emulator holds.
void probe_view_mode(uint32_t addr, int32_t kind, int32_t id, const uint32_t* raw) {
  mech3::CameraMode* m;
  switch (kind) {
    case 0: m = new mech3::CameraMode(id); break;
    case 1: m = new mech3::MechCamera(id); break;
    case 2: m = new mech3::CockpitCamera; break;
    default: m = new mech3::AreaCamera; break;
  }
  m->id = id;
  std::memcpy(m->delta_rot, raw + 1, 0x3c);  // +0x08..+0x43
  if (auto* mc = dynamic_cast<mech3::MechCamera*>(m)) {
    std::memcpy(mc->look_q, raw + 0x10, 16);
    mc->torso_look = static_cast<uint8_t>(raw[0x14]);
    std::memcpy(&mc->dist_now, raw + 0x15, 8);
    mc->view = g_view;
    mc->fixed = static_cast<uint8_t>(raw[0x18]);
    std::memcpy(&mc->look_left, raw + 0x19, 16);
  }
  g_mode_at[addr] = m;
  g_addr_of[m] = addr;
}
}  // extern "C"
namespace {
void mode_raw(const mech3::CameraMode* m, uint32_t* raw) {
  std::memset(raw, 0, 30 * 4);
  if (!m) return;
  raw[0] = static_cast<uint32_t>(m->id);
  std::memcpy(raw + 1, m->delta_rot, 0x3c);
  if (auto* mc = dynamic_cast<const mech3::MechCamera*>(m)) {
    std::memcpy(raw + 0x10, mc->look_q, 16);
    raw[0x14] = mc->torso_look;
    std::memcpy(raw + 0x15, &mc->dist_now, 8);
    raw[0x17] = mc->view ? 1u : 0u;
    raw[0x18] = mc->fixed;
    std::memcpy(raw + 0x19, &mc->look_left, 16);
  }
}
}  // namespace
extern "C" {
void probe_view_mode_get(uint32_t addr, uint32_t* raw) { mode_raw(g_mode_at[addr], raw); }

// The view at the emulator's address `addr` (camera, window from the world).
void probe_view_new(int32_t hardware) {
  for (auto& [a, m] : g_mode_at) delete m;
  g_mode_at.clear();
  g_addr_of.clear();
  delete g_view;
  probe_app::Mute quiet;  // set-up: the constructor's horizon calls are not compared
  g_view = new mech3::PlayerView(P(g_w.camera), nullptr, nullptr, hardware);
  delete g_cams;
  g_cams = nullptr;
}
// The view's fields +0x18..+0x78 (as the emulator's: mode and target by address).
void probe_view_set(const uint32_t* raw) {
  mech3::PlayerView& v = *g_view;
  std::memcpy(&v.speed, raw + 6, 4);
  std::memcpy(v.rot_q, raw + 7, 16);
  std::memcpy(v.look_q, raw + 11, 16);
  std::memcpy(v.final_q, raw + 15, 16);
  std::memcpy(v.pos, raw + 19, 12);
  v.mode = raw[22] ? g_mode_at[raw[22]] : nullptr;
  v.last_mode = static_cast<int32_t>(raw[23]);
  std::memcpy(&v.dist, raw + 24, 4);
  v.target = raw[25] ? g_mech : nullptr;
  v.handled = static_cast<uint8_t>(raw[26]);
  v.x69 = static_cast<uint8_t>(raw[26] >> 8);
  v.underwater = static_cast<uint8_t>(raw[26] >> 16);
  std::memcpy(&v.dt, raw + 30, 4);
  for (auto& [a, m] : g_mode_at)
    if (auto* mc = dynamic_cast<mech3::MechCamera*>(m)) mc->view = g_view;
}
void probe_view_get(uint32_t* raw) {
  const mech3::PlayerView& v = *g_view;
  std::memset(raw, 0, 31 * 4);
  std::memcpy(raw + 6, &v.speed, 4);
  std::memcpy(raw + 7, v.rot_q, 16);
  std::memcpy(raw + 11, v.look_q, 16);
  std::memcpy(raw + 15, v.final_q, 16);
  std::memcpy(raw + 19, v.pos, 12);
  raw[22] = v.mode ? g_addr_of[v.mode] : 0;
  raw[23] = static_cast<uint32_t>(v.last_mode);
  std::memcpy(raw + 24, &v.dist, 4);
  raw[25] = v.target ? g_w.mech : 0;
  raw[26] = v.handled | v.x69 << 8 | v.underwater << 16;
  std::memcpy(raw + 30, &v.dt, 4);
}

// One call by the original's address: 0x0040ada0 update(dt), 0x0040adf0
// attach(arg = mode address, arg2 = has target), 0x0040ae70 mode(arg),
// 0x0040ad90 reset, 0x0040aed0 cycle, 0x00421c00 / 0x004220c0 set_look(arg)
// on the view's mode, 0x00430e70 the cameras (arg roster).
int32_t probe_view_call(uint32_t addr, uint32_t arg, uint32_t arg2) {
  mech3::PlayerView& v = *g_view;
  switch (addr) {
    case 0x40ada0: v.update(bf(arg)); break;
    case 0x40adf0: v.attach(arg2 ? g_mech : nullptr, arg ? g_mode_at[arg] : nullptr); break;
    case 0x40ae70: v.set_camera_mode(static_cast<int32_t>(arg)); break;
    case 0x40ad90: v.reset(); break;
    case 0x40aed0: v.cycle_camera(); break;
    case 0x421c00:
    case 0x4220c0:
      if (v.mode) v.mode->set_look(static_cast<int32_t>(arg));
      break;
    case 0x430e70: {
      delete g_cams;
      g_cams = mech3::make_cameras(static_cast<int32_t>(arg));
      break;
    }
    default: return 1;
  }
  return 0;
}
// The made cameras: per mode its kind (0..3), id and raw fields.
int32_t probe_view_cameras(int32_t* kinds, uint32_t* raws) {
  if (!g_cams) return 0;
  int32_t n = 0;
  for (mech3::CameraMode* m : g_cams->modes) {
    kinds[n] = dynamic_cast<mech3::AreaCamera*>(m) ? 3 : dynamic_cast<mech3::CockpitCamera*>(m) ? 2
             : dynamic_cast<mech3::MechCamera*>(m)    ? 1
                                                      : 0;
    mode_raw(m, raws + n * 30);
    ++n;
  }
  return n;
}

}  // extern "C"

// The wall pull-in (0x0040b310) for test_pull_in.py: the port on a target
// whose node is 0x4444, the two segment queries answered from `script` (per
// query and record: a count, then four hits of x, y, z bits, has a node, the
// material: 0 none, 1 soil 0, 2 soil 1) and logged with the node switches.
namespace {
struct PullIn : Recording {
  const int32_t* script = nullptr;
  int32_t query = 0;
  std::vector<int32_t>* log = nullptr;
  zmod::Material soil0{}, soil1{};
  void node_set_intersect(mech3::Node3 n, int32_t on) override {
    log->insert(log->end(), {1, static_cast<int32_t>(A(n)), on});
  }
  void world_segments(float* segs, int32_t points, zcls::SegmentHits* out) override {
    log->insert(log->end(), {2, points});
    for (int i = 0; i < 12; ++i) log->push_back(static_cast<int32_t>(fb(segs[i])));
    soil0.soil = 0, soil1.soil = 1;
    for (int r = 0; r < 2; ++r) {
      const int32_t* s = script + (query * 2 + r) * 21;
      out[r].count = static_cast<uint8_t>(s[0]);
      for (int k = 0; k < s[0]; ++k) {
        const int32_t* e = s + 1 + 5 * k;
        zcls::SegmentHit& h = out[r].hit[k];
        h = zcls::SegmentHit{};
        for (int c = 0; c < 3; ++c) std::memcpy(&h.point[c], &e[c], 4);
        h.node = e[3] ? reinterpret_cast<zcls::Node*>(uintptr_t{0x5000} + k) : nullptr;
        h.material = e[4] == 1 ? &soil0 : e[4] == 2 ? &soil1 : nullptr;
      }
    }
    ++query;
  }
};
}  // namespace
extern "C" int32_t probe_pull_in(const float* pos_in, const float* at, const float* back, const int32_t* script,
                                 float* pos_out, int32_t* log_out, int32_t cap) {
  std::vector<int32_t> log;
  PullIn h;
  h.script = script, h.log = &log;
  mech3::set_view_hooks(&h);
  mech3::Mech m;
  m.node = reinterpret_cast<void*>(uintptr_t{0x4444});
  float pos[3] = {pos_in[0], pos_in[1], pos_in[2]};
  mech3::pull_in(&m, pos, at, back);
  mech3::set_view_hooks(nullptr);
  std::memcpy(pos_out, pos, 12);
  if (static_cast<int32_t>(log.size()) > cap) return -1;
  std::memcpy(log_out, log.data(), log.size() * 4);
  return static_cast<int32_t>(log.size());
}
