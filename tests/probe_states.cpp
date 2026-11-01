// Probes for test_app_states.py and test_app_init.py: the game's app and its
// states (mech3/app, mech3/app_states) with every call outside recorded.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include "gamez/zerror/zerr.h"
#include "mech3/app.h"
#include "mech3/app_states.h"
#include "mech3/controls_cfg.h"
#include "mech3/menu_states.h"
#include "mech3/options.h"
#include "probe_app.h"

namespace {
using probe_app::call;
using zgame::Arg;
using zgame::Handle;

struct Exit : std::runtime_error {
  Exit() : std::runtime_error("ExitProcess") {}
};

// Strings handed back by hooks that return text: what the Python side wrote
// into the emulator for the same answer.
std::deque<std::string> g_strings;
const char* text(const char* tag, uint32_t v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%s%08x", tag, v);
  g_strings.emplace_back(buf);
  if (g_strings.size() > 256) g_strings.pop_front();
  return g_strings.back().c_str();
}
std::string g_command_line;

struct Recording : mech3::Engine {
  bool software_renderer() override { return true; }  // the original's start-up
  uint32_t unported(const char* key, std::initializer_list<Arg> args) override { return call(key, args); }
  void timer_update() override { call("0x00575a00", {}); }
  float frame_dt() override { return zgame::from_bits(peek32(0x72c52c)); }
  // The loss and the cheat flag as the original reads them, the globals the
  // test pokes ([0x00609926], [0x00609994]); the program reads them from the
  // ported Missions and Options (0x0047ac20, campaign.md section 14).
  bool mission_failed() override { return peek8(0x609926) != 0; }
  bool mission_cheated() override { return peek8(0x609994) != 0; }
  int32_t in_game_vmode() override { return static_cast<int32_t>(peek32(peek32(0x5f4d10))); }
  float mouse_accel_x() override { return zgame::from_bits(peek32(peek32(0x5f4d2c))); }
  float mouse_accel_y() override { return zgame::from_bits(peek32(peek32(0x5f4d30))); }
  int32_t hw_card_flag() override { return static_cast<int32_t>(call("0x0040d4d0", {})); }
  int32_t hw_card_dev() override { return static_cast<int32_t>(call("0x0040cc10", {})); }
  void select_hw(int32_t v) override { call("0x0040d4c0", {v}); }
  void set_hw_card_dev(int32_t v) override { call("0x0040cbf0", {v}); }
  int32_t vmode() override { return static_cast<int32_t>(call("0x0040d9d0", {})); }
  void set_vmode(int32_t v) override { call("0x0040d810", {v}); }
  int32_t gfx_flags() override { return static_cast<int32_t>(call("0x0040d480", {})); }
  int32_t pixel_double() override { return static_cast<int32_t>(call("0x0040d4b0", {})); }
  Handle render_rect() override { return call("0x0040d6a0", {}); }
  Handle display_rect() override { return call("0x0040d750", {}); }
  Handle window_rect() override { return call("0x0040d7b0", {}); }
  Handle display_depth() override { return call("0x0040d790", {}); }
  Handle vstride() override { return call("0x0040d7a0", {}); }
  int32_t window_h() override { return static_cast<int32_t>(call("0x0040d7c0", {})); }
  void set_use_joystick(int32_t v) override { call("0x0040d070", {v}); }
  void set_fast_menu(int32_t v) override { call("0x0040ce90", {v}); }
  int32_t exchange_fast_menu(int32_t v) override { return static_cast<int32_t>(call("0x0040ce90", {v})); }
  void options_apply_all() override { call("0x0040cb30", {}); }
  void light_init() override { call("0x00542bb0", {}); }
  // The screen mode switch and the cockpits' refit as calls; the cases for
  // 0x004ab6b0 and 0x0041cdc0 run the ports (Engine::...).
  int32_t set_screen_mode(int32_t m) override { return static_cast<int32_t>(call("0x004ab6b0", {m})); }
  void cockpit_refit(Handle m) override { call("0x0041cdc0", {m}); }
  void set_in_game_vmode(int32_t v) override { poke32(peek32(0x5f4d10), static_cast<uint32_t>(v)); }
  void update_shadows() override { call("0x004814e0", {}); }
  void exit_process(uint32_t code) override {
    call("ExitProcess", {code});
    throw Exit();
  }
  void cursor_pos(int32_t* x, int32_t* y) override {
    call("GetCursorPos", {});
    *x = static_cast<int32_t>(call("@cursor_x", {}));
    *y = static_cast<int32_t>(call("@cursor_y", {}));
  }
  uint32_t rand() override { return call("rand", {}); }
  const char* command_line() override {
    call("GetCommandLineA", {});
    return g_command_line.c_str();
  }
  void print(const char* format, const char* arg) override {
    if (arg) call("printf", {format, arg});
    else call("printf", {format});
  }
  const char* message(int32_t id) override { return text("msg", call("0x00576040", {id})); }
  const char* message_format(int32_t id, const char* arg) override {
    return text("fmt", call("0x00575fd0", {id, arg}));
  }
  const char* version_string() override { return text("ver", call("0x004af280", {})); }
  const char* version_number() override { return text("num", call("0x004af290", {})); }
  const char* mission_dir(uint32_t n) override { return text("dir", call("0x00484c50", {n})); }
  void cd_pause() override { call("0x00572c90", {}); }
  void cd_resume(int32_t mode) override { call("0x00572d70", {mode}); }
  void cd_stop() override { call("0x00572c30", {}); }
  int32_t cd_track_count() override { return static_cast<int32_t>(call("0x00573040", {})); }
  void music_play(int32_t track, int32_t mode) override { call("0x004ac290", {track, mode}); }
  const char* pilot_name() override {
    const Handle p = pilot();
    if (!p) return nullptr;
    return call("@pilot_is_default", {}) ? "DefaultPlayer" : "Someone";
  }
  uint32_t winsock_check(int32_t* major, int32_t* minor) override {
    return call("0x00575e30", {*major, *minor});
  }
  uint32_t video_card_memory(int32_t dev, uint32_t* total) override {
    const uint32_t r = call("[0x0072f38c]", {dev});
    *total = call("@video_total", {});
    return r;
  }
};

Recording* g_engine = nullptr;
mech3::Mech3App* g_app = nullptr;

// The ported fields, by the original's address (size 1 or 4; kind 1 holds a
// state, as the original's address; kind 2 the Core renderer as its vtable).
struct Field {
  uint32_t addr;
  int size, kind;
  void* p;
};
std::vector<Field> g_fields;

uint32_t state_addr(const zgame::State* s) {
  const auto* m = dynamic_cast<const mech3::State*>(s);
  return m ? m->addr() : 0;
}
zgame::State* state_at(uint32_t addr) {
  for (int n = 0; n <= 10; ++n)
    if (auto* s = dynamic_cast<mech3::State*>(g_app->state(n)); s && s->addr() == addr) return s;
  return nullptr;
}

// The heap menu objects (menu_states.h) at the addresses the test gives them.
std::vector<mech3::MenuState*> g_menus;
mech3::MenuState* g_this = nullptr;

void build_fields() {
  using namespace mech3;
  g_fields.clear();
  Mech3App& a = *g_app;
  States& s = states();
  const uint32_t A = 0x608c88;
  auto f = [](uint32_t addr, int size, void* p, int kind = 0) { g_fields.push_back({addr, size, kind, p}); };
  f(A + 0x20, 4, &a.frame_);
  f(A + 0x6c, 4, &a.cmd_show_);
  f(A + 0xc4, 4, &a.first_state_, 1);
  f(A + 0x18c, 1, &a.mission_started_);
  f(A + 0x18d, 1, &a.lobby_launch_);
  f(A + 0x18e, 1, &a.lobby_flow_);
  f(A + 0x190, 4, &a.input_mode_);
  f(A + 0x194, 1, &a.field_194_);
  f(A + 0x19c, 1, &a.lobby_);
  f(A + 0x1a4, 1, &a.field_1a4_);
  for (int off = 0x1ac; off < 0x1dc; ++off) {
    if (off == 0x1bd || off == 0x1c4 || off == 0x1c5 || (off >= 0x1cc && off < 0x1d8)) continue;
    f(A + off, 1, &a.byte_at(off));
  }
  Options& o = options();
  f(A + 0x1bd, 1, &o.multiplayer);
  f(A + 0x1c4, 1, &o.intensity_flag[0]);
  f(A + 0x1c5, 1, &o.intensity_flag[1]);
  f(A + 0x1cc, 4, &o.intensity[0]);
  f(A + 0x1d0, 4, &o.intensity[1]);
  f(A + 0x1d4, 4, &o.intensity[2]);
  f(A + 0x1dc, 4, &a.post_intro_, 1);
  f(A + 0x1e0, 4, &a.field_1e0_);
  f(A + 0x1e4, 4, &a.field_1e4_);
  AppGlobals& g = globals();
  f(0x608f6c, 1, &g.class_registered);
  f(0x608f74, 4, &g.debug_view);
  f(0x608f78, 4, &g.last_vmode);
  f(0x608f80, 4, &g.main_hwnd);
  f(0x6040b0, 4, &g.script);
  f(0x608e90 + 0x8, 4, &s.attract.movie_);
  f(0x608f58 + 0x8, 4, &s.intro.intro_);
  f(0x608f58 + 0xc, 4, &s.intro.splash_);
  CoreState& c = s.core;
  f(0x608ec0 + 0x8, 4, &c.window_rect_);
  f(0x608ec0 + 0xc, 4, &c.display_rect_);
  f(0x608ec0 + 0x10, 4, &c.render_rect_);
  f(0x608ec0 + 0x14, 4, &c.elapsed_);
  f(0x608ec0 + 0x18, 1, &c.paused_);
  f(0x608ec0 + 0x19, 1, &c.active_);
  f(0x608ec0 + 0x1c, 4, &c.renderer_, 2);
  f(0x608ec0 + 0x20, 4, &c.message_box_);
  f(0x608ea0 + 0x18, 4, &s.key_list.list_);
  for (ShellState* sh : std::initializer_list<ShellState*>{&s.main, &s.pause, &s.load, &s.win, &s.salvage}) {
    f(sh->addr() + 0x8, 4, &sh->cursor_);
    f(sh->addr() + 0xc, 4, &sh->screen_);
  }
  f(0x608f00 + 0x10, 4, &s.main.save_pilots_);
  f(0x608f00 + 0x14, 4, &s.main.idle_);
  f(0x608f18 + 0x10, 1, &s.load.build_);
  f(0x608f18 + 0x18, 4, &s.load.movie_);
  f(0x608e70 + 0x10, 1, &s.win.campaign_won_);
  f(0x608e70 + 0x14, 4, &s.win.movie_);
  LabState& l = s.lab;
  const uint32_t L = l.addr();
  f(L + 0x8, 4, &l.cursor_);
  f(L + 0xc, 4, &l.screen_);
  f(L + 0x2c, 4, &l.model_);
  f(L + 0x30, 4, &l.slot_);
  f(L + 0x34, 4, &l.slots_);
  f(L + 0x38, 4, &l.mech_);
  f(L + 0x60, 4, &l.lab_screen_);
  f(L + 0x64, 4, &l.clear_);
  f(L + 0x6c, 4, &l.world_);
  for (int i = 0; i < 4; ++i) f(L + 0x70 + 4 * static_cast<uint32_t>(i), 4, &l.sounds_[i]);
  f(L + 0x84, 4, &l.pilot_);
  f(L + 0x88, 4, &l.store_);
  f(L + 0x8c, 1, &l.flag_);
  f(L + 0x90, 4, &l.result_);
  for (MenuState* m : g_menus) {
    f(m->addr() + 0x8, 4, &m->cursor_);
    f(m->addr() + 0xc, 4, &m->screen_);
  }
}

uint32_t renderer_vtable(int kind) {
  switch (kind) {
    case 0: return 0x5c4068;
    case 1: return 0x5c406c;
    case 2: return 0x5c4070;
    default: return 0;
  }
}

}  // namespace

