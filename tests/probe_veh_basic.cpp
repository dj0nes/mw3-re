// Probes for test_veh_basic.py (docs/vehicles.md): the basic vehicle's entry
// 0x004ac320, constructor 0x004ac530, destructor 0x004ac650 and the HUD
// markers 0x004ac3d0 (mech3/veh_basic_object). Every call out goes to the
// shared callback; the vehicle takes the original's block address for the
// arguments that name it, and its image is laid out as the original's 0xfc
// bytes.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#include "gamez/zgame/unported.h"
#include "mech3/veh_basic_object.h"
#include "mech3/veh_move.h"
#include "mech3/mech_collide.h"
#include "mech3/player_view.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zmodel/gmod_cull.h"
#include "probe_app.h"

namespace {
namespace m3 = mech3;

uint32_t lo(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
m3::Handle hd(uint32_t v) { return reinterpret_cast<m3::Handle>(static_cast<uintptr_t>(v)); }

uint32_t g_addr = 0;  // the original's block for the next vehicle
m3::BasicVehicle* g_v = nullptr;

struct Hooks : m3::VehBasicHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return probe_app::call(key, args);
  }
  uint32_t ref(const void* p) override { return p == g_v ? g_addr : lo(p); }
  void on_new(uint32_t size, const void* p) override {
    (void)size;
    g_v = static_cast<m3::BasicVehicle*>(const_cast<void*>(p));
  }
  // The stand-in keeps the node only (the test's 0x004d6c90 writes +0x04).
  void entity_set_node(m3::Vehicle& v, m3::Handle node) override {
    unported("0x004d6c90", {ref(&v), low(node), 0, 0, 0});
    v.node = node;
  }
  void weapons_free(void* w) override { unported("0x004b01d0", {low(w)}); }
  void cameras_once() override { unported("0x004ace00", {}); }  // the ctor's tests stub it
  void view_attach(m3::BasicVehicle& v, m3::CameraMode* mode) override;
  // 1d: the damage's, the controls and their glow block by the original's
  // addresses, reads through peek32.
  bool controls_destroyed(m3::VehicleControls&) override { return unported("ctl.vt+0x1c", {}) == 1; }
  void controls_died(m3::VehicleControls&) override { unported("ctl.ictl.vt+0xa8", {}); }
  void path_drop_temporary(m3::BasicVehicle& v) override { unported("0x00402510", {ref(&v)}); }  // the test stubs it
  int32_t glow_hit(m3::Block398&, m3::Handle, m3::Handle w, float amount) override;
  void glow_end(m3::Block398&, m3::Handle) override;
  uint32_t weapon_flags(m3::Handle w) override { return probe_app::call("peek32", {low(w) + 0x6c}); }
  const float* node_matrix(m3::Handle node) override {
    static float t[12];
    peekf(unported("0x004fc3f0", {low(node)}), t, 12);
    return t;
  }
  const float* shot_line() override {
    static float t[6];
    peekf(unported("0x00581e30", {}), t, 6);
    return t;
  }
  int32_t game_intensity() override { return static_cast<int32_t>(unported("0x0040cdf0", {})); }
  // The setup's zone (0x0049f710): the hits mirrored from the record the
  // test's 0x004edab0 also leaves at g_hits_addr, their materials' soil
  // copied; the node's flag 8 and zone byte by their calls.
  int32_t ground(float x, float y, float z, zcls::GroundHits* out) override;
  int32_t node_flag8(m3::Handle node) override { return static_cast<int32_t>(unported("0x004eb660", {low(node)})); }
  void node_set_flag8(m3::Handle node, int32_t on) override { unported("0x004eb620", {low(node), on}); }
  void node_set_zone(m3::Handle node, uint8_t zone) override { unported("0x004eb810", {low(node), zone}); }
  // 2c1: the frame time and its inverse read from the original's globals.
  float frame_dt() override { return zgame::from_bits(probe_app::call("peek32", {0x5e8110})); }
  // 2c2: the supports' query by its call (count, top, each probe), the
  // records read back from where the test's stub left them (g_hits_addr,
  // one 0x504 record each); the new position by vt+0x18's call.
  void ground_many(float* probes, int32_t count, float top, zcls::GroundHits* out) override;
  // 2c3: the frame's: the movement and the weapons by their calls, the game
  // time and the player's lock position read back.
  void move(m3::BasicVehicle& v) override { unported("0x004a1240", {ref(&v)}); }
  // Stubbed for the frame's test; test_veh_collide.py calls the port direct.
  void collisions(m3::BasicVehicle& v) override { unported("0x0049ff80", {ref(&v)}); }
  float game_time() override { return zgame::from_bits(probe_app::call("peek32", {0x60991c})); }
  const float* player_lock_position() override {
    static float p[3];
    const uint32_t pilot = probe_app::call("peek32", {0x60861c});
    const uint32_t mech = pilot ? probe_app::call("peek32", {pilot + 0x14}) : 0;
    if (!mech) return nullptr;
    const uint32_t a = unported("0x005a8630", {mech});
    for (int i = 0; i < 3; ++i) p[i] = zgame::from_bits(probe_app::call("peek32", {a + 4u * static_cast<uint32_t>(i)}));
    return p;
  }
  void weapons_frame(void* w, bool fire, float dt) override {
    unported("0x004b0c20", {low(w), fire ? 1 : 0});
    unported("0x004b0c30", {low(w), zgame::bits(dt)});
  }
  // 2c4: the collisions' query by its call (each point), the records read
  // from the test's mirror (g_hits_addr, 0x504 each); the flag and object
  // by their calls; the frame counter read back.
  void segments(float* segs, int32_t points, zcls::SegmentHits* out) override;
  void node_set_intersect(m3::Handle node, int32_t on) override { unported("0x004eb6d0", {low(node), on}); }
  m3::Handle hit_object(m3::Handle node) override { return hd(unported("0x0040eee0", {low(node)})); }
  uint32_t frame() override { return probe_app::call("peek32", {0x72f338}); }
  bool touchable(m3::Handle node) override {
    const uint32_t t = probe_app::call("peek32", {low(node) + 0xbc});
    return t > 1 && probe_app::call("peek32", {t + 0xc}) != 0;
  }
  float touch(m3::Handle node, const float from[3], const m3::CollisionHit&, float damage) override {
    unported("0x005836b0", {low(node), zgame::bits(from[0]), zgame::bits(from[1]), zgame::bits(from[2]), zgame::bits(damage)});
    return 0.0f;
  }
  // 2c3: the frame's node calls by their keys.
  void node_set_active(m3::Handle node, int32_t on) override { unported("0x004eb170", {low(node), on}); }
  void node_set_rotation(m3::Handle node, float x, float y, float z) override {
    unported("0x004fbd80", {low(node), zgame::bits(x), zgame::bits(y), zgame::bits(z)});
  }
  void node_update(m3::Handle node) override { unported("0x004eca00", {low(node)}); }
  m3::Handle node_model(m3::Handle node) override { return hd(unported("0x004eb420", {low(node)})); }
  void set_position(m3::BasicVehicle& v, const float pos[3]) override {
    unported("vt+0x18", {ref(&v), zgame::bits(pos[0]), zgame::bits(pos[1]), zgame::bits(pos[2])});
  }
  float frame_inv_dt() override { return zgame::from_bits(probe_app::call("peek32", {0x5e8114})); }
  int32_t view_camera_id() override {  // [0x005f4c8c]+0x58 +0x04, read as the exe reads it
    const uint32_t view = probe_app::call("peek32", {0x5f4c8c});
    const uint32_t cam = view ? probe_app::call("peek32", {view + 0x58}) : 0;
    return cam ? static_cast<int32_t>(probe_app::call("peek32", {cam + 4})) : 0;
  }
  // The readout's: the controls' slots keyless (as the test's stubs), the
  // node's name and the camera's zones read back, the position by its call.
  int32_t controls_dynamics(m3::VehicleControls&) override { return static_cast<int32_t>(unported("ctl.vt+0x20", {})); }
  bool controls_on_path(m3::VehicleControls&) override { return unported("ctl.ictl.vt+0x70", {}) == 1; }
  int32_t controls_mode(m3::VehicleControls&) override { return static_cast<int32_t>(unported("ctl.vt+0x4", {})); }
  int32_t controls_goal(m3::VehicleControls&) override { return static_cast<int32_t>(unported("ctl.vt+0x8", {})); }
  const char* node_name(m3::Handle node) override { return text(low(node)); }
  const float* entity_position(m3::Vehicle& v) override {
    static float t[3];
    peekf(unported("0x005a85e0", {ref(&v)}), t, 3);
    return t;
  }
  const uint8_t* camera_zones() override {
    static uint8_t z[4];
    const uint32_t view = probe_app::call("peek32", {0x5f4c8c});
    const uint32_t cam = probe_app::call("peek32", {view + 4});
    const uint32_t rec = probe_app::call("peek32", {cam + 0x38}) + 0x1e4;
    for (uint32_t i = 0; i < 4; ++i) z[i] = static_cast<uint8_t>(probe_app::call("peek8", {rec + i}));
    return z;
  }
  static void peekf(uint32_t a, float* out, int n) {
    for (int i = 0; i < n; ++i) {
      const uint32_t b = probe_app::call("peek32", {a + 4u * static_cast<uint32_t>(i)});
      std::memcpy(out + i, &b, 4);
    }
  }
  // 1b: the emulator's strings read back through peek8.
  m3::Handle sound_find(const char* name) override { return hd(unported("0x0056f6f0", {name})); }
  const char* sound_name(m3::Handle sound) override { return text(unported("0x0056f740", {low(sound)})); }
  void voice_unlock(m3::Handle v) override { unported("0x0056f410", {low(v)}); }
  void voice_stop(m3::Handle v) override { unported("0x0056e9f0", {low(v)}); }
  // 3b: the engine sound's claim and release, the place by value.
  int32_t voice_update(m3::Handle v, const float* pos, const float* vel, int32_t doppler) override {
    return static_cast<int32_t>(unported("0x00571f30", {low(v), zgame::bits(pos[0]), zgame::bits(pos[1]),
                                                        zgame::bits(pos[2]), vel ? 1 : 0, doppler}));
  }
  m3::Handle sound_play(m3::Handle s, const float* pos, float volume, const float* vel) override {
    return hd(unported("0x0056e7e0", {low(s), zgame::bits(pos[0]), zgame::bits(pos[1]), zgame::bits(pos[2]),
                                      zgame::bits(volume), vel ? 1 : 0}));
  }
  void voice_lock(m3::Handle v) override { unported("0x0056f3f0", {low(v)}); }
  m3::Handle anim_find(const char* name) override { return hd(unported("0x0050b450", {name})); }
  void report(int32_t level, const char* file, int32_t line, const char* message) override {
    unported("0x0049bfd0", {level, file, line, message});
  }
  zrdr::Node* reader_find(zrdr::Node* node, const char* key) override {
    return reinterpret_cast<zrdr::Node*>(hd(unported("0x005569e0", {low(node), key})));
  }
  int32_t reader_type(zrdr::Node* node) override {
    return static_cast<int32_t>(probe_app::call("peek32", {low(node)}));
  }
  const char* reader_string(zrdr::Node* node, const char* key) override {
    return text(unported("0x005569f0", {low(node), key}));
  }
  bool reader_string_value(zrdr::Node* node, const char* key, const char** out) override {
    const bool ok = unported("0x00557090", {low(node), key}) != 0;
    const uint32_t s = unported("@0x00557090.out", {});
    if (ok) *out = s ? text(s) : nullptr;
    return ok;
  }

