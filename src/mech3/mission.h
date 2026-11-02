#pragma once
#include <vector>
#include <cstdint>
#include <string>

#include "gamez/zgame/unported.h"
#include "mech3/mission_world.h"

// C:\zipper\Mech3\mission.cpp: bringing a mission up (docs/mission-load.md).
// The missions object (0x006098d0) loads the world 0x0048faf0 (the interp
// scripts that read gamez.zbd, the texture libraries, the view, effects,
// animations, weapons, the HUD) and then the rest 0x00490ab0 (the reader
// archives, mechs and AI, world.zrd, objectives, the mission's start
// animation), from the briefing's loader thread 0x00490880; each frame it
// runs the mission 0x0048f470 (the game time, the valves' timers, the
// objectives, the win and the loss) and at the end cleans it up 0x00491780
// (docs/mission-frame.md). Everything outside mission.cpp is a hook here.
namespace mech3 {

using zgame::Handle;

// Mech3.exe 1.2 @ 0x00484c50: a mission's directory by index (c1, c2, c3, c4,
// c4b, t1, lab, dev1, dev2; "none" past them).
const char* mission_dir(int32_t index);
// Ours (docs/modding.md): the worlds mods declare (world= in mod.txt), the
// folders from index 9 on, after the original's nine; and the count of all.
std::vector<std::string>& mod_worlds();
int32_t mission_dir_count();
int32_t mission_dir_index(const char* dir);  // any case; -1 when no world has it

// The calls out of mission.cpp, by the original's address.
class MissionHooks : public zgame::Unported {
 public:
  // The loading screen (briefing.cpp).
  virtual void progress(float f) { unported("0x004050d0", {zgame::bits(f)}); }
  virtual void progress_text(const char* s) { unported("0x00405120", {s}); }
  // The pilot's game ([[0x0060861c]+0x6c]).
  virtual int32_t game_mission_index() { return static_cast<int32_t>(unported("game.vt+0x54", {})); }
  virtual int32_t game_type() { return static_cast<int32_t>(unported("game.vt+0x2c", {})); }
  virtual int32_t game_mission_number() { return static_cast<int32_t>(unported("game.vt+0x4c", {})); }
  virtual uint8_t game_saved() { return static_cast<uint8_t>(unported("game.vt+0x28", {})); }
  virtual void game_objectives(const char* file, const char* block, Handle a, Handle b, Handle c, Handle d,
                               Handle e) {
    unported("game.vt+0x40", {file, block, a, b, c, d, e});
  }
  virtual uint8_t mp_flag() { return static_cast<uint8_t>(unported("[0x00479480]+0xa8", {})); }
  // App and engine globals.
  virtual uint8_t app_rgb() { return static_cast<uint8_t>(unported("[0x00608e41]", {})); }
  virtual uint8_t app_turrets() { return static_cast<uint8_t>(unported("[0x00608e3a]", {})); }
  virtual void set_holes(uint8_t on) { unported("[0x006061cc]=", {on}); }
  virtual void set_use_archive() { unported("[0x005f4244]=", {1}); }
  virtual void clear_9a17c0() { unported("[0x009a17c0]=", {0}); }
  virtual void set_view(Handle view) { unported("[0x005f4c8c]=", {view}); }
  virtual Handle view() { return unported("[0x005f4c8c]", {}); }
  virtual void set_608c84(Handle h) { unported("[0x00608c84]=", {h}); }
  virtual Handle get_608c84() { return unported("[0x00608c84]", {}); }
  virtual void set_stealth(Handle h) { unported("[0x00609a2c]=", {h}); }
  virtual Handle stealth() { return unported("[0x00609a2c]", {}); }
  // Fields of objects not ported (the view's window).
  virtual uint32_t field32(Handle h, uint32_t off) { return unported("peek", {h, off}); }
  virtual void set_field32(Handle h, uint32_t off, uint32_t v) { unported("poke", {h, off, v}); }
  virtual void set_field8(Handle h, uint32_t off, uint8_t v) { unported("poke8", {h, off, v}); }
  virtual Handle alloc(uint32_t size) { return unported("new", {size}); }
  // zInterp, on the script object [0x006040b0].
  virtual void interp_define(const char* name) { unported("0x00595bb0", {name}); }
  virtual uint8_t interp_source(const char* path) { return static_cast<uint8_t>(unported("0x00595990", {path})); }
  // The options (ported, mech3/options).
  virtual int32_t hw_card_flag() { return static_cast<int32_t>(unported("0x0040d4d0", {})); }
  virtual int32_t texture_memory() { return static_cast<int32_t>(unported("0x0040d410", {})); }
  virtual uint32_t install_options(uint32_t mask) { return unported("0x00597d80", {mask}); }
  virtual void set_render_target(Handle w) { unported("0x0040d670", {w}); }
  virtual void set_display_target(Handle d) { unported("0x0040d6b0", {d}); }
  virtual void set_camera(Handle c) { unported("0x0040d590", {c}); }
  virtual void render_rect(int32_t out[4]);   // 0x0040d6a0: x, y, x_end, y_end
  virtual Handle window_rect() { return unported("0x0040d7b0", {}); }
  virtual Handle display_rect() { return unported("0x0040d750", {}); }
  // zVideo: the card's texture memory ([0x0072f38c], [0x0072f390]).
  virtual uint32_t video_texture_memory(int32_t dev, uint32_t* total, uint32_t* free_);
  virtual uint32_t video_fn_390(int32_t dev) { return unported("[0x0072f390]", {dev}); }
  virtual const char* video_driver_name() { unported("0x005799f0", {}); return ""; }
  virtual const char* video_mode_name() { unported("0x00579040", {}); return ""; }
  // zImage: the texture libraries.
  virtual void textures_begin(int32_t v) { unported("0x00516f20", {v}); }
  virtual void texture_library(const char* name, int32_t level, int32_t hw) { unported("0x005182a0", {name, level, hw}); }
  virtual void images_path(const char* path) { unported("0x00516f50", {path}); }
  virtual void textures_load() { unported("0x005190d0", {}); }
  // zClass and zModel.
  virtual void classes_reset() { unported("0x004f8f70", {}); }
  virtual void models_reset() { unported("0x00526800", {}); }
  virtual Handle find_node(int32_t list, const char* name) { return unported("0x004f7fe0", {list, name}); }
  virtual void lists_4f7f70() { unported("0x004f7f70", {}); }
  virtual void lists_4f7bd0() { unported("0x004f7bd0", {}); }
  virtual void window_set_size(Handle node, int32_t w, int32_t h) { unported("0x004f9fa0", {node, w, h}); }
  virtual void window_set_origin(Handle node, int32_t x, int32_t y) { unported("0x004fa0b0", {node, x, y}); }
  virtual void light_5269e0(int32_t v) { unported("0x005269e0", {v}); }
  virtual void fn_535ed0(float f) { unported("0x00535ed0", {zgame::bits(f)}); }
  // Modules started for the mission.
  virtual void fn_518f50(int32_t v) { unported("0x00518f50", {v}); }
  virtual void fn_484270(int32_t v) { unported("0x00484270", {v}); }
  virtual void anim_allow_loading(int32_t on) { unported("0x00509150", {on}); }
  virtual void fn_576270(int32_t v) { unported("0x00576270", {v}); }
  virtual const char* version_number() { unported("0x004af290", {}); return ""; }
  virtual const char* message(int32_t id) { unported("0x00576040", {id}); return ""; }
  virtual Handle make_608c84(Handle mem, Handle world) { return unported("0x0047f2b0", {mem, world}); }
  virtual Handle make_view_window(Handle mem, Handle window) { return unported("0x004b52e0", {mem, window}); }
  virtual Handle make_view(Handle mem, Handle camera, Handle window, Handle x) {
    return unported("0x0040aca0", {mem, camera, window, x});
  }
  virtual void fn_4e7b00() { unported("0x004e7b00", {}); }
  virtual void effects_load(Handle world, Handle camera, const char* file) { unported("0x0050d2c0", {world, camera, file}); }
  virtual void anim_set_world(Handle world) { unported("0x005090e0", {world}); }
  virtual void anim_load(const char* file) { unported("0x0050ae60", {file}); }
  virtual void decals_load(Handle world, int32_t v) { unported("0x00501ab0", {world, v}); }
  virtual void weapons_load(Handle world, const char* file, int32_t a, uint32_t callback) {
    unported("0x00583880", {world, file, a, callback});
  }
  virtual void hud_load(Handle display, Handle window) { unported("0x0040f7f0", {display, window}); }
  virtual void fn_40db60() { unported("0x0040db60", {}); }
  virtual Handle stealth_4ac860(int32_t v) { return unported("0x004ac860", {v}); }
  virtual Handle stealth_4a51b0(Handle obj, int32_t v) { return unported("0x004a51b0", {obj, v}); }
  virtual Handle stealth_4a4de0(Handle mem, Handle x) { return unported("0x004a4de0", {mem, x}); }
  virtual void zerr(int32_t level, const char* file, int32_t line, const char* fmt, const char* arg) {
    unported("0x0049bfd0", {level, file, line, fmt, arg});
  }
  virtual void cleanup(int32_t v) { unported("0x00491780", {v}); }
  // The mission unload: the port (Missions::unload) unless a test stands in.
  virtual void unload();
  // What Missions::unload (0x00490480) frees.
  virtual void camera_flag_off(Handle camera) { unported("0x004f3430", {camera, 0}); }
  virtual void ai_lists_free() { unported("0x004e7b80", {}); }
  virtual void mech_types_clear() { unported("0x004318e0", {}); }
  virtual void mech_defs_clear() { unported("0x004844b0", {}); }
  virtual void sound_set_unload_608dd4() { unported("0x0056f530", {0x608dd4}); }
  virtual void effects_free() { unported("0x0050d570", {}); }
  virtual void anims_free() { unported("0x0050b380", {}); }
  virtual void decals_free() { unported("0x00502020", {}); }
  virtual void weapons_free() { unported("0x00584a60", {0x4b0090}); }
  virtual void view_free(Handle view) { unported("operator delete", {view}); }
  virtual void delete_608c84(Handle o) { unported("vt+0x04(1)", {o}); }
  virtual void classes_free() { unported("0x004f8fb0", {}); }
  virtual void models_free() { unported("0x00526920", {}); }
  virtual void textures_free() { unported("0x00516f30", {}); }
  // zReader.
  virtual void reader_open(const char* path, int32_t current) { unported("0x00557320", {path, current}); }
  virtual void reader_set_path(const char* path) { unported("0x005561a0", {path}); }
  virtual void reader_add_path(const char* path) { unported("0x005561e0", {path}); }
  // The rest of the mission (0x00490ab0) and its start 0x0048f400.
  virtual void bind_command(int32_t cmd, uint32_t handler) { unported("0x0051d2f0", {cmd, handler}); }
  virtual uint8_t app_network() { return static_cast<uint8_t>(unported("[0x00608e45]", {})); }
  virtual int32_t invincible() { return static_cast<int32_t>(unported("0x0040cf20", {})); }
  virtual int32_t infinite_ammo() { return static_cast<int32_t>(unported("0x0040cf80", {})); }
  virtual int32_t heat() { return static_cast<int32_t>(unported("0x0040cfc0", {})); }
  virtual void mechs_init() { unported("0x00413e60", {}); }
  virtual void fn_4319a0() { unported("0x004319a0", {}); }
  virtual void fn_459e80(Handle h) { unported("0x00459e80", {h}); }
  virtual void world_params(const char* file, const char* block) { unported("0x00491de0", {file, block}); }
  virtual void fn_4e7b40() { unported("0x004e7b40", {}); }
  virtual void entity_named(Handle h, const char* name) { unported("0x005a84c0", {h, name}); }
  virtual void inout_load() { unported("0x004c3b80", {}); }
  virtual void landmines_load(Handle h, const char* file, const char* block) { unported("0x004135c0", {h, file, block}); }
  virtual void fn_4b4ab0(Handle h) { unported("0x004b4ab0", {h}); }
  virtual void player_setup(uint8_t turrets) { unported("0x0049e810", {turrets}); }
  virtual void turrets_load(const char* file, Handle at) { unported("0x004a76b0", {file, at}); }
  virtual void objectives_load(const char* file, const char* block, Handle a, Handle b, Handle c, Handle d,
                               Handle e) {
    unported("0x0045c2d0", {file, block, a, b, c, d, e});
  }
  virtual Handle anim_start(const char* name) { return unported("0x0050b450", {name}); }
  virtual void mp_stations(Handle h) { unported("0x00490950", {h}); }
  virtual Handle sound_find(const char* name) { return unported("0x0056f6f0", {name}); }
  virtual void fn_4d2e20() { unported("0x004d2e20", {}); }
  virtual void fn_4d31d0() { unported("0x004d31d0", {}); }
  virtual void fn_4e2120() { unported("0x004e2120", {}); }
  virtual void ai_list_reset(Handle list) { unported("0x004d8610", {list}); }
  virtual void keys_reset() { unported("0x0051a300", {}); }
  virtual void hud_line_clear(int32_t i) { unported("hudline.vt+0x7c", {i}); }
  virtual void fn_40f040() { unported("0x0040f040", {}); }
  virtual void fn_40f890() { unported("0x0040f890", {}); }
  virtual void fn_4d4b30() { unported("0x004d4b30", {}); }
  virtual void anim_run(Handle anim) { unported("0x00508c10", {anim, 0, 0, 0, 0}); }
  virtual void interp_5964c0(int32_t v) { unported("0x005964c0", {v}); }

