// C entry points over ported functions, loaded by tests/differential/*.py to compare
// them with the original x86 code running in an emulator.
#include <cstdint>
#include <cstdlib>

#include "gamez/zrender/zrndr_span.h"

extern "C" {

void probe_span_fill16(uint16_t* dest, uint32_t color, uint32_t count) {
  zrndr::span_state().dest = reinterpret_cast<uint8_t*>(dest);
  zrndr::span_fill16(color, count);
}

void probe_span_textured16(uint16_t* dest, const uint16_t* texture, uint32_t du, uint32_t dv,
                           uint32_t v_mask, uint32_t u, uint32_t v, int32_t count) {
  zrndr::SpanState& s = zrndr::span_state();
  s.dest = reinterpret_cast<uint8_t*>(dest);
  s.texture = texture;
  s.du = du;
  s.dv = dv;
  s.v_mask = v_mask;
  zrndr::span_textured16(u, v, count);
}

}  // extern "C"

#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"
#include "platform/platform.h"

namespace {
// The whole probe library tests the original path: the zfix fixes off.
// MW3_FPU53=1 keeps the 53-bit rounding, to compare with the original run at
// full precision (MW3_X87=64).
const bool g_fixes_off = (zfix::options().per_second = false, zfix::options().bugs = false,
                          zfix::options().any_resolution = false,
                          zfix::options().nested_lock = false,
                          zfix::options().fpu53 = std::getenv("MW3_FPU53") && *std::getenv("MW3_FPU53") == '1', true);
struct ProbeClock : platform::Clock {
  uint32_t now = 0;
  uint32_t ticks_ms() override { return now; }
};
ProbeClock g_probe_clock;
}  // namespace

extern "C" {

// Runs ztimer::update() for each tick value, ztimer::init() (0x005759a0)
// first on frame init_frame; writes dt, raw_dt and the two totals per frame.
void probe_ztimer(const uint32_t* ticks, int frames, float scale_on_frame, int scale_frame, int init_frame,
                  float* dt_out, float* raw_out, float* totals_out) {
  ztimer::state() = ztimer::State{};
  ztimer::set_clock(&g_probe_clock);
  for (int i = 0; i < frames; ++i) {
    if (i == scale_frame) ztimer::state().scale = scale_on_frame;
    g_probe_clock.now = ticks[i];
    if (i == init_frame) ztimer::init();
    ztimer::update();
    dt_out[i] = ztimer::state().dt;
    raw_out[i] = ztimer::state().raw_dt;
    totals_out[2 * i] = ztimer::state().total_raw;
    totals_out[2 * i + 1] = ztimer::state().total_scaled;
  }
  ztimer::set_clock(nullptr);
}

}  // extern "C"

#include <cstdio>
#include <cstring>
#include <string>

#include "gamez/zreader/zreader.h"

namespace {
zrdr::Node g_probe_root;
bool g_probe_loaded = false;

void dump(const zrdr::Node& n, std::string& out) {
  char buf[32];
  switch (n.type) {
    case zrdr::Int: std::snprintf(buf, sizeof buf, "I%d ", n.i); out += buf; break;
    case zrdr::Float: {
      uint32_t bits;
      std::memcpy(&bits, &n.f, 4);
      std::snprintf(buf, sizeof buf, "F%08x ", bits);
      out += buf;
      break;
    }
    case zrdr::String:
      std::snprintf(buf, sizeof buf, "S%zu:", std::strlen(n.s));
      out += buf;
      out += n.s;
      out += ' ';
      break;
    case zrdr::List:
      std::snprintf(buf, sizeof buf, "L%d[", n.list[0].i);
      out += buf;
      for (int32_t i = 1; i < n.list[0].i; ++i) dump(n.list[i], out);
      out += "] ";
      break;
    default: out += "? ";
  }
}

// The index path from the root to `target`, e.g. "3/1/2", or "" if not inside.
bool path_to(zrdr::Node* n, const zrdr::Node* target, std::string& out) {
  if (n->type != zrdr::List) return false;
  for (int32_t i = 1; i <= n->list[0].i; ++i) {  // <= : one past the end is a valid answer
    if (&n->list[i] == target) { out = std::to_string(i); return true; }
    if (i < n->list[0].i) {
      std::string sub;
      if (path_to(&n->list[i], target, sub)) { out = std::to_string(i) + "/" + sub; return true; }
    }
  }
  return false;
}

int copy_out(const std::string& s, char* buf, int size) {
  int n = static_cast<int>(s.size());
  if (n >= size) return -1;
  std::memcpy(buf, s.data(), s.size() + 1);
  return n;
}
}  // namespace

extern "C" {

// A text file through zrdr::parse_text with the #ifdef depth given and
// returned: 1 with the tree loaded, 0 when it did not balance, -1 no file.
int probe_zrdr_text(const char* path, int32_t* depth) {
  if (g_probe_loaded) zrdr::free_contents(&g_probe_root);
  g_probe_loaded = false;
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  zrdr::state().if_depth = *depth;
  zrdr::Node* root = zrdr::parse_text(f);
  std::fclose(f);
  *depth = zrdr::state().if_depth;
  if (!root) return 0;
  g_probe_root = *root;
  std::free(root);
  g_probe_loaded = true;
  return 1;
}

// Reads one node tree from `path` at `offset` with zrdr::read_node; returns bytes read.
int probe_zrdr_load(const char* path, long offset) {
  if (g_probe_loaded) zrdr::free_contents(&g_probe_root);
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  std::fseek(f, offset, SEEK_SET);
  int n = static_cast<int>(zrdr::read_node(f, &g_probe_root));
  std::fclose(f);
  g_probe_loaded = true;
  return n;
}

int probe_zrdr_dump(char* buf, int size) {
  std::string s;
  dump(g_probe_root, s);
  return copy_out(s, buf, size);
}

// zrdr::find, get_float and get_int on the loaded tree. Writes the hit's index path.
int probe_zrdr_find(const char* key, char* buf, int size, int* got_float, float* f, int* got_int, int32_t* i) {
  *got_float = zrdr::get_float(&g_probe_root, key, f);
  *got_int = zrdr::get_int(&g_probe_root, key, i);
  std::string p;
  if (zrdr::Node* hit = zrdr::find(&g_probe_root, key)) path_to(&g_probe_root, hit, p);
  return copy_out(p, buf, size);
}

// The list and boolean getters by key, each into outputs prefilled with `fill`
// so a slot left alone shows. ok[] gets the six results in header order.
void probe_zrdr_lists(const char* key, uint32_t fill, float* v3, float* v2, int32_t* iv3,
                      int32_t* iv2, int32_t* bi, uint8_t* b, int* ok) {
  for (int k = 0; k < 3; ++k) std::memcpy(&v3[k], &fill, 4), std::memcpy(&iv3[k], &fill, 4);
  for (int k = 0; k < 2; ++k) std::memcpy(&v2[k], &fill, 4), std::memcpy(&iv2[k], &fill, 4);
  std::memcpy(bi, &fill, 4);
  *b = static_cast<uint8_t>(fill);
  ok[0] = zrdr::get_vec3(&g_probe_root, key, v3);
  ok[1] = zrdr::get_vec2(&g_probe_root, key, v2);
  ok[2] = zrdr::get_ivec3(&g_probe_root, key, iv3);
  ok[3] = zrdr::get_ivec2(&g_probe_root, key, iv2);
  ok[4] = zrdr::get_bool_int(&g_probe_root, key, bi);
  bool bb = *b != 0;
  ok[5] = zrdr::get_bool(&g_probe_root, key, &bb);
  if (ok[5]) *b = bb;
}

}  // extern "C"

#include <vector>

#include "crt/msvc_rand.h"
#include "mech3/mech.h"

namespace {
mech3::Mech g_mech;
mech3::Leg g_legs[2];
mech3::Foot g_feet[2];
struct {
  float controls = 0;
  int mission = 0, option = 1, screen = 0, ammo = 0;
  uintptr_t next_sound = 1;
} g_script;
std::vector<int32_t> g_log;

void log3(int32_t a, int32_t b, int32_t c) { g_log.insert(g_log.end(), {a, b, c}); }
int32_t fbits(float f) {
  int32_t i;
  std::memcpy(&i, &f, 4);
  return i;
}
float bitsf(int32_t i) {
  float f;
  std::memcpy(&f, &i, 4);
  return f;
}
int32_t snd(mech3::Sound s) { return static_cast<int32_t>(reinterpret_cast<uintptr_t>(s)); }
mech3::Sound snd(int32_t i) { return reinterpret_cast<mech3::Sound>(static_cast<uintptr_t>(static_cast<uint32_t>(i))); }
}  // namespace