extern "C" {

// A fresh app and states, made unrecorded.
void probe_states_reset() {
  // The states are torn down with their app; nothing in them is recorded.
  probe_app::Mute quiet;
  delete g_app;
  delete g_engine;
  mech3::reset_states();
  mech3::globals() = mech3::AppGlobals{};
  mech3::options() = mech3::Options{};
  // The axis table as the emulator's .data holds it: 0x410950 does not run there.
  mech3::axis_table() = mech3::AxisTable{};
  for (mech3::AxisBinding& a : mech3::axis_table().axes) a = mech3::AxisBinding{};
  mech3::axis_table().ready = 0;
  g_engine = new Recording;
  zerr::set_sink(nullptr);  // the release build's report (0x0049bfd0) is empty
  g_app = new mech3::Mech3App(g_engine);  // recorded only through probe_states_construct
  mech3::set_app(g_app);
  build_fields();
}

void probe_states_set_command_line(const char* line) { g_command_line = line; }

// The game options, graphics, controls and credits states at a, b, c, d
// (their constructors' calls muted: set-up). Then the calls on `this`.
void probe_states_set_menus(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  {
    probe_app::Mute quiet;
    for (mech3::MenuState* m : g_menus) {
      m->screen_ = 0;
      delete m;
    }
    g_menus = {new mech3::OptionsState(mech3::OptionsState::kGame, a),
               new mech3::OptionsState(mech3::OptionsState::kGfxAudio, b),
               new mech3::OptionsState(mech3::OptionsState::kControls, c), new mech3::CreditsState(d)};
  }
  build_fields();
}
void probe_states_this(uint32_t addr) {
  g_this = nullptr;
  for (mech3::MenuState* m : g_menus)
    if (m->addr() == addr) g_this = m;
}

// The constructor 0x0047f7a0, recorded, on a fresh app.
void probe_states_construct() {
  {
    probe_app::Mute quiet;
    delete g_app;
    mech3::reset_states();
  }
  g_app = new mech3::Mech3App(g_engine);
  mech3::set_app(g_app);
  build_fields();
}

// The Lab State's parts (+0x20): the handles the test put in the original's vector.
void probe_states_lab_parts(const uint32_t* parts, int32_t n) { mech3::states().lab.models_.assign(parts, parts + n); }
// The third argument of 0x00448830 (its byte flag).
uint32_t g_arg3 = 0;
void probe_states_arg3(uint32_t v) { g_arg3 = v; }

int32_t probe_states_field_count() { return static_cast<int32_t>(g_fields.size()); }
void probe_states_field_info(int32_t i, uint32_t* addr, int32_t* size) {
  *addr = g_fields[i].addr;
  *size = g_fields[i].size;
}
uint32_t probe_states_field_get(int32_t i) {
  const Field& f = g_fields[i];
  if (f.kind == 1) return state_addr(*static_cast<zgame::State**>(f.p));
  if (f.kind == 2) return renderer_vtable(*static_cast<int*>(f.p));
  uint32_t v = 0;
  std::memcpy(&v, f.p, static_cast<size_t>(f.size));
  return v;
}
void probe_states_field_set(int32_t i, uint32_t v) {
  const Field& f = g_fields[i];
  if (f.kind == 1) {
    *static_cast<zgame::State**>(f.p) = state_at(v);
  } else if (f.kind == 2) {
    int k = -1;
    for (int j = 0; j < 3; ++j)
      if (renderer_vtable(j) == v) k = j;
    *static_cast<int*>(f.p) = k;
  } else {
    std::memcpy(f.p, &v, static_cast<size_t>(f.size));
  }
}

// The app's queued state changes: (type, state address, arg) each.
int32_t probe_states_queue(uint32_t* out, int32_t max) {
  int32_t n = 0;
  for (const auto& p : g_app->pending_list()) {
    if (n >= max) break;
    out[3 * n] = static_cast<uint32_t>(p.type);
    out[3 * n + 1] = state_addr(p.state);
    out[3 * n + 2] = p.arg;
    ++n;
  }
  return n;
}

// Runs one ported function by its original address; 1 when ExitProcess ended it
// (its result in *ret otherwise).
int32_t probe_states_call(uint32_t addr, uint32_t arg, uint32_t arg2, uint32_t* ret) {
  using namespace mech3;
  States& s = states();
  Mech3App& a = *g_app;
  uint32_t r = 0;
  try {
    switch (addr) {
      case 0x47f7a0: {  // the constructor, on a fresh app
        {
          probe_app::Mute quiet;
          delete g_app;
          mech3::reset_states();
        }
        g_app = new mech3::Mech3App(g_engine);
        mech3::set_app(g_app);
        build_fields();
        break;
      }
      case 0x480000: r = a.init_instance(); break;
      case 0x47fa10: r = static_cast<uint32_t>(a.exit_instance()); break;
      case 0x480570: r = a.start_game(arg); break;
      case 0x4808a0: a.stop_game(); break;
      case 0x480970: r = a.open_video(arg); break;
      case 0x482d10: r = a.create_main_window(); break;
      case 0x4803f0: r = a.pre_translate_message(arg); break;
      case 0x482a20: r = a.confirm_quit(); break;
      case 0x47fed0: a.pick_post_intro_state(arg != 0); break;
      case 0x47fe10: a.restore_video(arg != 0, arg2 != 0); break;
      case 0x47fb80: a.set_defaults(); break;
      case 0x47ff60: a.restart_in_lab(); break;
      case 0x482970: r = state_addr(a.state(static_cast<int>(arg))); break;
      case 0x482330: r = s.attract.enter(); break;
      case 0x482430: r = s.attract.update(); break;
      case 0x482470: s.attract.leave(); break;
      case 0x482070: r = s.intro.enter(); break;
      case 0x482170: r = s.intro.update(); break;
      case 0x4822d0: s.intro.leave(); break;
      case 0x482060: r = s.exit.enter(); break;
      case 0x480b30: r = s.pre_lab.enter(); break;
      case 0x480b60: r = s.pre_lab.update(); break;
      case 0x481280: s.core.on_activate(arg); break;
      case 0x480c70: r = s.core.enter(); break;
      case 0x481f70: r = s.core.update(); break;
      case 0x481e70: r = s.core.frame(); break;
      case 0x481fc0: s.core.leave(); break;
      case 0x481250: s.core.suspend(arg); break;
      case 0x481170: s.core.resume(arg); break;
      case 0x480bf0: s.core.pick_renderer(); break;
      case 0x4ab6b0: r = static_cast<uint32_t>(g_engine->Engine::set_screen_mode(static_cast<int32_t>(arg))); break;
      case 0x4ab910: g_engine->vmode_toggle(); break;
      case 0x41cdc0: g_engine->Engine::cockpit_refit(arg); break;
      case 0x480df0: s.core.start_mission(); break;
      case 0x481290: r = s.core.draw_software(arg, arg2 != 0); break;
      case 0x481a10: r = s.core.draw_doubled(arg, arg2 != 0); break;
      case 0x481790: r = s.core.draw_hardware(arg, arg2 != 0); break;
      case 0x482520: r = s.key_list.update(); break;
      case 0x482550: s.key_list.leave(); break;
      case 0x48ebf0: r = s.salvage.ShellState::enter(); break;
      case 0x48ed30: r = s.salvage.ShellState::update(); break;
      case 0x48ec40: s.salvage.ShellState::leave(); break;
      case 0x48ec80: s.salvage.ShellState::suspend(arg); break;
      case 0x48ecc0: s.salvage.ShellState::resume(arg); break;
      case 0x486990: r = s.main.enter(); break;
      case 0x486d40: r = s.main.update(); break;
      case 0x486c50: s.main.leave(); break;
      case 0x48e400: s.main.suspend(arg); break;
      case 0x486e80: s.main.resume(arg); break;
      case 0x486ec0: s.main.escape(); break;
      case 0x493040: r = s.pause.enter(); break;
      case 0x4931e0: s.pause.leave(); break;
      case 0x493250: s.pause.escape(); break;
      case 0x4091e0: r = s.load.enter(); break;
      case 0x409440: r = s.load.update(); break;
      case 0x4093e0: s.load.leave(); break;
      case 0x4092e0: s.load.resume(arg); break;
      case 0x47ac20: r = s.win.enter(); break;
      case 0x47aed0: r = s.win.update(); break;
      case 0x47af20: s.win.leave(); break;
      case 0x47d310: r = s.salvage.enter(); break;
      case 0x47d390: s.salvage.leave(); break;
      case 0x448830: r = mech_lab_open(arg, arg2, static_cast<uint8_t>(g_arg3)); break;
      case 0x448960: r = s.lab.enter(); break;
      case 0x448a80: r = s.lab.update(); break;
      case 0x448a20: s.lab.leave(); break;
      case 0x448870: r = s.lab.accept(); break;
      case 0x448e20: r = s.lab.model_valid(static_cast<int32_t>(arg)); break;
      case 0x4507c0: r = static_cast<uint32_t>(s.lab.model_index(static_cast<int32_t>(arg))); break;
      case 0x448e70: s.lab.make_screen(); break;
      case 0x448ef0: s.lab.delete_screen(); break;
      case 0x448f10: s.lab.draw(zgame::from_bits(arg)); break;
      case 0x438c90:
      case 0x42da50: r = g_this->enter(); break;
      case 0x42da70: g_this->resume(arg); break;
      case 0x438c80: g_this->escape(); break;
      case 0x429600: r = g_this->enter(); break;
      case 0x429670: g_this->leave(); break;
      default: r = 0xdeadbeef;
    }
  } catch (const Exit&) {
    return 1;
  }
  *ret = r;
  return 0;
}

}  // extern "C"
