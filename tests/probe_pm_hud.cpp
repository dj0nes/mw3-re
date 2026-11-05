#include "gamez/zvideo/zvid_format.h"
#include "gamez/zmath/zmth_vector.h"
#include <cmath>
#include <cstdlib>
#include <new>
// Probes for test_pm_hud_objectives.py: PM's objectives box (HudObjectives::fill,
// src/mech3/hud.cpp on the pm branch) on a made-up mission, its lines read back.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "gamez/ztwod/twod_host.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_msg.h"
#include "mech3/controls_cfg.h"
#include "mech3/hud.h"
#include "mech3/mech_objective.h"

namespace {
std::map<int32_t, std::string> g_msgs;
const char* get_text(int32_t id) {
  auto it = g_msgs.find(id);
  return it == g_msgs.end() ? nullptr : it->second.c_str();
}

// zTwoD's host: a font by its name's index, a fixed glyph size.
struct Host : ztwod::Host {
  std::vector<std::string> fonts;
  // The radar test's: the static's noisy circle and lines draw nothing, as the
  // exe's draws are stubbed there (they take rand; their own tests are zrndr's).
  void circle_noisy(int32_t, int32_t, int32_t, uint32_t, float, const ztwod::Rect*) override {}
  void line_noisy(int32_t, int32_t, int32_t, int32_t, uint32_t, float, const ztwod::Rect*) override {}
  ztwod::Handle font_find(const char* name) override {
    for (size_t i = 0; i < fonts.size(); ++i)
      if (fonts[i] == name) return static_cast<ztwod::Handle>(0x100 + i);
    fonts.push_back(name);
    return static_cast<ztwod::Handle>(0x100 + fonts.size() - 1);
  }
  ztwod::NamedFont font_fields(ztwod::Handle) override { return {}; }
  void font_addref(ztwod::Handle) override {}
  void font_release(ztwod::Handle) override {}
  void font_measure(const char* text, ztwod::Handle, int32_t* w, int32_t* h) override {
    *w = 7 * static_cast<int32_t>(std::strlen(text));
    *h = 14;
  }
  // Just enough GDI for a line's height: 14 a line, 7 a character.
  ztwod::Handle CreateCompatibleDC(ztwod::Handle) override { return 1; }
  ztwod::Handle SelectObject(ztwod::Handle, ztwod::Handle) override { return 0; }
  void DeleteDC(ztwod::Handle) override {}
  int32_t DrawTextA(ztwod::Handle, const char* text, int32_t, ztwod::Rect* r, uint32_t) override {
    r->left = r->top = 0;
    r->right = 7 * static_cast<int32_t>(std::strlen(text));
    r->bottom = 14;
    return 1;
  }
  ztwod::Handle image_dc(zimg::Texture*) override { return 0; }
  uint32_t unported(const char*, std::initializer_list<zgame::Arg>) override { return 0; }
};

struct Hooks : mech3::HudHooks {
  ObjectivesSource src;
  bool multiplayer = false;
  std::vector<std::string> scores;
  ObjectivesSource objectives_source() override { return src; }
  const char* message(int32_t id) override { return get_text(id) ? get_text(id) : ""; }
  std::string command_key(int32_t cmd) override { return "K" + std::to_string(cmd); }
  uint32_t multiplayer_game() override { return multiplayer ? 1 : 0; }
  std::vector<std::string> multiplayer_scores() override { return scores; }
  const char* nav_name(uint32_t) override { return ""; }
  uint32_t unported(const char*, std::initializer_list<zgame::Arg>) override { return 0; }
};

std::string unescape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') o += '\n', ++i;
    else o += s[i];
  }
  return o;
}
std::vector<std::string> fields(const std::string& l) {
  std::vector<std::string> f;
  std::stringstream ss(l);
  std::string x;
  while (std::getline(ss, x, '|')) f.push_back(x);
  return f;
}
std::string g_out;
}  // namespace

