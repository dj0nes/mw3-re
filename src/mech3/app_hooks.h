#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mech3/lance_armor.h"

#include "gamez/zgame/zgameapp.h"

namespace zcls { struct Node; }
namespace zimg { struct Texture; }
namespace ztwod { class Object2D; class Dialog; }
namespace zrdr { struct Node; }

// What the game's app and states call in code not ported yet (docs/game-loop.md).
// One method per original function, its address in the key; by default each
// reports to unported() and does nothing. Ported functions default to the port.
//
// Objects of modules not ported are opaque handles. Their fields are read and
// written through peek/poke with the original's addresses and offsets, and their
// virtual methods through vcall(object, vtable slot), so the code stays the
// original's and a module, once ported, replaces the generic access.
namespace mech3 {

using zgame::Arg;
using zgame::Handle;

class MenuScreen;
class Pilot;

class Engine : public zgame::AppHooks {
 public:
  // Memory of modules not ported yet, by the original's address.
  virtual uint32_t peek32(uint32_t addr) { return unported("peek32", {addr}); }
  virtual uint8_t peek8(uint32_t addr) { return static_cast<uint8_t>(unported("peek8", {addr})); }
  virtual void poke32(uint32_t addr, uint32_t v) { unported("poke32", {addr, v}); }
  virtual void poke8(uint32_t addr, uint8_t v) { unported("poke8", {addr, v}); }
  uint32_t field32(Handle obj, uint32_t off) { return peek32(obj + off); }
  uint8_t field8(Handle obj, uint32_t off) { return peek8(obj + off); }
  void set_field32(Handle obj, uint32_t off, uint32_t v) { poke32(obj + off, v); }
  void set_field8(Handle obj, uint32_t off, uint8_t v) { poke8(obj + off, v); }
  // A virtual call on an object not ported: "vt+<slot>" (object, arguments...).
  virtual uint32_t vcall(Handle obj, uint32_t slot, std::initializer_list<Arg> args);

  // The globals these files read.
  Handle pilot() { return peek32(0x60861c); }          // the current pilot ([0x0060861c])
  void set_pilot(Handle p) { poke32(0x60861c, p); }
  Handle player_view() { return peek32(0x5f4c8c); }    // the player's view ([0x005f4c8c])
  Handle force_feedback() { return peek32(0x608620); }  // [0x00608620] the force feedback (mech3/force_feedback)
  void set_force_feedback(Handle f) { poke32(0x608620, f); }
  int32_t video_mode() { return static_cast<int32_t>(peek32(0x72f348)); }  // 0 software, 1 Direct3D
  int32_t fatal_error() { return static_cast<int32_t>(peek32(0x608d5c)); }
  virtual const char* pilot_name();                    // [0x0060861c]+0x18, null without a pilot
  virtual float frame_dt();                            // [0x0072c52c], ported: ztimer
  // The options ([0x005f4d10], [0x005f4d2c], [0x005f4d30]), ported.
  virtual int32_t in_game_vmode();
  // Ours: the menus' mode, 5 (the exe's 640x480), or with wide_menus the
  // mission's mode when its canvas is wider than 4:3 (16:9). Chosen once, at the
  // start-up: the menu screens are laid out for it when made, so a mode
  // picked in the menus applies to them at the next start.
  int32_t menu_vmode();
  int32_t menu_vmode_ = -1;
  virtual float mouse_accel_x();
  virtual float mouse_accel_y();

  // The options' functions (mech3/options), ported.
  virtual int32_t hw_card_flag();                      // 0x0040d4d0
  virtual int32_t hw_card_dev();                       // 0x0040cc10
  virtual void select_hw(int32_t hw);                  // 0x0040d4c0
  virtual void set_hw_card_dev(int32_t dev);           // 0x0040cbf0
  virtual int32_t vmode();                             // 0x0040d9d0
  virtual void set_vmode(int32_t mode);                // 0x0040d810
  virtual int32_t gfx_flags();                         // 0x0040d480
  virtual int32_t pixel_double();                      // 0x0040d4b0
  virtual Handle render_rect();                        // 0x0040d6a0 (1)
  virtual Handle display_rect();                       // 0x0040d750 (2)
  virtual Handle window_rect();                        // 0x0040d7b0 (3)
  virtual Handle display_depth();                      // 0x0040d790
  virtual Handle vstride();                            // 0x0040d7a0
  virtual int32_t window_h();                          // 0x0040d7c0
  virtual void set_use_joystick(int32_t on);           // 0x0040d070
  virtual void set_fast_menu(int32_t on);              // 0x0040ce90
  virtual int32_t fast_menu();                         // 0x0040cec0
  virtual int32_t display_width();                     // 0x0040d760
  virtual int32_t display_height();                    // 0x0040d770
  virtual void options_apply_all();                    // 0x0040cb30
  // The game options screen's (docs/menus.md), ported.
  virtual int32_t game_intensity();                    // 0x0040cdf0
  virtual void set_game_intensity(int32_t v);          // 0x0040cc20
  virtual int32_t camera_mode();                       // 0x0040ce70
  virtual void set_camera_mode(int32_t v);             // 0x0040ce10
  virtual int32_t invincible();                        // 0x0040cf20
  virtual void set_invincible(int32_t v);              // 0x0040ced0
  virtual int32_t infinite_ammo();                     // 0x0040cf80
  virtual void set_infinite_ammo(int32_t v);           // 0x0040cf30
  virtual int32_t heat();                              // 0x0040cfc0
  virtual void set_heat(int32_t v);                    // 0x0040cf90
  virtual int32_t torso_auto_return();                 // 0x0040d010
  virtual void set_torso_auto_return(int32_t v);       // 0x0040cfd0
  virtual int32_t throttle_const();                    // 0x0040d060
  virtual void set_throttle_const(int32_t v);          // 0x0040d020
  virtual int32_t mw2_jump_jets();                     // 0x0040d1e0
  virtual void set_mw2_jump_jets(int32_t v);           // 0x0040d1c0
  virtual void set_mouse_accel_y(float v);             // *[0x005f4d30] = v (no setter)
  virtual void settings_write();                       // 0x00597a60, ported: zreg::write_all
  // The graphics and audio screen's, ported.
  virtual float cd_volume();                           // 0x0040dae0
  virtual void set_cd_volume(float v);                 // 0x0040dab0
  virtual float sound_frequency();                        // 0x0040daa0
  virtual void set_sound_frequency(float v);              // 0x0040da80
  virtual int32_t texture_memory();                    // 0x0040d410
  virtual void set_texture_memory(int32_t v);          // 0x0040d3f0
  virtual int32_t sound_lod();                         // 0x0040d3e0
  virtual void set_sound_lod(int32_t v);               // 0x0040d3d0
  virtual int32_t object_lod();                        // 0x0040d3b0
  virtual void set_object_lod(int32_t v);              // 0x0040d310
  virtual int32_t effects_level();                     // 0x0040d2f0
  virtual void set_effects_level(int32_t v);           // 0x0040d2a0
  virtual int32_t shadow_parts();                      // 0x0040d220
  virtual void set_shadow_parts(int32_t v);            // 0x0040d1f0
  virtual int32_t shadow();                            // 0x0040d280
  virtual void set_shadow(int32_t v);                  // 0x0040d240
  virtual void set_gfx_flags(int32_t v);               // 0x0040d430
  virtual void set_in_game_vmode(int32_t v);           // *[0x005f4d10] = v (no setter)
  // 0x004ab860 (ECX device, EDX the lowest mode out): the screen modes a
  // device can run, the highest by its video memory. Ported, on zVideo's
  // memory counts below.
  virtual void video_mode_range(int32_t device, int32_t* lo, int32_t* hi);
  virtual int32_t video_memory_total() { return static_cast<int32_t>(unported("0x00579a10", {})); }
  virtual int32_t video_memory_used() { return static_cast<int32_t>(unported("0x00579a40", {})); }
  virtual uint32_t video_has_cap(int32_t cap) { return unported("0x00579b70", {cap}); }
  virtual uint32_t video_device_count() { return unported("0x00579aa0", {}); }
  virtual const char* video_device_name(uint32_t i) {  // 0x00579a70
    unported("0x00579a70", {i});
    return "";
  }
  // zModel's and zRender's GfxFlags: the settings entry `name` if the store
  // has it, else a copy of `flags` (0x00526750, 0x00557ae0).
  virtual void model_use_gfx_flags(const char* name, int32_t flags) { unported("0x00526750", {name, flags}); }
  virtual void render_use_gfx_flags(const char* name, int32_t flags) { unported("0x00557ae0", {name, flags}); }
  virtual void pop_state(uint32_t arg);                // 0x005905a0, ported: zGameApp::Pop
  // Replace(state(4), 0) (0x00482970, 0x00590250): the Exit state, so the game
  // restarts with a new video device. Ported.
  virtual void replace_with_exit();
  virtual void effects_set_level(int32_t v) { unported("0x00508fb0", {v}); }
  virtual int32_t options_load(const char* name) {     // 0x0040c420, detail.zrd + registry
    return static_cast<int32_t>(unported("0x0040c420", {name}));
  }

