#include "mech3/app.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "mech3/app_states.h"
#include "mech3/options.h"

namespace mech3 {

namespace {
AppGlobals g_globals;
Mech3App* g_app = nullptr;
}  // namespace

AppGlobals& globals() { return g_globals; }
Mech3App& app() { return *g_app; }
Mech3App* app_if_any() { return g_app; }
void set_app(Mech3App* a) { g_app = a; }

int32_t Mech3App::cd_track() const {
  int32_t v;
  std::memcpy(&v, &block_[0x1b4 - 0x1ac], 4);
  return v;
}
void Mech3App::set_cd_track(int32_t track) { std::memcpy(&block_[0x1b4 - 0x1ac], &track, 4); }
uint8_t& Mech3App::multiplayer() { return options().multiplayer; }

// Mech3.exe 1.2 @ 0x0047f7a0. FIX (A): the Mech3Msg.dll load (0x00575f30, with
// a FormatMessage box and exit when missing) and the DirectX 6 check
// (0x0057a3d0, exit below 0x600) are the front end's; RunDir (0x004828d0) is
// SetCurrentDirectory from HKLM\Software\Zipper\MechWarrior3\<version>.
Mech3App::Mech3App(Engine* engine) : App(engine), engine_(engine) {
  field_1a4_ = 0;
  field_1e0_ = 0;
  field_1e4_ = 0;
  mission_started_ = 0;
  first_state_ = state(1);
  post_intro_ = state(2);
  engine_->run_dir();
  engine_->message_dll_load("mech3Msg.dll");  // FIX (A): the front end's message table
  engine_->directx_version();                 // FIX (A): no DirectX to need 6.0 of
  field_194_ = 0;
  lobby_launch_ = 1;
  g_globals.script = engine_->script_new();  // new(0xc0) + 0x00410700, "DamageScale"
  input_mode_ = 5;
}

// Mech3.exe 1.2 @ 0x00482970
State* Mech3App::state(int n) {
  switch (n) {
    case 0: return &states().attract;
    case 1: return &states().intro;
    case 2: return &states().main;
    case 3: return &states().pause;
    case 4: return &states().exit;
    case 5: return &states().core;
    case 6: return &states().load;
    case 7: return &states().pre_lab;
    case 8: return &states().salvage;
    case 9: return &states().win;
    case 10: return &states().key_list;
    case 12: return post_intro_;
    default: return nullptr;
  }
}

// Mech3.exe 1.2 @ 0x0047fb80
void Mech3App::set_defaults() {
  mech_ = "bushwacker";
  std::memset(block_, 0, sizeof block_);
  byte_at(0x1b2) = 1;
  byte_at(0x1c1) = 1;
  byte_at(0x1c7) = 1;
  // +0x1bd, +0x1c4/+0x1c5 and +0x1cc..+0x1d4 live in options() here.
  Options& o = options();
  o.multiplayer = 0;
  o.intensity_flag[0] = o.intensity_flag[1] = 0;
  o.intensity[0] = o.intensity[1] = o.intensity[2] = 1.0f;
  for (int i = 0; i < 6; ++i) block_[i] = 1;  // +0x1ac..+0x1b1
}

// Mech3.exe 1.2 @ 0x0047fed0: after the intro, the Load state for a pilot named
// "DefaultPlayer" (a lobby launch), else the main menu. FIX (trap): the
// tamper flag (0x00608f70) that forced the Exit state here is never set.
void Mech3App::pick_post_intro_state(bool lobby) {
  lobby_flow_ = lobby ? 1 : 0;
  int n = 2;
  if (lobby) {
    const char* name = engine_->pilot_name();  // [0x0060861c]+0x18, null without a pilot
    if (name && std::strcmp("DefaultPlayer", name) == 0) n = 6;
  }
  post_intro_ = state(n);
}

// Ours, FIX (A): the software renderer is not ported; a saved software
// choice (HWCardFlag 0, HWCardDev -1) starts on the GPU's device, windowed
// too (restore_video runs only full screen; fixes.md).
void Mech3App::gpu_only() {
  Engine& e = *engine_;
  if (!e.software_renderer() && e.install_options(0xff) && (e.hw_card_flag() == 0 || e.hw_card_dev() < 0))
    e.set_hw_card_dev(999);
}

// Mech3.exe 1.2 @ 0x0047fe10: the 3D card and screen mode saved by an earlier run.
// FIX (A): the software renderer is not ported, so a saved software choice
// (HWCardFlag 0, HWCardDev -1) or a pick of it starts on the GPU's device.
void Mech3App::restore_video(bool hw, bool pick) {
  Engine& e = *engine_;
  const bool sw = e.software_renderer();
  if ((!hw && sw) || e.install_options(0xff) == 0) {
    e.select_hw(0);
    return;
  }
  int32_t dev;
  if (pick) {
    dev = e.pick_video_device(1, 1);
  } else {
    dev = e.hw_card_dev();
    if (dev == 999) dev = e.pick_video_device(0, 0);
  }
  if (!sw && (dev < 0 || dev == 999)) dev = 0;
  if (dev == -1) {
    e.select_hw(0);
    e.set_hw_card_dev(-1);
    e.set_vmode(e.vmode());  // 0x0047fc10
    return;
  }
  e.select_hw(1);
  e.set_hw_card_dev(dev);
  if (e.vmode() < 5) e.set_vmode(5);
  e.set_vmode(e.menu_vmode() != 5 ? e.menu_vmode() : e.vmode());  // ours: wide menus
  byte_at(0x1c7) = 1;
}

// Mech3.exe 1.2 @ 0x0047fc20 / 0x0047fe00
void Mech3App::set_reopen_instant_action(uint8_t a, uint8_t b) {
  byte_at(0x1bc) = a;
  byte_at(0x1bf) = b;
}
void Mech3App::set_reopen_training(uint8_t v) { byte_at(0x1be) = v; }

// Mech3.exe 1.2 @ 0x0047ff60: the debug menu's Mech Lab, starting over.
void Mech3App::restart_in_lab() {
  lobby_flow_ = 1;
  post_intro_ = state(7);
  byte_at(0x1b2) = 0;
  start(main_window());
}

// Mech3.exe 1.2 @ 0x00482a20: Escape on the main menu.
bool Mech3App::confirm_quit() {
  if (engine_->message_box(1, 0xdc8, 0xde1) != 1) return false;
  replace(state(4), 0);
  return true;
}

// Mech3.exe 1.2 @ 0x004803f0: swallows WM_SYSKEYDOWN/UP while +0x1d8 is set.
bool Mech3App::pre_translate_message(uint32_t msg) {
  return byte_at(0x1d8) != 0 && msg >= 0x104 && msg <= 0x105;
}

// Mech3.exe 1.2 @ 0x00480000, InitInstance
bool Mech3App::init_instance() {
  Engine& e = *engine_;
  // FIX (trap): the exe self-check (0x004854c0) and its "Exit after the intro"
  // are left out (docs/protection.md).
  lobby_ = e.lobby_connection() ? 1 : 0;
  if (!lobby_) {
    if (!e.activate_running_copy()) return false;
  } else {
    e.close_running_copy();
    lobby_launch_ = 1;
    pick_post_intro_state(true);
    int32_t major = 2, minor = 0;
    if (!e.winsock_check(&major, &minor)) {  // "needs Winsock 2.0": message 0xf60
      char version[64] = "";
      if (major != 0) e.print_version(version, sizeof version, major, minor);
      const std::string text = e.message_format(0xf60, version);
      e.message_box_text(g_globals.main_hwnd, text.c_str(), e.message(0xd78), 0x30);
      e.exit_process(0);
      return false;
    }
    if (!e.lobby_connect()) return false;
  }
  if (!e.register_window_class()) return false;  // FIX (A): "Mech3Class", icon 0x94
  g_globals.class_registered = 1;
  const char* version = e.version_string();
  e.settings_init("MicroProse", e.message(7), version);
  e.input_init(0xa7, "DefaultCmdMap");
  e.list_pool_init(0x200);
  e.reader_init("..\\data\\common\\zrdr");
  if (!e.options_load("detail.zrd")) {
    e.reader_open_archive("zbd\\reader.zbd", 1);
    if (!e.options_load("detail.zrd")) {
      // "Cannot read required data. Please ensure proper installation."
      const std::string caption = e.message(7);
      e.message_beep(0x10);
      e.message_box_text(g_globals.main_hwnd, e.message(0x14d), caption.c_str(), 0x10);
      e.exit_process(0);
      return false;
    }
  }
  set_defaults();
  e.save_init();
  e.anim_enable(1);
  e.effects_set_a(0);
  e.effects_set_b(0);
  e.weapons_set_mode(0);
  e.decals_set_mode(0);
  e.classes_set_mode(0);
  frame_ = create_main_window();  // vtable +0xbc, after Enable3dControls (FIX (A))
  e.set_field32(frame_, 0xc0, 0x608c88);  // the frame's app: hooks know it by its address
  gpu_only();
  if (fullscreen()) restore_video(e.hw_card_flag() != 0, false);
  if (!start(main_window())) return false;
  e.update_window(main_window());
  e.options_apply_all();
  return true;
}

// Mech3.exe 1.2 @ 0x0047fa10, ExitInstance
int Mech3App::exit_instance() {
  Engine& e = *engine_;
  if (g_globals.class_registered) {
    e.unregister_window_class();  // FIX (A), then CWinApp::ExitInstance
    e.free_block(field_1e4_);
    field_1e4_ = 0;
    if (g_globals.script) e.script_delete(g_globals.script);
    g_globals.script = 0;
    e.cd_stop();
    e.ai_targets_shutdown();
    e.sound_shutdown();
    e.network_shutdown();
    e.input_devices_shutdown();
    e.list_pool_free();
    e.save_shutdown();
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x0047fab0, the app's destructor (at process exit, after
// ExitInstance): the settings are saved here (docs/settings.md). The frame's
// delete and MFC's own destructors are left to the platform (FIX (A)).
void Mech3App::destroy() {
  Engine& e = *engine_;
  if (g_globals.script) e.script_delete(g_globals.script);
  g_globals.script = 0;
  e.message_dll_free();
  e.input_maps_free();
  e.archives_shutdown();
  e.reader_shutdown();
  e.list_pool_free();
  e.options_save();
  e.settings_free();
}

// Mech3.exe 1.2 @ 0x00480570, zGameApp start (+0xac): the engine modules, the
// video mode, then the data the menus need.
bool Mech3App::start_game(uint32_t window) {
  Engine& e = *engine_;
  e.remove_debug_menu(main_window());  // FIX (A): GetMenu / SetMenu(0) / DestroyMenu
  e.geometry_init("zbd");
  auto report = [&](const char* fmt, bool failed) { e.print(fmt, failed ? "FAILED" : "PASSED"); };
  report("zEffInit:  %s\n", e.effects_init() != 0);
  report("gClsInit:  %s\n", e.classes_init() != 0);
  report("gModInit:  %s\n", e.models_init() != 0);
  report("zRndrInit: %s\n", e.render_init() != 0);
  report("zSndInit:  %s\n", e.sound_init(window) != 1);
  report("zUtlInit:  %s\n", e.util_init() != 0);
  report("zWepInit:  %s\n", e.weapons_init() != 0);
  e.textures_set_mode(1);
  e.sound_set_mode(1);
  if (!open_video(window)) {
    const std::string caption = e.message(0x1eb);
    e.message_beep(0x10);
    e.message_box_text(g_globals.main_hwnd, e.message(0x1ec), caption.c_str(), 0x10);
    return false;
  }
  const bool hw3d = e.video_mode() == 1;
  e.models_load(hw3d ? "GfxFlags_HW" : "GfxFlags_SW", e.gfx_flags());
  e.render_load(hw3d ? "GfxFlags_HW" : "GfxFlags_SW", e.gfx_flags());
  e.light_init();
  e.fonts_init("fonts.zrd", 1);
  e.sound_load(window, "sounds.zrd", 0x4e6b00, 0);
  e.input_start(window, cmd_show_);
  if (!e.joystick_present()) {
    AxisTable& t = axis_table();
    if (t.stock) t.ready = 1;
    e.set_use_joystick(0);
  } else {
    if (Handle old = e.force_feedback()) e.force_feedback_delete(old);
    e.set_force_feedback(0);
    if (e.force_feedback_present()) e.set_force_feedback(e.force_feedback_new());
  }
  const Handle win = e.window_rect();
  e.message_panel_init(e.display_rect(), win);
  e.maps_init("maps.zrd");
  e.clock_init();
  e.cd_init(1);
  int32_t message = 0;
  if (!check_video_memory(hw3d, &message)) {
    e.message_box(0, 0x1eb, 0xfbf, message);
    return false;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00480970: opens the screen and the software renderer.
bool Mech3App::open_video(uint32_t window) {
  Engine& e = *engine_;
  if (e.hw_card_flag() != 0) {
    e.video_select_device(e.hw_card_dev());
    e.effects_set_level(2);
  } else if (e.install_options(0xff00) == 0) {
    const std::string caption = e.message(0x1eb);
    const std::string body = e.message(0xf41);
    e.message_box_text(e.focus_window(), body.c_str(), caption.c_str(), 0x30);
    return false;
  }
  const uint8_t restore = fullscreen();
  const bool hw = e.hw_card_flag() != 0;
  if (e.video_open(window, hw ? 1 : 0, restore, e.vmode()) != 0) {
    e.print("Error opening video... ABORTING RUN\n", nullptr);
    e.flush_stdout();
    return false;
  }
  if (e.video_mode() == 0 && e.span_init(e.window_h()) != 0) {
    e.print("Error opening HSE... ABORTING RUN\n", nullptr);
    e.flush_stdout();
    return false;
  }
  const Handle a = e.video_back_buffer();
  const Handle b = e.video_front_buffer();
  const Handle r = e.video_screen_rect();
  e.render_set_target(e.video_target(), r, b, a);
  e.render_set_pitch(e.video_pitch());
  e.render_reset();
  e.video_clear(0);
  e.video_flip(0, 0, 1, 0);
  return true;
}

// Mech3.exe 1.2 @ 0x004804f0: a 3D card needs 4 MB, unless installed otherwise.
bool Mech3App::check_video_memory(bool hw3d, int32_t* message) {
  Engine& e = *engine_;
  if (!hw3d) return true;
  uint32_t mb = 0;
  if (e.hw_card_flag() != 0 && e.video_card_fn_390(-1) == 0) {
    uint32_t total = 0;
    mb = e.video_card_memory(-1, &total) != 0 ? (total >> 20) : 1;
  }
  if (mb != 0 && mb < 4 && e.install_options(1) == 0) {
    *message = 2;
    return false;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x004808a0, the zGameApp stop (+0xb0): everything in reverse.
// [0x00608d5c] (a fatal error happened) skips the modules that may be half made.
void Mech3App::stop_game() {
  Engine& e = *engine_;
  if (e.pilot()) {
    e.set_pilot(0);
    e.pilots_delete();
  }
  e.mechs_shutdown();
  e.missions_shutdown();
  e.mech_data_shutdown();
  if (Handle ff = e.force_feedback()) e.force_feedback_delete(ff);
  const bool fatal = e.fatal_error() != 0;
  if (!fatal) {
    e.message_panel_free();
    if (e.fatal_error() == 0) e.span_shutdown();
  }
  e.network_shutdown();
  e.decals_shutdown();
  if (e.fatal_error() == 0) {
    e.geometry_shutdown();
    e.weapons_shutdown();
    e.effects_shutdown();
    e.classes_shutdown();
    e.models_shutdown();
  }
  e.sound_shutdown_groups(0x4e6be0);
  e.reader_text_shutdown();
  e.sound_shutdown();
  if (e.fatal_error() == 0) e.archives_shutdown();
  e.input_devices_shutdown();
  if (e.fatal_error() == 0) e.reader_shutdown();
  e.video_shutdown();
  e.ai_targets_shutdown();
}

// Mech3.exe 1.2 @ 0x00482d10, the main frame window's constructor (made by
// 0x0047ff90): the command line, then the window. FIX (A): the window size
// clamping to the work area, CreateEx and the debug menu are the front end's;
// FIX (trap): the "insert the CD" loop (0x00482f90) is left out.
Handle Mech3App::create_main_window() {
  Engine& e = *engine_;
  const Handle frame = e.frame_new();
  parse_command_line(e, *this, e.command_line());
  e.input_set_mode(static_cast<uint32_t>(input_mode_));
  const Handle hwnd = e.frame_create(frame);
  e.attach_debug_menu(e.frame_hwnd(frame));
  g_globals.main_hwnd = e.frame_hwnd(frame);
  e.error_log_open(hwnd, 0, "mech3.err");
  const char* version = e.version_number();
  char title[256];
  e.format("%s %s", {e.message(7), version}, title, sizeof title);
  e.frame_set_title(frame, title);
  return frame;
}

// Mech3.exe 1.2 @ 0x00482d10 (its first part): strtok on " ", strncmp prefixes.
void parse_command_line(Engine& e, Mech3App& app, const char* line) {
  std::vector<char> buf(line, line + std::strlen(line) + 1);
  for (char* tok = std::strtok(buf.data(), " "); tok; tok = std::strtok(nullptr, " ")) {
    if (std::strncmp(tok, "-nofmv", 6) == 0) {
      app.fmv() = 0;
    } else if (std::strncmp(tok, "-seasick", 8) == 0) {
      e.set_fast_menu(1);
    } else if (std::strncmp(tok, "-cd", 3) == 0) {
      app.set_cd_track(tok[3] == '\0' ? 0 : static_cast<int32_t>(std::atol(tok + 3)));
    }
  }
}

}  // namespace mech3