extern "C" {

void probe_pm_obj_msg(int id, const char* text) { g_msgs[id] = text; }

// The mission, one line each ('|' between fields):
//   G|type|mission|name|aborting|multiplayer   D|desc
//   S|score line                               P|S|F|X|group's text|done
//   I|done|failed|visible|text (\n for a line break), into the last group
// Returns the box's lines (text and font id), then the scores', then the
// panel's height over the box's line height.
const char* probe_pm_obj_fill(const char* spec) {
  zfix::options().bugs = true;  // the 0x236 line (FIX (bug)): the test's stub passes the mission as %2 too
  zutl::messages().get_text = get_text;
  // PM's command numbers, as the exe pushes them (its table is not ported yet).
  mech3::controls_setup().names["CMD_MISSION_COMPLETE"] = 0x79;
  mech3::controls_setup().names["CMD_OBJECTIVES"] = 0x44;
  static Host host;
  ztwod::set_host(&host);
  static Hooks hooks;
  hooks = Hooks{};
  mech3::set_hud_hooks(&hooks);
  auto obj = std::make_unique<mech3::Objectives>();
  std::unique_ptr<mech3::ObjectiveGroup>* group = nullptr;
  std::stringstream in(spec);
  std::string line;
  while (std::getline(in, line)) {
    std::vector<std::string> f = fields(line);
    if (f.empty()) continue;
    if (f[0] == "G") {
      hooks.src.game_type = std::stoi(f[1]);
      hooks.src.mission = std::stoi(f[2]);
      static std::string name;
      name = f[3];
      hooks.src.game_name = name.c_str();
      hooks.src.aborting = static_cast<uint8_t>(std::stoi(f[4]));
      hooks.multiplayer = std::stoi(f[5]) != 0;
    } else if (f[0] == "D") {
      obj->desc = f[1];
    } else if (f[0] == "S") {
      hooks.scores.push_back(f[1]);
    } else if (f[0] == "P" || f[0] == "Q" || f[0] == "F" || f[0] == "X") {
      group = f[0] == "P" ? &obj->primary : f[0] == "Q" ? &obj->secondary : f[0] == "F" ? &obj->final : &obj->fail;
      *group = std::make_unique<mech3::ObjectiveGroup>(f[1].empty() ? nullptr : f[1].c_str());
      (*group)->done = static_cast<uint8_t>(std::stoi(f[2]));
    } else if (f[0] == "I" && group) {
      const std::string t = unescape(f.size() > 4 ? f[4] : "");
      auto it = std::make_unique<mech3::Objective>(mech3::Objective::kGroup, t.empty() ? nullptr : t.c_str());
      it->done = static_cast<uint8_t>(std::stoi(f[1]));
      it->failed = static_cast<uint8_t>(std::stoi(f[2]));
      it->visible = static_cast<uint8_t>(std::stoi(f[3]));
      (*group)->items.push_back(std::move(it));
    }
  }
  hooks.src.objectives = obj.get();
  auto box = std::make_unique<mech3::HudObjectives>();
  box->set_text_plain("Ay");  // the box's own text, for its line height (0x005a1380): 14
  box->fill();
  std::ostringstream o;
  auto dump = [&](ztwod::TextBox& b, const char* tag) {
    for (int32_t i = 0; i <= b.current && static_cast<size_t>(i) < b.lines.size(); ++i)
      o << tag << " " << b.lines[static_cast<size_t>(i)].named << " " << b.lines[static_cast<size_t>(i)].text << "\n";
  };
  dump(*box, "box");
  dump(box->scores, "scores");
  const int32_t h = box->text_height();
  o << "panel " << (box->panel.rect.right - box->panel.rect.left) << " "
    << (h ? (box->panel.rect.bottom - box->panel.rect.top) / h : -1) << "\n";
  for (size_t i = 0; i < host.fonts.size(); ++i) o << "font " << (0x100 + i) << " " << host.fonts[i] << "\n";
  g_out = o.str();
  mech3::set_hud_hooks(nullptr);
  ztwod::set_host(nullptr);
  return g_out.c_str();
}

}  // extern "C"