extern "C" {

// Field order shared with tests/differential/test_heat.py (FIELDS).
void probe_heat_io(int32_t* v, int write) {
  mech3::Mech& m = g_mech;
  mech3::World& w = mech3::world();
  struct F { float* f = nullptr; int32_t* i = nullptr; bool* b = nullptr; };
  int32_t mission = g_script.mission, option = g_script.option, screen = g_script.screen,
          ammo = g_script.ammo, beep = snd(m.heat_beep()), alarm = snd(m.heat_alarm()),
          warn = snd(m.shutdown_warning()), voice = snd(m.shutdown_voice());
  int32_t player = w.player == &m;
  const F fields[] = {
      {&m.height}, {&m.heat}, {&m.heat_added}, {&m.heat_dissipation}, {&m.masc_time},
      {&m.shutdown_time}, {&m.beep_time}, {&m.voice_time}, {&g_feet[0].water_depth},
      {&g_feet[1].water_depth}, {&w.time}, {&w.dt}, {&g_script.controls},
      {nullptr, nullptr, &m.power_down_pending}, {nullptr, nullptr, &m.heat_shutdown},
      {nullptr, nullptr, &m.powered_down}, {nullptr, nullptr, &m.invincible},
      {nullptr, nullptr, &m.heat_sinks.doubles}, {nullptr, nullptr, &g_feet[0].in_water},
      {nullptr, nullptr, &g_feet[1].in_water}, {nullptr, nullptr, &w.full_heat_all_mechs},
      {nullptr, nullptr, &w.no_heat_haze},
      {nullptr, &m.jump_jets_firing}, {nullptr, &m.heat_sinks.count},
      {nullptr, reinterpret_cast<int32_t*>(&m.heat_state)}, {nullptr, &g_feet[0].soil},
      {nullptr, &g_feet[1].soil}, {nullptr, &beep}, {nullptr, &alarm}, {nullptr, &warn},
      {nullptr, &voice}, {nullptr, &player}, {nullptr, &mission}, {nullptr, &option},
      {nullptr, &screen}, {nullptr, &ammo},
  };
  int k = 0;
  for (const F& f : fields) {
    if (write) {
      if (f.f) *f.f = bitsf(v[k]);
      else if (f.i) *f.i = v[k];
      else *f.b = v[k] != 0;
    } else {
      v[k] = f.f ? fbits(*f.f) : f.i ? *f.i : (*f.b ? 1 : 0);
    }
    ++k;
  }
  if (write) {
    m.heat_beep() = snd(beep), m.heat_alarm() = snd(alarm);
    m.shutdown_warning() = snd(warn), m.shutdown_voice() = snd(voice);
    w.player = player ? &m : nullptr;
    g_script.mission = mission, g_script.option = option;
    g_script.screen = screen, g_script.ammo = ammo;
  }
}

void probe_heat_reset(uint32_t seed) {
  g_mech = mech3::Mech{};
  for (int i = 0; i < 2; ++i) g_legs[i].foot = &g_feet[i], g_feet[i] = {}, g_mech.legs[i] = &g_legs[i];
  g_script = {};
  crt::msvc_srand(seed);
  mech3::HeatCalls& c = mech3::heat_calls();
  c.heat_option = [] { return g_script.option != 0; };
  c.game_type = [] { return g_script.mission; };
  c.throttle = [](mech3::Mech&) { return g_script.controls; };
  c.screen = [](mech3::Mech&) -> void* { return g_script.screen ? &g_script : nullptr; };
  c.heat_haze = [](void*, float a, int k, float dt) { log3(1, fbits(a), k), log3(1, fbits(dt), 0); };
  c.play_sound = [](mech3::Mech&, int id) {
    log3(2, id, static_cast<int32_t>(g_script.next_sound));
    return snd(static_cast<int32_t>(g_script.next_sound++));
  };
  c.stop_sound = [](mech3::Sound s) { log3(3, snd(s), 0); };
  c.play_mech_sound = [](int i) { log3(4, i, 0); };
  c.power = [](mech3::Mech&, int on) { log3(5, on, 0); };
  c.torso_hit = [](mech3::Mech&, int explode) { log3(6, explode, 1); };
  c.ammo_explosion = [](mech3::Mech&) { log3(7, g_script.ammo, 0); return g_script.ammo != 0; };
  c.hud_message = [](float s, int msg) { log3(8, fbits(s), msg); };
}

int probe_heat_step(int32_t* log, int cap) {
  g_log.clear();
  mech3::update_heat(g_mech, mech3::world().dt);
  const int n = static_cast<int>(g_log.size());
  for (int i = 0; i < n && i < cap; ++i) log[i] = g_log[i];
  return n;
}

}  // extern "C"

#include "gamez/zimage/zimg_library.h"

namespace {
zimg::Library* g_tex_lib = nullptr;
std::string g_tex_dir;
}  // namespace

extern "C" {

// Opens <dir>/<file> as a texture library (the name already resolved).
int probe_tex_open(const char* dir, const char* file, int display_format) {
  g_tex_dir = dir;
  zimg::state() = zimg::State{};
  zimg::state().dir = g_tex_dir.c_str();
  zimg::state().display_format = display_format;
  g_tex_lib = zimg::new_library(file, 0, false);
  g_tex_lib->needs_resolve = false;
  return zimg::open(*g_tex_lib) ? g_tex_lib->header.texture_count : -1;
}

// Loads one texture and serialises it: the 0x38-byte fields that matter, then
// pixels, alpha and palette. Returns the length, or -1.
int probe_tex_load(const char* name, uint8_t* out, int cap) {
  zimg::Texture* t = zimg::load(*g_tex_lib, name);
  if (!t) return -1;
  std::vector<uint8_t> b;
  auto put = [&](const void* p, size_t n) {
    const auto* c = static_cast<const uint8_t*>(p);
    b.insert(b.end(), c, c + n);
  };
  put(&t->pixel_count, 4), put(&t->width, 2), put(&t->height, 2), put(&t->unk_08, 1);
  put(&t->flags, 1), put(&t->stretch, 2), put(&t->palette_count, 2), put(&t->width_again, 4);
  const int bytes = zimg::bytes_per_pixel(*t);
  if (t->pixels) put(t->pixels, t->palette_count ? t->pixel_count : bytes * t->pixel_count);
  if (t->alpha) put(t->alpha, t->pixel_count);
  if (t->palette) put(t->palette, (t->flags & zimg::kGlobalPalette) ? 0x200 : 2 * t->palette_count);
  zimg::free_texture(t);
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}

}  // extern "C"

extern "C" {

// Power on/off: fields in, call log out. f: [vec x,y,z]; i: pending, heat_shutdown,
// powered_down, anim_state, no_power_down, hud_saved, loops[3], up, down,
// detail, player, screen, screen_byte. op 0/1 = power(op), 2 = finish.
int probe_power(int op, float* f, int32_t* i, int32_t* log, int cap) {
  mech3::Mech& m = g_mech;
  m = mech3::Mech{};
  m.vec_3c = {f[0], f[1], f[2]};
  m.power_down_pending = i[0], m.heat_shutdown = i[1], m.powered_down = i[2];
  m.anim_state = i[3], m.no_power_down = i[4], m.hud_saved = static_cast<uint8_t>(i[5]);
  for (int k = 0; k < 3; ++k) m.loops()[k] = snd(i[6 + k]);
  m.power_up_sound() = snd(i[9]), m.power_down_sound() = snd(i[10]);
  m.detail = static_cast<uint32_t>(i[11]);
  mech3::world().player = i[12] ? &m : nullptr;
  g_script.screen = i[13];
  static int32_t screen_byte;
  screen_byte = i[14];
  g_script.next_sound = 100;
  g_log.clear();
  mech3::HeatCalls& c = mech3::heat_calls();
  c.stop_sound = [](mech3::Sound s) { log3(3, snd(s), 0); };
  c.screen = [](mech3::Mech&) -> void* { return g_script.screen ? &g_script : nullptr; };
  c.stop_controls = [](mech3::Mech&, int a) { log3(9, a, 0); };
  c.screen_power_down = [](void*) { log3(10, screen_byte, 0); return static_cast<uint8_t>(screen_byte); };
  c.screen_power_up = [](void*, uint8_t b) { log3(11, b, 0); };
  c.play_sound_at = [](mech3::Mech&, int id) {
    log3(2, id, static_cast<int32_t>(g_script.next_sound));
    return snd(static_cast<int32_t>(g_script.next_sound++));
  };
  if (op == 2) m.finish_power_down();
  else m.power(op);
  f[0] = m.vec_3c.x, f[1] = m.vec_3c.y, f[2] = m.vec_3c.z;
  i[0] = m.power_down_pending, i[1] = m.heat_shutdown, i[2] = m.powered_down;
  i[3] = m.anim_state, i[4] = m.no_power_down, i[5] = m.hud_saved;
  for (int k = 0; k < 3; ++k) i[6 + k] = snd(m.loops()[k]);
  i[9] = snd(m.power_up_sound()), i[10] = snd(m.power_down_sound());
  const int n = static_cast<int>(g_log.size());
  for (int k = 0; k < n && k < cap; ++k) log[k] = g_log[k];
  return n;
}

}  // extern "C"

#include "gamez/zsiscript/si_script.h"

extern "C" {

// Reads one motion from <path> at <offset> and serialises the script: header
// fields, then each part's name, translations, rotations and scales.
int probe_motion(const char* path, long offset, uint8_t* out, int cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  std::fseek(f, offset, SEEK_SET);
  zsis::Script s;
  const bool ok = s.read(f);
  std::fclose(f);
  std::vector<uint8_t> b;
  auto put = [&](const void* p, size_t n) {
    const auto* c = static_cast<const uint8_t*>(p);
    b.insert(b.end(), c, c + n);
  };
  const uint8_t okb = ok, act = s.active;
  put(&okb, 1), put(&s.rate, 4), put(&s.mult, 4), put(&s.stride, 4), put(&act, 1);
  put(&s.loop_time, 4), put(&s.inv_loop_time, 4), put(&s.frame_time, 4), put(&s.inv_frame_time, 4);
  put(&s.frame_count, 4), put(&s.part_count, 4), put(&s.time, 4);
  const uint32_t n = s.frame_count + 1;
  for (uint32_t i = 0; ok && i < s.part_count; ++i) {
    const zsis::Part* p = s.parts[i];
    put(p->name, std::strlen(p->name) + 1);
    put(p->translations, n * 12);
    put(p->rotations, n * 16);
    if (p->velocities) put(p->velocities, n * 12);
  }
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}

}  // extern "C"

#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zmodel/gmod_const.h"

namespace {
void put_bytes(std::vector<uint8_t>& b, const void* p, size_t n) {
  const auto* c = static_cast<const uint8_t*>(p);
  b.insert(b.end(), c, c + n);
}
int finish(const std::vector<uint8_t>& b, uint8_t* out, int cap) {
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}
}  // namespace