  // The frame 0x0048f470 (docs/mission-frame.md).
  virtual void valves_tick(float dt) { unported("0x004d3d30", {zgame::bits(dt)}); }
  // Silent: the game time [0x0060991c] (+0x4c) just written. The exe has the
  // one global; the ports' modules keep copies, set here with each write.
  virtual void game_time_set(float t) { (void)t; }
  // Ours: at the mission's start (the end of 0x0048f400) and each mission
  // frame (0x0048f470's start), for the program's own check of its hooks
  // (the census stand-ins for those still unset; a wiring may reset them).
  virtual void hooks_check() {}
  virtual void map_update(float dt) { unported("0x0045a270", {0x605ae0, zgame::bits(dt)}); }  // the field bases' dock
  virtual void stations_update(Handle st, float dt, Handle vehicle) {
    unported("stations.vt+0x6c", {st, zgame::bits(dt), vehicle});
  }
  virtual Handle player_vehicle() { return unported("[0x0060861c]+0x14", {}); }
  virtual Handle mech_screen(Handle data) { return unported("0x00430aa0", {data}); }
  virtual void screen_flash(Handle obj, float v) { unported("screen.vt+0x60", {obj, zgame::bits(v)}); }
  virtual void screen_reset(Handle obj) { unported("0x00446ad0", {obj}); }
  virtual uint8_t objective_check(Handle obj) { return static_cast<uint8_t>(unported("0x0045e540", {obj, 1})); }
  virtual uint8_t objective_done(Handle obj) { return static_cast<uint8_t>(unported("objective+0x08", {obj})); }
  virtual uint8_t condition_met(Handle obj) { return static_cast<uint8_t>(unported("condition.vt+0x4", {obj})); }
  virtual uint8_t objectives_announce(Handle a, Handle b, Handle c, Handle d) {
    return static_cast<uint8_t>(unported("0x0045c180", {a, b, c, d}));
  }
  virtual uint8_t app_1c2() { return static_cast<uint8_t>(unported("[0x00608e4a]", {})); }
  // zSound: the mission's voice lines.
  virtual uint32_t sound_playing(Handle s) { return unported("0x0056e8e0", {s}); }
  virtual void sound_stop(Handle s) { unported("0x0056e9f0", {s}); }
  virtual Handle sound_play(Handle s, float volume) { return unported("0x0056e3d0", {s, zgame::bits(volume)}); }
  // The win (0x0048f740): Instant Action's next wave, training's lessons.
  virtual Handle ia_game() { return unported("0x00479460", {}); }
  virtual void fn_5392c0() { unported("0x005392c0", {}); }
  virtual void fn_49dc80(uint8_t v) { unported("0x0049dc80", {v}); }
  virtual void objectives_free(Handle a, Handle b, Handle c, Handle d, Handle e) {
    unported("0x0045c1d0", {a, b, c, d, e});
  }
  virtual void objectives_start(Handle b, Handle c, Handle d, Handle e) { unported("0x0045c4a0", {b, c, d, e}); }
  virtual Handle training(Handle a, Handle b, Handle c, Handle d, Handle e) {
    return unported("0x004794a0", {a, b, c, d, e});
  }
  virtual uint8_t training_next(Handle t) { return static_cast<uint8_t>(unported("0x004373b0", {t})); }
  // The loss (0x0048f9e0): in multiplayer the player's mech is put down.
  virtual Handle multiplayer() { return unported("0x00479480", {}); }
  virtual Handle mech_action(Handle mech, int32_t v) { return unported("0x0041d510", {mech, v}); }
  virtual void action_start(Handle a) { unported("action.vt+0x3c", {a, 0, 0}); }
  // The end screen (0x00491bf0).
  virtual void fn_40f0b0(uint8_t v) { unported("0x0040f0b0", {v}); }
  virtual void fade_out(Handle seq) { unported("0x0048ef00", {seq, 0, 1, 2, zgame::bits(2.0f), 0, 0, 0}); }

