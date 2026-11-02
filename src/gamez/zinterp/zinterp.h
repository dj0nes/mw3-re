#pragma once
#include <map>
#include <string>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <list>
#include <string>
#include <vector>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"

namespace zmod { struct Model; }
namespace zimg { struct TextureEntry; }

// The GameZ script interpreter (docs/interp.md): interp.zbd's archive, the
// line reader and tokenizer, macros, variables, the preprocessor and the
// command dispatcher. The dispatcher and the preprocessor are
// zInterp\zinterp_parse.cpp (named by its assert path); the functions after it
// (0x594dc0..0x596e04: dumps, the interpreter object, reader, macros, the
// archive, the scroll list) have no name in the exe and are kept in zinterp.cpp.
namespace zint {

using zcls::Node;
using zmod::Model;

// FIX (A): a script file. The loose files are opened "r" (text mode) on
// Windows; the port opens them binary and does MSVCRT's text-mode reading
// itself (CR LF -> LF, Ctrl-Z ends the file), so ftell/fseek stay byte offsets.
struct ScriptFile {
  std::FILE* f = nullptr;
  bool text = false;
  bool eof = false;                // _IOEOF (FILE +0x0c bit 0x10)
  bool err = false;                // _IOERR (bit 0x20)
  long end = -1;                   // ours (docs/modding.md, M3): an archived script's end
  std::string what;                // ours: "<archive>: script <name>", for its log line
  bool cut = false;                // ours: a line past `end` ended it
};

struct Macro {                     // 8 bytes in the exe: {name, value}
  std::string name, value;
};

struct Variable {                  // 0x0c bytes: {name, type, pointer}
  std::string name;
  int32_t type;                    // 0 int, 1 float, 2 string
  void* value;
};

struct Include {                   // 0x0c bytes: the parent file while a `source` runs
  ScriptFile* file;
  long pos;
  uint8_t binary;
  long end = -1;                   // ours (M3): the parent's own end, the archive shared
  std::string what;
};

struct ArchiveEntry {              // 0x80 bytes
  char name[0x78];
  int32_t timestamp;               // +0x78  time_t, against _stat's st_mtime
  int32_t offset;                  // +0x7c
};

// The model a script builds with ModelNew/ModelPolygon*/Matl* (malloc(0x1d8)
// at +0xa4, zeroed). Raw except for the two pointers the dispatcher keeps.
struct Build : zcls::Raw<0x1d8> {
  zimg::TextureEntry* texture = nullptr;  // +0x10   MatlTexture
  Model* model = nullptr;                 // +0x1d4  ModelNew
};

struct Interp;
using LogFn = void (*)(const char* fmt, va_list args);

struct Vtable {                    // base 0x005cf670, MW3 0x005bccc8
  bool (*pre)(Interp& in, const char* argv0);       // +0x00, before the dispatcher
  bool (*post)(Interp& in, const char* argv0);      // +0x04, after it
  bool (*on_error)(Interp& in, const char* argv0);  // +0x08, when the line set the error flag
};

struct Interp {                    // 0xbc bytes; MW3's subclass adds +0xbc
  const Vtable* vt = nullptr;      // +0x00
  uint8_t console = 0;             // +0x04  set by the debug console only
  uint32_t argc = 0;               // +0x08
  uint32_t cursor = 0;             // +0x0c  next argument for next_arg
  uint8_t error = 0;               // +0x10
  int32_t misses = 0;              // +0x14  layers that did not know the command
  uint8_t quit = 0;                // +0x18
  char* line = nullptr;            // +0x1c  the tokens' storage
  char* argv[16] = {};             // +0x20
  std::vector<Macro> macros;       // +0x60, count +0x64
  std::vector<Variable> vars;      // +0x68, count +0x6c
  LogFn log_fn = nullptr;          // +0x70  only the debug console sets it
  std::string search_path;         // +0x74  ".;zbd"
  std::string archive_name;        // +0x78  "interp.zbd"
  ScriptFile archive;              // +0x80  stays open once accepted
  uint32_t magic = 0;              // +0x84
  uint32_t version = 0;            // +0x88
  int32_t entry_count = 0;         // [+0x8c]
  std::vector<ArchiveEntry> dir;   // +0x90
  long archive_size = -1;          // ours (M3): bounds the scripts
  std::string archive_where;       // ours: the archive's file as the player has it
  uint8_t binary = 0;              // +0x94  reading the archive
  ScriptFile* file = nullptr;      // +0x98  the file being run
  std::vector<Include> includes;   // +0x9c, count +0xa0
  Build* build = nullptr;          // +0xa4
  int32_t loose_opens = 0;         // +0xb0  fopen calls for loose scripts
  uint32_t skip_depth = 0;         // +0xb4  open ifdef/ifndef blocks being skipped
  Node* current = nullptr;         // +0xb8
  void* mech = nullptr;            // +0xbc  MW3 subclass: the current mech
  int32_t exec_depth = 0;          // FIX (bug): lines kept until the outermost
  std::vector<char*> pending_free; //   line ends, see exec_line
};

void init(Interp& in, const char* archive, const char* search_path);  // 0x005952f0
void destroy(Interp& in);                                             // 0x005953d0
void reset(Interp& in);                                               // 0x00595440
bool source(Interp& in, const char* name);                            // 0x00595990
bool run(Interp& in, ScriptFile* f, bool binary);                     // 0x005954a0
bool exec_line(Interp& in, char* line);                               // 0x00595510
bool read_line(Interp& in, ScriptFile* f, char* line);                // 0x005955e0
void tokenize(Interp& in, char* line);                                // 0x00595850
char* expand(Interp& in, char* arg);                                  // 0x005956e0
const char* macro_find(Interp& in, const char* name, Macro** out);    // 0x00595a90
bool macro_set(Interp& in, const char* name, const char* value);      // 0x00595c60
void macro_set_true(Interp& in, const char* name);                    // 0x00595bb0
void macro_set_false(Interp& in, const char* name);                   // 0x00595bd0
bool macro_defined(Interp& in, const char* name);                     // 0x00595bf0
char* next_arg(Interp& in);                                           // 0x00595e70
bool arg_bool(Interp& in, bool def);                                  // 0x00595ea0
double arg_float(Interp& in, float def);                              // 0x00595ef0
int32_t arg_int(Interp& in, int32_t def);                             // 0x00595f10
Variable* var_find(Interp& in, const char* name);                     // 0x00595f30
bool var_register(Interp& in, const char* name, int32_t type, void* value);  // 0x00595fa0
void var_print(Interp& in, const Variable* v);                        // 0x00596020
void miss(Interp& in);                                                // 0x00596090
bool open_archive(Interp& in, const char* name);                      // 0x00596620
ScriptFile* archive_find(Interp& in, const char* name);               // 0x00596810
bool check_args(Interp& in, uint32_t count, int32_t cls, Node* node); // 0x005962d0
bool argv0_is(Interp& in, const char* name);                          // 0x00595270
bool argv0_starts(Interp& in, const char* name, size_t n);            // 0x00595240
void log(Interp& in, const char* fmt, ...);                           // 0x004108f0
void error(Interp& in, const char* fmt, ...);                         // 0x005952d0
void echo(Interp& in);                                                // 0x00595d50
void node_dump(Interp& in, Node* node);                               // 0x005951e0
bool preprocess(Interp& in, const char* argv0);                       // 0x00590e80
bool condition(Interp& in);                                           // 0x00590d80
void print_tree(Interp& in, Node* node, int indent);                  // 0x00591290
bool dispatch(Interp& in, const char* argv0);                         // 0x00591300
const Vtable& base_vtable();                                          // 0x005cf670

// Globals the dispatcher keeps between commands.
struct Globals {
  Model* object_model = nullptr;   // 0x00730c14  Object3DSetColor/Priority/ShowBackFace
  Model* cycle_model = nullptr;    // 0x00730c18  CycleTexture*, facade, morph
  Model* model_out = nullptr;      // 0x00730c1c  where node_get_model writes
  int32_t use_archive = 0;         // 0x005f4244  set by the mission load, never cleared
  int32_t verbose = 0;             // 0x0073f764  Verbose; nothing reads it
};
Globals& globals();

// The ScrollAlways list, a static object at 0x00731430: nodes whose texture
// scroll runs every frame through one holder node's action.
struct ScrollList {
  std::list<Node*> nodes;          // +0x04, size +0x08
  Node* holder = nullptr;          // +0x0c  "ScrollAlways", made on first use
};
ScrollList& scroll_list();
void scroll_list_init();                                                // 0x005962a0
bool scroll_on(Node* node, float u, float v, bool always);              // 0x005963f0
bool scroll_off(Node* node);                                            // 0x00596370
void scroll_tick(Node* node);                                           // 0x00596380
void scroll_always_tick(Node* holder);                                  // 0x005963a0
void scroll_list_clear(bool full);                                      // 0x005964c0
void scroll_list_destroy();                                             // 0x00596570

// AddClutterTemplates' names (std::list at 0x0073188c, size 0x00731890).
// Nothing in the exe reads them.
std::list<std::string>& clutter_templates();
void add_clutter_template(const char* name);                            // 0x00596db0

// Everything the commands call outside zInterp. Arguments as the original
// passes them: ECX, EDX, then the stack. engine() starts filled with the
// ported functions; the others are null until ported and a null call does
// nothing and returns 0. A test may replace any of them to record the calls.
using Action = void (*)(Node* node);
struct Engine {
  // zClass nodes and lists
  Node* (*find_node)(int list, const char* name);                 // 0x004f7fe0
  Node* (*find_in_tree)(Node* node, const char* name);            // 0x004f9d60
  Node* (*walk_prefix)(const char* name, int list);               // 0x004f8b80
  int (*list_count)(int list);                                    // 0x004f7f80
  void (*list_print)(int list);                                   // 0x004f7fa0
  Node* (*node_alloc)();                                          // 0x004eacd0
  int (*set_name)(Node* node, const char* name);                  // 0x004eb300
  int (*set_active)(Node* node, int on);                          // 0x004eb170
  int (*add_child)(Node* parent, Node* child);                    // 0x004eb9b0
  int (*delete_child)(Node* parent, Node* child);                 // 0x004ebb40
  int (*delete_tree)(Node* node);                                 // 0x004f9050
  int (*free_node)(Node* node);                                   // 0x004f3390
  int (*free_world)(Node* node);                                  // 0x004f1c60
  int (*get_model)(Node* node, Model** out);                      // 0x004eb420
  int (*set_model)(Node* node, Model* model);                     // 0x004eb370
  int (*set_action)(Node* node, Action action);                   // 0x004eb450
  int (*set_can_modify)(Node* node, int on);                      // 0x004eb260
  int (*set_overwrite)(Node* node, int on);                       // 0x004eb960
  int (*set_action_priority)(Node* node, int priority);           // 0x004eb5b0
  int (*set_altitude_surface)(Node* node, int on);                // 0x004eb620
  int (*set_intersect_surface)(Node* node, int on);               // 0x004eb6d0
  int (*set_intersect_bbox)(Node* node, int on);                  // 0x004eb750
  int (*set_proximity)(Node* node, int on);                       // 0x004eb790
  int (*set_landmark)(Node* node, int on);                        // 0x004eb7d0
  int (*set_lighting)(Node* node, int on);                        // 0x004f9e50
  int (*pool_alive)();                                            // 0x004f8f50
  // gamez.zbd
  int (*read_zbd)(const char* path);                              // 0x004ff520
  void (*write_prepare_a)();                                      // 0x004f7f70
  void (*write_prepare_b)();                                      // 0x004f7bd0
  int (*write_zbd)(const char* path);                             // 0x004fe8e0
  // Camera.c
  Node* (*camera_create)();                                       // 0x004f32e0
  int (*camera_set_rotate)(Node* node, float x, float y, float z);      // 0x004f36b0
  int (*camera_get_translate)(Node* node, float* x, float* y, float* z);  // 0x004f39d0
  int (*camera_set_active)(Node* node, int on);                   // 0x004f3420
  void (*camera_set_dynamic_lod)(int on, float distance);         // 0x004f32a0
  int (*camera_set_fov)(Node* node, float h, float v);            // 0x004f3d90
  int (*camera_set_horizon_xz)(Node* node, Node* horizon);        // 0x004f4100
  int (*camera_set_horizon)(Node* node, Node* horizon);           // 0x004f4090
  int (*camera_set_lod_multiplier)(Node* node, float m);          // 0x004f3ff0
  int (*camera_set_clip)(Node* node, float near_clip, float far_clip);  // 0x004f3a70
  int (*camera_get_clip)(Node* node, float* near_clip, float* far_clip);  // 0x004f3b00
  int (*camera_set_object_hse_test)(int on);                      // 0x004f3550
  int (*camera_set_window)(Node* node, Node* window);             // 0x004f3620
  int (*camera_set_world)(Node* node, Node* world);               // 0x004f3560
  int (*camera_set_translate)(Node* node, float x, float y, float z);  // 0x004f3870
  // Display.c, Window.c
  Node* (*display_create)();                                      // 0x004fa4c0
  int (*display_origin)(Node* node, int x, int y);                // 0x004fa600
  int (*display_resolution)(Node* node, int w, int h);            // 0x004fa580
  int (*display_set_clear_color)(Node* node, float r, float g, float b);  // 0x004fa680
  Node* (*window_create)();                                       // 0x004f9e90
  int (*window_add_clear_vertex)(Node* node, const float* xyz);   // 0x004fa2c0
  int (*window_buffer)(Node* node, int buffer);                   // 0x004fa1c0
  int (*window_close_clear_polygon)(Node* node);                  // 0x004fa3e0
  int (*window_origin)(Node* node, int x, int y);                 // 0x004fa0b0
  int (*window_resolution)(Node* node, int w, int h);             // 0x004f9fa0
  int (*window_set_clear_polygon)(Node* node, int on);            // 0x004fa230
  // Light.c
  Node* (*light_create)();                                        // 0x004fa720
  int (*light_set_ambient)(Node* node, float v);                  // 0x004fa940
  int (*light_set_color)(Node* node, float r, float g, float b);  // 0x004fb510
  int (*light_set_diffuse)(Node* node, float v);                  // 0x004fa9b0
  int (*light_set_directed)(Node* node);                          // 0x004faaa0
  int (*light_set_directional)(Node* node, int on);               // 0x004faa20
  int (*light_set_orientation)(Node* node, float a, float b, float c);  // 0x004faff0
  int (*light_set_point)(Node* node);                             // 0x004fab20
  int (*light_set_ranges)(Node* node, float near_range, float far_range);  // 0x004fadf0
  int (*light_set_saturated)(Node* node, int on);                 // 0x004faba0
  int (*light_set_static)(Node* node, int on);                    // 0x004fad30
  int (*light_set_subdivide)(Node* node, int on);                 // 0x004fac70
  int (*light_set_translate)(Node* node, float x, float y, float z);  // 0x004faf80
  // LOD, SEQ, Object3d
  Node* (*lod_create)();                                          // 0x004fe030
  int (*lod_add_child)(Node* node, Node* child);                  // 0x004fe0a0
  Node* (*seq_create)();                                          // 0x004fdc70
  int (*seq_add_child)(Node* node, Node* child, int frame, float time);  // 0x004fdcd0
  int (*seq_set_active)(Node* node, int v);                       // 0x004fde50
  int (*seq_set_loop)(Node* node, int v);                         // 0x004fded0
  int (*seq_set_pause)(Node* node, int v);                        // 0x004fdf10
  int (*seq_set_repeat)(Node* node, int v);                       // 0x004fde90
  Node* (*object3d_create)();                                     // 0x004fb740
  int (*object3d_add_child)(Node* parent, Node* child);           // 0x004fb7a0
  int (*object3d_get_translate)(Node* node, float* x, float* y, float* z);  // 0x004fc020
  int (*object3d_rotate)(Node* node, float a, float b, float c);  // 0x004fbd80
  int (*object3d_scale)(Node* node, float x, float y, float z);   // 0x004fbc10
  int (*object3d_set_opacity_is_set)(Node* node, int on);         // 0x004fbb90
  int (*object3d_set_opacity)(Node* node, float v);               // 0x004fba20
  int (*object3d_translate)(Node* node, float x, float y, float z);  // 0x004fc0b0
  // World
  Node* (*world_create)();                                        // 0x004f1bd0
  int (*world_add_light)(Node* node, Node* light);                // 0x004f2d50
  int (*world_extents)(Node* node, float x, float z);             // 0x004f2530
  int (*world_origin)(Node* node, float x, float z);              // 0x004f2500
  int (*world_partition_tolerance)(Node* node, float a, float b); // 0x004f2860
  int (*world_partition_max_dec)(Node* node, int count);          // 0x004f2880
  int (*world_partition)(Node* node, float x, float z);           // 0x004f2560
  int (*world_set_fog_type)(Node* node, uint32_t type);           // 0x004f23e0
  int (*world_set_fog_color)(Node* node, float r, float g, float b);  // 0x004f23f0
  void (*world_get_fog_color)(Node* node, float* r, float* g, float* b);  // 0x004f24a0
  int (*world_set_fog_altitude)(Node* node, float a, float b);    // 0x004f2420
  int (*world_set_fog_density)(Node* node, float v);              // 0x004f2460
  int (*world_set_fog_range)(Node* node, float near_range, float far_range);  // 0x004f2440
  void (*world_get_fog_range)(Node* node, float* near_range, float* far_range);  // 0x004f24c0
  int (*world_set_virtual_map_size)(Node* node, int w, int h);    // 0x004f1dc0
  int (*world_set_virtual_partition)(Node* node, int on);         // 0x004f1d90
  // Models, materials, textures
  zimg::TextureEntry* (*texture_add)(const char* name);           // 0x00518c60
  zimg::TextureEntry* (*find_texture)(const char* name);          // 0x00518220
  int (*texture_directory)(const char* dirs);                     // 0x00516f50
  uint16_t (*pack_rgb)(const float rgb[3]);                       // 0x005793c0
  int (*matl_new)(Build* build);                                  // 0x00536a70
  int (*material_array_size)(int n);                              // 0x00536a60
  int (*model3d_array_size)(int n);                               // 0x005269a0
  Model* (*model_new)();                                          // 0x00538fe0
  int (*model_set_facade)(Model* model, int mode);                // 0x00539cc0
  int (*model_do_shadows)(int on);                                // 0x00526a70
  int (*polygon_material)(Build* build);                          // 0x00536ad0
  int (*polygon_prepare)(void* at_1d0);                           // 0x0053cc40
  int (*polygon_add)(Model* model, int verts, float* xyz, int zero_a, float* uv, int uv_count, void* at_fc,
                     void* at_174, int material, int at_1c8, int at_1cc, void* at_1d0, int zero_b);  // 0x0053c6c0
  int (*cycle_set_looping)(Model* model, int on);                 // 0x0053bcd0
  int (*cycle_set_map)(Model* model, zimg::TextureEntry* tex);    // 0x0053bd10
  int (*cycle_set_on)(Model* model, int on);                      // 0x0053bc30
  int (*cycle_set_speed)(Model* model, float speed);              // 0x0053bcf0
  int (*model_set_color)(Model* model, int color);                // 0x0053bd60
  int (*model_set_priority)(Model* model, int priority);          // 0x0053bb20
  int (*model_set_show_backface)(Model* model, int on);           // 0x0053bb50
  int (*model_set_morph)(Model* model, void* stack_vector, float v, int zero, int six);  // 0x0053b960
  int (*model_set_scroll)(Model* model, int on, float u, float v);  // 0x00526ae0
  void (*model_scroll_tick)(Model* model);                        // 0x00541ad0
  int (*texture_world_base)(float u, float v);                    // 0x00526ac0
  int (*texture_world_per_meter)(float u, float v);               // 0x00526aa0
  int (*palette_name)(const char* name);                          // 0x005975e0
  int (*palette_shading)(int on);                                 // 0x005269e0
  int (*vertex_shading)(int on);                                  // 0x00526a00
  int (*inverse_z_tolerance)(float v);                            // 0x00526df0
  int (*small_polygon_reject)(float v);                           // 0x00535ed0
  void (*bfe_tolerance)(float v);                                 // 0x0053cef0
  float (*get_bfe_tolerance)();                                   // 0x0053cf00
  void (*coplanar_tolerance)(float v);                            // 0x00538070
  void (*colinear_tolerance)(float v);                            // 0x00538080
  int (*write_texture_type)(int type);                            // 0x005426a0
  int (*write_texture_map)(zimg::TextureEntry* tex);              // 0x005426b0
  // Renderer, video, the rest
  int (*perspective_delta_x)(int v);                              // 0x005580d0
  int (*perspective_adaptive)(int a, int b, float c);             // 0x00558120
  int (*perspective_inverse_z)(float v);                          // 0x00558170
  int (*perspective_far_z)(float v);                              // 0x00558140
  int (*clear_screen_buffer)(int on);                             // 0x0057a3c0
  int (*video_dither)(int on);                                    // 0x00579240
  int (*video_wireframe)(int on);                                 // 0x00579230
  void (*clouds_on)(int32_t on);                                  // 0x00543910
  void (*clouds_period)(float v);                                 // 0x00543920
  void (*clouds_wavelength)(float v);                             // 0x00543940
  void (*clouds_level)(float base, float amplitude);              // 0x00543960
  void (*clouds_heading)(float radians);                          // 0x005439f0
  int (*make_shadows)();                                          // 0x005487d0
  int (*wind_max_speed)(float v);                                 // 0x0054b650
  int (*wind_accel)(float v);                                     // 0x0054b660
  int (*wind_ang_vel)(float v);                                   // 0x0054b670
  int (*wind_static_velocity)(const float* xyz);                  // 0x0054b630
  int (*lens_flare_texture)(int index, zimg::TextureEntry* tex);  // 0x005676d0
  int (*load_soils)(const char* name);                            // 0x00537cb0
  int (*rdr_add_path)(const char* dirs);                          // 0x005561e0
  int (*rdr_set_path)(const char* dirs);                          // 0x005561a0
  void (*anim_set_zbd_path)(const char* path);                    // 0x005090f0
  int (*anim_debug_frame)();                                      // 0x00507dd0
  // C runtime and Win32, FIX (A): through the write dir (zutl_path.h)
  int (*delete_file)(const char* path);                           // DeleteFileA
  int (*make_dir)(const char* path);                              // _mkdir: 0, or errno
  void (*print)(const char* text);                                // printf, formatted
};
Engine& engine();
// Ours: the engine entries a script reached while null (skipped), by field.
std::map<std::string, uint32_t>& null_calls();

// Test seam (not in the exe): told of each line, each dispatch and each
// line's end, to compare the port's run with the original's.
struct Observer {
  void (*line)(Interp& in);
  void (*dispatched)(Interp& in);
  void (*line_done)(Interp& in, bool go_on);
};
Observer& observer();

}  // namespace zint