  static const char* text(uint32_t a) {
    if (!a) return nullptr;
    static std::map<uint32_t, std::string> seen;
    std::string& t = seen[a];
    t.clear();
    for (uint32_t i = 0; i < 256; ++i) {
      const char c = static_cast<char>(probe_app::call("peek8", {a + i}));
      if (!c) break;
      t.push_back(c);
    }
    return t.c_str();
  }
};
Hooks g_hooks;

void put(uint8_t* b, uint32_t off, uint32_t v) { std::memcpy(b + off, &v, 4); }
void putf(uint8_t* b, uint32_t off, float v) { std::memcpy(b + off, &v, 4); }
void put3(uint8_t* b, uint32_t off, const m3::Vec3& v) { putf(b, off, v.x), putf(b, off + 4, v.y), putf(b, off + 8, v.z); }

}  // namespace

extern "C" {

void probe_vb_hooks(int on) { m3::set_veh_basic_hooks(on ? &g_hooks : nullptr); }
// Another probe's vehicle (probe_veh_fieldbase.cpp) under these hooks, at the original's `addr`.
void probe_vb_bind(void* v, uint32_t addr) { g_v = static_cast<m3::BasicVehicle*>(v), g_addr = addr; }

// 0x004ac320(ctl, type, name) as the original's block `addr` would be; 1 when
// a vehicle was made.
int probe_vb_create(uint32_t addr, uint32_t ctl, const char* type, const char* name) {
  g_addr = addr;
  g_v = nullptr;
  return m3::vehicle_create(hd(ctl), type, name) != nullptr;
}

// The fields the destructor reads, set as the test set the original's.
void probe_vb_set(uint32_t ammo, uint32_t net_collision, uint32_t anim, uint32_t engine) {
  g_v->ammo = hd(ammo);
  g_v->net_collision = reinterpret_cast<m3::NetCollision*>(hd(net_collision));
  g_v->motion.anim = hd(anim);
  g_v->engine.entry = reinterpret_cast<m3::EngineSoundEntry*>(hd(engine));
}

void probe_vb_destroy() {
  m3::vehicle_destroy(*g_v);
  g_v->ammo = nullptr, g_v->net_collision = nullptr;  // fakes: not the port's to free
  delete g_v;
  g_v = nullptr;
}

// The vehicle as the original's 0xfc bytes (the vtables and the tracked list's
// pointers, +0x88..+0x93, left as the test fills them).
void probe_vb_image(uint8_t* b) {
  const m3::BasicVehicle& v = *g_v;
  put(b, 4, lo(v.node)), b[8] = v.active, b[9] = v.x09, put(b, 0xc, lo(v.transform));
  put(b, 0x10, lo(v.target_node)), put(b, 0x14, lo(v.centroid));
  put3(b, 0x18, v.x18), put3(b, 0x24, v.forward), put3(b, 0x30, v.velocity), put3(b, 0x3c, v.vec_3c);
  for (int k = 0; k < 4; ++k) putf(b, 0x48 + 4 * k, v.orientation[k]);
  put3(b, 0x58, v.vec_58), b[0x64] = v.x64;
  putf(b, 0x68, v.height);
  put(b, 0x6c, lo(v.target)), put(b, 0x70, lo(v.ally_target)), put(b, 0x74, lo(v.enemy_target));
  put(b, 0x78, lo(v.saved_target)), put(b, 0x7c, lo(v.own_lance)), put(b, 0x80, lo(v.nearest_lance));
  b[0x84] = v.x84;
  put(b, 0x94, lo(v.own_player)), put(b, 0x98, lo(v.player_contact));
  putf(b, 0x9c, v.sensor_range), putf(b, 0xa0, v.range), putf(b, 0xa4, v.range2);
  b[0xa8] = v.xa8;
  for (int k = 0; k < 3; ++k) put(b, 0xac + 4 * k, 0);  // the contacts: none
  put(b, 0xb8, lo(v.touching));
  b[0xbc] = v.xbc;
  put(b, 0xc0, v.team), put(b, 0xc4, v.allies), put(b, 0xc8, v.enemies), put(b, 0xcc, v.shadow ? 1u : 0u);
  put(b, 0xd0, lo(v.net)), put(b, 0xd4, lo(v.ammo)), put(b, 0xd8, lo(v.net_collision));
  put(b, 0xe0, v.xe0), put(b, 0xe4, lo(v.controls));
  // +0xec, the engine sound: compared as an index (probe_vb_engine_index).
  put(b, 0xf0, lo(v.motion.def)), put(b, 0xf4, lo(v.motion.anim)), putf(b, 0xf8, v.motion.until);
}

// The engine sounds: 0x004ad930 (the entry's index, -1 for null), the
// entries (x00, sound, voice), a voice set, 0x004add10.
int probe_vb_sound_find(const char* name) {
  const m3::EngineSoundEntry* e = m3::engine_sound_find(name);
  return e ? static_cast<int>(e - m3::engine_sounds().entries.data()) : -1;
}
int probe_vb_sound_count() { return static_cast<int>(m3::engine_sounds().entries.size()); }
void probe_vb_sound_entry(int i, uint32_t* out) {
  const m3::EngineSoundEntry& e = m3::engine_sounds().entries[static_cast<size_t>(i)];
  out[0] = lo(e.owner), out[1] = lo(e.sound), out[2] = lo(e.voice);
}
void probe_vb_sound_voice(int i, uint32_t v) { m3::engine_sounds().entries[static_cast<size_t>(i)].voice = hd(v); }
void probe_vb_sounds_clear() { m3::engine_sounds_clear(); }
void probe_vb_sounds_reset() { m3::engine_sounds().entries.clear(); }  // silent, between runs
// 3b: the engine sound's claim 0x004adce0 and release 0x004adcd0 on two
// vehicles' EngineSounds (A and B) sharing one entry; the owner compared as
// 0 none, 1 A, 2 B.
m3::EngineSoundEntry g_entry;
m3::EngineSound g_engines[2];
void probe_veng_set(int32_t owner, uint32_t sound, uint32_t voice, int32_t a_has, int32_t b_has) {
  g_entry.owner = owner == 1 ? &g_engines[0] : owner == 2 ? &g_engines[1] : nullptr;
  g_entry.sound = hd(sound), g_entry.voice = hd(voice);
  g_engines[0].entry = a_has ? &g_entry : nullptr;
  g_engines[1].entry = b_has ? &g_entry : nullptr;
}
int32_t probe_veng_play(int32_t which, const float* pos, const float* vel, int32_t doppler) {
  return g_engines[which].play(pos, vel, static_cast<uint8_t>(doppler));
}
void probe_veng_release(int32_t which) { g_engines[which].release(); }
void probe_veng_image(uint32_t* out) {
  out[0] = g_entry.owner == &g_engines[0] ? 1u : g_entry.owner == &g_engines[1] ? 2u : 0u;
  out[1] = lo(g_entry.sound), out[2] = lo(g_entry.voice);
}
void probe_veng_far(float* out) { std::memcpy(out, m3::kEngineFarPoint, 12); }
int probe_vb_engine_index() {
  const m3::EngineSoundEntry* e = g_v->engine.entry;
  return e ? static_cast<int>(e - m3::engine_sounds().entries.data()) : -1;
}

// 0x0049ec00 on the vehicle made last.
void probe_vb_setup(uint32_t def, const char* name, int32_t kind) {
  m3::vehicle_setup(*g_v, reinterpret_cast<zrdr::Node*>(hd(def)), name, kind);
}

void probe_vb_markers(uint32_t* out) {
  m3::vehicle_markers_create();
  const m3::VehicleMarkers& m = m3::vehicle_markers();
  out[0] = lo(m.ally), out[1] = lo(m.enemy), out[2] = lo(m.other);
}

// The cameras: 0x004ace00 (twice), each mode's +0x04..+0x43 as the
// original's 0x44 bytes, the lookups by id (index in the list, -1 none).
void probe_vb_cameras_reset() { m3::vehicle_cameras_reset(); }
void probe_vb_cameras_make() { m3::vehicle_cameras_make(); }
int probe_vb_camera_count() { return static_cast<int>(m3::vehicle_cameras().modes.size()); }
void probe_vb_camera_image(int i, uint32_t* out) {
  const m3::CameraMode* c = m3::vehicle_cameras().modes[static_cast<size_t>(i)];
  std::memcpy(out, &c->id, 4);
  std::memcpy(out + 1, c->delta_rot, 16);
  std::memcpy(out + 5, c->desired_pos, 12);
  const float f[7] = {c->dist_vary_factor, c->dist, c->dist_factor, c->dist_vary,
                      c->dist_catch_up, c->turn_rate, c->look_rate};
  std::memcpy(out + 8, f, 28);
  std::memcpy(out + 15, &c->look_, 4);
}
static int probe_vb_camera_index(const m3::CameraMode* c) {
  const auto& v = m3::vehicle_cameras().modes;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i] == c) return static_cast<int>(i);
  return -1;
}
int probe_vb_camera_find(int32_t id) { return probe_vb_camera_index(m3::vehicle_camera(id)); }
int probe_vb_camera_current() { return probe_vb_camera_index(m3::vehicle_camera_current()); }
// 0x004acd90: which vehicle and which mode the view was given.
uint32_t g_attach[2];
void probe_vb_view_attach(uint32_t v, uint32_t* out) {
  alignas(16) static unsigned char stand_in[sizeof(m3::BasicVehicle)];
  g_v = reinterpret_cast<m3::BasicVehicle*>(stand_in);
  g_addr = v;
  m3::vehicle_view_attach(*g_v);
  out[0] = g_attach[0], out[1] = g_attach[1];
}

}  // extern "C"