extern "C" {

// One model at <offset>: the header (pointer fields as present/absent), the
// arrays, then each polygon's fields and arrays. Same layout as test_mechlib.py.
int probe_mechlib_model(const char* path, long offset, uint8_t* out, int cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  std::fseek(f, offset, SEEK_SET);
  zmod::models() = zmod::ModelPool{};
  zmod::reset_pool();
  zmod::Model* m = zcls::read_model(f);
  const long end = std::ftell(f);
  std::fclose(f);
  std::vector<uint8_t> b;
  const int32_t used = static_cast<int32_t>(end - offset);
  put_bytes(b, &used, 4);
  put_bytes(b, m->header, 0x34);
  put_bytes(b, m->header + 0x48, 0x5c - 0x48);
  if (m->vertices) put_bytes(b, m->vertices, m->count(0x14) * 12);
  if (m->normals) put_bytes(b, m->normals, m->count(0x18) * 12);
  if (m->morphs) put_bytes(b, m->morphs, m->count(0x1c) * 12);
  for (uint32_t i = 0; i < m->count(0x10); ++i) {
    const zmod::Polygon& p = m->polygons[i];
    const uint32_t k = p.flags & 0xff;
    put_bytes(b, &p.flags, 4), put_bytes(b, &p.priority, 4);
    put_bytes(b, &p.material_index, 4), put_bytes(b, &p.zone_set, 4);
    const uint8_t has[4] = {p.vertex_indices != nullptr, p.normal_indices != nullptr,
                            p.uvs != nullptr, p.colors != nullptr};
    put_bytes(b, has, 4);
    if (p.vertex_indices) put_bytes(b, p.vertex_indices, k * 4);
    if (p.normal_indices) put_bytes(b, p.normal_indices, k * 4);
    if (p.uvs) put_bytes(b, p.uvs, k * 8);
    if (p.colors) put_bytes(b, p.colors, k * 12);
  }
  zmod::models() = zmod::ModelPool{};
  return finish(b, out, cap);
}

// One material at <offset>: the 0x28 bytes, then the texture name.
int probe_mechlib_material(const char* path, long offset, uint8_t* out, int cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  std::fseek(f, offset, SEEK_SET);
  zcls::Material m{};
  zcls::read_material(f, m);
  const int32_t used = static_cast<int32_t>(std::ftell(f) - offset);
  std::fclose(f);
  std::vector<uint8_t> b;
  put_bytes(b, &used, 4);
  put_bytes(b, m.raw, 0x28);
  if (m.texture) put_bytes(b, m.texture, std::strlen(m.texture) + 1);
  std::free(m.raw);
  std::free(m.texture);
  return finish(b, out, cap);
}

}  // extern "C"

#include "gamez/zimage/zimg_texture.h"

extern "C" {

// Reads a world's texture directory: count at offset, group set first. Output:
// result, table count, then per entry name[20], state, group, mip index.
int probe_texture_directory(const char* path, long offset, int count, int group, uint8_t* out, int cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  std::fseek(f, offset, SEEK_SET);
  zimg::TextureTable& t = zimg::textures();
  t.count = -7;
  t.group = group;
  const int32_t r = zimg::read_texture_directory(count, f);
  std::fclose(f);
  std::vector<uint8_t> b;
  put_bytes(b, &r, 4), put_bytes(b, &t.count, 4);
  for (int i = 0; i < (r > 0 ? r : 0); ++i) {
    const zimg::TextureEntry& e = t.entries[i];
    const int32_t mip = e.mip ? static_cast<int32_t>(e.mip - t.entries) : -1;
    put_bytes(b, e.name, 20), put_bytes(b, &e.state, 4), put_bytes(b, &e.group, 4), put_bytes(b, &mip, 4);
  }
  return finish(b, out, cap);
}

}  // extern "C"

#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zvideo/zvid_format.h"

extern "C" {

// Reads a world's materials at offset with the display set to r/g/b bits.
// Output: result, count, free head, used head, then every slot (texture as an
// index), then each used cycled material's cycle, in used-list order.
int probe_materials(const char* path, long offset, int rb, int gb, int bb, uint8_t* out, int cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  std::fseek(f, offset, SEEK_SET);
  zvid::set_pixel_format(rb, gb, bb);
  zmod::MaterialTable& t = zmod::materials();
  t = zmod::MaterialTable{};
  const int32_t r = zmod::read_materials(f);
  std::fclose(f);
  std::vector<uint8_t> b;
  put_bytes(b, &r, 4), put_bytes(b, &t.count, 4), put_bytes(b, &t.free_head, 4), put_bytes(b, &t.used_head, 4);
  std::vector<bool> used(t.capacity > 0 ? t.capacity : 0);
  for (int i = t.used_head; i != -1; i = t.at(i).next) used[i] = true;
  for (int i = 0; i < r; ++i) {
    const zmod::Material& m = t.at(i);
    put_bytes(b, &m.alpha, 1), put_bytes(b, &m.flags, 1), put_bytes(b, &m.rgb, 2);
    put_bytes(b, &m.r, 4), put_bytes(b, &m.g, 4), put_bytes(b, &m.b, 4);
    const int32_t tex = (used[i] && (m.flags & 1))
                            ? (m.texture ? static_cast<int32_t>(m.texture - zimg::textures().entries) : -1)
                            : static_cast<int32_t>(m.texture_index);
    put_bytes(b, &tex, 4), put_bytes(b, m.unk_14, 12), put_bytes(b, &m.soil, 4);
    put_bytes(b, &m.prev, 2), put_bytes(b, &m.next, 2);
  }
  for (int i = t.used_head; i != -1; i = t.at(i).next) {
    const zmod::Material& m = t.at(i);
    if (!(m.flags & 4)) continue;
    const zmod::Cycle* c = m.cycle;
    put_bytes(b, &c->looping, 4), put_bytes(b, &c->frame, 4), put_bytes(b, &c->index, 4);
    put_bytes(b, &c->speed, 4), put_bytes(b, &c->texture_count, 4), put_bytes(b, &c->added, 4);
    for (uint32_t k = 0; k < c->texture_count; ++k) {
      const int32_t idx = c->textures[k] ? static_cast<int32_t>(c->textures[k] - zimg::textures().entries) : -1;
      put_bytes(b, &idx, 4);
    }
  }
  return finish(b, out, cap);
}

}  // extern "C"

extern "C" {

int probe_pack_rgb(int rb, int gb, int bb, float r, float g, float b) {
  zvid::set_pixel_format(rb, gb, bb);
  const float rgb[3] = {r, g, b};
  return zvid::pack_rgb(rgb);
}

}  // extern "C"

#include "gamez/zmodel/gmod_const.h"

extern "C" {

// Reads materials at moff then models at off (display 5-6-5). Output: result,
// count, free head, then per slot the header (pointer fields skipped), link,
// arrays, lights and polygons; materials as indices.
int probe_models(const char* path, long moff, long off, uint8_t* out, int cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  zvid::set_pixel_format(5, 6, 5);
  zmod::materials() = zmod::MaterialTable{};
  std::fseek(f, moff, SEEK_SET);
  zmod::read_materials(f);
  zmod::models() = zmod::ModelPool{};
  std::fseek(f, off, SEEK_SET);
  const int32_t r = zmod::read_models(f);
  std::fclose(f);
  const zmod::ModelPool& p = zmod::models();
  std::vector<uint8_t> b;
  put_bytes(b, &r, 4), put_bytes(b, &p.count, 4), put_bytes(b, &p.free_head, 4);
  for (int i = 0; i < r; ++i) {
    const zmod::Model& m = p.slots[i];
    put_bytes(b, m.header, 0x34), put_bytes(b, m.header + 0x48, 0x14), put_bytes(b, &m.link, 4);
    const auto n = [&](int o) { return static_cast<int32_t>(m.count(o)); };
    if (n(0x14) > 0) put_bytes(b, m.vertices, n(0x14) * 12);
    if (n(0x18) > 0) put_bytes(b, m.normals, n(0x18) * 12);
    if (n(0x1c) > 0) put_bytes(b, m.morphs, n(0x1c) * 12);
    for (int k = 0; k < n(0x20); ++k) {
      const zmod::Light& l = m.lights[k];
      put_bytes(b, l.raw, 0x28), put_bytes(b, &l.color, 2), put_bytes(b, l.raw + 0x30, 0x1c);
      int32_t vc;
      std::memcpy(&vc, l.raw + 0x0c, 4);
      if (vc > 0) put_bytes(b, l.vertices, vc * 12);
    }
    for (int k = 0; k < n(0x10); ++k) {
      const zmod::Polygon& q = m.polygons[k];
      const uint32_t c = q.flags & 0xff;
      const int32_t mat = q.material ? zmod::materials().index_of(q.material) : -1;
      put_bytes(b, &q.flags, 4), put_bytes(b, &q.priority, 4), put_bytes(b, &mat, 4), put_bytes(b, &q.zone_set, 4);
      if (q.vertex_indices) put_bytes(b, q.vertex_indices, c * 4);
      if (q.normal_indices) put_bytes(b, q.normal_indices, c * 4);
      if (q.uvs) put_bytes(b, q.uvs, c * 8);
      if (q.colors) put_bytes(b, q.colors, c * 12);
    }
  }
  return finish(b, out, cap);
}

}  // extern "C"

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_zbd.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zrender/zrndr_fog.h"