// test_pm_hud_static.py: PM's static (0x0043dda0, 0x0043dfa0, 0x0043d0b0, 0x0043e1f0).
#include "crt/msvc_rand.h"
extern "C" {

// The noise from `seed`: which green each pixel took (0..2) and its alpha.
void probe_pm_static_noise(uint32_t seed, uint8_t* pick, uint8_t* alpha) {
  crt::msvc_srand(seed);
  mech3::HudStaticNoise* n = mech3::static_noise_make();
  std::memcpy(pick, n->pick.data(), n->pick.size());
  std::memcpy(alpha, n->alpha.data(), n->alpha.size());
  mech3::static_noise_free(n);
}

// A quad over the rect from `seed`: 4 vertices of 8 floats.
void probe_pm_static_quad(uint32_t seed, int l, int t, int r, int b, float* out) {
  crt::msvc_srand(seed);
  float q[4][8];
  mech3::static_quad_vertices(ztwod::Rect{l, t, r, b}, q);
  std::memcpy(out, q, sizeof q);
}

// `n` frames of the tick's channels (the panels not drawn): the channels in
// and out (12 floats), per frame on, health and dt; the radar's fade after each
// frame (as the exe's 0x0043f190 last set it: the add's, then the end's).
void probe_pm_static_frames(uint32_t seed, int per_second, float* channels, int n, const int* on, const float* health,
                            const float* dt, float* fade) {
  crt::msvc_srand(seed);
  zfix::options().per_second = per_second != 0;
  mech3::HudStaticChannel c[4];
  std::memcpy(c, channels, sizeof c);
  for (int i = 0; i < n; ++i) {
    if (!mech3::static_warn(c, on[i] != 0, health[i], dt[i])) {
      fade[i] = 0.0f;
      continue;
    }
    mech3::static_ease(c, dt[i]);
    fade[i] = c[3].level;
  }
  std::memcpy(channels, c, sizeof c);
  zfix::options().per_second = true;
}

}  // extern "C"