  // Windows, through the front end (FIX (A)).
  virtual uint32_t lobby_connection() { return unported("0x005899b0", {}); }  // DirectPlay lobby
  virtual uint32_t activate_running_copy() { return unported("0x00480340", {}); }  // 0: one runs
  virtual void close_running_copy() { unported("0x004803a0", {}); }
  virtual uint32_t winsock_check(int32_t* major, int32_t* minor) {
    return unported("0x00575e30", {*major, *minor});
  }
  // 0x00575fd0: message `id` of Mech3Msg.dll formatted with one string argument.
  virtual const char* message_format(int32_t id, const char* arg);
  virtual const char* message_format_n(int32_t id, int32_t n);  // 0x00575fd0 with a number
  // sprintf, for the "%d.%d" Winsock version.
  virtual void print_version(char* out, int size, int32_t major, int32_t minor);
  virtual uint32_t lobby_connect() { return unported("0x00589a50", {0x5c3f28}); }
  virtual uint32_t register_window_class() { return unported("AfxRegisterClass", {}); }
  virtual void unregister_window_class() { unported("UnregisterClassA", {"Mech3Class"}); }
  virtual void update_window(uint32_t hwnd) { unported("UpdateWindow", {hwnd}); }
  virtual void message_beep(uint32_t type) { unported("MessageBeep", {type}); }
  virtual uint32_t message_box_text(uint32_t hwnd, const char* text, const char* caption, uint32_t type) {
    return unported("MessageBoxA", {hwnd, text, caption, type});
  }
  virtual uint32_t focus_window() { return unported("GetFocus", {}); }
  virtual void exit_process(uint32_t code);            // ExitProcess
  virtual void lock_system_keys(uint32_t on) {         // SystemParametersInfoA(0x61, on, ..)
    unported("SystemParametersInfoA", {0x61, on});
  }
  virtual void sleep(uint32_t ms) { unported("Sleep", {ms}); }
  virtual void cursor_pos(int32_t* x, int32_t* y);     // GetCursorPos
  virtual uint32_t rand();                             // MSVCRT rand, ported: crt
  virtual void remove_debug_menu(uint32_t hwnd) { unported("SetMenu", {hwnd, 0}); }
  // The debug menu (MENU resource 0x93, gametypes.md) on the window: SetMenu
  // with the loaded menu, whose handle the MFC wrapper leaves 0 here.
  virtual void attach_debug_menu(uint32_t hwnd) { unported("SetMenu", {hwnd, 0}); }
  virtual const char* command_line();                  // GetCommandLineA
  virtual void print(const char* format, const char* arg);  // printf
  virtual void flush_stdout() { unported("fflush", {}); }
  virtual void run_dir() { unported("0x004828d0", {}); }    // RunDir from the registry
  virtual uint32_t message_dll_load(const char* name) { return unported("0x00575f30", {name}); }
  virtual uint32_t directx_version() { return unported("0x0057a3d0", {}); }
  // Ours: whether the menus offer the software renderer (VIDEO DEVICE's
  // "software" entry). FIX (A): it is not ported, so no; the differential
  // tests' engines say yes to check the original's cycle.
  virtual bool software_renderer() { return false; }
  virtual uint32_t install_options(uint32_t mask) {    // 0x00597d80, InstallOptions & mask
    return unported("0x00597d80", {mask});
  }

  // The app's own objects.
  virtual Handle script_new() { return unported("0x00410700", {}); }  // new(0xc0) "interp.zbd"
  virtual void script_delete(Handle s) { unported("0x0047faa0", {s}); }
  virtual void free_block(Handle p) { unported("delete", {p}); }  // operator delete
  virtual Handle frame_new() { return unported("0x00590920", {"mech3"}); }  // new(0xd0) frame
  virtual Handle frame_create(Handle frame) { return unported("CWnd::CreateEx", {frame}); }
  virtual uint32_t frame_hwnd(Handle frame) { return field32(frame, 0x20); }
  virtual void frame_set_title(Handle frame, const char* title) {
    unported("CWnd::SetWindowTextA", {frame, title});
  }
  virtual void error_log_open(uint32_t hwnd, uint32_t a, const char* name) {
    unported("0x0050da50", {hwnd, a, name});
  }
  virtual const char* message(int32_t id);             // 0x00576040, Mech3Msg.dll's text
  virtual const char* version_string();                // 0x004af280
  virtual const char* version_number();                // 0x004af290
  // 0x004ac150 (cdecl): a message box from the message table; the fourth
  // argument (a format argument) is pushed only by some callers.
  virtual uint32_t message_box(int32_t a, int32_t caption, int32_t text, int32_t arg = 0) {
    (void)arg;
    return unported("0x004ac150", {a, caption, text});
  }
  virtual int32_t pick_video_device(int32_t a, int32_t b) {  // 0x004eaab0
    return static_cast<int32_t>(unported("0x004eaab0", {a, b}));
  }

  // Engine modules at start and stop.
  virtual void settings_init(const char* company, const char* product, const char* version) {
    unported("0x00598210", {company, product, version});
  }
  virtual void input_init(int32_t commands, const char* map) { unported("0x0051c630", {commands, map}); }
  virtual void list_pool_init(int32_t links) { unported("0x00555cd0", {links}); }
  virtual void reader_init(const char* dir) { unported("0x00556170", {dir}); }
  virtual void reader_open_archive(const char* path, int32_t current) {
    unported("0x00557320", {path, current});
  }
  virtual void save_init() { unported("0x005a8b30", {}); }  // zSave's start (docs/save.md)
  virtual void anim_enable(int32_t on) { unported("0x005090d0", {on}); }
  virtual void effects_set_a(int32_t v) { unported("0x0050d1e0", {v}); }
  virtual void effects_set_b(int32_t v) { unported("0x0050d1f0", {v}); }
  virtual void weapons_set_mode(int32_t v) { unported("0x00583740", {v}); }
  virtual void decals_set_mode(int32_t v) { unported("0x00501f90", {v}); }
  virtual void classes_set_mode(int32_t v) { unported("0x004f8f60", {v}); }
  virtual void geometry_init(const char* dir) { unported("0x00516f50", {dir}); }
  virtual int32_t effects_init() { return static_cast<int32_t>(unported("0x0050d270", {})); }
  virtual int32_t classes_init() { return static_cast<int32_t>(unported("0x004f8f20", {})); }
  virtual int32_t models_init() { return static_cast<int32_t>(unported("0x00526510", {})); }
  virtual int32_t render_init() { return static_cast<int32_t>(unported("0x005578f0", {})); }
  virtual int32_t sound_init(uint32_t window) { return static_cast<int32_t>(unported("0x005700f0", {window})); }
  virtual int32_t util_init() { return static_cast<int32_t>(unported("0x00576c50", {})); }
  virtual int32_t weapons_init() { return static_cast<int32_t>(unported("0x00583750", {})); }
  virtual void textures_set_mode(int32_t v) { unported("0x00518790", {v}); }
  virtual void sound_set_mode(int32_t v) { unported("0x0056f4b0", {v}); }
  virtual void models_load(const char* flags_name, int32_t flags) { unported("0x00526750", {flags_name, flags}); }
  virtual void render_load(const char* flags_name, int32_t flags) { unported("0x00557ae0", {flags_name, flags}); }
  virtual void light_init();                           // 0x00542bb0, ported: zmod::shadows_init
  virtual void fonts_init(const char* name, int32_t a) { unported("0x00516e90", {name, a}); }
  virtual void sound_load(uint32_t window, const char* name, uint32_t callback, uint32_t a) {
    unported("0x005705e0", {window, name, callback, a});
  }
  virtual void input_start(uint32_t window, uint32_t cmd_show) { unported("0x0051b0a0", {window, cmd_show}); }
  virtual uint32_t joystick_present() { return unported("0x0051dc10", {}); }
  virtual uint32_t force_feedback_present() { return unported("0x0051ddb0", {}); }
  // The force feedback object (mech3/force_feedback): new(0x34) + 0x00433090,
  // its delete 0x004333f0, its Spring 0x004338a0 (the mission's start).
  virtual Handle force_feedback_new() { return unported("0x00433090", {}); }
  virtual void force_feedback_delete(Handle f) { unported("0x004333f0", {f}); }
  virtual void force_feedback_spring(Handle f, int32_t on) { unported("0x004338a0", {f, on}); }
  // 0x0040f410: the message panel made (ECX, EDX the display and window
  // rects, unused; mech3/message_panel).
  virtual void message_panel_init(Handle display, Handle window) { unported("0x0040f410", {display, window}); }
  virtual void maps_init(const char* name) { unported("0x004583b0", {name}); }
  virtual void clock_init() { unported("0x005759a0", {}); }
  virtual void cd_init(int32_t a) { unported("0x00575d00", {a}); }
  virtual void video_select_device(int32_t dev) { unported("0x00579ab0", {dev}); }
  virtual int32_t video_open(uint32_t window, int32_t hw, int32_t restore, int32_t vmode) {
    return static_cast<int32_t>(unported("0x00579c60", {window, hw, restore, vmode}));
  }
  virtual int32_t span_init(int32_t height) { return static_cast<int32_t>(unported("0x00564370", {height})); }
  virtual uint32_t video_card_fn_390(int32_t dev) { return unported("[0x0072f390]", {dev}); }
  virtual uint32_t video_card_memory(int32_t dev, uint32_t* total) {
    const uint32_t r = unported("[0x0072f38c]", {dev});
    *total = unported("@video_total", {});
    return r;
  }
  virtual void pilots_delete() { unported("0x004785f0", {}); }
  virtual void mechs_shutdown() { unported("0x00414000", {}); }
  virtual void missions_shutdown() { unported("0x004844b0", {}); }
  virtual void mech_data_shutdown() { unported("0x004584e0", {}); }
  virtual void message_panel_free() { unported("0x0040f7d0", {}); }  // the message panel deleted
  virtual void span_shutdown() { unported("0x005646d0", {}); }
  virtual void network_shutdown() { unported("0x00586c20", {}); }
  virtual void decals_shutdown() { unported("0x00502020", {}); }
  virtual void geometry_shutdown() { unported("0x00516f00", {}); }
  virtual void weapons_shutdown() { unported("0x00583870", {}); }
  virtual void effects_shutdown() { unported("0x0050d2b0", {}); }
  virtual void classes_shutdown() { unported("0x004f8f40", {}); }
  virtual void models_shutdown() { unported("0x00526780", {}); }
  virtual void sound_shutdown_groups(uint32_t callback) { unported("0x005715d0", {callback}); }
  virtual void reader_text_shutdown() { unported("0x00557b10", {}); }
  virtual void sound_shutdown() { unported("0x00570510", {}); }
  virtual void archives_shutdown() { unported("0x00576c60", {}); }
  virtual void input_devices_shutdown() { unported("0x0051b170", {}); }
  virtual void reader_shutdown() { unported("0x00556210", {}); }
  virtual void video_shutdown() { unported("0x00579ef0", {}); }
  virtual void ai_targets_shutdown() { unported("0x004d72a0", {}); }
  virtual void list_pool_free() { unported("0x00555d90", {}); }
  virtual void save_shutdown() { unported("0x005a8bb0", {}); }  // zSave's end
  // The app's destructor (0x0047fab0).
  virtual void message_dll_free() { unported("0x00575f70", {}); }
  virtual void input_maps_free() { unported("0x0051d0c0", {}); }
  virtual void options_save();                         // 0x0040cb20, ported
  virtual void settings_free();                        // 0x00598270, ported
  virtual void input_set_mode(uint32_t mode) { unported("0x0051bae0", {mode}); }