namespace {
std::vector<int32_t> g_reports;

void record_report(int level, const char*, int line, const char*) {
  g_reports.push_back(level), g_reports.push_back(line);
}

int32_t node_index(const zcls::Node* n) {
  if (!n) return -1;
  const zcls::NodePool& p = zcls::nodes();
  for (size_t b = 0; b < p.blocks.size(); ++b) {
    const zcls::Node* base = p.blocks[b].get();
    if (n >= base && n < base + p.block_size) return static_cast<int32_t>(b * p.block_size + (n - base));
  }
  return -2;
}

void put_i32(std::vector<uint8_t>& b, int32_t v) { put_bytes(b, &v, 4); }

void put_nodes(std::vector<uint8_t>& b, zcls::Node* const* list, int32_t count) {
  put_i32(b, list != nullptr);
  if (list)
    for (int32_t i = 0; i < count; ++i) put_i32(b, node_index(list[i]));
}

// Bytes as read with the given 4-byte pointer fields zeroed.
void put_raw(std::vector<uint8_t>& b, const uint8_t* raw, size_t n, std::initializer_list<int> ptrs) {
  std::vector<uint8_t> copy(raw, raw + n);
  for (int off : ptrs) std::memset(copy.data() + off, 0, 4);
  put_bytes(b, copy.data(), n);
}

void put_class_data(std::vector<uint8_t>& b, zcls::Node* n) {
  using namespace zcls;
  switch (n->cls()) {
    case 1: {
      auto* c = static_cast<Camera*>(n->data);
      put_raw(b, c->raw, sizeof c->raw, {0, 4, 8, 0xc});
      put_nodes(b, c->nodes, 4);
      break;
    }
    case 2: {
      auto* w = static_cast<World*>(n->data);
      put_raw(b, w->raw, sizeof w->raw, {0x0c, 0x90, 0xa4, 0xa8, 0xb0, 0xb4});
      // The cells waiting for the area update, as (row, column).
      for (int32_t i = 0; i < w->get<int32_t>(4); ++i) {
        int32_t at = -1;
        for (int32_t z = 0; at < 0 && z < w->get<int32_t>(0x8c); ++z)
          for (int32_t x = 0; x < w->get<int32_t>(0x88); ++x)
            if (reinterpret_cast<uint32_t*>(w->rows[z][x].raw) == w->area_pending[i]) at = z * 0x10000 + x;
        put_i32(b, at);
      }
      const int32_t lights = w->get<int32_t>(0xa0), sounds = w->get<int32_t>(0xac);
      put_nodes(b, w->lights, lights);
      for (int32_t i = 0; w->lights && i < lights; ++i) put_i32(b, w->light_data[i] == w->lights[i]->data);
      put_nodes(b, w->sounds, sounds);
      for (int32_t i = 0; w->sounds && i < sounds; ++i) put_i32(b, w->sound_data[i] == w->sounds[i]->data);
      for (int32_t z = 0; z < w->get<int32_t>(0x8c); ++z)
        for (int32_t x = 0; x < w->get<int32_t>(0x88); ++x) {
          const Partition& p = w->rows[z][x];
          put_raw(b, p.raw, sizeof p.raw, {0x3c});
          put_nodes(b, p.nodes, p.get<int16_t>(0x3a));
        }
      break;
    }
    case 3: put_bytes(b, static_cast<Window*>(n->data)->raw, 0xf8); break;
    case 4: put_bytes(b, static_cast<Display*>(n->data)->raw, 0x1c); break;
    case 5: put_bytes(b, static_cast<Object3d*>(n->data)->raw, 0x90); break;
    case 6: put_bytes(b, static_cast<Lod*>(n->data)->raw, 0x50); break;
    case 9: {
      auto* l = static_cast<Light*>(n->data);
      put_raw(b, l->raw, sizeof l->raw, {0xcc});
      put_nodes(b, l->nodes, l->get<int32_t>(0xc8));
      break;
    }
    case 10: {
      auto* s = static_cast<Sound*>(n->data);
      put_raw(b, s->raw, sizeof s->raw, {0x90});
      put_nodes(b, s->nodes, s->get<int32_t>(0x8c));
      break;
    }
    default: break;
  }
}

void put_item(std::vector<uint8_t>& b, const zcls::ListItem* item) {
  const zcls::Lists& l = zcls::lists();
  int32_t chunk = -1, slot = -1;
  for (size_t c = 0; item && c < l.chunks.size(); ++c) {
    const zcls::ListItem* base = l.chunks[c].get();
    if (item >= base && item < base + l.chunk_size) chunk = static_cast<int32_t>(c), slot = static_cast<int32_t>(item - base);
  }
  put_i32(b, chunk), put_i32(b, slot);
}

void reset_world_state(int free_head) {
  zcls::nodes() = zcls::NodePool{};
  zcls::nodes().free_head = free_head;
  zcls::lists() = zcls::Lists{};
  zmod::fog() = zmod::Fog{};
  zrndr::fog_color() = zrndr::FogColor{};
  zrndr::software_format() = zrndr::SoftwareFormat{0xf800, 0x07e0, 8, 3, 3};
  zvid::clear_color() = 0;
  g_reports.clear();
  zerr::set_sink(record_report);
}

// With `items`, each node's list-7 item (+0xcc) as its place in the item pool.
void dump_world(std::vector<uint8_t>& b, int size, bool items = false) {
  const zcls::NodePool& p = zcls::nodes();
  put_i32(b, p.capacity), put_i32(b, p.live), put_i32(b, p.free_head);
  put_i32(b, static_cast<int32_t>(p.blocks.size()));
  for (int32_t i = 0; i < p.capacity; ++i) {
    zcls::Node* n = zcls::node_at(i);
    if (items) put_raw(b, n->raw, 0xd0, {0x38, 0x3c, 0x48, 0x58, 0x60, 0xcc});
    else put_raw(b, n->raw, 0xd0, {0x38, 0x3c, 0x48, 0x58, 0x60});
    put_bytes(b, &n->link, 4);
    put_i32(b, n->model ? static_cast<int32_t>(n->model - zmod::models().slots.data()) : -1);
    put_i32(b, n->action != nullptr);
    if (items) put_item(b, n->changed);
    if (i >= size) continue;
    put_nodes(b, n->parents, n->parent_count());
    put_nodes(b, n->children, n->child_count());
    if (n->data) put_class_data(b, n);
  }
  const zcls::Lists& l = zcls::lists();
  for (const zcls::List& list : l.list) {
    int32_t count = 0;
    for (zcls::ListItem* i = list.head; i; i = i->next) ++count;
    put_i32(b, count);
    for (zcls::ListItem* i = list.head; i; i = i->next) {
      put_i32(b, node_index(i->node)), put_item(b, i), put_i32(b, i->removable);
      put_item(b, i->prev);
    }
    put_item(b, list.tail), put_i32(b, list.needs_purge);
  }
  int32_t free_count = 0;
  for (zcls::ListItem* i = l.free; i; i = i->next) ++free_count;
  put_i32(b, free_count), put_item(b, l.free), put_i32(b, static_cast<int32_t>(l.chunks.size()));
  put_bytes(b, &zmod::fog(), sizeof(zmod::Fog));
  const zrndr::FogColor& fc = zrndr::fog_color();
  put_bytes(b, fc.rgb, 12), put_bytes(b, &fc.r, 4), put_bytes(b, &fc.g, 4), put_bytes(b, &fc.b, 4);
  put_bytes(b, &fc.packed, 2), put_bytes(b, &fc.doubled, 4), put_bytes(b, fc.ramp, 128);
  put_bytes(b, &zvid::clear_color(), 4);
  put_i32(b, static_cast<int32_t>(g_reports.size() / 2));
  for (int32_t v : g_reports) put_i32(b, v);
}
}  // namespace

extern "C" {

// The node pool after reading one world's textures-to-nodes (test_gamez.py).
int probe_nodes(const char* path, long moff, long mdl_off, long node_off, int size, int free_head,
                uint8_t* out, int cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  zvid::set_pixel_format(5, 6, 5);
  zmod::materials() = zmod::MaterialTable{};
  std::fseek(f, moff, SEEK_SET);
  zmod::read_materials(f);
  zmod::models() = zmod::ModelPool{};
  std::fseek(f, mdl_off, SEEK_SET);
  zmod::read_models(f);
  reset_world_state(free_head);
  std::fseek(f, node_off, SEEK_SET);
  const int32_t r = zcls::read_nodes(size, f);
  std::fclose(f);
  zerr::set_sink(nullptr);
  std::vector<uint8_t> b;
  put_i32(b, r);
  dump_world(b, size);
  return finish(b, out, cap);
}

// The whole of 0x4ff520 on one file: result, reports, then the world when read.
int probe_read_zbd(const char* path, int size, uint8_t* out, int cap) {
  zimg::textures().count = 0;
  zimg::textures().group = 3;
  zmod::materials() = zmod::MaterialTable{};
  zmod::models() = zmod::ModelPool{};
  zvid::set_pixel_format(5, 6, 5);
  reset_world_state(-1);
  const int32_t r = zcls::read_zbd(path);
  zerr::set_sink(nullptr);
  std::vector<uint8_t> b;
  put_i32(b, r);
  put_i32(b, static_cast<int32_t>(g_reports.size() / 2));
  for (int32_t v : g_reports) put_i32(b, v);
  put_i32(b, zimg::textures().count), put_i32(b, zmod::materials().count);
  put_i32(b, zmod::models().count);
  if (r == 0) dump_world(b, size);
  return finish(b, out, cap);
}

}  // extern "C"

#include "gamez/zclass/class.h"
#include "mech3/veh_basic.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zclass/sound.h"

namespace {
void destroy_probe_action(zcls::Node*) {}
int32_t idx(const zcls::Node* n) { return node_index(n); }
}  // namespace

extern "C" {

// Taking nodes down (test_destroy.py): a world read whole, then operations on
// its nodes by pool index, each answered with its result.
int probe_destroy_load(const char* path) {
  zimg::textures().count = 0;
  zimg::textures().group = 3;
  zmod::materials() = zmod::MaterialTable{};
  zmod::models() = zmod::ModelPool{};
  zvid::set_pixel_format(5, 6, 5);
  reset_world_state(-1);
  zcls::immediate_delete() = 1;
  zcls::frames() = zcls::Frames{};
  return zcls::read_zbd(path);
}

// The finds by name (test_node_find.py), on a world loaded as above: a
// node's name set (its 0x24 bytes), and 0x004eb0d0 (whole) or 0x004abb60
// (prefix) from node `a` (-1 the null node), answered by pool index.
void probe_node_name(int a, const char* name) {
  zcls::Node* n = zcls::node_at(a);
  std::memset(n->raw, 0, 0x24);
  std::strncpy(reinterpret_cast<char*>(n->raw), name, 0x23);
}
int probe_node_find(int a, const char* name, int prefix) {
  zcls::Node* n = a >= 0 ? zcls::node_at(a) : nullptr;
  zcls::Node* r = prefix ? static_cast<zcls::Node*>(mech3::node_find_prefix(n, name)) : zcls::node_find(n, name);
  return r ? idx(r) : -1;
}

int probe_destroy_op(int op, int a, int b, int c) {
  using namespace zcls;
  Node* n = a >= 0 ? node_at(a) : nullptr;
  switch (op) {
    case 0: return node_destroy(n);
    case 1: return node_delete(n);
    case 2: return node_release(n);
    case 3: return tree_destroy(n);
    case 4: return node_remove_child(node_at(b), n);
    case 5: immediate_delete() = a; return 0;
    case 6: lists().enabled = a; return 0;
    case 7: return purge_lists();
    case 8: {
      Node* o = object3d_create();
      if (n) n->cls() == 2 ? world_add_child(n, o) : node_add_child(n, o);
      if (b >= 0) {
        node_set_priority(o, b);
        c ? node_set_action_back(o, destroy_probe_action) : node_set_action(o, destroy_probe_action);
      }
      return idx(o);
    }
    case 9: {
      Node* l = light_create();
      if (n) world_add_light(n, l);
      return idx(l);
    }
    case 10: {
      Node* s = sound_create();
      if (n) world_add_sound(n, s);
      return idx(s);
    }
    case 11: return node_free(n);
    case 12: return destroy_all();
    case 13: return classes_reset();
    case 14: return node_set_model(n, b >= 0 ? zmod::model_at(b) : nullptr);
    case 15: return reread(n, b);
    default: return -99;
  }
}

}  // extern "C"