uint32_t g_ctl = 0;  // the original's controls block
uint32_t g_hits_addr = 0;
zmod::Material g_mats[32];
void read_hits(uint32_t addr, zcls::GroundHits* out, zmod::Material* mats) {
  auto p32 = [](uint32_t x) { return probe_app::call("peek32", {x}); };
  out->count = static_cast<uint8_t>(p32(addr));
  for (int i = 0; i < out->count && i < 32; ++i) {
    const uint32_t h = addr + 4 + 0x28u * static_cast<uint32_t>(i);
    zcls::GroundHit& g = out->hit[i];
    for (int k = 0; k < 3; ++k) g.normal[k] = zgame::from_bits(p32(h + 4u * static_cast<uint32_t>(k)));
    g.x = zgame::from_bits(p32(h + 0xc)), g.height = zgame::from_bits(p32(h + 0x10)), g.z = zgame::from_bits(p32(h + 0x14));
    g.zone[0] = p32(h + 0x18), g.zone[1] = p32(h + 0x1c);
    const uint32_t m = p32(h + 0x20);
    mats[i].soil = m ? p32(m + 0x20) : 0;
    g.material = m ? &mats[i] : nullptr;
  }
}
zmod::Material g_many_mats[8][32];
void Hooks::segments(float* segs, int32_t points, zcls::SegmentHits* out) {
  unported("0x004efcd0", {points});
  for (int32_t i = 0; i < points; ++i)
    unported("0x004efcd0.point", {zgame::bits(segs[3 * i]), zgame::bits(segs[3 * i + 1]), zgame::bits(segs[3 * i + 2])});
  static zcls::GroundHits tmp;
  for (int32_t i = 0; i < points / 2; ++i) {
    read_hits(g_hits_addr + 0x504u * static_cast<uint32_t>(i), &tmp, g_many_mats[i % 8]);
    out[i].count = tmp.count;
    for (int k = 0; k < tmp.count; ++k) {
      zcls::SegmentHit& o = out[i].hit[k];
      const zcls::GroundHit& g = tmp.hit[k];
      for (int j = 0; j < 3; ++j) o.normal[j] = g.normal[j];
      o.point[0] = g.x, o.point[1] = g.height, o.point[2] = g.z;
      o.zone[0] = g.zone[0], o.zone[1] = g.zone[1];
      o.material = g.material;
      o.node = static_cast<zcls::Node*>(hd(probe_app::call("peek32", {g_hits_addr + 0x504u * static_cast<uint32_t>(i) + 4 + 0x28u * static_cast<uint32_t>(k) + 0x24})));
    }
  }
}
void Hooks::ground_many(float* probes, int32_t count, float top, zcls::GroundHits* out) {
  unported("0x004ee2f0", {count, zgame::bits(top)});
  for (int32_t i = 0; i < count; ++i)
    unported("0x004ee2f0.probe", {zgame::bits(probes[3 * i]), zgame::bits(probes[3 * i + 1]), zgame::bits(probes[3 * i + 2])});
  for (int32_t i = 0; i < count && i < 8; ++i) read_hits(g_hits_addr + 0x504u * static_cast<uint32_t>(i), &out[i], g_many_mats[i]);
}
int32_t Hooks::ground(float x, float y, float z, zcls::GroundHits* out) {
  const int32_t r = static_cast<int32_t>(unported("0x004edab0", {zgame::bits(x), zgame::bits(y), zgame::bits(z)}));
  read_hits(g_hits_addr, out, g_mats);
  return r;
}
m3::BasicVehicle* g_dv = nullptr;
int32_t Hooks::glow_hit(m3::Block398&, m3::Handle, m3::Handle w, float amount) {
  return static_cast<int32_t>(unported("0x00586a30", {low(w), g_ctl + 0x398, zgame::bits(amount)}));
}
void Hooks::glow_end(m3::Block398&, m3::Handle) { unported("0x00586af0", {g_ctl + 0x398}); }