  // The cleanup 0x00491780.
  virtual Handle fn_4eb040() { return unported("0x004eb040", {}); }
  virtual void fn_4eb030(Handle v) { unported("0x004eb030", {v}); }
  virtual void close_handle(Handle h) { unported("CloseHandle", {h}); }
  virtual void set_thread_priority(Handle thread, int32_t p) { unported("SetThreadPriority", {thread, p}); }
  virtual void fn_4e7bc0() { unported("0x004e7bc0", {}); }
  virtual Handle fn_4582c0(Handle obj) { return unported("0x004582c0", {obj}); }
  virtual void anim_cleanup() { unported("0x0050ace0", {1}); }
  virtual void fn_570410() { unported("0x00570410", {1}); }
  virtual Handle sounds_bank() { return unported("0x0056eb10", {}); }
  virtual void sounds_stop(Handle bank) { unported("0x0056f0a0", {bank}); }
  virtual void sounds_free(Handle bank) { unported("0x0056f190", {bank}); }
  virtual void sound_reset() { unported("0x005716a0", {}); }
  virtual void view_fn_40ae70(Handle view) { unported("0x0040ae70", {view, 0}); }
  virtual void view_fn_40adf0(Handle view) { unported("0x0040adf0", {view, 0, 0}); }
  virtual void pilot_set_vehicle(Handle v) { unported("0x004782c0", {v}); }
  virtual void delete_object(Handle obj) { unported("object.vt+0x0", {obj, 1}); }
  virtual void entities_remove(Handle e) { unported("0x005a89c0", {e}); }
  virtual void entities_add(Handle e) { unported("0x005a8820", {e}); }
  virtual void fn_4e6910() { unported("0x004e6910", {}); }
  virtual void fn_4b4ae0() { unported("0x004b4ae0", {0x60b228}); }
  virtual void landmines_free() { unported("0x00413770", {0x604e48}); }
  virtual void fn_49dc40() { unported("0x0049dc40", {}); }
  virtual void fn_4a7970() { unported("0x004a7970", {}); }
  virtual void fn_4d8270() { unported("0x004d8270", {}); }
  virtual void fn_584fe0() { unported("0x00584fe0", {}); }
  virtual void fn_47a720(Handle obj) { unported("0x0047a720", {obj}); }
  virtual void fn_4d54f0() { unported("0x004d54f0", {}); }
  virtual void fn_479850() { unported("0x00479850", {}); }
  virtual void fn_4ea750() { unported("0x004ea750", {}); }
  virtual void znet_free() { unported("0x0058b290", {0x603fd8}); }
  virtual void fn_4c5fa0() { unported("0x004c5fa0", {}); }
  virtual void fn_4c3240() { unported("0x004c3240", {}); }
  virtual void valves_end() { unported("0x004d3fa0", {}); }
  virtual void targets_end() { unported("0x004d3030", {}); }
  virtual void fn_4d71f0() { unported("0x004d71f0", {}); }
  virtual void fn_427300() { unported("0x00427300", {}); }
  virtual void fn_414000() { unported("0x00414000", {}); }
  virtual void fn_431800() { unported("0x00431800", {}); }
  virtual void fn_50b2f0() { unported("0x0050b2f0", {}); }
  virtual void fn_5394c0() { unported("0x005394c0", {1, 1}); }
  virtual void fn_5573d0() { unported("0x005573d0", {0}); }
  virtual void voice_release(const char* name) { unported("0x0056f530", {name}); }
};

// The missions object (0x006098d0), the fields mission.cpp uses.
struct Missions {
  MissionWorld world;            // +0x00..+0x4b  world.zrd's parameters (mission_world.h)
  float time = 0;                // +0x4c  the game time [0x0060991c], seconds since the setup
  float clock = 0;               // +0x50  the mission clock, counting down while +0x54
  uint8_t clock_down = 0;        // +0x54
  uint8_t ended = 0;             // +0x55  won or lost: the objectives are no longer checked
  uint8_t aborting = 0;          // +0x56  lost: the end screen follows after +0x58
  float end_delay = 0;           // +0x58  seconds left (8 from a loss)
  uint8_t loaded = 0;            // +0x5c  the mission is up
  int32_t current = -1;          // +0x60  the world's mission index, -1 none
  int32_t number = 0;            // +0x64  the game's mission number (0x00490720)
  Handle event = 0;              // +0x68  the loader thread's event
  Handle thread = 0;             // +0x6c  and the thread
  uint8_t abort = 0;             // +0x70  set from outside to stop a load
  // +0x78..+0x88 objectives.zrd's: +0x7c the primary objectives, +0x80 the
  // secondary, +0x84 the win's condition, +0x88 the loss's.
  uint32_t objectives[5] = {};
  Handle sounds[4] = {};         // +0x8c com_success, com_failed, obj_primary, obj_secondary
  Handle objective_sound = 0;    // +0x9c  the line an objective checked now announces itself with
  Handle voice_next = 0;         // +0xa0  the line asked for this frame (0x0048f6c0)
  float voice_next_priority = 0; // +0xa4
  Handle voice = 0;              // +0xa8  the line playing
  float voice_priority = 0;      // +0xac  its priority, -1 when none
  Handle stations = 0;           // +0xb0  multiplayer stations' list
  Handle xb4 = 0;                // +0xb4  the stations' owner (0x004797c0), deleted by the cleanup
  Handle xb8 = 0, xbc = 0;       // +0xb8 +0xbc  an object 0x00491c80 makes (0x004580c0) and its user
  uint8_t cheats = 0;            // +0xc4  [0x00609994]: a cheat was on as the mission started (0x00491d20); options().cheated is the one read
  uint8_t done = 0;              // the event +0x68 (set when the load ends)
  uint8_t failed = 0;            // the thread's exit code