namespace {
zcls::MechLib g_copy_lib;

int32_t texture_index(const zimg::TextureEntry* e) {
  return e ? static_cast<int32_t>(e - zimg::textures().entries) : -1;
}

// A model's bytes (its pointer fields left out), arrays and polygons, the
// materials as table indices.
void put_model(std::vector<uint8_t>& b, const zmod::Model& m) {
  put_raw(b, m.header, 0x5c, {0x34, 0x38, 0x3c, 0x40, 0x44});
  auto n = [&](int off) { return static_cast<int32_t>(m.count(off)); };
  if (m.vertices && n(0x14) > 0) put_bytes(b, m.vertices, n(0x14) * 12);
  if (m.normals && n(0x18) > 0) put_bytes(b, m.normals, n(0x18) * 12);
  if (m.morphs && n(0x1c) > 0) put_bytes(b, m.morphs, n(0x1c) * 12);
  for (int32_t k = 0; m.lights && k < n(0x20); ++k) {
    const zmod::Light& l = m.lights[k];
    put_bytes(b, l.raw, 0x28), put_bytes(b, &l.color, 2), put_bytes(b, l.raw + 0x30, 0x1c);
    int32_t vc;
    std::memcpy(&vc, l.raw + 0x0c, 4);
    if (vc > 0) put_bytes(b, l.vertices, vc * 12);
  }
  for (int32_t k = 0; m.polygons && k < n(0x10); ++k) {
    const zmod::Polygon& q = m.polygons[k];
    const uint32_t c = q.flags & 0xff;
    const int32_t mat = q.material ? zmod::materials().index_of(q.material) : -1;
    put_bytes(b, &q.flags, 4), put_bytes(b, &q.priority, 4), put_i32(b, mat), put_bytes(b, &q.zone_set, 4);
    const uint8_t has[5] = {q.vertex_indices != nullptr, q.normal_indices != nullptr, q.uvs != nullptr,
                            q.colors != nullptr, q.scratch != nullptr};
    put_bytes(b, has, 5);
    if (q.vertex_indices) put_bytes(b, q.vertex_indices, c * 4);
    if (q.normal_indices) put_bytes(b, q.normal_indices, c * 4);
    if (q.uvs) put_bytes(b, q.uvs, c * 8);
    if (q.colors) put_bytes(b, q.colors, c * 12);
  }
}

// The material table: its counts, then each used material, newest first.
void put_material_table(std::vector<uint8_t>& b) {
  const zmod::MaterialTable& t = zmod::materials();
  put_i32(b, t.capacity), put_i32(b, t.count), put_i32(b, t.free_head), put_i32(b, t.used_head);
  put_i32(b, t.last ? t.index_of(t.last) : -1);
  int32_t free_count = 0;
  for (int32_t i = t.free_head; i >= 0; i = t.at(i).next) ++free_count;
  put_i32(b, free_count);
  for (int32_t i = t.used_head; i >= 0; i = t.at(i).next) {
    const zmod::Material& m = t.at(i);
    put_i32(b, i);
    put_bytes(b, &m.alpha, 1), put_bytes(b, &m.flags, 1), put_bytes(b, &m.rgb, 2);
    put_bytes(b, &m.r, 4), put_bytes(b, &m.g, 4), put_bytes(b, &m.b, 4);
    put_i32(b, (m.flags & 1) ? texture_index(m.texture) : static_cast<int32_t>(m.texture_index));
    put_bytes(b, m.unk_14, 12), put_bytes(b, &m.soil, 4);
    put_i32(b, (m.cycle != nullptr || m.file_cycle != 0) ? 1 : 0);
    put_bytes(b, &m.prev, 2), put_bytes(b, &m.next, 2);
  }
}

void put_textures(std::vector<uint8_t>& b) {
  const zimg::TextureTable& t = zimg::textures();
  put_i32(b, t.count);
  for (int32_t i = 0; i < t.count; ++i) {
    const zimg::TextureEntry& e = t.entries[i];
    put_bytes(b, e.name, 20), put_i32(b, e.state), put_i32(b, e.group);
  }
}
}  // namespace

extern "C" {

// Copying trees (test_tree_copy.py): a world, mechlib opened, its trees loaded.
int probe_copy_world(const char* zbd) { return probe_destroy_load(zbd); }
int probe_copy_lib(const char* path) {
  g_copy_lib = zcls::MechLib{};
  zutl_zar::init(g_copy_lib.archive);
  zcls::copy_state() = zcls::CopyState{};
  zcls::mechlib_post_process() = false;
  return zcls::mechlib_open(g_copy_lib, path) ? 1 : 0;
}
int probe_copy_load(const char* name) { return node_index(zcls::mechlib_load(g_copy_lib, name)); }
// mode 0 tree_copy(a, b), 1 tree_copy_materials(a, b), 2 tree_destroy, 3 node_destroy.
int probe_copy(int root, int mode, int a, int b) {
  zcls::Node* n = root >= 0 ? zcls::node_at(root) : nullptr;
  switch (mode) {
    case 0: return node_index(zcls::tree_copy(n, a, b));
    case 1: return node_index(zcls::tree_copy_materials(n, a, b));
    case 2: return zcls::tree_destroy(n);
    case 3: return zcls::node_destroy(n);
    default: return -99;
  }
}
// The pool and lists, every model a node holds (first time seen), the
// material and texture tables.
int probe_copy_dump(uint8_t* out, int cap) {
  std::vector<uint8_t> b;
  dump_world(b, zcls::nodes().capacity, true);
  const zmod::ModelPool& mp = zmod::models();
  put_i32(b, mp.count), put_i32(b, mp.free_head);
  std::vector<bool> seen(mp.slots.size());
  for (int32_t i = 0; i < zcls::nodes().capacity; ++i) {
    const zcls::Node* n = zcls::node_at(i);
    if (!n->model) continue;
    const auto k = static_cast<size_t>(n->model - mp.slots.data());
    if (seen[k]) continue;
    seen[k] = true;
    put_i32(b, static_cast<int32_t>(k));
    put_model(b, *n->model);
  }
  put_material_table(b);
  put_textures(b);
  zerr::set_sink(record_report);
  return finish(b, out, cap);
}

// The world as test_gamez dumps it, then the delete state and, with `pool`,
// every model slot's use count, polygon count and link.
int probe_destroy_dump(int pool, uint8_t* out, int cap) {
  std::vector<uint8_t> b;
  dump_world(b, zcls::nodes().capacity, true);
  put_i32(b, zcls::immediate_delete()), put_i32(b, zcls::lists().enabled);
  put_bytes(b, zcls::zbd_path(), 4);
  int32_t deferred = 0;
  for (zcls::ListItem* i = zcls::lists().deferred; i; i = i->next) ++deferred;
  put_i32(b, deferred);
  for (zcls::ListItem* i = zcls::lists().deferred; i; i = i->next) put_i32(b, idx(i->node)), put_item(b, i);
  const zmod::ModelPool& mp = zmod::models();
  put_i32(b, mp.count), put_i32(b, mp.free_head);
  if (pool)
    for (const zmod::Model& m : mp.slots) put_i32(b, static_cast<int32_t>(m.count(0xc))), put_i32(b, static_cast<int32_t>(m.count(0x10))), put_bytes(b, &m.link, 4);
  zerr::set_sink(record_report);
  return finish(b, out, cap);
}

}  // extern "C"

#include "gamez/zclass/sound.h"
#include "gamez/zeffect/zeff_anim_init.h"