void Hooks::view_attach(m3::BasicVehicle& v, m3::CameraMode* mode) {
  g_attach[0] = ref(&v);
  g_attach[1] = static_cast<uint32_t>(probe_vb_camera_index(mode));
}

// 1d: a vehicle for the damage (vals: node, active, ammo, engine set,
// frozen, state, health_max, health, glow x08, beam_on, beam, inv_mass,
// destroy_anim, angles x, z, +0x70 x, z), then one call (0 damage, 1 the
// hit callback, 2 the tint on, 3 off), then the same fields back.
extern "C" {
void probe_vb_dmg_setup(uint32_t v, uint32_t ctl, const uint32_t* a) {
  static m3::EngineSoundEntry entry;
  delete g_dv;
  g_dv = new m3::BasicVehicle;
  auto* c = new m3::VehicleControls;  // leaked: the probe's
  g_v = g_dv, g_addr = v, g_ctl = ctl;
  g_dv->controls = c;
  g_dv->node = hd(a[0]);
  g_dv->active = a[1] != 0;
  g_dv->ammo = hd(a[2]);
  g_dv->engine.entry = a[3] ? &entry : nullptr;
  c->frozen = a[4];
  c->state = static_cast<int32_t>(a[5]);
  c->health_max = zgame::from_bits(a[6]);
  c->health = zgame::from_bits(a[7]);
  c->x398.x08 = zgame::from_bits(a[8]);
  c->beam_on = a[9];
  c->first = &c->slots[0];
  c->slots[0].beam = hd(a[10]);
  c->motion.inv_mass = zgame::from_bits(a[11]);
  c->motion.destroy_anim = hd(a[12]);
  c->angles.x = zgame::from_bits(a[13]), c->angles.z = zgame::from_bits(a[14]);
  c->x70.x = zgame::from_bits(a[15]), c->x70.z = zgame::from_bits(a[16]);
}
// The readout's extra state: motion mode, +0x20, angles.y, zone count and ids.
void probe_vb_dbg_setup(int32_t mode, uint32_t x20, uint32_t yaw, uint32_t zones) {
  m3::VehicleControls& c = *g_dv->controls;
  c.motion.mode = static_cast<m3::VehicleMotion::Mode>(mode);
  c.x20 = x20;
  c.angles.y = zgame::from_bits(yaw);
  g_dv->x64 = static_cast<uint8_t>(zones);
  for (int i = 0; i < 3; ++i) g_dv->zones[i] = static_cast<uint8_t>(zones >> (8 * (i + 1)));
}
void probe_vb_dbg(int null) { m3::vehicle_debug_lines(null ? nullptr : g_dv); }
int32_t probe_vb_dmg_call(int mode, uint32_t weapon, uint32_t x, uint32_t amount) {
  switch (mode) {
    case 0: return m3::vehicle_damage(*g_dv, hd(weapon), x, zgame::from_bits(amount));
    case 1: m3::vehicle_hit(*g_dv, hd(weapon), x, zgame::from_bits(amount)); return 0;
    default: m3::vehicle_view_tint(*g_dv, static_cast<uint8_t>(x)); return 0;
  }
}
void probe_vb_dmg_image(uint32_t* a) {
  const m3::VehicleControls& c = *g_dv->controls;
  auto b = [](float f) { return zgame::bits(f); };
  a[0] = lo(g_dv->node), a[1] = g_dv->active, a[2] = lo(g_dv->ammo), a[3] = g_dv->engine.entry != nullptr;
  a[4] = c.frozen, a[5] = static_cast<uint32_t>(c.state), a[6] = b(c.health_max), a[7] = b(c.health);
  a[8] = b(c.x398.x08), a[9] = c.beam_on, a[10] = lo(c.slots[0].beam), a[11] = b(c.motion.inv_mass);
  a[12] = lo(c.motion.destroy_anim), a[13] = b(c.angles.x), a[14] = b(c.angles.z), a[15] = b(c.x70.x);
  a[16] = b(c.x70.z);
}
}  // extern "C"