// test_pm_hud_radar.py: PM's radar (0x0043f220) on one frame, off the map and
// without contacts (stage 1). In: the radar's state; out: what it left.
struct RadarIn {
  int32_t changed, zoomed, cx[2], cy[2], r[2], to_r, to_x, to_y, r_steps, x_steps, y_steps, radius, x, y;
  float range;
  int32_t bc, light_current, hud_active, hidden;
  float noise, dir[3], cos_half, sin_half, dt;
  int32_t start[3][2];  // text1, text2, the light: where they are
  // Stage 2: the map, the mech, the contacts, the network marker.
  int32_t active, map_h, map_rect[4];
  float map_scale, me[3], q[4];
  int32_t c3, text2_hidden, seed, target, marker, n;
  float marker_pos[3];
  struct Contact {
    int32_t team, on_radar, blip, has_mech, slave;
    float sensor, pos[3], dist;
  } contacts[8];
};
struct RadarOut {
  int32_t radius, radius_sq, x, y, text1_x, text1_y, text2_x, text2_y, text2_shown, light_x, light_y, light_current;
  int32_t lines[2][4], changed, hidden;
  char text1[32];
  int32_t n_events, events[64][5];  // kind, a, b, c, d: 0 ring, 1 triangle, 2 text filled, 3 blip placed, 4 blip drawn
};
namespace {
struct RadarHooks : Hooks {
  RadarIn in{};
  RadarOut* out = nullptr;
  ztwod::Rect rect{};
  char map_obj = 0, marker_obj = 0, target_slot = 0;
  void event(int32_t k, int32_t a, int32_t b, int32_t c, uint32_t d) {
    if (out->n_events < 64) {
      int32_t* e = out->events[out->n_events++];
      e[0] = k, e[1] = a, e[2] = b, e[3] = c, e[4] = static_cast<int32_t>(d);
    }
  }
  // A contact, its vehicle, its mech and its blip are all the record's address.
  int index(void* p) const { return static_cast<int>(static_cast<RadarIn::Contact*>(p) - in.contacts); }
  float mech_sensor_range(mech3::Mech* m) override {
    void* p = m;
    return p >= in.contacts && p < in.contacts + 8 ? in.contacts[index(p)].sensor : in.range;
  }
  uint8_t mech_bc(mech3::Mech*) override { return static_cast<uint8_t>(in.bc); }
  void mech_orientation(mech3::Mech*, float q[4]) override { std::memcpy(q, in.q, sizeof in.q); }
  void mech_position(mech3::Mech*, float o[3]) override { std::memcpy(o, in.me, sizeof in.me); }
  zrdr::Node* load_hud_zrd() override { return nullptr; }  // no hud.zrd: the pieces keep their own places
  void* radar_map() override { return in.active ? &map_obj : nullptr; }
  const ztwod::Rect* map_rect(void*) override {
    rect = {in.map_rect[0], in.map_rect[1], in.map_rect[2], in.map_rect[3]};
    return &rect;
  }
  float map_scale(void*) override { return in.map_scale; }
  int32_t map_image_height(void*) override { return in.map_h; }
  bool map_point(void*, const float p[3], int32_t o[2]) override {
    o[0] = static_cast<int32_t>(static_cast<double>(p[0]) * 0.1) + 100;
    o[1] = static_cast<int32_t>(static_cast<double>(p[2]) * 0.1) + 200;
    return true;
  }
  void map_update(void*, float) override {}
  int32_t mech_contact_count(mech3::Mech*) override { return in.n; }
  void* mech_contact(mech3::Mech*, int32_t i) override { return &in.contacts[i]; }
  void* contact_vehicle(void* c) override { return c; }
  void* slot_contact(void*) override { return &in.contacts[in.target]; }
  bool contact_same(void*, void*) override { return false; }
  bool contact_on_radar(void* c) override { return in.contacts[index(c)].on_radar != 0; }
  float contact_distance(void* c) override { return in.contacts[index(c)].dist; }
  void object_position(void* o, float p[3]) override { std::memcpy(p, in.contacts[index(o)].pos, 12); }
  uint32_t object_team(void* o) override { return static_cast<uint32_t>(in.contacts[index(o)].team); }
  uint32_t mech_allies(mech3::Mech*) override { return 1; }
  uint32_t mech_enemies(mech3::Mech*) override { return 2; }
  void* object_blip(void* o, int32_t) override { return in.contacts[index(o)].blip ? o : nullptr; }
  void blip_place(void* b, int32_t x, int32_t y) override { event(3, b == &marker_obj ? 100 : index(b), x, y, 0); }
  void blip_draw(void* b) override { event(4, b == &marker_obj ? 100 : index(b), 0, 0, 0); }
  void* vehicle_mech(void* o) override { return in.contacts[index(o)].has_mech ? o : nullptr; }
  int32_t mech_c3(void*) override { return in.c3; }
  int32_t mech_c3_slave(void* m) override { return in.contacts[index(m)].slave; }
  void radar_test(int32_t k, int32_t a, int32_t b, int32_t c, uint32_t d) override { event(k, a, b, c, d); }
  float normalize(float v[3]) override {
    zmth::Vec3 d{v[0], v[1], v[2]};  // the program's (app_hud.cpp)
    const float len = zmth::normalize(d);
    v[0] = d.x, v[1] = d.y, v[2] = d.z;
    return len;
  }
  void* network_marker() override { return in.marker ? &marker_obj : nullptr; }
  void* marker_image(void*) override { return &marker_obj; }
  void marker_position(void*, float o[3]) override { std::memcpy(o, in.marker_pos, 12); }
  const char* marker_name(void*) override { return "op"; }
  bool radar_shot(int32_t, float*, bool*) override { return false; }
  int32_t mine_count() override { return 0; }
};
}  // namespace