namespace {
// Each hook call appends (id, args) and returns a fake id, 0x70000000 +
// 0x10 * call number, or 0 (-1 for an effect index) on call `g_fail_at`.
std::vector<uint8_t> g_trace;
int32_t g_calls = 0, g_fail_at = -1;

uint32_t next_fake() {
  const int32_t k = g_calls++;
  return k == g_fail_at ? 0 : 0x70000000u + 0x10u * static_cast<uint32_t>(k);
}
void trace_u32(uint32_t v) { put_bytes(g_trace, &v, 4); }
void trace_str(const char* s) {
  if (!s) return trace_u32(0xffffffff);
  put_bytes(g_trace, s, std::strlen(s) + 1);
}
uint32_t id(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
template <typename T> T* fake(uint32_t v) { return reinterpret_cast<T*>(static_cast<uintptr_t>(v)); }
int32_t def_index(const zeff::AnimDef* d) { return d ? static_cast<int32_t>(d - zeff::anim().defs) : -1; }

void install_anim_hooks() {
  zeff::AnimHooks& h = zeff::anim_hooks();
  h.copy_tree = [](zcls::Node* n) { trace_u32(3), trace_u32(node_index(n)); return fake<zcls::Node>(next_fake()); };
  h.create_puffer = [] { trace_u32(7); return fake<void>(next_fake()); };
  h.find_sound = [](const char* name) { trace_u32(10), trace_str(name); return fake<void>(next_fake()); };
  h.effect_index = [](const char* name) {
    trace_u32(11), trace_str(name);
    const uint32_t v = next_fake();
    return v ? static_cast<int32_t>(v) : -1;
  };
  zcls::set_find_sound(h.find_sound);
}

// A node as its pool slot, anything else (a puffer, a sound) as its fake id.
uint32_t ref_id(const void* p, bool is_node) {
  return is_node ? static_cast<uint32_t>(node_index(static_cast<const zcls::Node*>(p))) : id(p);
}

template <typename R>
void put_rec(std::vector<uint8_t>& b, const R& r, std::initializer_list<int> ptrs) {
  put_raw(b, r.raw, sizeof r.raw, ptrs);
}

void dump_anim(std::vector<uint8_t>& b) {
  const zeff::AnimState& a = zeff::anim();
  put_raw(b, a.block.raw, 0x44, {0x0c, 0x14, 0x18});
  for (int32_t i = 0; i < a.def_count(); ++i) {
    const zeff::AnimDef& d = a.defs[i];
    const bool fixed = i != 0 && d.raw[0x98] != 5;
    put_raw(b, d.raw, sizeof d.raw, {0x40, 0x64, 0xc4, 0x100, 0x114, 0x118, 0x11c, 0x120, 0x124, 0x128,
                                     0x12c, 0x130, 0x134});
    put_i32(b, fixed ? node_index(d.node) : 0), put_i32(b, fixed ? node_index(d.root) : 0);
    const auto refs = [&](const auto* list, int count_off, auto field, bool nodes) {
      for (int k = 0; k < d.count(count_off); ++k) {
        put_rec(b, list[k], {0x24});
        put_i32(b, fixed && k > 0 ? ref_id(field(list[k]), nodes) : 0);
      }
    };
    refs(d.objects, 0x109, [](const auto& r) { return r.node; }, true);
    refs(d.nodes, 0x10a, [](const auto& r) { return r.node; }, true);
    refs(d.lights, 0x10b, [](const auto& r) { return r.made; }, true);
    refs(d.puffers, 0x10c, [](const auto& r) { return r.made; }, false);
    refs(d.sounds, 0x10d, [](const auto& r) { return r.made; }, true);
    for (int k = 0; k < d.count(0x10e); ++k) {
      put_rec(b, d.static_sounds[k], {0x20});
      put_i32(b, fixed && k > 0 ? id(d.static_sounds[k].sound) : 0);
    }
    for (int k = 0; k < d.count(0x10f); ++k) put_bytes(b, d.effects[k].raw, 0x24);
    for (int k = 0; k < d.count(0x110); ++k) {
      const zeff::Prerequisite& p = d.prereqs[k];
      put_rec(b, p, {0x28, 0x2c});
      if (!fixed) continue;
      if (p.raw[4] == 1) put_i32(b, def_index(p.def));
      if (p.raw[4] == 2 || p.raw[4] == 3) put_i32(b, node_index(p.node));
    }
    for (int k = 0; k < d.count(0x112); ++k) put_bytes(b, d.anim_refs[k].raw, 0x48);
    if (d.get<int32_t>(0x104) > 0) put_bytes(b, d.reset_events, d.get<int32_t>(0x104));
    for (int k = 0; k < d.count(0x108); ++k) {
      const zeff::SeqDef& s = d.seqs[k];
      put_rec(b, s, {0x38});
      if (s.get<int32_t>(0x3c) > 0) put_bytes(b, s.events, s.get<int32_t>(0x3c));
    }
  }
}
}  // namespace

extern "C" {

// 0 (default): the exe's prerequisite path; 1: the port's FIX (bug).
int g_anim_fix = 0;
void probe_anim_fix(int on) { g_anim_fix = on; }
// The port's play-changing fixes on or off (zfix bugs), for a test's fixed path.
void probe_set_bugs(int on) { zfix::options().bugs = on != 0; }

// After probe_anim: def i's prerequisite count | the count to meet << 8, and
// its entries' types (the FIX (bug) case in test_anim.py).
int probe_anim_prereqs(int i, uint8_t* types, int cap) {
  const zeff::AnimDef& d = zeff::anim().defs[i];
  for (int k = 0; k < d.count(0x110) && k < cap; ++k) types[k] = d.prereqs[k].raw[4];
  return d.count(0x110) | d.count(0x111) << 8;
}

// 0x50a0e0 on one anim.zbd against a world read by read_zbd (test_anim.py),
// with the calls into unported parts recorded. `full` adds the read
// structures and the node pool after.
int probe_anim(const char* world, const char* path, int fail_at, int full, uint8_t* out, int cap) {
  zimg::textures().count = 0;
  zimg::textures().group = 3;
  zmod::materials() = zmod::MaterialTable{};
  zmod::models() = zmod::ModelPool{};
  zvid::set_pixel_format(5, 6, 5);
  reset_world_state(-1);
  zcls::frames() = zcls::Frames{};
  zcls::list_walk() = zcls::ListWalk{};
  if (zcls::read_zbd(world) != 0) return -2;
  g_reports.clear();
  zeff::anim() = zeff::AnimState{};
  zeff::anim().fix_prerequisites = g_anim_fix != 0;
  const bool bugs = zfix::options().bugs;
  zfix::options().bugs = g_anim_fix != 0;  // the fix acts with zfix bugs
  zeff::anim().fix_instance_tree = false;  // the exe's path
  zeff::anim_allow_loading(1);
  zeff::anim_set_zbd_path(path);
  install_anim_hooks();
  g_trace.clear(), g_calls = 0, g_fail_at = fail_at;
  const int32_t r = zeff::anim_read_zbd();
  zfix::options().bugs = bugs;
  zerr::set_sink(nullptr);
  std::vector<uint8_t> b;
  put_i32(b, r), put_i32(b, g_calls);
  put_i32(b, static_cast<int32_t>(g_reports.size() / 2));
  for (int32_t v : g_reports) put_i32(b, v);
  put_bytes(b, g_trace.data(), g_trace.size());
  if (full && r == 0) {
    dump_anim(b);
    dump_world(b, zcls::nodes().capacity);
  }
  return finish(b, out, cap);
}

}  // extern "C"

#include "gamez/zutil/zutl_msg.h"
#include "mech3/mech_skin.h"

namespace {
const char* const kSkinKeys[] = {"MSG_SKIN_PRIMARY",  "MSG_SKIN_VARIANTB", "MSG_SKIN_VARIANTC",
                                 "MSG_SKIN_VARIANTD", "MSG_SKIN_VARIANTE", "MSG_SKIN_VARIANTF",
                                 "MSG_SKIN_VARIANTG", "MSG_SKIN_VARIANTH"};
const char* const kSkinNames[] = {"GUNMETAL", "SUNFIRE", "DESERT", "WASP", "SLATE", "JUNGLE", "ARCTIC", "SPECTRE"};
int32_t g_fetches = 0;

void put_cstr(std::vector<uint8_t>& b, const char* s) {
  if (!s) return b.push_back(0xff);
  put_bytes(b, s, std::strlen(s) + 1);
}

void put_skin(std::vector<uint8_t>& b, const mech3::Skin* s) {
  b.push_back(s != nullptr);
  if (!s) return;
  put_cstr(b, s->name), b.push_back(s->loaded);
  put_i32(b, static_cast<int32_t>(s->entries.size()));
  for (const mech3::SkinEntry* e : s->entries) {
    put_cstr(b, e->name);
    b.push_back(e->alt == e->name);
    if (e->alt != e->name) put_cstr(b, e->alt);
    b.push_back(e->cached), b.push_back(e->damage == nullptr), b.push_back(e->b14);
    put_i32(b, e->refs), b.push_back(e->texture == nullptr);
  }
}
}  // namespace

extern "C" {

// The skins block of the mech reader on one dfn file (test_mechdef.py).
// `localized` installs the eight MSG_SKIN_* names; `damage_mode` sets
// 0x6061cc, with a texture fetch that fails for hole_8x8b.
int probe_skins(const char* path, long offset, int localized, int damage_mode, uint8_t* out, int cap) {
  if (probe_zrdr_load(path, offset) < 0) return -1;
  mech3::skin_globals() = mech3::SkinGlobals{};
  mech3::skin_globals().damage_mode = static_cast<uint8_t>(damage_mode);
  g_fetches = 0;
  mech3::skin_globals().fetch_texture = [](const char* name) -> void* {
    const int32_t k = g_fetches++;
    return std::strcmp(name, "hole_8x8b") == 0 ? nullptr : fake<void>(0x60000000u + 0x10u * k);
  };
  zutl::messages() = zutl::Messages{};
  if (localized) {
    zutl::messages().get_id = [](const char* name) -> int32_t {
      for (int i = 0; i < 8; ++i)
        if (std::strcmp(name, kSkinKeys[i]) == 0) return 0xf10 + i;
      return 0;
    };
    zutl::messages().get_text = [](int32_t id) { return kSkinNames[id - 0xf10]; };
  }
  const mech3::SkinSet* set = mech3::read_skins(&g_probe_root);
  if (damage_mode) set = mech3::read_skins(&g_probe_root);
  std::vector<uint8_t> b;
  b.push_back(set != nullptr);
  if (set) {
    put_i32(b, static_cast<int32_t>(set->skins.size()));
    for (const mech3::Skin* s : set->skins) put_skin(b, s);
    put_skin(b, set->damage);
  }
  const mech3::SkinGlobals& g = mech3::skin_globals();
  put_i32(b, g.entry_count), put_i32(b, g.sets_built), put_i32(b, static_cast<int32_t>(g.entries.size()));
  for (const mech3::SkinEntry* e : g.entries) put_cstr(b, e->name);
  put_i32(b, static_cast<int32_t>(g.holes.size()));
  for (void* h : g.holes) put_i32(b, static_cast<int32_t>(id(h)));
  return finish(b, out, cap);
}

}  // extern "C"

extern "C" {

// zClass node allocation: ops[i] = -1 allocates, k >= 0 frees slot k
// (test_zclass.py). Block and item chunk sizes are set small to grow often.
int probe_node_ops(const int32_t* ops, int count, int block_size, int chunk_size, int32_t* results,
                   uint8_t* out, int cap) {
  zcls::nodes() = zcls::NodePool{};
  zcls::nodes().block_size = block_size;
  zcls::lists() = zcls::Lists{};
  zcls::lists().chunk_size = chunk_size;
  for (int i = 0; i < count; ++i) {
    if (ops[i] < 0) {
      results[i] = node_index(zcls::node_alloc());
    } else {
      zcls::node_slot_free(zcls::node_at(ops[i]));
      results[i] = zcls::nodes().free_head;
    }
  }
  const zcls::NodePool& p = zcls::nodes();
  std::vector<uint8_t> b;
  put_i32(b, p.capacity), put_i32(b, p.live), put_i32(b, p.free_head);
  put_i32(b, static_cast<int32_t>(p.blocks.size()));
  for (int32_t i = 0; i < p.capacity; ++i) {
    const zcls::Node* n = zcls::node_at(i);
    put_bytes(b, n->raw, 0xd0), put_bytes(b, &n->link, 4);
  }
  const zcls::List& all = zcls::lists().list[zcls::kAllNodes];
  int32_t c = 0;
  for (zcls::ListItem* i = all.head; i; i = i->next) ++c;
  put_i32(b, c);
  for (zcls::ListItem* i = all.head; i; i = i->next) put_i32(b, node_index(i->node)), put_item(b, i);
  put_item(b, all.tail);
  int32_t free_count = 0;
  for (zcls::ListItem* i = zcls::lists().free; i; i = i->next) ++free_count;
  put_i32(b, free_count), put_item(b, zcls::lists().free);
  return finish(b, out, cap);
}

}  // extern "C"

#include "mech3/mech_names.h"
#include "mech3/mech_reader.h"

// The probes' MechDfn never touches configs\<mech>.mw3: the emulator stubs those out.
const bool g_no_saved_loadouts = (mech3::set_load_saved(nullptr), mech3::set_save_all(nullptr), true);

// The probes' MechDfn borrows g_probe_root, which its dtor must not free.
struct KeepTree {
  mech3::MechDfn& d;
  ~KeepTree() { d.reader = nullptr; }
};

extern "C" {

// The MechDfn constructor, then the firepoints and part-slot blocks of the
// mech reader on one dfn file (test_mechdef.py).
int probe_firepoints(const char* path, long offset, uint8_t* out, int cap) {
  if (probe_zrdr_load(path, offset) < 0) return -1;
  mech3::MechDfn d(7, &g_probe_root);
  KeepTree keep{d};
  mech3::read_firepoints(d);
  mech3::read_part_slots(d);
  uint8_t image[0x1a8];
  std::memcpy(image, d.raw, sizeof image);
  std::memcpy(image, &d.index, 4), image[4] = d.loaded;
  for (int i = 0; i < 18; ++i) {
    const mech3::PartSlot& s = d.parts[i];
    uint8_t* p = image + 0x28 + 0x10 * i;
    std::memset(p, 0, 4), std::memcpy(p + 4, &s.f4, 4), std::memcpy(p + 8, &s.i8, 4), std::memcpy(p + 12, &s.id, 4);
  }
  std::vector<uint8_t> b;
  put_bytes(b, image, sizeof image);
  for (const mech3::PartSlot& s : d.parts) put_cstr(b, s.name);
  put_i32(b, static_cast<int32_t>(d.firepoints->size()));
  for (const mech3::Firepoint* f : *d.firepoints) {
    put_cstr(b, f->name), put_bytes(b, &f->radius, 4), put_bytes(b, &f->share, 4), put_i32(b, f->part);
  }
  return finish(b, out, cap);
}

int probe_part_id(const char* name) { return mech3::part_id(name); }
const char* probe_part_name(int id) { return mech3::part_name(id); }

}  // extern "C"

namespace {
void put_node_graph(std::vector<uint8_t>& b) {
  const zcls::NodePool& p = zcls::nodes();
  put_i32(b, p.capacity), put_i32(b, p.live), put_i32(b, p.free_head);
  for (int32_t i = 0; i < p.capacity; ++i) {
    const zcls::Node* n = zcls::node_at(i);
    if (n->cls() == 0 && n->flags() == 0) continue;
    put_i32(b, i);
    put_raw(b, n->raw, 0xd0, {0x38, 0x3c, 0x58, 0x60, 0xcc});
    put_bytes(b, &n->link, 4);
    b.push_back(n->model != nullptr);
    for (int32_t k = 0; k < n->parent_count(); ++k) put_i32(b, node_index(n->parents[k]));
    for (int32_t k = 0; k < n->child_count(); ++k) put_i32(b, node_index(n->children[k]));
    put_item(b, n->changed);
    if (n->cls() == 5 && n->data) put_bytes(b, static_cast<zcls::Object3d*>(n->data)->raw, 0x90);
  }
  for (int list : {zcls::kAllNodes, zcls::kChanged}) {
    const zcls::List& l = zcls::lists().list[list];
    int32_t c = 0;
    for (zcls::ListItem* i = l.head; i; i = i->next) ++c;
    put_i32(b, c);
    for (zcls::ListItem* i = l.head; i; i = i->next) put_i32(b, node_index(i->node)), put_item(b, i), put_i32(b, i->removable);
    put_item(b, l.tail);
  }
}
}  // namespace

extern "C" {

// mechlib read_tree on one .flt entry at `offset` (test_mechlib.py).
int probe_mechlib_tree(const char* path, long offset, int frame, uint8_t* out, int cap) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  std::fseek(f, offset, SEEK_SET);
  zcls::nodes() = zcls::NodePool{};
  zcls::lists() = zcls::Lists{};
  zcls::frames() = zcls::Frames{static_cast<uint32_t>(frame), 0};
  zmod::models() = zmod::ModelPool{};
  zmod::reset_pool();
  zcls::TreeReader lib;
  zcls::Node* root = zcls::read_tree(lib, f, nullptr);
  const long end = std::ftell(f);
  std::fclose(f);
  std::vector<uint8_t> b;
  put_i32(b, static_cast<int32_t>(end - offset)), put_i32(b, node_index(root)), put_i32(b, lib.depth);
  put_i32(b, static_cast<int32_t>(zcls::frames().expiry));
  put_node_graph(b);
  return finish(b, out, cap);
}

}  // extern "C"