// The setup's points and zone (0x0049f660): a vehicle at `v` with base
// controls at `ctl`: `count` points, the motion's collision points.
extern "C" {
void probe_vb_pts_setup(uint32_t v, uint32_t ctl, uint32_t node, int32_t count, const float* coll, uint32_t hits) {
  delete g_dv;
  g_dv = new m3::BasicVehicle;
  auto* c = new m3::VehicleControls;  // leaked: the probe's
  g_v = g_dv, g_addr = v, g_ctl = ctl, g_hits_addr = hits;
  g_dv->controls = c;
  g_dv->node = hd(node);
  c->point_count = count;
  for (int i = 0; i < 12; ++i) c->motion.collision[i] = {coll[3 * i], coll[3 * i + 1], coll[3 * i + 2]};
}
void probe_vb_pts_run() { m3::vehicle_points_place(*g_dv); }
void probe_vb_pts_image(float* pts, uint8_t* zones) {
  const m3::VehicleControls& c = *g_dv->controls;
  for (int i = 0; i < 15; ++i) pts[3 * i] = c.points[i].x, pts[3 * i + 1] = c.points[i].y, pts[3 * i + 2] = c.points[i].z;
  zones[0] = g_dv->x64, zones[1] = g_dv->zones[0], zones[2] = g_dv->zones[1], zones[3] = g_dv->zones[2];
}
float probe_vb_gravity_x1c() { return g_dv->controls->x1c; }
}  // extern "C"