extern "C" uint32_t probe_pm_pack(int r, int g, int b) { return zvid::pack_bytes(r, g, b); }

extern "C" void probe_pm_radar_run(const RadarIn* in, RadarOut* out) {
  static Host host;
  ztwod::set_host(&host);
  static RadarHooks hooks;
  hooks.in = *in;
  hooks.out = out;
  out->n_events = 0;
  crt::msvc_srand(static_cast<uint32_t>(in->seed));
  mech3::set_hud_hooks(&hooks);
  // One HUD for every run, in zeroed memory: with fixes off and no hud.zrd,
  // the constructor reads Hud::block unset (the original's, hud.h).
  static mech3::Hud* hud = new (std::calloc(1, sizeof(mech3::Hud))) mech3::Hud(nullptr);
  static char fake_mech[0x800];
  hud->mech = reinterpret_cast<mech3::Mech*>(fake_mech);
  hud->active = static_cast<uint8_t>(in->hud_active);
  mech3::HudRadar& r = hud->radar;
  r.changed = static_cast<uint8_t>(in->changed);
  r.active = static_cast<uint8_t>(in->active);
  hud->target.target = in->target >= 0 ? reinterpret_cast<decltype(hud->target.target)>(&hooks.target_slot) : nullptr;
  r.zoomed = static_cast<uint16_t>(in->zoomed);
  for (int i = 0; i < 2; ++i) r.cx[i] = in->cx[i], r.cy[i] = in->cy[i], r.r[i] = in->r[i];
  r.to_r = in->to_r, r.to_x = in->to_x, r.to_y = in->to_y;
  r.r_steps = in->r_steps, r.x_steps = in->x_steps, r.y_steps = in->y_steps;
  r.radius = in->radius, r.radius_sq = in->radius * in->radius;
  r.x = in->x, r.y = in->y;
  r.light.current = in->light_current;
  r.noise = in->noise;
  for (int i = 0; i < 3; ++i) r.dir[i] = in->dir[i];
  r.cos_half = in->cos_half, r.sin_half = in->sin_half;
  if (in->hidden) r.flags |= ztwod::kHidden;
  else r.flags &= ~ztwod::kHidden;
  if (in->text2_hidden) r.text2.flags |= ztwod::kHidden;
  else r.text2.flags &= ~ztwod::kHidden;
  r.text1.set_pos(in->start[0][0], in->start[0][1]);
  r.text2.set_pos(in->start[1][0], in->start[1][1]);
  r.light.set_pos(in->start[2][0], in->start[2][1]);
  for (int i = 0; i < 2; ++i) r.lines[i].x = r.lines[i].y = r.lines[i].end_x = r.lines[i].end_y = 0;  // the reused HUD's last run
  r.text1.set_text("%s", "");
  r.update(in->dt);
  out->radius = r.radius, out->radius_sq = r.radius_sq, out->x = r.get_x(), out->y = r.get_y();
  out->text1_x = r.text1.get_x(), out->text1_y = r.text1.get_y();
  out->text2_x = r.text2.get_x(), out->text2_y = r.text2.get_y();
  out->text2_shown = (r.text2.flags & ztwod::kHidden) ? 0 : 1;
  out->light_x = r.light.get_x(), out->light_y = r.light.get_y(), out->light_current = r.light.current;
  for (int i = 0; i < 2; ++i)
    out->lines[i][0] = r.lines[i].x, out->lines[i][1] = r.lines[i].y, out->lines[i][2] = r.lines[i].end_x,
    out->lines[i][3] = r.lines[i].end_y;
  out->changed = r.changed, out->hidden = (r.flags & ztwod::kHidden) ? 1 : 0;
  std::snprintf(out->text1, sizeof out->text1, "%s", r.text1.text);
  mech3::set_hud_hooks(nullptr);
  ztwod::set_host(nullptr);
}
