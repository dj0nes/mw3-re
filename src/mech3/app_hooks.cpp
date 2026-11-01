#include "mech3/app_hooks.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "crt/msvc_rand.h"
#include "mech3/mech_config.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_salvage.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zutil/zfix.h"
#include "mech3/app.h"
#include "mech3/app_states.h"
#include "mech3/controls_cfg.h"
#include "mech3/mainmenu.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zutil/zutl_msg.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/pilot.h"
#include "mech3/music.h"
#include "mech3/mission.h"
#include "mech3/options.h"

namespace mech3 {

// The object, then up to three arguments (the most any call here passes).
uint32_t Engine::vcall(Handle obj, uint32_t slot, std::initializer_list<Arg> args) {
  char key[16];
  std::snprintf(key, sizeof key, "vt+0x%x", slot);
  const Arg* a = args.begin();
  switch (args.size()) {
    case 0: return unported(key, {obj});
    case 1: return unported(key, {obj, a[0]});
    case 2: return unported(key, {obj, a[0], a[1]});
    default: return unported(key, {obj, a[0], a[1], a[2]});
  }
}

// The controls screen's calls, to the ports.
uint32_t Engine::input_read_key(int32_t wait) { return zin::read_key(wait != 0); }
int32_t Engine::joy_wait_button(int32_t wait) { return zin::joy_wait_button(wait != 0); }
int32_t Engine::mouse_wait_button(int32_t wait) { return zin::mouse_wait_button(wait != 0); }
void Engine::joy_use() { zin::joy_use(); }
void Engine::joy_unuse() { zin::joy_unuse(); }
uint32_t Engine::key1_of(int32_t cmd) { return zin::key1_of(cmd); }
uint32_t Engine::key2_of(int32_t cmd) { return zin::key2_of(cmd); }
uint32_t Engine::joy_of(int32_t cmd) { return zin::joy_of(cmd); }
uint32_t Engine::mouse_of(int32_t cmd) { return zin::mouse_of(cmd); }
int32_t Engine::key1_command(uint32_t key) { return zin::key1_command(static_cast<int32_t>(key)); }
int32_t Engine::key2_command(uint32_t key) { return zin::key2_command(static_cast<int32_t>(key)); }
int32_t Engine::joy_command(uint32_t b) { return zin::joy_command(static_cast<int32_t>(b)); }
int32_t Engine::mouse_command(uint32_t b) { return zin::mouse_command(static_cast<int32_t>(b)); }
void Engine::bind_key1(uint32_t key, int32_t cmd) { zin::bind_key1(key, cmd); }
void Engine::bind_key2(uint32_t key, int32_t cmd) { zin::bind_key2(key, cmd); }
void Engine::bind_joy(uint32_t b, int32_t cmd) { zin::bind_joy(b, cmd); }
void Engine::bind_mouse(uint32_t b, int32_t cmd) { zin::bind_mouse(b, cmd); }
const char* Engine::command_name(int32_t cmd, char* out, int32_t n) {
  return zin::command_name(cmd, out, static_cast<size_t>(n));
}
const char* Engine::key_name(uint32_t key, char* out, int32_t n) {
  return zin::key_name(key, out, static_cast<size_t>(n));
}
const char* Engine::joy_button_name(uint32_t b, char* out, int32_t n) {
  return zin::joy_button_name(static_cast<int32_t>(b), out, static_cast<size_t>(n));
}
const char* Engine::mouse_button_name(uint32_t b, char* out, int32_t n) {
  return zin::mouse_button_name(static_cast<int32_t>(b), out, static_cast<size_t>(n));
}
void Engine::input_map_refresh() { zin::map_refresh(); }
int32_t Engine::controls_defaults() { return mech3::controls_set_defaults(); }
int32_t Engine::controls_group_count() { return mech3::controls_group_count(); }
const char* Engine::controls_group_title(int32_t g) { return mech3::controls_group_title(g); }
int32_t Engine::controls_group_size(int32_t g) { return mech3::controls_group_size(g); }
int32_t Engine::controls_group_command(int32_t g, int32_t i) { return mech3::controls_group_command(g, i); }
void Engine::controls_read(const char* path) { mech3::controls_load(path); }
void Engine::controls_write(const char* path) { mech3::controls_save(path); }
char* Engine::active_ctl_cfg() { return mech3::options().active_ctl_cfg; }
std::vector<std::string> Engine::list_files(const char* pattern) { return zutl::list(pattern); }

void Engine::format_key(char* out, uint32_t addr) { std::snprintf(out, 16, "0x%08x", addr); }

void Engine::light_init() { zmod::shadows_init(); }
void Engine::update_shadows() { mech3::update_shadows(*this); }

namespace {
// Caster handles: index + 1 into the port's records (0 is none).
std::vector<zmod::ShadowCaster*> g_casters;
zmod::ShadowCaster* caster(Handle h) {
  return h && h <= g_casters.size() ? g_casters[h - 1] : nullptr;
}
}  // namespace

Handle Engine::shadow_new() {
  g_casters.push_back(zmod::shadow_new());
  return static_cast<Handle>(g_casters.size());
}
void Engine::shadow_set_size(Handle c, int32_t w, int32_t h) {
  if (zmod::ShadowCaster* s = caster(c)) zmod::shadow_set_size(s, w, h);
}
void Engine::shadow_set_node(Handle c, Handle node) {
  if (zmod::ShadowCaster* s = caster(c)) zmod::shadow_set_node(s, node_of(node));
}
void Engine::shadow_enable(Handle c, int32_t on) {
  if (zmod::ShadowCaster* s = caster(c)) zmod::shadow_enable(s, on);
}
void Engine::shadow_set_color(Handle c, uint32_t color) {
  if (zmod::ShadowCaster* s = caster(c)) zmod::shadow_set_color(s, color);
}
void Engine::shadow_set_position(Handle c, Handle pos) {
  const float p[3] = {zgame::from_bits(peek32(pos)), zgame::from_bits(peek32(pos + 4)),
                      zgame::from_bits(peek32(pos + 8))};
  if (zmod::ShadowCaster* s = caster(c)) zmod::shadow_set_position(s, p);
}
int32_t Engine::shadow_draw(Handle c, Handle light_dir, float ground) {
  const float d[3] = {zgame::from_bits(peek32(light_dir)), zgame::from_bits(peek32(light_dir + 4)),
                      zgame::from_bits(peek32(light_dir + 8))};
  zmod::ShadowCaster* s = caster(c);
  return s ? zmod::shadow_draw(s, d, ground) : 1;
}
void Engine::shadow_upload(Handle c) {
  if (zmod::ShadowCaster* s = caster(c)) zmod::shadow_upload(s);
}
void Engine::shadow_release_texture(Handle c) {
  if (zmod::ShadowCaster* s = caster(c)) zmod::shadow_release_texture(s);
}
void Engine::shadow_delete(Handle c) {
  if (zmod::ShadowCaster* s = caster(c)) {
    zmod::shadow_delete(s);
    g_casters[c - 1] = nullptr;
  }
}
float Engine::clouds_at(float x, float z) { return zmod::clouds_at(x, z); }

// [0x0060861c]+0x18, the current pilot's name (mech3/pilot).
const char* Engine::pilot_name() { return current_pilot() ? current_pilot()->name : nullptr; }
zrdr::Node* Engine::main_menu_definition() { return main_menu() ? main_menu()->definition : nullptr; }
std::string Engine::message_format_ints(int32_t id, std::initializer_list<int32_t> args) {
  return zutl::format_message_ints(id, args);
}

float Engine::frame_dt() { return ztimer::state().dt; }
int32_t Engine::in_game_vmode() { return options().in_game_vmode; }
int32_t Engine::menu_vmode() {
  if (menu_vmode_ >= 0) return menu_vmode_;
  int32_t w = 0, h = 0;
  const int32_t m = in_game_vmode();
  // Wide means a shape wider than 4:3, not a bigger canvas: the original's
  // modes up to 10 (1600x1200) draw the 2D at full size, still 4:3.
  const bool wide = zfix::options().any_resolution && zfix::options().wide_menus && mech3::vmode_canvas(m, &w, &h) &&
                    w > 640 && w * 3 > h * 4;
  return menu_vmode_ = wide ? m : 5;
}
float Engine::mouse_accel_x() { return options().mouse_accel[0]; }
float Engine::mouse_accel_y() { return options().mouse_accel[1]; }

int32_t Engine::hw_card_flag() { return mech3::hw_card_flag(); }
int32_t Engine::hw_card_dev() { return mech3::hw_card_dev(); }
void Engine::select_hw(int32_t hw) { mech3::select_hw(hw); }
void Engine::set_hw_card_dev(int32_t dev) { mech3::set_hw_card_dev(dev); }
int32_t Engine::vmode() { return mech3::vmode(); }
void Engine::set_vmode(int32_t mode) { mech3::set_vmode(mode); }
int32_t Engine::gfx_flags() { return mech3::gfx_flags(); }
int32_t Engine::pixel_double() { return mech3::pixel_double(); }
// The rectangles are named by number for the hooks that take them.
Handle Engine::render_rect() { return 1; }
Handle Engine::display_rect() { return 2; }
Handle Engine::window_rect() { return 3; }
Handle Engine::display_depth() { return static_cast<Handle>(mech3::display_depth()); }
Handle Engine::vstride() { return static_cast<Handle>(mech3::vstride()); }
int32_t Engine::window_h() { return mech3::window_h(); }
void Engine::set_use_joystick(int32_t on) { mech3::set_use_joystick(on); }
void Engine::set_fast_menu(int32_t on) { mech3::set_fast_menu(on); }
int32_t Engine::fast_menu() { return mech3::fast_menu(); }
int32_t Engine::display_width() { return mech3::display_w(); }
int32_t Engine::display_height() { return mech3::display_h(); }
void Engine::confirm_quit() { app().confirm_quit(); }
void Engine::options_apply_all() { mech3::apply_all(); }
int32_t Engine::game_intensity() { return mech3::game_intensity(); }
void Engine::set_game_intensity(int32_t v) { mech3::set_game_intensity(v); }
int32_t Engine::camera_mode() { return mech3::camera_mode(); }
void Engine::set_camera_mode(int32_t v) { mech3::set_camera_mode(v); }
int32_t Engine::invincible() { return mech3::invincible(); }
void Engine::set_invincible(int32_t v) { mech3::set_invincible(v); }
int32_t Engine::infinite_ammo() { return mech3::infinite_ammo(); }
void Engine::set_infinite_ammo(int32_t v) { mech3::set_infinite_ammo(v); }
int32_t Engine::heat() { return mech3::heat(); }
void Engine::set_heat(int32_t v) { mech3::set_heat(v); }
int32_t Engine::torso_auto_return() { return mech3::torso_auto_return(); }
void Engine::set_torso_auto_return(int32_t v) { mech3::set_torso_auto_return(v); }
int32_t Engine::throttle_const() { return mech3::throttle_const(); }
void Engine::set_throttle_const(int32_t v) { mech3::set_throttle_const(v); }
int32_t Engine::mw2_jump_jets() { return mech3::mw2_jump_jets(); }
void Engine::set_mw2_jump_jets(int32_t v) { mech3::set_mw2_jump_jets(v); }
void Engine::set_mouse_accel_y(float v) { options().mouse_accel[1] = v; }
void Engine::settings_write() { zreg::write_all(); }
float Engine::cd_volume() { return mech3::cd_volume(); }
void Engine::set_cd_volume(float v) { mech3::set_cd_volume(v); }
float Engine::sound_frequency() { return mech3::sound_frequency(); }
void Engine::set_sound_frequency(float v) { mech3::set_sound_frequency(v); }
int32_t Engine::texture_memory() { return mech3::texture_memory(); }
void Engine::set_texture_memory(int32_t v) { mech3::set_texture_memory(v); }
int32_t Engine::sound_lod() { return mech3::sound_lod(); }
void Engine::set_sound_lod(int32_t v) { mech3::set_sound_lod(v); }
int32_t Engine::object_lod() { return mech3::object_lod(); }
void Engine::set_object_lod(int32_t v) { mech3::set_object_lod(v); }
int32_t Engine::effects_level() { return mech3::effects_level(); }
void Engine::set_effects_level(int32_t v) { mech3::set_effects_level(v); }
int32_t Engine::shadow_parts() { return mech3::shadow_parts(); }
void Engine::set_shadow_parts(int32_t v) { mech3::set_shadow_parts(v); }
int32_t Engine::shadow() { return mech3::shadow(); }
void Engine::set_shadow(int32_t v) { mech3::set_shadow(v); }
void Engine::set_gfx_flags(int32_t v) { mech3::set_gfx_flags(v); }
void Engine::set_in_game_vmode(int32_t v) { options().in_game_vmode = v; }

// Mech3.exe 1.2 @ 0x004ab860: software 3..5 (320x240 doubled to 640x480);
// a 3D card 5 up to 7 (1024x768), 6 below 3 x 1.5 MB of free video memory,
// 5 below 3 x 960,000 bytes. Modes 8..10 are never offered (docs/menus.md).
void Engine::video_mode_range(int32_t device, int32_t* lo, int32_t* hi) {
  *lo = 3;
  *hi = 7;
  int32_t free = 0, frames = 1;
  if (device < 0) {
    *lo = 3;
    *hi = 5;
  } else {
    const int32_t total = video_memory_total();
    free = total - video_memory_used();
    frames = 3;
    if (free < 1) free = video_memory_total();
    *lo = 5;
  }
  if (free > 0) {
    *hi = 7;
    if (free < frames * 0x180000) {
      *hi = 6;
      if (free < frames * 960000) *hi = 5;
    }
  }
  // FIX (B) (resolution.md): a card offers every size; today's have the memory.
  if (device >= 0 && zfix::options().any_resolution) *hi = kLastVMode;
}

// Mech3.exe 1.2 @ 0x004ab6b0: the screen to `mode`. The same mode only
// restores the surfaces; zVideo refusing it leaves the old one. Then the
// options' rects, the render targets, the software spans (or the hardware
// clears), the cursor, the message panel, and every mech's cockpit: the view's
// own mech's HUD laid out for the mode (resolution.md step 3).
int32_t Engine::set_screen_mode(int32_t mode) {
  const int32_t old = vmode();
  if (mode == old) {
    video_restore();  // 0x00577cb0
    return mode;
  }
  Handle still = 0;
  video_lock();
  if (!peek32(0x72f348)) still = video_grab(1);  // software: the screen kept to draw back
  video_unlock();
  if (video_set_mode(mode) != 0) return old;
  set_vmode(mode);
  // FIX (B), playtest W13: the camera's angle across for the new shape. The
  // exe sets it only at the mission's load (0x0040d590), its modes all 4:3.
  if (zfix::options().any_resolution && mech3::camera()) mech3::set_camera(mech3::camera());
  {
    const Handle back = video_back_buffer();
    const Handle depth = display_depth();
    const Handle rect = display_rect();
    render_set_target(video_target(), rect, depth, back);
  }
  render_set_pitch(vstride());
  {
    const Handle back = video_back_buffer();
    const Handle b = video_buffer_da0();
    const Handle r = video_buffer_d90();
    render_set_target2(video_target(), r, b, back);
  }
  if (!peek32(0x72f348)) {
    video_blit(still, 0, 0, display_rect());
    video_flip(0, 0, 1, 1);
    video_blit(still, 0, 0, display_rect());
    span_shutdown();  // 0x005646d0
    if (span_init(window_h()) != 0) {  // 0x00564370
      print("Error opening HSE... ABORTING RUN\n", nullptr);
      flush_stdout();
      return old;
    }
  } else {
    video_clear_a(0);
    video_clear_b(0);
  }
  video_show_cursor(static_cast<uint32_t>(pixel_double()));
  twod_set_cursor(static_cast<uint32_t>(pixel_double()));
  {
    const Handle render = render_rect();
    message_panel_place(display_rect(), render);
  }
  screen_mode_cockpits();
  if (still) image_free(still);
  return mode;
}

// 0x004ab6b0's loop: zEntity's list, each entry's object (vt+0x2c) and its
// mech (vt+0x50), cockpits refitted.
void Engine::screen_mode_cockpits() {
  for (Handle it = peek32(0x73655c); it != peek32(0x736560); it += 4) {
    const Handle o = vcall(peek32(it), 0x2c, {});
    if (const Handle m = vcall(o, 0x50, {})) cockpit_refit(m);
  }
}

// Mech3.exe 1.2 @ 0x0041cdc0: the view's own mech's HUD (0x00430aa0 on its
// +0x684) set up again (0x0043bb80) for the screen mode.
void Engine::cockpit_refit(Handle mech) {
  const Handle hud = mech == field32(peek32(0x5f4c8c), 0x64) ? pilot_hud(field32(mech, 0x684)) : 0;
  if (hud) hud_setup(hud, mech);
}

// Mech3.exe 1.2 @ 0x004ab910: Shift+V, the next mode in the device's range
// (3 to 5, 5 to 6, 6 to 7, the rest to its lowest; past its highest, the
// lowest), saved as InGameVMode. FIX (B): with any_resolution 7..15 step on
// to the next (the range reaching 16), where the exe went back to the lowest.
void Engine::vmode_toggle() {
  int32_t lo = 0, hi = 0;
  video_mode_range(hw_card_dev(), &lo, &hi);  // always true in the exe
  const int32_t cur = vmode();
  int32_t next = cur == 3 ? 5 : cur == 5 ? 6 : cur == 6 ? 7 : lo;
  if (zfix::options().any_resolution && cur >= 7 && cur < hi) next = cur + 1;
  if (next > hi) next = lo;
  set_in_game_vmode(set_screen_mode(next));
}
void Engine::pop_state(uint32_t arg) { app().pop(arg); }
std::string Engine::peek_string(Handle p) {
  std::string s;
  if (!p) return s;
  for (uint8_t c; (c = peek8(p)) != 0; ++p) s += static_cast<char>(c);
  return s;
}
std::string Engine::lab_config_name(Handle config) { return peek_string(field32(config, 0x18)); }
void Engine::lab_dfn_skins(Handle dfn, std::vector<LabSkin>* out) {
  out->clear();
  const Handle set = field32(dfn, 0x1c);
  const Handle first = set ? field32(set, 0x04) : 0, last = set ? field32(set, 0x08) : 0;
  for (Handle p = first; first && p < last; p += 4) {
    const Handle skin = peek32(p);
    const Handle name = skin ? field32(skin, 0) : 0;
    out->push_back({skin, name != 0, peek_string(name)});
  }
}
std::vector<Handle> Engine::lab_list_configs(Handle list) {
  std::vector<Handle> out;
  const Handle first = field32(list, 0x0c), last = field32(list, 0x10);
  for (Handle p = first; first && p < last; p += 4) out.push_back(peek32(p));
  return out;
}
// Mech3.exe 1.2 @ 0x00424e90 (on handles): the first with that name.
Handle Engine::lab_list_find(Handle list, const char* name) {
  if (!name) return 0;
  for (Handle c : lab_list_configs(list))
    if (c && lab_config_name(c) == name) return c;
  return 0;
}
void Engine::lab_store_remove(Handle store, int32_t mech) {
  if (Store* s = store_of(store)) s->remove_mech(mech);
}
void Engine::lab_store_add(Handle store, int32_t mech) {
  if (Store* s = store_of(store)) s->add_mech(mech);
}
// The double as its two dwords on the stack, low first.
void Engine::screen_tint(uint16_t colour, Handle rect, double alpha) {
  uint64_t q;
  std::memcpy(&q, &alpha, sizeof q);
  unported("0x00558540", {colour, rect, static_cast<uint32_t>(q), static_cast<uint32_t>(q >> 32)});
}
void Engine::replace_with_exit() { app().replace(app().state(4), 0); }
bool Engine::main_menu_background(zimg::Texture** image, uint8_t* moving) {
  MainMenu* m = main_menu();
  if (!m) return false;
  *image = m->background.image;
  *moving = m->background.moving;
  return true;
}
uint8_t Engine::mission_running() { return states().core.active_; }
void Engine::options_save() { mech3::save_options(); }
void Engine::settings_free() { zreg::shutdown(); }

void Engine::exit_process(uint32_t code) { std::exit(static_cast<int>(code)); }
void Engine::cursor_pos(int32_t* x, int32_t* y) {
  unported("GetCursorPos", {});
  *x = 0;
  *y = 0;
}
uint32_t Engine::rand() { return static_cast<uint32_t>(crt::msvc_rand()); }
const char* Engine::command_line() { return ""; }
void Engine::print(const char* format, const char* arg) {
  if (arg) std::printf(format, arg);
  else std::fputs(format, stdout);
}

// FIX (bug): a message the table lacks (1.0's Mech3Msg.dll has 38 fewer)
// reads as "", where the original's null reached unchecked copies (fixes.md).
const char* Engine::message(int32_t id) {
  const zutl::Messages& m = zutl::messages();
  const char* s = m.get_text ? m.get_text(id) : nullptr;
  return s ? s : "";
}
const char* Engine::version_string() { return "1.0"; }
const char* Engine::message_format(int32_t id, const char* arg) {
  unported("0x00575fd0", {id, arg});
  return message(id);
}
const char* Engine::message_format_n(int32_t id, int32_t n) {
  unported("0x00575fd0", {id, static_cast<uint32_t>(n)});
  return message(id);
}
void Engine::print_version(char* out, int size, int32_t major, int32_t minor) {
  std::snprintf(out, static_cast<size_t>(size), "%d.%d", static_cast<int>(major), static_cast<int>(minor));
  unported("sprintf", {out});
}
const char* Engine::version_number() { return "1.2"; }
const char* Engine::mission_dir(uint32_t n) { return mech3::mission_dir(static_cast<int32_t>(n)); }
const ConfigList* Engine::mech_loadouts(int32_t mech) {
  const MechDfn* d = mech3::mech_dfn(mech);
  return d ? d->configs : nullptr;
}
bool Engine::file_exists(const char* path) { return zutl::exists(path); }
// Inline in 0x0047d030
void Engine::action_next_round() {
  Pilot* p = current_pilot();
  ActionGame* g = p ? p->action() : nullptr;
  if (!g) return;  // (the exe reads it unchecked)
  g->x6c = g->mode == 2 ? action_setup().g87c + 1 : 1;
  g->x70 = 0;
}

void Engine::cd_pause() { zsnd_cd::pause(); }
void Engine::cd_resume(int32_t mode) { zsnd_cd::resume(mode); }
void Engine::cd_stop() { zsnd_cd::stop(); }
int32_t Engine::cd_track_count() {
  const zsnd_cd::State& s = zsnd_cd::state();
  return (s.flags & 2) ? s.num_tracks : 0;
}
void Engine::music_play(int32_t track, int32_t mode) { mech3_music::play(track, mode); }

// MFC's CString::Format for the formats these files use: %s and %d.
void Engine::format(const char* fmt, std::initializer_list<Arg> args, char* out, int size) {
  std::string s;
  const Arg* a = args.begin();
  for (const char* p = fmt; *p; ++p) {
    if (p[0] == '%' && (p[1] == 's' || p[1] == 'd') && a != args.end()) {
      if (p[1] == 's') s += a->s ? a->s : "";
      else s += std::to_string(static_cast<int32_t>(a->v));
      ++a;
      ++p;
    } else {
      s += *p;
    }
  }
  std::snprintf(out, static_cast<size_t>(size), "%s", s.c_str());
  unported("CString::Format", {out});
}

}  // namespace mech3