  // zVideo's buffers and frame (zvid_buff.c and neighbours).
  virtual Handle video_back_buffer() { return unported("0x00576db0", {}); }
  virtual Handle video_front_buffer() { return unported("0x00576c70", {}); }
  virtual Handle video_screen_rect() { return unported("0x00579880", {}); }
  virtual Handle video_target() { return unported("0x00576d80", {}); }
  virtual Handle video_pitch() { return unported("0x00576dc0", {}); }
  virtual Handle video_buffer_da0() { return unported("0x00576da0", {}); }
  virtual Handle video_buffer_d90() { return unported("0x00576d90", {}); }
  virtual Handle video_buffer_cd0() { return unported("0x00576cd0", {}); }
  virtual Handle video_buffer_ca0() { return unported("0x00576ca0", {}); }
  virtual void video_clear(Handle rect) { unported("0x00576dd0", {rect}); }
  virtual void video_clear_a(Handle rect) { unported("0x00576ce0", {rect}); }
  virtual void video_clear_b(Handle rect) { unported("0x00576cf0", {rect}); }
  virtual void video_begin_3d() { unported("0x00576d00", {}); }
  virtual void video_end_3d() { unported("0x00576d60", {}); }
  virtual void video_lock() { unported("0x00576de0", {}); }
  virtual void video_unlock() { unported("0x00576e70", {}); }
  virtual void video_flip(int32_t a, int32_t b, int32_t c, int32_t d) { unported("0x00576ea0", {a, b, c, d}); }
  virtual void video_set_frame_mode(int32_t a) { unported("0x00579250", {a}); }
  virtual uint32_t video_show_cursor(uint32_t on) { return unported("0x00579840", {on}); }
  virtual void video_set_clear(int32_t on) { unported("0x0057a3c0", {on}); }  // [0x008026f0] the frame clear, the old back
  virtual void video_restore() { unported("0x00577cb0", {}); }
  virtual void video_begin_scene() { unported("0x00579af0", {}); }
  virtual void video_draw_3d() { unported("[0x0072f3ec]", {}); }
  virtual void video_end_scene() { unported("0x00579b10", {}); }
  virtual void video_double(Handle a, Handle b) { unported("[0x0072f358]", {a, b}); }
  virtual void video_apply_fog() { unported("[0x0072f3f0]", {}); }
  // The fade state's screen calls (0x0048f0b0).
  virtual Handle video_grab(int32_t buffer) { return unported("0x00579500", {buffer}); }  // a new image
  // 0x0055b1f0: ECX image, EDX x; y, the colour key, the source rectangle (0: all).
  virtual void image_draw(Handle image, int32_t x, int32_t y, int32_t key, Handle rect) {
    unported("0x0055b1f0", {image, x, y, key, rect});
  }
  // 0x00558540: CX colour, EDX rectangle (0: the screen); the alpha a double.
  virtual void screen_tint(uint16_t colour, Handle rect, double alpha);
  virtual void image_free(Handle image) { unported("0x00517cc0", {image}); }
  virtual void render_set_target(Handle target, Handle rect, Handle b, Handle a) {
    unported("0x00557f90", {target, rect, b, a});
  }
  virtual void render_set_target2(Handle target, Handle rect, Handle b, Handle a) {
    unported("0x00558290", {target, rect, b, a});
  }
  virtual void render_set_pitch(Handle p) { unported("0x00558070", {p}); }
  virtual void render_reset() { unported("0x00557b20", {}); }
  virtual void render_flush(Handle rect) { unported("0x00558010", {rect}); }
  virtual void render_finish() { unported("0x00558610", {}); }

  // The 2D front end (zTwoD) and the shell screens.
  virtual void twod_set_cursor(uint32_t v) { unported("0x0059fb80", {v}); }
  virtual void twod_reset(Handle h) { unported("0x0059fa10", {h}); }
  virtual void screen_refresh(Handle s) { unported("0x0059fa60", {s}); }
  virtual void screen_hide(Handle s) { unported("0x0059fa40", {s}); }
  virtual void shell_clear(uint32_t state) { unported("0x0048ee00", {state}); }
  // zGameApp::Push (0x00590400) of a state the port does not have (a menu
  // object of 0x00609858..0x00609878, by its handle).
  virtual void push_state(Handle state, uint32_t arg) { unported("0x00590400", {state, arg}); }
  // The menu objects' fields the screens read: a shell state's screen (+0x0c).
  virtual Handle state_screen(Handle state) { return peek32(state + 0xc); }
  // The main menu's BACKGROUND image and its scrolling ([0x00609854] +0x110ec,
  // +0x111e8); false without a main menu. Ported: mech3::main_menu().
  virtual bool main_menu_background(zimg::Texture** image, uint8_t* moving);
  virtual uint8_t mission_running();                   // 0x0047fb70: the Core state's +0x19, ported
  // The credits state's reset of its screen's clock ([screen+0x11adc] = 0).
  virtual void credits_restart(Handle screen) { poke32(screen + 0x11adc, 0); }
  virtual void confirm_quit();                         // 0x00482a20, ported: Mech3App::confirm_quit
  virtual Handle construct(uint32_t ctor, std::initializer_list<Arg> args) {  // new + ctor
    char key[16];
    format_key(key, ctor);
    return unported(key, args);
  }
  virtual Handle wait_new(int32_t a, int32_t b) { return unported("0x00599f70", {a, b}); }
  virtual void wait_start() { unported("wait.vt+0x8", {0, 0}); }
  virtual uint32_t wait_poll() { return unported("wait.vt+0x4", {0, 0}); }
  virtual void wait_end() { unported("wait.vt+0xc", {}); }

  // Movies (zFmv).
  virtual Handle movie_new() { return unported("0x00598cc0", {0, 0, 0}); }  // new(0x20)
  virtual void movie_set_window(Handle m, uint32_t hwnd) { poke32(m + 4, hwnd); }
  virtual int32_t movie_load(Handle m, const char* file, const char* name) {
    return static_cast<int32_t>(unported("0x00598d80", {m, file, name}));
  }
  virtual void movie_play(Handle m) { unported("0x005996d0", {m}); }
  virtual uint32_t movie_playing(Handle m) { return unported("0x00599710", {m}) & 0xff; }
  virtual void movie_stop(Handle m, int32_t a) { unported("0x00599750", {m, a}); }
  virtual void movie_delete(Handle m) { unported("0x00598d00", {m}); }  // + operator delete