  bool load_world(int32_t index);          // 0x0048faf0
  bool setup();                            // 0x00490ab0
  void start();                            // 0x00490880 (FIX (A): no thread)
  bool finished() const { return done; }   // 0x00490930
  void open_readers(const char* name);     // 0x00490630
  void block_name(char* out);              // 0x00490600
  void block_name(int32_t number, char* out);  // 0x00490590: m%d, ia%d or mp%d
  bool texture_budget(uint32_t* mb);       // 0x004902a0
  bool begin();                            // 0x0048f400
  void check_cheats();                     // 0x00491d20
  void update(float dt);                   // 0x0048f470
  void objectives_again();                 // 0x00491050: a network game's, at a respawn
  void play_voice();                       // 0x0048f630
  void ask_voice(Handle sound, float priority);  // 0x0048f6c0
  void complete();                         // 0x0048f740
  void lose();                             // 0x0048f9e0
  void end_screen();                       // 0x00491bf0
  void on_command(int32_t cmd);            // 0x0048f440, bound to 0x71 and 0x72
  void abort_now();                        // 0x0048fa80: ended now, as a failure
  bool cleanup(bool unload);               // 0x00491780
  bool unload();                           // 0x00490480: the world and all it brought
};

// The command handler 0x0048f400 binds (MISSION_COMPLETE 0x71 and 0x72).
constexpr uint32_t kMissionCommand = 0x0048f440;

Missions& missions();
void set_mission_hooks(MissionHooks* h);
MissionHooks& mission_hooks();

}  // namespace mech3