namespace mech3 {

const char* part_display_name(int32_t id, const char* (*message)(void* ctx, int32_t), void* ctx);  // mech_names.h

// The lab's item lists' asks (app_hooks.h): a store item through item_of.
int32_t Engine::lab_item_kind(Handle h) {
  const ConfigItem* it = item_of(h);
  return it ? it->kind() : 0;
}
std::string Engine::lab_item_name(Handle h) {
  const ConfigItem* it = item_of(h);
  if (!it) return std::string();
  return item_name(*it, [this](int32_t id) { return message(id); });
}
int32_t Engine::lab_item_slots(Handle h) {
  const ConfigItem* it = item_of(h);
  return it ? it->slots() : 0;
}
int32_t Engine::lab_item_id(Handle h) {
  const ConfigItem* it = item_of(h);
  return it ? it->id() : 0;
}
float Engine::lab_item_mass(Handle h) {
  const ConfigItem* it = item_of(h);
  return it ? static_cast<float>(it->weight(1)) : 0.0f;
}
int32_t Engine::lab_item_count(Handle h) {
  const ConfigItem* it = item_of(h);
  return it ? it->count : 0;
}
uint8_t Engine::lab_item_unlimited(Handle h) {
  const ConfigItem* it = item_of(h);
  return it ? it->b10 : 0;
}
int32_t Engine::lab_item_equipment(Handle h) {
  const ConfigItem* it = item_of(h);
  return it && it->kind() == 3 ? static_cast<const EquipmentItem*>(it)->equipment : 0;
}
int32_t Engine::lab_item_tech(Handle h) {
  const ConfigItem* it = item_of(h);
  return it && it->kind() == 3 ? static_cast<const EquipmentItem*>(it)->tech : 0;
}
std::vector<Engine::LabFirepoint> Engine::lab_dfn_firepoints(Handle dfn) {
  std::vector<LabFirepoint> out;
  const Handle v = dfn ? field32(dfn, 0x24) : 0;
  if (!v) return out;
  for (Handle p = field32(v, 0x04), end = field32(v, 0x08); p && p < end; p += 4) {
    const Handle f = field32(p, 0);
    out.push_back({static_cast<int32_t>(field32(f, 0x0c)), zgame::from_bits(field32(f, 0x04))});
  }
  return out;
}
std::vector<Handle> Engine::lab_config_items(Handle config) {
  std::vector<Handle> out;
  const Handle head = config ? field32(config, 0x08) : 0;
  if (!head) return out;
  for (Handle n = field32(head, 0); n && n != head; n = field32(n, 0)) out.push_back(field32(n, 8));
  return out;
}
int32_t Engine::lab_item_location(Handle h, int32_t mech) {
  const ConfigItem* it = item_of(h);
  if (!it) return -1;
  if (it->kind() != 1) return it->location(mech);
  // 0x00473220: the definition's firepoint at the weapon's +0x0c
  const Handle dfn = mech_dfn_handle(mech);
  if (!dfn) return -1;
  const std::vector<LabFirepoint> fps = lab_dfn_firepoints(dfn);
  return it->part >= 0 && it->part < static_cast<int32_t>(fps.size()) ? fps[static_cast<size_t>(it->part)].part : -1;
}
int32_t Engine::lab_item_group(Handle h) {
  const ConfigItem* it = item_of(h);
  return it ? it->group : 0;
}
double Engine::lab_item_weight(Handle h, int32_t n) {
  const ConfigItem* it = item_of(h);
  return it ? it->weight(n) : 0.0;
}
std::string Engine::lab_item_label(Handle h) {
  const ConfigItem* it = item_of(h);
  return it ? item_label(*it, [this](int32_t id) { return message(id); }) : std::string();
}
void Engine::lab_set_item_group(Handle h, int32_t group) {
  if (auto* it = const_cast<ConfigItem*>(item_of(h))) it->group = group;
}
std::string Engine::lab_engine_name(Handle config) {
  return engine_name(static_cast<int32_t>(field32(config, 0x24)), field8(config, 0x28),
                     [this](int32_t id) { return message(id); });
}
const char* Engine::part_text(int32_t part) {
  return part_display_name(part, [](void* c, int32_t id) { return static_cast<Engine*>(c)->message(id); }, this);
}

}  // namespace mech3