  // Input (zInput).
  virtual void input_poll(uint8_t a) { unported("0x0051b360", {a}); }
  virtual uint32_t key_state(int32_t key) { return unported("0x0051a9b0", {key}); }  // a DIK code's state, zin::key_state
  virtual void input_clear() { unported("0x0051a300", {}); }
  virtual void input_reset() { unported("0x0051b1c0", {}); }
  virtual void input_capture() { unported("0x0051b310", {}); }
  virtual void input_release() { unported("0x0051b340", {}); }
  virtual void input_center_mouse() { unported("0x0051b560", {}); }
  virtual void mouse_set_pos(int32_t x, int32_t y) { unported("0x0051b4f0", {x, y}); }
  virtual void mouse_set_accel(float x, float y) { unported("0x0051b790", {zgame::bits(x), zgame::bits(y)}); }
  // The screen mode switch (resolution.md step 3): 0x004ab6b0 (ECX the mode;
  // the mode it is in after, the old one when zVideo refuses), ported on the
  // hooks below; Shift+V 0x004ab910, the next in the device's range.
  virtual int32_t set_screen_mode(int32_t mode);
  virtual void vmode_toggle();
  // zVideo's mode (0x00576c80, ECX the mode: its sizes, the surfaces and the
  // device made again; non-zero when that fails).
  virtual int32_t video_set_mode(int32_t mode) { return static_cast<int32_t>(unported("0x00576c80", {mode})); }
  // The software path's still drawn back (the display rect; [0x0072f360]: ECX image, EDX, then two words).
  virtual void video_blit(Handle image, Handle a, Handle b, Handle rect) { unported("[0x0072f360]", {image, a, b, rect}); }
  // 0x0040f330: the message panel placed; ECX a rect kept, EDX the rect it
  // is centred in (the switch passes the display rect, then the render rect).
  virtual void message_panel_place(Handle kept, Handle area) { unported("0x0040f330", {kept, area}); }
  // The switch's loop over zEntity's list: each object's mech (vt+0x2c, vt+0x50)
  // has its cockpit refitted (0x0041cdc0). The program keeps its own list.
  virtual void screen_mode_cockpits();
  virtual void cockpit_refit(Handle mech);   // 0x0041cdc0, ported
  virtual void hud_setup(Handle hud, Handle mech) { unported("0x0043bb80", {hud, mech}); }
  // What the controls screen calls (docs/menus.md, The controls screen): zInput's
  // key and button capture and its command map (the current map's 0x0051d2xx
  // wrappers), the stick's reading and the controls setup (controls_cfg).
  // Ported: the defaults are the ports.
  virtual uint32_t input_read_key(int32_t wait);                // 0x0051aa00
  virtual int32_t joy_wait_button(int32_t wait);                // 0x0051dd20
  virtual int32_t mouse_wait_button(int32_t wait);              // 0x0051baf0
  virtual Handle joy_state() { return unported("0x0051dce0", {}); }  // the stick's last read, 0 none
  // 0x0042fea0 (src, joy, mouse 0): a source's reading off a joy_state() read.
  virtual float source_value(int32_t src, Handle joy) {
    return zgame::from_bits(unported("0x0042fea0", {src, joy, 0}));
  }
  virtual void joy_use();                                       // 0x0051b2c0
  virtual void joy_unuse();                                     // 0x0051b2f0
  virtual uint32_t key1_of(int32_t cmd);                        // 0x0051d200
  virtual uint32_t key2_of(int32_t cmd);                        // 0x0051d210
  virtual uint32_t joy_of(int32_t cmd);                         // 0x0051d220
  virtual uint32_t mouse_of(int32_t cmd);                       // 0x0051d230
  virtual int32_t key1_command(uint32_t key);                   // 0x0051d240
  virtual int32_t key2_command(uint32_t key);                   // 0x0051d250
  virtual int32_t joy_command(uint32_t b);                      // 0x0051d260
  virtual int32_t mouse_command(uint32_t b);                    // 0x0051d270
  virtual void bind_key1(uint32_t key, int32_t cmd);            // 0x0051d280
  virtual void bind_key2(uint32_t key, int32_t cmd);            // 0x0051d290
  virtual void bind_joy(uint32_t b, int32_t cmd);               // 0x0051d2a0
  virtual void bind_mouse(uint32_t b, int32_t cmd);             // 0x0051d2b0
  virtual const char* command_name(int32_t cmd, char* out, int32_t n);      // 0x0051d310
  virtual const char* key_name(uint32_t key, char* out, int32_t n);         // 0x0051d330
  virtual const char* joy_button_name(uint32_t b, char* out, int32_t n);    // 0x0051d350
  virtual const char* mouse_button_name(uint32_t b, char* out, int32_t n);  // 0x0051d370
  virtual void input_map_refresh();                             // 0x0051d1c0
  virtual int32_t controls_defaults();                          // 0x00411150
  virtual int32_t controls_group_count();                       // 0x00410f80
  virtual const char* controls_group_title(int32_t group);      // 0x00410fa0
  virtual int32_t controls_group_size(int32_t group);           // 0x00410fb0
  virtual int32_t controls_group_command(int32_t group, int32_t i);  // 0x00410fd0
  virtual void controls_read(const char* path);                 // 0x004120a0
  virtual void controls_write(const char* path);                // 0x00411e60
  virtual char* active_ctl_cfg();                               // *[0x005f4d40], ActiveCtlCfg (0x104 bytes)
  // FindFirstFileA / FindNextFileA's files for a pattern ("keys\\*.zrd"), in
  // the listing's order. FIX (A): the write dir's and the mounts' (zutl::list).
  virtual std::vector<std::string> list_files(const char* pattern);
  // 0x004ac150 with three strings for the message's %1 %2 %3.
  virtual uint32_t message_box3(int32_t a, int32_t caption, int32_t text, const char* s1, const char* s2,
                                const char* s3) {
    return unported("0x004ac150", {a, caption, text, s1, s2, s3});
  }
  // A ported screen made by a ported state rather than by construct(): its
  // handle (the program registers it as construct's screens; 0 elsewhere).
  virtual Handle adopt_screen(MenuScreen* screen, uint32_t size) {
    (void)screen, (void)size;
    return 0;
  }

  // Sound and music.
  virtual void cd_pause();                             // 0x00572c90, ported: zsnd_cd
  virtual void cd_resume(int32_t mode);                // 0x00572d70, ported
  virtual void cd_stop();                              // 0x00572c30, ported
  virtual int32_t cd_track_count();                    // 0x00573040, ported
  virtual void music_play(int32_t track, int32_t mode);  // 0x004ac290, ported: mech3_music
  virtual void sound_update(int32_t a, int32_t b) { unported("0x0056dfb0", {a, b}); }
  virtual Handle sound_find(const char* name) { return unported("0x0056f6f0", {name}); }
  virtual void sound_play(Handle s, float v) { unported("0x0056e3d0", {s, zgame::bits(v)}); }
  // The briefing's voice (zsnd): played (0x0056e3d0, the voice), its state
  // (0x0056e8e0: 1 playing, 0 not, -1 none), its cue markers sent to
  // briefing_cue (0x0056f3e0 with 0x00405ed0).
  virtual Handle voice_play(Handle s, float v) { return unported("0x0056e3d0", {s, zgame::bits(v)}); }
  virtual int32_t voice_status(Handle v) { return static_cast<int32_t>(unported("0x0056e8e0", {v})); }
  virtual void sound_cue_briefing(Handle s) { unported("0x0056f3e0", {s}); }
  virtual Handle sound_listener() { return unported("0x0056eb10", {}); }
  virtual void sound_stop_all(Handle s) { unported("0x0056f0a0", {s}); }
  virtual void sound_release_all(Handle s) { unported("0x0056f190", {s}); }
  // The pause screen's (0x00492750, 0x00492d70): the paused sounds resumed,
  // and zSound's playing groups taken away, given back or freed.
  virtual void sound_resume_all(Handle s) { unported("0x0056f130", {s}); }
  virtual Handle sound_take_playing() { return unported("0x005732f0", {}); }
  virtual void sound_restore_playing(Handle l) { unported("0x00573300", {l}); }
  virtual void sound_free_playing(Handle l) { unported("0x00573350", {l}); }
  virtual void sound_reset() { unported("0x005716a0", {}); }
  virtual void voice_start(const char* name) { unported("0x0056f520", {name}); }
  virtual void voice_stop(Handle s) { unported("0x0056e9f0", {s}); }
  virtual void voice_release(const char* name) { unported("0x0056f530", {name}); }