extern "C" void probe_zones_mode(int32_t on) { zmod::zones().mode = on; }  // [0x005ea1d0]

// 2c1: the movement's helpers on a vehicle at `v` with controls at `ctl`.
// f: forward x, z, the matrix (12), throttle, turn, heading x, z, x50.y,
// velocity (3), x70 (3), x314 (3), top speed, top turn, angles y, rates[0],
// turns[0], turns[1], turn_damping, rate_damping (2), friction (2); u:
// sliding, x760.
namespace {
float g_vm_matrix[12];
}
extern "C" {
void probe_vm_setup(uint32_t v, uint32_t ctl, const float* f, const uint32_t* u) {
  delete g_dv;
  g_dv = new m3::BasicVehicle;
  auto* c = new m3::VehicleControls;  // leaked: the probe's
  g_v = g_dv, g_addr = v, g_ctl = ctl;
  g_dv->controls = c;
  g_dv->forward.x = f[0], g_dv->forward.z = f[1];
  for (int i = 0; i < 12; ++i) g_vm_matrix[i] = f[2 + i];
  g_dv->transform = g_vm_matrix;
  int k = 14;
  c->throttle = f[k++], c->turn = f[k++];
  c->heading.x = f[k++], c->heading.z = f[k++];
  c->x50.y = f[k++];
  c->velocity = {f[k], f[k + 1], f[k + 2]}, k += 3;
  c->x70 = {f[k], f[k + 1], f[k + 2]}, k += 3;
  c->x314 = {f[k], f[k + 1], f[k + 2]}, k += 3;
  c->top_speed = f[k++], c->top_turn = f[k++], c->angles.y = f[k++];
  m3::VehicleMotion& m = c->motion;
  m.rates[0] = f[k++], m.turns[0] = f[k++], m.turns[1] = f[k++], m.turn_damping = f[k++];
  m.rate_damping[0] = f[k++], m.rate_damping[1] = f[k++], m.friction[0] = f[k++], m.friction[1] = f[k++];
  c->sliding = u[0], c->x760 = u[1];
}
// 0 turn_to_heading, 1 up_refresh, 2 speed_step, 3 side_force (its answer),
// 4 the base's turn step, 5 the AI's (top_turn x turn).
float probe_vm_call(int which) {
  m3::BasicVehicle& v = *g_dv;
  m3::VehicleControls& c = *v.controls;
  switch (which) {
    case 0: m3::vehicle_turn_to_heading(v); break;
    case 1: m3::vehicle_up_refresh(v); break;
    case 2: m3::vehicle_speed_step(v); break;
    case 3: return m3::vehicle_side_force(v);
    case 4: c.turn_step(c.motion, zgame::from_bits(probe_app::call("peek32", {0x5e8110}))); break;
    case 5: c.x50.y = c.top_turn * c.turn; break;  // AiVehicleControls::turn_step
    default: break;
  }
  return 0.0f;
}
// x50.y, x70 (3), throttle, turn, angles y, up (3); sliding, x760.
void probe_vm_image(float* f, uint32_t* u) {
  const m3::VehicleControls& c = *g_dv->controls;
  f[0] = c.x50.y, f[1] = c.x70.x, f[2] = c.x70.y, f[3] = c.x70.z, f[4] = c.throttle, f[5] = c.turn;
  f[6] = c.angles.y, f[7] = c.up.x, f[8] = c.up.y, f[9] = c.up.z;
  u[0] = c.sliding, u[1] = c.x760;
}
}  // extern "C"

// 2c2: the suspension 0x004a1760 on a vehicle at `v` with controls at `ctl`
// (zones at `hits`, one record per support). f: the matrix (12), forward
// x, z, velocity (3), x70 y, x50 x, z, angles x, y, z, gravity x1c, rates[1],
// turns[1], quicksand, lava, a_damping, supports (4 x 3), support speeds
// (8); u: support flags (8), x20.
namespace {
float g_vs_matrix[12];
}
extern "C" {
void probe_vs_setup(uint32_t v, uint32_t ctl, uint32_t node, uint32_t hits, const float* f, const uint32_t* u) {
  delete g_dv;
  g_dv = new m3::BasicVehicle;
  auto* c = new m3::VehicleControls;  // leaked: the probe's
  g_v = g_dv, g_addr = v, g_ctl = ctl, g_hits_addr = hits;
  g_dv->controls = c;
  g_dv->node = hd(node);
  for (int i = 0; i < 12; ++i) g_vs_matrix[i] = f[i];
  g_dv->transform = g_vs_matrix;
  int k = 12;
  g_dv->forward.x = f[k++], g_dv->forward.z = f[k++];
  c->velocity = {f[k], f[k + 1], f[k + 2]}, k += 3;
  c->x70.y = f[k++];
  c->x50.x = f[k++], c->x50.z = f[k++];
  c->angles = {f[k], f[k + 1], f[k + 2]}, k += 3;
  c->x1c = f[k++];
  m3::VehicleMotion& m = c->motion;
  m.rates[1] = f[k++], m.turns[1] = f[k++], m.quicksand_slowdown = f[k++], m.lava_slowdown = f[k++], m.a_damping = f[k++];
  m.support_count = 4;
  for (int i = 0; i < 4; ++i) m.support[i] = {f[k + 3 * i], f[k + 3 * i + 1], f[k + 3 * i + 2]};
  k += 12;
  for (int i = 0; i < 8; ++i) std::memcpy(&m.support_word[i], &f[k + i], 4);
  for (int i = 0; i < 8; ++i) m.support_word[8 + i] = u[i];
  c->x20 = u[8];
}
int probe_vs_run() { return m3::vehicle_suspension(*g_dv); }
// The matrix (12), x18 (3), velocity y, x70 y, x50 x, z, angles x, z, up (3),
// top speed, top turn, the world supports (4 x 3), support words (16, raw);
// u: x20, the zone record.
void probe_vs_image(float* f, uint32_t* u) {
  const m3::VehicleControls& c = *g_dv->controls;
  const m3::VehicleMotion& m = c.motion;
  int k = 0;
  for (int i = 0; i < 12; ++i) f[k++] = g_vs_matrix[i];
  f[k++] = g_dv->x18.x, f[k++] = g_dv->x18.y, f[k++] = g_dv->x18.z;
  f[k++] = c.velocity.y, f[k++] = c.x70.y, f[k++] = c.x50.x, f[k++] = c.x50.z, f[k++] = c.angles.x, f[k++] = c.angles.z;
  f[k++] = c.up.x, f[k++] = c.up.y, f[k++] = c.up.z, f[k++] = c.top_speed, f[k++] = c.top_turn;
  for (int i = 0; i < 4; ++i) f[k++] = m.support[8 + i].x, f[k++] = m.support[8 + i].y, f[k++] = m.support[8 + i].z;
  for (int i = 0; i < 16; ++i) std::memcpy(&f[k++], &m.support_word[i], 4);
  u[0] = c.x20;
  u[1] = g_dv->x64 | (uint32_t{g_dv->zones[0]} << 8) | (uint32_t{g_dv->zones[1]} << 16) | (uint32_t{g_dv->zones[2]} << 24);
}
}  // extern "C"