extern "C" {
int probe_mech_tech(int i) { return mech3::mech_tech(i); }
const char* probe_mech_name(int i) { return mech3::mech_name(i); }
int probe_tech_id(const char* s) { return mech3::tech_id(s); }
int probe_equip_kind(const char* s) { return mech3::equip_kind(s); }
int probe_weapon_index(const char* s) { return mech3::weapon_index(s); }
int probe_engine_index(int r) { return mech3::engine_index(r); }
int probe_armor_points(int tech, int ferro, float tons) { return mech3::armor_points(tech, ferro != 0, tons); }
}  // extern "C"

extern "C" {

// The MechDfn constructor and the head of the mech reader on one dfn file:
// the definition image, the result and the five values left for the loadouts.
int probe_dfn_head(const char* path, long offset, int index, uint8_t* out, int cap) {
  if (probe_zrdr_load(path, offset) < 0) return -1;
  mech3::MechDfn d(index, &g_probe_root);
  KeepTree keep{d};
  mech3::HeadValues h;
  const bool ok = mech3::read_head(d, &h);
  std::vector<uint8_t> b;
  b.push_back(ok);
  put_bytes(b, d.raw + 0xc, 0x10), put_bytes(b, d.raw + 0x148, 0x1a8 - 0x148);
  if (ok) {
    put_bytes(b, &h.half_turn_rate, 4), put_i32(b, h.tech), put_bytes(b, &h.accel_rate, 4);
    put_bytes(b, &h.decel_rate, 4), put_bytes(b, &h.turn_accel_factor, 4);
  }
  return finish(b, out, cap);
}

}  // extern "C"

#include "mech3/mech_config.h"

extern "C" {

// The mech reader from the head through the loadouts on one dfn file
// (test_mechdef.py): every Config and item, the firepoints, the reports.
static int dfn_configs(const char* path, long offset, int index, int localized, bool whole, uint8_t* out, int cap);
int probe_dfn_configs(const char* path, long offset, int index, int localized, uint8_t* out, int cap) {
  return dfn_configs(path, offset, index, localized, false, out, cap);
}
int probe_dfn_read(const char* path, long offset, int index, uint8_t* out, int cap) {
  return dfn_configs(path, offset, index, 0, true, out, cap);
}
}  // extern "C"