  // The mission: world, pilots, mechs, views.
  virtual Handle pilot_get(const char* name) { return unported("0x004788b0", {name}); }
  virtual void pilot_select_game(Handle p, int32_t type) { unported("0x00478250", {p, type}); }
  virtual Handle pilot_game(Handle p) { return unported("0x0040a0f0", {p}); }
  virtual void pilot_set_mech(Handle p, uint32_t id) { unported("0x004783c0", {p, id}); }
  virtual void pilot_update(Handle p, float dt) { unported("0x004783a0", {p, zgame::bits(dt)}); }
  virtual void pilot_record_mission(Handle p, int32_t a) { unported("0x00478f10", {p, a}); }
  virtual Handle pilot_campaign(Handle p) { return unported("0x00479440", {p}); }
  virtual uint32_t pilot_campaign_number(Handle p) { return unported("0x00479420", {p}); }
  // 0x00477f50: the pilot of that name and its files deleted (the main menu's
  // "mechlab", a pilot the mech lab leaves). Ported: mech3::delete_pilot.
  virtual void pilot_delete(const char* name) { unported("0x00477f50", {name}); }
  // The pilot screen (docs/menus.md, The pilot screen) and the game states.
  // Ours: [0x0060861c] set by the ported pilot screen, for the parts that
  // still reach the pilot through this memory.
  virtual void pilot_chosen(Pilot* p) { (void)p; }
  // 0x00488ad0 on the Pilot State at that address: the game it is for.
  virtual void pilot_state_set_mode(Handle state, int32_t mode) { unported("0x00488ad0", {state, mode}); }
  virtual zrdr::Node* main_menu_definition();          // [0x00609854] +0x12600, the main menu's dialog.zrd tree
  virtual Handle lobby_session() { return unported("0x00586d10", {}); }
  virtual const char* lobby_player_name(Handle session) {  // 0x00588a60
    unported("0x00588a60", {session});
    return "";
  }
  // The Action screen's reset (0x0048aac0 on the Action State's screen) after the pilot.
  virtual void action_open(Handle screen) {
    (void)screen;
    unported("0x0048aac0", {});
  }
  // The Action screen's: a mech's loadouts, through its definition (0x004846a0,
  // ported: mech3::mech_dfn; null without one), and _access (0x00576080).
  virtual const struct ConfigList* mech_loadouts(int32_t mech);
  virtual bool file_exists(const char* path);
  virtual void network_set(int32_t on) { unported("0x0047fc40", {on}); }  // the app's network on or off
  virtual void network_leave() { unported("0x0046d100", {}); }  // the network session closed (the quit box's OK)
  virtual void multiplayer_switch(Handle state) { unported("0x0049cb20", {state}); }
  // 0x00494920 on the first multiplayer screen with the pilot, then the
  // second's +0x15058 (the pilot) and +0x116ec (its multiplayer game).
  virtual void multiplayer_take_pilot(Handle first, Handle second) {
    (void)second;
    unported("0x00494920", {first});
  }
  virtual void multiplayer_resume(Handle screen) { unported("0x0049b660", {screen}); }
  // 0x004ac150 with one string for the message's %1 (the pilot's delete question).
  virtual uint32_t message_box1(int32_t a, int32_t caption, int32_t text, const char* s) {
    return unported("0x004ac150", {a, caption, text, s});
  }
  // 0x00575fd0 with numbers for the message's %1!d! ...: the text.
  virtual std::string message_format_ints(int32_t id, std::initializer_list<int32_t> args);
  virtual void lab_start(Handle cfg, int32_t a) { unported("0x005a2ba0", {cfg, a}); }
  virtual void mech_apply_config(Handle mech, uint32_t cfg) { unported("0x00414430", {mech, cfg}); }
  virtual void mech_set_mode_a(Handle mech, int32_t a) { unported("0x0041ca30", {mech, a}); }
  virtual void mech_set_mode_b(Handle mech, int32_t a) { unported("0x0041cc30", {mech, a}); }
  virtual void mech_draw_cockpit(Handle mech) { unported("0x0041ab90", {mech}); }
  // The draw paths' HUD calls after the HUD frame: the pilot's HUD (MechData
  // +0x224, made when first asked), its static (hud.md section 21) and its
  // reticle's zoom window's 3D pass (+0x144c vt+0x28 -> 0x00444230).
  virtual Handle pilot_hud(Handle data) { return unported("0x00430aa0", {data}); }
  virtual void hud_static(Handle hud, float dt) { unported("0x0043cd20", {hud, zgame::bits(dt)}); }
  virtual void hud_zoom_pass(Handle hud, float dt) { vcall(hud + 0x144c, 0x28, {zgame::bits(dt)}); }
  virtual void ai_reset(uint32_t list) { unported("0x004d8610", {list}); }
  virtual void entities_sort() { unported("0x004d5ca0", {}); }  // ported: mech3/sensors
  virtual void mission_results(uint8_t a, uint8_t b) { unported("0x0047ae10", {a, b}); }
  virtual void missions_update(uint32_t list, float dt) { unported("0x0048f470", {list, zgame::bits(dt)}); }
  virtual void missions_reset(uint32_t list) { unported("0x00490720", {list}); }
  virtual void missions_start(uint32_t list) { unported("0x00490880", {list}); }
  virtual const char* mission_dir(uint32_t n);         // 0x00484c50, ported: mech3/mission
  virtual void sensors_update() { unported("0x004a8cd0", {}); }  // ported: mech3/sensors
  virtual void camera_update() { unported("0x0040f8d0", {}); }
  // The weather manager [0x00736328]: its rect slot (0x005a6790) and its
  // objects' update and draw, the rain and snow (0x005a67c0).
  virtual void weather_rect(int32_t i, Handle rect) { unported("0x005a6790", {i, rect}); }
  virtual void weather_tick(float dt) { unported("0x005a67c0", {zgame::bits(dt)}); }
  virtual void lists_update() { unported("0x004f7d80", {}); }
  virtual void view_reset(Handle view) { unported("0x0040ad90", {view}); }
  virtual void view_draw_node(Handle cam, Handle node, Handle world) { unported("0x004f73f0", {cam, node, world}); }
  // The vehicle's nodes the cockpit frame reads: its node (+0x04), its type's
  // cockpit model (+0x684 +0x228 +0x04) and an arm's node (+0x394 / +0x398, +0x08).
  virtual Handle vehicle_node(Handle veh) { return field32(veh, 0x4); }
  virtual Handle cockpit_node(Handle veh) { return field32(field32(field32(veh, 0x684), 0x228), 0x4); }
  virtual Handle arm_node(Handle veh, int32_t off) { return field32(field32(veh, off), 0x8); }
  // 0x004b51f0: the view's camera matrices and projection again (after the cockpit passes).
  virtual void view_camera_refresh(Handle view) { unported("0x004b51f0", {view}); }
  virtual void node_set_active(Handle node, int32_t on) { unported("0x004eb170", {node, on}); }
  virtual void node_set_shadow(Handle node, int32_t on) { unported("0x004eb6d0", {node, on}); }
  virtual void camera_set_5ea53c(int32_t a) { unported("0x004f3540", {a}); }  // [0x005ea53c], read by nothing
  virtual void camera_set_hse_test(int32_t on) { unported("0x004f3550", {on}); }  // CameraSetObjectHSETest
  virtual void camera_draw_scene(Handle cam, Handle scene) { unported("0x004f7350", {cam, scene}); }
  virtual void window_set_size(Handle w, int32_t width, int32_t height) {
    unported("0x004f9fa0", {w, width, height});
  }
  virtual void window_set_origin(Handle w, int32_t x, int32_t y) { unported("0x004fa0b0", {w, x, y}); }
  virtual void window_apply(Handle w) { unported("0x00526e10", {w}); }
  virtual int32_t model_global_light() { return static_cast<int32_t>(unported("0x00526a80", {})); }
  virtual void model_set_global_light(int32_t on) { unported("0x00526a70", {on}); }
  virtual Handle scene_begin() { return unported("0x005674d0", {}); }
  virtual void scene_begin_3d() { unported("0x00567570", {}); }
  virtual void scene_draw(Handle s) { unported("0x00567580", {s}); }
  virtual void scene_end(Handle s) { unported("0x005675e0", {s}); }
  virtual void scene_set_scale(int32_t a, float v) { unported("0x005674e0", {a, zgame::bits(v)}); }
  virtual void scene_set_scale_b(float v) { unported("0x00567530", {zgame::bits(v)}); }
  virtual void scene_present() { unported("0x00567dc0", {}); }
  virtual void scene_reset() { unported("0x00566f10", {}); }
  virtual void update_shadows();                       // 0x004814e0, ported: app_states
  // The projected-shadow casters (zmod, ported; docs/render-shadows.md). A
  // caster is a handle the defaults map to the port's records; the node,
  // position and light are the original's objects (node_of, peek).
  virtual Handle shadow_new();                                  // 0x00542c20
  virtual void shadow_set_size(Handle c, int32_t w, int32_t h); // 0x00542d30
  virtual void shadow_set_node(Handle c, Handle node);          // 0x00542dd0
  virtual void shadow_enable(Handle c, int32_t on);             // 0x00542e60
  virtual void shadow_set_color(Handle c, uint32_t color);      // 0x00542e00
  virtual void shadow_set_position(Handle c, Handle pos);       // 0x00542de0, 3 floats at pos
  virtual int32_t shadow_draw(Handle c, Handle light_dir, float ground);  // 0x00542e70
  virtual void shadow_upload(Handle c);                         // 0x00542e10
  virtual void shadow_release_texture(Handle c);                // 0x00542e40
  virtual void shadow_delete(Handle c);                         // 0x00542ca0
  virtual float clouds_at(float x, float z);                    // 0x00543ac0, ported: gmod_light
  virtual Handle entity_position(Handle entity) { return unported("0x005a85e0", {entity}); }  // 3 floats
  // A node handle as the port's node: none until the entities are ported.
  virtual zcls::Node* node_of(Handle node) {
    unported("node_of", {node});
    return nullptr;
  }
  virtual void hud_update() { unported("0x0040fac0", {}); }
  virtual void hud_draw() { unported("0x0040e4b0", {}); }
  virtual void shadows_reset() { unported("0x004e6900", {}); }
  virtual void ai_on() { unported("0x004ea740", {}); }  // the AI switch on (ai::ai_switch_on)
  virtual void reader_close_archives(int32_t all) { unported("0x005573d0", {all}); }
  virtual void anim_stop(int32_t a) { unported("0x0050ace0", {a}); }
  virtual void debug_mark(const char* text) { unported("0x0040fef0", {text}); }
  virtual uint32_t network_lobby_active() { return unported("0x00586ce0", {}) & 0xff; }
  virtual Handle message_box_new(const char* file, const char* name) {  // new(0x12688)
    return unported("0x0048dfe0", {file, name});
  }
  virtual void message_box_quit_prompt(Handle box) { unported("0x0048e2d0", {box}); }
  virtual void fade(int32_t a, int32_t b, int32_t c, uint32_t d, int32_t e, int32_t f, int32_t g) {
    unported("0x0048ee70", {a, b, c, d, e, f, g});
  }
  virtual Handle key_list_build() { return unported("key_list_build", {}); }  // 0x00482570's body
  virtual void hud_text_clear(Handle text) { unported("vt+0x7c", {text, ""}); }  // cdecl (this, "")