// 2c3: the track step's state over probe_vs_setup's. f: throttle, turn,
// heading x, z, x50.y, x70 x, z, x314 (3), rates[0], turns[0], turn_damping,
// rate_damping (2), friction (2), the collision points (12 x 3); u: mode,
// sliding, x760, x24, point count, shadow, rtracks, ltracks.
extern "C" {
void probe_vf_extra(const float* f, const uint32_t* u) {
  m3::VehicleControls& c = *g_dv->controls;
  m3::VehicleMotion& m = c.motion;
  int k = 0;
  c.throttle = f[k++], c.turn = f[k++], c.heading.x = f[k++], c.heading.z = f[k++], c.x50.y = f[k++];
  c.x70.x = f[k++], c.x70.z = f[k++];
  c.x314 = {f[k], f[k + 1], f[k + 2]}, k += 3;
  m.rates[0] = f[k++], m.turns[0] = f[k++], m.turn_damping = f[k++], m.rate_damping[0] = f[k++];
  m.rate_damping[1] = f[k++], m.friction[0] = f[k++], m.friction[1] = f[k++];
  for (int i = 0; i < 12; ++i) m.collision[i] = {f[k + 3 * i], f[k + 3 * i + 1], f[k + 3 * i + 2]};
  m.mode = static_cast<m3::VehicleMotion::Mode>(u[0]);
  c.sliding = u[1], c.x760 = u[2], c.x24 = u[3], c.point_count = static_cast<int32_t>(u[4]);
  c.shadow = hd(u[5]), m.rtracks = hd(u[6]), m.ltracks = hd(u[7]);
}
void probe_vf_more(uint32_t frozen, float yaw) {
  g_dv->controls->frozen = frozen, g_dv->controls->angles.y = yaw;
  for (float& q : g_dv->orientation) q = 0.0f;  // the test's block starts zeroed
}
// BASIC (editor.md S2): mode_alt, then its step 0x004a1d90 (0), steering
// 0x004a20d0 (1), panning 0x004a2000 (2), ground 0x004a1f00 (3).
void probe_vb_basic_alt(float alt) { g_dv->controls->motion.mode_alt = alt; }
void probe_vb_basic_run(int which) {
  switch (which) {
    case 0: m3::vehicle_basic_step(*g_dv); break;
    case 1: m3::vehicle_basic_steer(*g_dv); break;
    case 2: m3::vehicle_basic_pan(*g_dv); break;
    default: m3::vehicle_basic_ground(*g_dv); break;
  }
}
void probe_vf_run(int which) {
  if (which == 0) m3::vehicle_track_step(*g_dv);
  else m3::vehicle_move(*g_dv);
}
// yaw, x50.y, velocity x, z, x70 x, z, x7c (3), x314 (3), the points (15 x 3,
// x_ac then +0x160), the last matrix (12), the entity's orientation (4),
// velocity (3), +0x3c (3); u: x24, sliding, x760.
void probe_vf_image(float* f, uint32_t* u) {
  const m3::VehicleControls& c = *g_dv->controls;
  int k = 0;
  f[k++] = c.angles0.x, f[k++] = c.angles0.y, f[k++] = c.angles0.z;
  f[k++] = c.angles.y, f[k++] = c.x50.y, f[k++] = c.velocity.x, f[k++] = c.velocity.z, f[k++] = c.x70.x, f[k++] = c.x70.z;
  f[k++] = c.x7c.x, f[k++] = c.x7c.y, f[k++] = c.x7c.z, f[k++] = c.x314.x, f[k++] = c.x314.y, f[k++] = c.x314.z;
  for (int i = 0; i < 15; ++i) f[k++] = c.x_ac[i].x, f[k++] = c.x_ac[i].y, f[k++] = c.x_ac[i].z;
  for (int i = 0; i < 15; ++i) f[k++] = c.points[i].x, f[k++] = c.points[i].y, f[k++] = c.points[i].z;
  for (int i = 0; i < 12; ++i) f[k++] = c.last_matrix[i];
  for (int i = 0; i < 4; ++i) f[k++] = g_dv->orientation[i];
  f[k++] = g_dv->velocity.x, f[k++] = g_dv->velocity.y, f[k++] = g_dv->velocity.z;
  f[k++] = g_dv->vec_3c.x, f[k++] = g_dv->vec_3c.y, f[k++] = g_dv->vec_3c.z;
  u[0] = c.x24, u[1] = c.sliding, u[2] = c.x760;
}
}  // extern "C"