static int dfn_configs(const char* path, long offset, int index, int localized, bool whole, uint8_t* out, int cap) {
  if (probe_zrdr_load(path, offset) < 0) return -1;
  zutl::messages() = zutl::Messages{};
  if (localized) {
    zutl::messages().get_id = [](const char* name) -> int32_t {
      for (int i = 0; i < 8; ++i)
        if (std::strcmp(name, kSkinKeys[i]) == 0) return 0xf10 + i;
      return 0;
    };
    zutl::messages().get_text = [](int32_t id) { return kSkinNames[id - 0xf10]; };
  }
  mech3::skin_globals() = mech3::SkinGlobals{};
  g_reports.clear();
  zerr::set_sink(record_report);
  mech3::MechDfn d(index, &g_probe_root);
  KeepTree keep{d};
  mech3::HeadValues h;
  std::vector<uint8_t> b;
  if (whole) {
    b.push_back(mech3::read_dfn(d) ? 1 : 0);
    b.push_back(d.loaded);
  }
  const bool ok = whole || mech3::read_head(d, &h);
  if (whole) {
  } else if (ok) {
    d.skins = mech3::read_skins(d.reader);
    mech3::read_firepoints(d);
    mech3::read_part_slots(d);
    b.push_back(mech3::read_configurations(d, h));
  } else {
    b.push_back(0xff);
  }
  zerr::set_sink(nullptr);
  if (d.configs) {
    put_i32(b, d.configs->count), put_i32(b, d.configs->mech);
    put_i32(b, static_cast<int32_t>(d.configs->configs.size()));
    for (const mech3::Config* c : d.configs->configs) {
      put_cstr(b, c->name);
      for (uint8_t v : {c->base_flag, c->player_made, c->b1d, c->training, c->modified, c->endo_steel, c->xl,
                        c->ferro, c->double_sinks})
        b.push_back(v);
      put_i32(b, c->mech), put_i32(b, c->engine), put_bytes(b, &c->armor_tons, 4), put_i32(b, c->armor_tech);
      put_bytes(b, c->armor, sizeof c->armor);
      put_i32(b, c->sink_tech), put_i32(b, c->sinks);
      put_bytes(b, &c->turn_rate, 4), put_bytes(b, &c->accel_rate, 4), put_bytes(b, &c->decel_rate, 4);
      put_bytes(b, &c->turn_accel, 4);
      put_i32(b, c->item_count());
      for (const mech3::ConfigItem* i : c->items) {
        put_i32(b, i->kind()), put_i32(b, i->count), put_i32(b, i->group), put_i32(b, i->part), b.push_back(i->b10);
        if (i->kind() == 1) {
          const auto* w = static_cast<const mech3::WeaponItem*>(i);
          put_i32(b, w->weapon), put_i32(b, w->tech);
        } else if (i->kind() == 2) {
          put_i32(b, static_cast<const mech3::AmmoItem*>(i)->weapon);
        } else {
          const auto* e = static_cast<const mech3::EquipmentItem*>(i);
          put_i32(b, e->equipment), put_i32(b, e->tech);
        }
      }
    }
  } else {
    put_i32(b, -1);
  }
  put_i32(b, d.firepoints ? static_cast<int32_t>(d.firepoints->size()) : -1);
  if (d.firepoints)
    for (const mech3::Firepoint* f : *d.firepoints)
      put_cstr(b, f->name), put_bytes(b, &f->radius, 4), put_bytes(b, &f->share, 4), put_i32(b, f->part);
  put_i32(b, static_cast<int32_t>(g_reports.size() / 2));
  for (int32_t v : g_reports) put_i32(b, v);
  return finish(b, out, cap);
}


#include <unistd.h>

namespace {
mech3::WeaponLab g_labs[37];
uint8_t g_lab_present[37];

// One loaded config as test_mechsave.py dumps it from the original.
void put_config(std::vector<uint8_t>& b, const mech3::Config* c) {
  put_cstr(b, c->name);
  put_i32(b, c->mech);
  put_bytes(b, &c->g70, 4), put_bytes(b, &c->turn_rate, 4), put_bytes(b, &c->g78, 4);
  put_bytes(b, &c->accel_rate, 4), put_bytes(b, &c->decel_rate, 4), put_bytes(b, &c->turn_accel, 4);
  for (uint8_t v : {c->player_made, c->b1d, c->modified, c->endo_steel, c->ferro, c->xl, c->double_sinks}) b.push_back(v);
  put_bytes(b, &c->crc, 4), put_bytes(b, &c->armor_tons, 4), put_i32(b, c->armor_tech);
  put_bytes(b, c->armor, sizeof c->armor);
  put_i32(b, c->engine), put_i32(b, c->sink_tech), put_i32(b, c->sinks);
  put_i32(b, c->item_count());
  for (const mech3::ConfigItem* i : c->items) {
    put_i32(b, i->kind()), put_i32(b, i->count), put_i32(b, i->group), put_i32(b, i->part), b.push_back(i->b10);
    put_i32(b, i->id());
    if (i->kind() == 1) put_i32(b, static_cast<const mech3::WeaponItem*>(i)->tech);
    if (i->kind() == 3) put_i32(b, static_cast<const mech3::EquipmentItem*>(i)->tech);
    if (i->kind() == 4) b.push_back(static_cast<const mech3::EngineItem*>(i)->xl);
  }
}
}  // namespace

extern "C" {

// The weapons' mech-lab blocks the test hands both sides; absent ones are 0.
void probe_set_weapon_labs(const float* table, const uint8_t* present) {
  std::memcpy(g_labs, table, sizeof g_labs);
  std::memcpy(g_lab_present, present, sizeof g_lab_present);
  mech3::set_weapon_lab([](const char* name) -> const mech3::WeaponLab* {
    for (int i = 0; i < 37; ++i)
      if (std::strcmp(mech3::weapon_name(i), name) == 0) return g_lab_present[i] ? &g_labs[i] : nullptr;
    return nullptr;
  });
}

// Config(-1, name) + Load on one saved config, then the checksum, each weight,
// and Validate's other checks (test_mechsave.py).
int probe_config_blob(const char* name, const uint8_t* blob, int n, uint8_t* out, int cap) {
  std::FILE* f = std::tmpfile();
  std::fwrite(blob, 1, static_cast<size_t>(n), f);
  std::rewind(f);
  mech3::Config c(-1, name);
  std::vector<uint8_t> b;
  b.push_back(c.load(f));
  put_i32(b, static_cast<int32_t>(std::ftell(f)));
  std::fclose(f);
  put_config(b, &c);
  const uint32_t crc = c.checksum();
  put_bytes(b, &crc, 4);
  for (int32_t k = -1; k <= 8; ++k) {
    const auto w = static_cast<float>(c.weight(k));
    put_bytes(b, &w, 4);
  }
  b.push_back(c.unplaced_item());
  put_i32(b, c.jump_jets()), put_i32(b, c.jump_jet_limit());
  b.push_back(c.validate(false));
  for (int32_t v : {c.slots_left(), c.ferro_slots(), c.endo_slots(), c.engine_slots(), c.sink_limit(), c.sink_items()})
    put_i32(b, v);
  const size_t extras = b.size();
  put_i32(b, c.weapon_count());  // the mech lab's counts (0x00453840's)
  for (int32_t kind = 0; kind < 15; ++kind)
    for (int32_t tech = 1; tech <= 3; ++tech) put_i32(b, c.equipment_count(kind, tech));
  const float tons = c.armor_tons;
  for (float t : {tons, tons + 0.5f, tons - 0.5f, tons + 5.0f, std::max(tons - 5.0f, 0.0f), 0.0f, 40.0f}) {
    c.armor_tons = t;
    c.spread_armor(c.mech);  // 0x004247a0
    put_bytes(b, c.armor, sizeof c.armor);
  }
  std::vector<mech3::ConfigItem*> its(c.items.begin(), c.items.end());
  if (its.size() > 12) its.resize(12);
  for (mech3::ConfigItem* it : its) {  // 0x00471de0, one of each in turn
    b.push_back(c.remove(it, 1));
    put_bytes(b, &c.armor_tons, 4), put_i32(b, c.armor_tech), b.push_back(c.ferro);
    put_i32(b, c.sink_tech), put_i32(b, c.sinks), b.push_back(c.double_sinks);
  }
  put_i32(b, static_cast<int32_t>(c.items.size()));
  put_i32(b, static_cast<int32_t>(b.size() - extras));  // their length, last
  return finish(b, out, cap);
}

// Config::unpacked (0x004259c0) on another player's packed loadout, then
// its weight and Validate (test_config_unpack.py).
int probe_config_unpack(const uint32_t* p, int words, uint8_t* out, int cap) {
  mech3::Config* c = mech3::Config::unpacked(p, static_cast<size_t>(words));
  std::vector<uint8_t> b;
  put_config(b, c);
  const auto w = static_cast<float>(c->weight(-1));
  put_bytes(b, &w, 4);
  b.push_back(c->validate(false));
  delete c;
  return finish(b, out, cap);
}

// Config(-1, name) + Load, then Config::copy_of as the slot copy makes it,
// over stand-in dfns whose firepoints' parts test_mechsave.py's fake_parts gives.
int probe_config_copy(const char* name, const uint8_t* blob, int n, uint8_t* out, int cap) {
  static std::vector<mech3::Firepoint> fps[19];
  static std::vector<mech3::Firepoint*> ptrs[19];
  static mech3::MechDfn* dfns[19] = {};
  for (int m = 0; m < 19; ++m) {
    if (!dfns[m]) {
      for (int i = 0; i < 8; ++i) fps[m].push_back(mech3::Firepoint{nullptr, 0, 0, ((i * 5 + m) % 10) - 1});
      for (auto& f : fps[m]) ptrs[m].push_back(&f);
      dfns[m] = static_cast<mech3::MechDfn*>(::operator new(sizeof(mech3::MechDfn)));
      std::memset(static_cast<void*>(dfns[m]), 0, sizeof(mech3::MechDfn));
      dfns[m]->firepoints = &ptrs[m];
    }
    mech3::mech_dfn_slot(m) = dfns[m];
  }
  std::FILE* f = std::tmpfile();
  std::fwrite(blob, 1, static_cast<size_t>(n), f);
  std::rewind(f);
  mech3::Config c(-1, name);
  c.load(f);
  std::fclose(f);
  mech3::Config* d = mech3::Config::copy_of(c.name, c);
  std::vector<uint8_t> b;
  put_config(b, d);
  put_i32(b, d->free_slots);
  for (int32_t v : d->free_in) put_i32(b, v);
  delete d;
  for (int m = 0; m < 19; ++m) mech3::mech_dfn_slot(m) = nullptr;
  return finish(b, out, cap);
}

// ConfigList(mech, 0).load_saved() with the working directory at `dir`.
int probe_load_saved(const char* dir, int mech, uint8_t* out, int cap) {
  char was[1024];
  if (!getcwd(was, sizeof was) || chdir(dir) != 0) return -1;
  mech3::ConfigList list(mech, 0);
  std::vector<uint8_t> b;
  b.push_back(list.load_saved());
  if (chdir(was) != 0) return -1;
  put_i32(b, static_cast<int32_t>(list.configs.size()));
  for (const mech3::Config* c : list.configs) put_config(b, c);
  return finish(b, out, cap);
}

// ConfigList(mech, 0): load_saved(), then save_all() over the same file.
int probe_load_and_save(const char* dir, int mech) {
  char was[1024];
  if (!getcwd(was, sizeof was) || chdir(dir) != 0) return -1;
  mech3::ConfigList list(mech, 0);
  list.load_saved();
  const bool ok = list.save_all();
  if (chdir(was) != 0) return -1;
  return ok;
}

}  // extern "C"