  // The app's small accessors called from the briefing (0x0040a7d0..0x0040a830).
  virtual uint32_t app_window() { return unported("0x0040a7d0", {}); }
  virtual Handle app_sounds() { return unported("0x0040a800", {}); }
  virtual uint8_t app_flag_1c3() { return static_cast<uint8_t>(unported("0x0040a830", {})); }
  virtual uint8_t briefing_has_movie() { return static_cast<uint8_t>(unported("0x0040a820", {})); }
  virtual void sounds_nop(Handle s, uint32_t a) { unported("0x004a5bc0", {s, a}); }
  virtual void sounds_load(Handle s, const char* name, int32_t a) { unported("0x0040baf0", {s, name, a}); }
  virtual void briefing_play_movie(uint32_t state) { unported("0x00409320", {state}); }
  virtual void debrief_voice_play(uint32_t state) { unported("0x0047abe0", {state}); }
  // Our addition: the Load state is entered (its 0x004091e0 reads the
  // pilot's game first). The program fills its game from the chosen pilot.
  virtual void load_state_enter() {}
  // The Load state's start of the briefing's script (inline in 0x00409440:
  // the list +0x11220's playing byte set, its current entry the first).
  virtual void briefing_script_start(Handle s) {
    const Handle list = s + 0x11220;
    set_field8(list, 0x10, 1);
    set_field32(list, 0xc, list_head(list));
  }
  virtual void briefing_init(Handle s) { unported("0x00409ec0", {s}); }
  virtual void briefing_start(Handle s) { unported("0x00409eb0", {s}); }
  virtual void briefing_refresh(Handle s) { unported("0x00408bd0", {s}); }
  virtual void briefing_show(Handle s, int32_t a) { unported("0x00404ae0", {s, a}); }
  // The ported briefing screen's (mech3/briefing, docs/campaign.md section 2).
  // The current game as it reads it (memory in the original; the ported
  // pilot's by default, briefing.cpp): its kind, a campaign's operation and
  // mission, a campaign's unclaimed salvage (+0x3c not empty), and
  // multiplayer's vt+0x4c (+0x94; 0 for another game).
  virtual int32_t game_kind();
  virtual bool game_campaign(int32_t* operation, int32_t* mission);
  virtual bool game_salvage_left();
  virtual int32_t game_multiplayer_kind();
  virtual uint8_t app_flag_1ba() { return static_cast<uint8_t>(unported("0x0040a810", {})); }
  // Its calls not ported yet: the map's image (0x00405160 on the briefing's
  // section), the reallocation list shown or not (0x00404ae0), the
  // lancemates' loadouts once loaded (0x00408c30), the mech lab with its
  // store (0x00408b40, 0x00448830), the Salvage State (0x0047d2f0), a
  // multiplayer launch's preparation (0x0046ceb0).
  virtual void briefing_map_image(ztwod::Dialog* screen, zrdr::Node* section) {
    (void)screen;
    unported("0x00405160", {section ? 1u : 0u});
  }
  // The briefing map's reads of the game and the mission (mech3/briefing,
  // campaign.md section 2): the current game's texts (vt+0x04 game type,
  // +0x08 db name, +0x0c scenario name, +0x10 its description; multiplayer's),
  // the mission's nav points ([0x00609980], made by 0x00490720) and one by
  // name (0x00479be0), the mission's map (0x00491c80), a nav point's place
  // (its vt+0x18), a place on the map in pixels (0x004586e0; false when off
  // it), the map's image (its +0x3c) and whether a world is up
  // ([0x0060bf48], 0x004f8f50).
  virtual const char* game_text(int32_t slot) {
    unported("game.vt", {slot});
    return nullptr;
  }
  virtual Handle mission_navs() { return 0; }
  virtual Handle nav_find(Handle navs, const char* name) { return unported("0x00479be0", {navs, name}); }
  virtual Handle mission_map() { return unported("0x00491c80", {}); }
  virtual Handle nav_position(Handle nav) { return unported("nav.vt+0x18", {nav}); }
  virtual bool map_project(Handle map, Handle pos, int32_t xy[2]) {
    xy[0] = xy[1] = 0;
    return unported("0x004586e0", {map, pos}) != 0;
  }
  virtual zimg::Texture* map_image(Handle map) {
    unported("map+0x3c", {map});
    return nullptr;
  }
  virtual bool world_up() { return unported("0x004f8f50", {}) != 0; }
  // 0x0049bfd0: the game's report, an empty function in the release build.
  virtual void report(int32_t level, const char* file, int32_t line, const char* text, const char* arg) {
    (void)level, (void)file, (void)line, (void)text, (void)arg;
  }
  // The current game's slots (vt+0x3c the count, vt+0x34 one: 0 the
  // player's, then the lancemates), for 0x00408c30.
  virtual int32_t game_slot_count();
  virtual struct GameSlot* game_slot(int32_t i);
  // The stores the briefing's reallocation reads (0x00408b40, 0x00408bd0):
  // the current game's (+0x6c, its +0x08), a campaign's salvage (+0x3c, none
  // when not a campaign) and its +0xa8 (the lab's room in units of 300 t and
  // two mechs); a store's handle for the mech lab's call.
  virtual class Store* game_store();
  virtual class Store* campaign_salvage();
  virtual int32_t campaign_units();
  virtual Handle store_handle(class Store* s);
  virtual void mech_lab_open(Handle store, int32_t a) { unported("0x00448830", {store, a}); }  // ECX the pilot
  // The Lab State's (mech_lab.cpp, docs/mechlab.md section 1). The pilot's
  // game's slots (+0x6c vt+0x3c, vt+0x34 and its 0x00433ee0(0)); the program
  // answers them from the ported campaign.
  virtual int32_t lab_slot_count(Handle pilot) {
    return static_cast<int32_t>(vcall(field32(pilot, 0x6c), 0x3c, {}));
  }
  virtual Handle lab_slot(Handle pilot, int32_t i) { return vcall(field32(pilot, 0x6c), 0x34, {i}); }
  virtual int32_t lab_slot_mech(Handle slot) { return static_cast<int32_t>(field32(slot, 0x04)); }
  // The lab's screen behind the Lab State's handle (null: not the port's).
  // The chassis page's (step 3, mechlab.md): a slot's skin (+0x10, 0x005a2b90
  // its setter) and loadout (+0x08); a loadout's changed flag (+0x1f), name
  // (+0x18), recount (0x00425bd0) and check (0x004254d0); a definition's
  // skins (+0x1c's vector, each a name first); the 3D model's skin
  // (0x0044fd90, ECX the model); a question with two strings (0x004ac150).
  virtual int32_t lab_slot_skin(Handle slot) { return static_cast<int32_t>(field32(slot, 0x10)); }
  virtual void lab_slot_set_skin(Handle slot, int32_t skin) { set_field32(slot, 0x10, static_cast<uint32_t>(skin)); }
  virtual Handle lab_slot_cfg(Handle slot) { return field32(slot, 0x08); }
  virtual uint8_t lab_config_modified(Handle config) { return field8(config, 0x1f); }
  virtual std::string lab_config_name(Handle config);
  virtual void lab_config_recount(Handle config) { unported("0x00425bd0", {config}); }
  virtual bool lab_config_valid(Handle config) { return unported("0x004254d0", {config, 0}) != 0; }
  struct LabSkin {
    Handle skin;                     // none: an empty place
    bool named;                      // its name pointer set
    std::string name;
  };
  virtual void lab_dfn_skins(Handle dfn, std::vector<LabSkin>* out);
  // The loadouts page's (step 4): a definition's list of loadouts (+0x20),
  // its loadouts (+0x0c..+0x10), added at the end (0x0049ccc0), taken out
  // (0x00425090: a player's own only), found by name (0x00424e90) and
  // written (0x00424f60, configs\<mech>.mw3); a loadout copied (0x00424010
  // with no store), deleted (0x00424350), renamed, its bytes +0x1c (the
  // player's own) and +0x1e (training's); the store's check (0x004735f0)
  // and its +0x10 (full).
  virtual Handle lab_dfn_configs(Handle dfn) { return field32(dfn, 0x20); }
  virtual std::vector<Handle> lab_list_configs(Handle list);
  virtual void lab_list_append(Handle list, Handle config) { unported("0x0049ccc0", {list, config}); }
  virtual bool lab_list_remove(Handle list, Handle config) { return unported("0x00425090", {list, config}) != 0; }
  virtual Handle lab_list_find(Handle list, const char* name);
  virtual void lab_list_save(Handle list) { unported("0x00424f60", {list}); }
  virtual Handle lab_config_copy(const char* name, Handle config) {
    return unported("0x00424010", {name, config, 0, 0});
  }
  virtual void lab_config_delete(Handle config) { unported("0x00424350", {config}); }
  virtual void lab_config_set_name(Handle config, const char* name) { unported("lab.config_name", {config, name}); }
  virtual void lab_config_set_modified(Handle config, uint8_t v) { set_field8(config, 0x1f, v); }
  virtual uint8_t lab_config_byte(Handle config, uint32_t off) { return field8(config, off); }
  virtual bool lab_store_has_config(Handle store, int32_t mech, Handle other, Handle config) {
    return unported("0x004735f0", {store, mech, other, config}) != 0;
  }
  virtual uint8_t lab_store_full(Handle store) { return field8(store, 0x10); }
  virtual void lab_model_skin(Handle model, int32_t skin) { unported("0x0044fd90", {model, skin}); }
  virtual uint32_t message_box2(int32_t a, int32_t caption, int32_t text, const char* s1, const char* s2) {
    return unported("0x004ac150", {a, caption, text, s1, s2});
  }
  // A store item behind its handle (item_handle's other way).
  virtual const struct ConfigItem* item_of(Handle item);
  // The lab's item lists' asks of a store item (lab_items.h): its kind
  // (vt+0x0c), name (vt+0x20, 0x004738f0..0x004739d0) and critical slots
  // (vt+0x18); and a part's name for the player (0x00484d70).
  virtual int32_t lab_item_kind(Handle item);
  virtual std::string lab_item_name(Handle item);
  virtual int32_t lab_item_slots(Handle item);
  // UNASSIGNED_LIST's (lab_screen.h): an item's id (vt+0x14), weight of one
  // (vt+0x00 with 1), count (+0x04), unlimited byte (+0x10) and, for
  // equipment, its kind (+0x18); a loadout's slots left (0x00425d80), its
  // ferro-fibrous and endo steel slots (0x00425e40, 0x00425e80), its sink
  // items (0x00425f30) and the sinks the engine holds (0x00426070).
  virtual int32_t lab_item_id(Handle item);
  virtual float lab_item_mass(Handle item);
  virtual int32_t lab_item_count(Handle item);
  virtual uint8_t lab_item_unlimited(Handle item);
  virtual int32_t lab_item_equipment(Handle item);
  virtual int32_t lab_config_slots_left(Handle config) { return static_cast<int32_t>(unported("0x00425d80", {config})); }
  virtual int32_t lab_config_ferro_slots(Handle config) { return static_cast<int32_t>(unported("0x00425e40", {config})); }
  virtual int32_t lab_config_endo_slots(Handle config) { return static_cast<int32_t>(unported("0x00425e80", {config})); }
  virtual int32_t lab_config_sink_items(Handle config) { return static_cast<int32_t>(unported("0x00425f30", {config})); }
  virtual int32_t lab_config_sink_limit(Handle config) { return static_cast<int32_t>(unported("0x00426070", {config})); }
  // The unassigned list's rule (0x00453840, lab_screen.cpp): a loadout's
  // weight (0x00425300(-1), a float), weapons (0x00425ea0), equipment of a
  // kind (0x00473570), a part's free slots (0x00425e20), its armour (+0x2c
  // tons, +0x30 tech, +0x34 ferro-fibrous); an equipment item's tech
  // (+0x1c); the parts a definition has firepoints on (+0x24, each +0x0c).
  virtual float lab_config_weight(Handle config) {
    return zgame::from_bits(unported("0x00425300", {config, 0xffffffffu}));
  }
  virtual int32_t lab_config_weapons(Handle config) { return static_cast<int32_t>(unported("0x00425ea0", {config})); }
  virtual int32_t lab_config_equipment(Handle config, int32_t kind, int32_t tech) {
    return static_cast<int32_t>(unported("0x00473570", {config, static_cast<uint32_t>(kind), static_cast<uint32_t>(tech)}));
  }
  virtual int32_t lab_config_part_free(Handle config, int32_t part) {
    return static_cast<int32_t>(unported("0x00425e20", {config, static_cast<uint32_t>(part)}));
  }
  virtual float lab_config_armor_tons(Handle config) { return zgame::from_bits(field32(config, 0x2c)); }
  virtual int32_t lab_config_armor_tech(Handle config) { return static_cast<int32_t>(field32(config, 0x30)); }
  virtual uint8_t lab_config_armor_ferro(Handle config) { return field8(config, 0x34); }
  virtual int32_t lab_config_section(Handle config, int32_t part) {  // +0x38: a section's points
    return static_cast<int32_t>(field32(config, 0x38 + 4 * static_cast<uint32_t>(part)));
  }
  virtual void lab_config_set_section(Handle config, int32_t part, int32_t v) {
    set_field32(config, 0x38 + 4 * static_cast<uint32_t>(part), static_cast<uint32_t>(v));
  }
  // 0x004247a0 (ECX the armour at +0x2c, the mech pushed): the sections spread anew.
  virtual void lab_config_spread_armor(Handle config) { unported("0x004247a0", {config}); }
  // The loadout's vt+0x14 (0x00471de0): n fewer of the item.
  virtual void lab_config_remove(Handle config, Handle item, int32_t n) {
    unported("0x00471de0", {config, item, static_cast<uint32_t>(n)});
  }
  // The store's vt+0x0c (0x00471a20): n of the equipment back.
  virtual void lab_store_add_equipment(Handle store, int32_t kind, int32_t tech, int32_t n) {
    unported("0x00471a20", {store, static_cast<uint32_t>(kind), static_cast<uint32_t>(tech), static_cast<uint32_t>(n)});
  }
  virtual int32_t lab_item_tech(Handle item);
  struct LabFirepoint {
    int32_t part;
    float radius;                    // +0x04: a missile weapon needs one over 0
  };
  virtual std::vector<LabFirepoint> lab_dfn_firepoints(Handle dfn);
  // The put-on (0x00454600, lab_screen.cpp's lab_put_on): the loadout behind
  // a handle, MESSAGE_AREA's text (its vt+0x7c, the text as the format),
  // ASSIGNED_LIST's part (step 6's 0x00455eb0), and the put-on itself (by
  // default the stand-in "lab.vt+0xac" of the lab test).
  virtual struct Config* lab_config_of(Handle config) { (void)config; return nullptr; }
  virtual void lab_message(const char* text) { unported("lab.message", {text}); }
  virtual int32_t lab_assigned_part() { return static_cast<int32_t>(unported("0x00455eb0", {0x1a13cu})); }
  virtual void lab_put_on(class LabUnassignedList& list, int32_t index);
  virtual void lab_take_off(class LabAssignedList& list, int32_t index);  // (lab_screen.cpp's lab_take_off)
  // ASSIGNED_LIST (lab_screen.cpp): a loadout's items in their order (its
  // list at +0x08), an item's place on a mech (vt+0x08: a weapon's
  // firepoint's part, through the definition 0x004846a0), its group (+0x08)
  // and weight (vt+0x00), the loadout's engine's name (0x00432f00 on +0x24).
  virtual std::vector<Handle> lab_config_items(Handle config);
  virtual int32_t lab_item_location(Handle item, int32_t mech);
  virtual int32_t lab_item_group(Handle item);
  virtual double lab_item_weight(Handle item, int32_t n);
  virtual std::string lab_engine_name(Handle config);
  // ENGINE_TABLE and the engine's presses (lab_screen.cpp): a loadout's
  // engine (+0x24 its index, +0x28 XL), its engine's critical slots
  // (0x00425e60), a definition's least and most rating (+0x10, +0x14).
  virtual int32_t lab_config_engine(Handle config) { return static_cast<int32_t>(field32(config, 0x24)); }
  virtual void lab_config_set_engine(Handle config, int32_t index) { set_field32(config, 0x24, static_cast<uint32_t>(index)); }
  virtual void lab_config_set_xl(Handle config, uint8_t v) { set_field8(config, 0x28, v); }
  virtual int32_t lab_config_engine_slots(Handle config) { return static_cast<int32_t>(unported("0x00425e60", {config})); }
  virtual int32_t lab_dfn_engine_min(Handle dfn) { return static_cast<int32_t>(field32(dfn, 0x10)); }
  virtual int32_t lab_dfn_engine_max(Handle dfn) { return static_cast<int32_t>(field32(dfn, 0x14)); }
  // The data panels (step 8b1): a loadout's tons of one kind (0x00425300:
  // 0..7 by kind, 8 the mech's most, -1 all; a float) and its endo steel
  // (+0x20).
  virtual float lab_config_weight_of(Handle config, int32_t what) {
    return zgame::from_bits(unported("0x00425300", {config, static_cast<uint32_t>(what)}));
  }
  virtual void lab_config_set_endo(Handle config, uint8_t v) { set_field8(config, 0x20, v); }
  // LOADOUT_LIST (step 8b2): a loadout's heat sinks (+0x68) and whether
  // they are double (+0x6c).
  virtual int32_t lab_config_sinks(Handle config) { return static_cast<int32_t>(field32(config, 0x68)); }
  virtual uint8_t lab_config_double_sinks(Handle config) { return field8(config, 0x6c); }
  virtual std::string lab_item_label(Handle item);  // vt+0x1c (0x00473740 for weapons): "name (n)"
  virtual void lab_set_item_group(Handle item, int32_t group);  // +0x08
  virtual const char* part_text(int32_t part);
  std::string peek_string(Handle p);  // a C string through peek8 (none: "")
  virtual void lab_show_slots(Handle screen, int32_t slot) { unported("0x0044ce90", {screen, slot}); }
  virtual class LabScreen* lab_screen(Handle screen) {
    (void)screen;
    return nullptr;
  }
  virtual void lab_slot_release(Handle pilot, int32_t i) { set_field32(lab_slot(pilot, i), 0x0c, 0); }
  // A slot's loadout (+0x08; the slot asked for twice, as the exe does) and
  // a loadout's mech (+0x14).
  virtual Handle lab_slot_config(Handle pilot, int32_t i) {
    return lab_slot(pilot, i) ? field32(lab_slot(pilot, i), 0x08) : 0;
  }
  virtual int32_t config_mech(Handle config) { return static_cast<int32_t>(field32(config, 0x14)); }
  // Not ported yet (mechlab.md section 5): the 3D world (0x0044ffc0 its load,
  // 0x00450390, the frame's 0x00450810 / 0x00450800 / 0x00450690), the
  // screen's (0x00448f80 the slot shown, 0x0044ce30 a page, 0x0044cf40 /
  // 0x0044cf50 the page refreshed) and the loadout files (0x00426180 write,
  // 0x00426300 read, then the read one added to its mech's list).
  virtual bool lab_world_load(Handle state) { return unported("0x0044ffc0", {state}) != 0; }
  virtual void lab_world_unload(Handle state) { unported("0x00450390", {state}); }
  virtual void lab_view_begin(Handle state) { unported("0x00450810", {state}); }
  virtual void lab_view_end(Handle state) { unported("0x00450800", {state}); }
  virtual void lab_world_frame(Handle state, float dt) { unported("0x00450690", {state, zgame::bits(dt)}); }
  virtual void lab_show_slot(Handle state, int32_t slot) { unported("0x00448f80", {state, slot}); }
  virtual void lab_page(Handle screen, int32_t page) { unported("0x0044ce30", {screen, page}); }
  virtual void lab_page_part(Handle screen, Handle part) { unported("0x0044cf40", {screen, part}); }
  virtual void lab_page_refresh(Handle screen) { unported("0x0044cf50", {screen}); }
  virtual void config_export(Handle config) { unported("0x00426180", {config, 1}); }
  virtual Handle config_import() { return unported("0x00426300", {}); }
  virtual Handle mech_dfn_handle(int32_t mech) { return unported("0x004846a0", {mech}); }
  virtual void node_frame_update() { unported("0x004f7f70", {}); }
  virtual void model_purge(int32_t a, int32_t b) { unported("0x005394c0", {a, b}); }
  // The lab screen's (lab_screen.cpp): the Lab State's armour section
  // highlight on the model (0x004504d0, ECX the state) and the weapon groups' refresh (0x0044dcf0,
  // ECX the screen).
  virtual void lab_section_highlight(int32_t part) { unported("0x004504d0", {part}); }
  // The slot switch's (0x00448f80, 0x00449010): a mech out of the lab's store
  // (0x00472a30) and into it (0x00471c10), a slot's loadout given back
  // (0x00433df0(0, store, 1)) and its mech set (0x005a2ba0, a +0x04 setter
  // the linker shares with other classes). The program's are the ported
  // Store's and GameSlot's.
  virtual void lab_store_remove(Handle store, int32_t mech);
  virtual void lab_store_add(Handle store, int32_t mech);
  virtual void lab_slot_drop_config(Handle slot, Handle store) { lab_slot_set_config(slot, 0, store); }
  // 0x00433df0(config, store, 1): the old loadout's items back to the store,
  // a copy of `config` taken from it.
  virtual void lab_slot_set_config(Handle slot, Handle config, Handle store) {
    unported("0x00433df0", {slot, config, store, 1});
  }
  virtual void lab_slot_set_mech(Handle slot, int32_t mech) { set_field32(slot, 0x04, static_cast<uint32_t>(mech)); }
  virtual void salvage_open() { unported("0x0047d2f0", {}); }
  virtual void multiplayer_launch_prep() { unported("0x0046ceb0", {}); }
  // The missions object [0x006098d0]: loaded (0x00490930), the launch
  // (0x00491150, true with the player's mech), the cleanup (0x00491780).
  virtual bool missions_loaded() { return unported("0x00490930", {}) != 0; }
  virtual bool mission_launch() { return unported("0x00491150", {}) != 0; }
  virtual void mission_cleanup(int32_t unload) { unported("0x00491780", {unload}); }
  // The mission ended now as a failure (0x0048fa80; not in multiplayer).
  virtual void mission_abort() { unported("0x0048fa80", {}); }
  // The app's +0x1be (0x0047fe00): the training menu opens again after the mission.
  virtual void set_reopen_training(uint8_t v) { unported("0x0047fe00", {v}); }
  // Replace(state(n), 0) (0x00482970, 0x00590250).
  virtual void replace_state(int32_t n) { unported("0x00590250", {n}); }
  // The current campaign's salvage merged (0x00434cb0): the ported pilot's.
  virtual void campaign_merge_salvage();
  // The debriefing's (mech3/debriefing, campaign.md section 3): the mission's
  // flags and time (memory in the original: [0x00609926] failed, [0x00609994]
  // a cheat, [0x0060991c] seconds, [0x00609990] the salvage store).
  virtual bool mission_failed();
  virtual bool mission_cheated();
  virtual float mission_time();
  virtual Handle mission_salvage_store() { return 0; }
  // 0x0040ce90: FastMenu set, the old value back.
  virtual int32_t exchange_fast_menu(int32_t on) {
    const int32_t old = fast_menu();
    set_fast_menu(on);
    return old;
  }
  // Its parts not ported yet: the results (0x0047baf0..0x0047c84b, 6b), a
  // lance card's mech display (0x0047e840 and its calls, 6c), the mech lab's
  // store list's rows (0x0048dbe0), a mech's name (0x00484650).
  // The results' reads (memory and the mechs' own calls in the original;
  // campaign.md section 3): zEntity's list in its order, each entity with a
  // mech (vt+0x2c then vt+0x50): the player's mech ([0x0060861c] +0x14) or
  // not, its team (vt+0x28 +0xc0), its type (+0x684 +0xc, -1 none), the
  // kill credit's mechs, vehicles and turrets and their points (+0x3f4..
  // +0x408), its pilot's name (vt+0xa4, 0x0041df30). Its
  // health (vt+0x98 x 100, floored) is read and dropped.
  struct LanceMech {
    Handle mech = 0;
    bool player = false;
    uint32_t team = 0;
    int32_t type = -1;
    int32_t stats[6] = {};             // +0x3f4 .. +0x408
    const char* name = nullptr;
  };
  virtual std::vector<LanceMech> debrief_lance() { return {}; }
  virtual uint32_t player_allies() { return 0; }  // the player's mech's +0xc4
  // The mission's primary (0) and secondary (1) objectives ([0x0060994c],
  // [0x00609950]; none when the list is missing or empty): each one's
  // object, done (+0x08) and shown (+0x30); its line (0x0045e080(0), called).
  struct Objective {
    Handle obj = 0;
    bool done = false, shown = false;
  };
  virtual std::vector<Objective> debrief_objectives(int32_t which) {
    (void)which;
    return {};
  }
  virtual const char* objective_text(Handle obj) {
    unported("0x0045e080", {obj});
    return nullptr;
  }
  // The campaign's score (+0xc4) and a pilot's mechs destroyed (+0xb4 + 4 i), the
  // won flag of the mission ([0x00609925]), Instant Action's mode (+0x44),
  // rounds (+0x6c) and waves (+0x70).
  virtual int32_t campaign_score();
  virtual int32_t campaign_record(int32_t i);
  virtual bool mission_ended();
  virtual void action_rounds(int32_t* mode, int32_t* rounds, int32_t* waves);
  // The Win State's copy of the debriefing's scores into the campaign
  // (0x0047ac20: +0x13be4 into +0xb4 for n lancemates, +0x13be0 into +0xc4).
  virtual void win_keep_scores(Handle screen, int32_t n);
  // A lance card's vehicle as its display reads it (lance_armor.h).
  using LanceArmor = ::mech3::LanceArmor;
  virtual bool vehicle_armor(Handle v, LanceArmor* out) {
    (void)v, (void)out;
    return false;
  }
  // A store list's reads (0x0048dbe0): its store by handle, a store item's
  // handle (the row's value: the item itself in the exe), its +0x200 filter
  // (a function on the item; set by nothing ported), 0x00575fd0 with a name
  // and maybe a number (the ammo rows' 0x1c3 / 0x1c4).
  virtual class Store* store_of(Handle store);
  virtual Handle item_handle(const struct ConfigItem* item);
  virtual bool store_list_filter(Handle fn, Handle item) { return unported("store_list +0x200", {fn, item}) != 0; }
  virtual std::string message_format_sd(int32_t id, const char* s, int32_t n, bool has_n) {
    unported("0x00575fd0", {id, s, n, has_n ? 1u : 0u});
    return std::string();
  }
  virtual const char* mech_name_of(int32_t mech) {
    unported("0x00484650", {mech});
    return "";
  }
  // 0x00575fd0 with a number and a string.
  virtual std::string message_format_is(int32_t id, int32_t a, const char* s) {
    unported("0x00575fd0", {id, a, s});
    return std::string();
  }
  // The same with a number, a string and a number.
  virtual std::string message_format_isi(int32_t id, int32_t a, const char* s, int32_t b) {
    unported("0x00575fd0", {id, a, s, b});
    return std::string();
  }
  // Its press's: Instant Action's fade out (0x0048ef00: ECX the IA Dummy
  // State 0x00608bd0, EDX 0 (kept in [0x006098bc]), then direction 1, buffer
  // 2, [0x005e7274] seconds and black, as fade_replace takes them), the pilot's file written (the inline open,
  // header and vt+0x04), the lancemates reset (0x00435240(0)), the
  // animations' reset (0x0050ad60), a state by address replacing this one
  // (0x00590250: the Win Movie State), the campaign's +0xb0 cleared, Instant
  // Action's next round (its +0x6c and +0x70).
  virtual void fade_to_state(Handle next, uint32_t arg, int32_t direction, int32_t buffer, float seconds,
                             uint8_t r, uint8_t g, uint8_t b) {
    unported("0x0048ef00", {next, arg, direction, buffer, zgame::bits(seconds), r, g, b});
  }
  virtual void pilot_save_now();
  virtual void campaign_reset_lancemates();
  virtual void anim_reset() { unported("0x0050ad60", {}); }
  virtual void replace_state_at(Handle state) { unported("0x00590250", {state}); }
  virtual void campaign_set_b0(uint8_t v);
  // CONTINUE in Instant Action (inline in 0x0047d030): the round again, from
  // the kept wave + 1 in Progression, else 1; no waves won.
  virtual void action_next_round();
  // The IA Dummy State's tally of every mech's kills (0x0047ce80's loop).
  virtual void action_tally() { unported("0x0047ce80 tally", {}); }
  virtual uint32_t list_head(Handle list) { return unported("0x0049ccb0", {list}); }
  virtual void format(const char* fmt, std::initializer_list<Arg> args, char* out, int size);  // CString::Format

  static void format_key(char* out, uint32_t addr);
};

}  // namespace mech3