// 2c3: the frame 0x004a1040 with controls whose frame, destroyed and on a
// path answer by their calls; the throttle, the motion animation's state,
// fire, the weapons.
namespace {
struct FrameControls : m3::VehicleControls {
  int update(float dt) override { return static_cast<int>(probe_app::call("ctl.ictl.vt+0xc", {zgame::bits(dt)})); }
  bool destroyed() override { return probe_app::call("ctl.vt+0x1c", {}) != 0; }
  bool on_path() override { return probe_app::call("ctl.ictl.vt+0x70", {}) != 0; }
};
}  // namespace
extern "C" {
void probe_vfr_setup(uint32_t v, uint32_t ctl, uint32_t node, float throttle, uint32_t fire, uint32_t ammo,
                     uint32_t def, uint32_t anim, float until, uint32_t entry) {
  static m3::EngineSoundEntry e;
  delete g_dv;
  g_dv = new m3::BasicVehicle;
  auto* c = new FrameControls;  // leaked: the probe's
  g_v = g_dv, g_addr = v, g_ctl = ctl;
  g_dv->controls = c;
  g_dv->node = hd(node);
  g_dv->ammo = hd(ammo);
  g_dv->engine.entry = entry ? &e : nullptr;
  g_dv->motion.def = hd(def), g_dv->motion.anim = hd(anim), g_dv->motion.until = until;
  c->throttle = throttle, c->fire_on = fire;
}
void probe_vfr_run(float dt) { m3::vehicle_frame(*g_dv, dt); }
// active, engine set, anim, until (bits), player_at (3, bits).
void probe_vfr_image(uint32_t* u) {
  const m3::VehicleControls& c = *g_dv->controls;
  u[0] = g_dv->active, u[1] = g_dv->engine.entry != nullptr, u[2] = lo(g_dv->motion.anim);
  u[3] = zgame::bits(g_dv->motion.until);
  u[4] = zgame::bits(c.player_at.x), u[5] = zgame::bits(c.player_at.y), u[6] = zgame::bits(c.player_at.z);
}
}  // extern "C"

// 2c4: the collisions 0x0049ff80 on a vehicle at `v` with controls at `ctl`
// (the records at `hits`). f: the matrix (12), the last one (12), x70 (3),
// x50.y, velocity y, the points now (+0xac, 15 x 3) and last (+0x160, 15 x
// 3), the collision points (12 x 3), support speeds (4); u: point count,
// support count, x2c.
extern "C" {
void probe_vco_setup(uint32_t v, uint32_t ctl, uint32_t node, uint32_t hits, const uint32_t* net, const float* f, const uint32_t* u) {
  static float matrix[12];
  delete g_dv;
  g_dv = new m3::BasicVehicle;
  auto* c = new m3::VehicleControls;  // leaked: the probe's
  g_v = g_dv, g_addr = v, g_ctl = ctl, g_hits_addr = hits;
  g_dv->controls = c;
  g_dv->node = hd(node);
  for (int i = 0; i < 12; ++i) matrix[i] = f[i];
  g_dv->transform = matrix;
  int k = 12;
  for (int i = 0; i < 12; ++i) c->last_matrix[i] = f[k++];
  c->x70 = {f[k], f[k + 1], f[k + 2]}, k += 3;
  c->x50.y = f[k++], c->velocity.y = f[k++];
  for (int i = 0; i < 15; ++i) c->x_ac[i] = {f[k + 3 * i], f[k + 3 * i + 1], f[k + 3 * i + 2]};
  k += 45;
  for (int i = 0; i < 15; ++i) c->points[i] = {f[k + 3 * i], f[k + 3 * i + 1], f[k + 3 * i + 2]};
  k += 45;
  for (int i = 0; i < 12; ++i) c->motion.collision[i] = {f[k + 3 * i], f[k + 3 * i + 1], f[k + 3 * i + 2]};
  k += 36;
  for (int i = 0; i < 4; ++i) std::memcpy(&c->motion.support_word[i], &f[k + i], 4);
  k += 4;
  c->motion.collision_d[0] = f[k++], c->motion.collision_d[1] = f[k++];
  c->angles.y = f[k++];
  c->point_count = static_cast<int32_t>(u[0]);
  c->motion.support_count = static_cast<int32_t>(u[1]);
  c->x2c = u[2];
  static m3::NetCollision nc;
  // net: present, frame, sent, the +0x08 word (on, active at +0x0b), normal, point (bits).
  if (net[0]) {
    nc.frame = net[1], nc.sent = net[2];
    nc.on = static_cast<uint8_t>(net[3]), nc.active = static_cast<uint8_t>(net[3] >> 24);
    nc.normal = {zgame::from_bits(net[4]), zgame::from_bits(net[5]), zgame::from_bits(net[6])};
    nc.point = {zgame::from_bits(net[7]), zgame::from_bits(net[8]), zgame::from_bits(net[9])};
    g_dv->net_collision = &nc;
  }
}
void probe_vco_run() { m3::vehicle_collisions(*g_dv); }  // direct: the hook's stub is for the frame's test
// velocity y, the points now (15 x 3), support speeds (4); u: clear, the
// three flags, x2c.
void probe_vco_image(float* f, uint32_t* u) {
  const m3::VehicleControls& c = *g_dv->controls;
  int k = 0;
  f[k++] = c.velocity.y;
  for (int i = 0; i < 15; ++i) f[k++] = c.x_ac[i].x, f[k++] = c.x_ac[i].y, f[k++] = c.x_ac[i].z;
  for (int i = 0; i < 4; ++i) std::memcpy(&f[k++], &c.motion.support_word[i], 4);
  f[k++] = c.velocity.x, f[k++] = c.velocity.z, f[k++] = c.x70.x, f[k++] = c.x70.y, f[k++] = c.x70.z, f[k++] = c.x50.y;
  u[0] = static_cast<uint32_t>(c.clear), u[1] = static_cast<uint32_t>(c.hit_object), u[2] = static_cast<uint32_t>(c.hit_ceiling);
  u[3] = static_cast<uint32_t>(c.hit_wall), u[4] = c.x2c;
}
}  // extern "C"

