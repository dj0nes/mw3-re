// zInterp\zinterp_parse.cpp: the preprocessor and the command dispatcher
// (docs/interp.md, sections 3 and 4). The preprocessor and the tree print sit
// just before the dispatcher, after zgameapp.cpp, so they are kept here.
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "crt/msvc_math.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zgame/zgame_table.h"
#include "gamez/zinterp/zinterp.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zrender/zrndr_fog.h"

namespace zint {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zInterp\\zinterp_parse.cpp";
constexpr double kDegToRad = 0.01745329251994;  // 0x005cf668

// An engine entry the program leaves null is skipped, as before, and counted
// by name (null_calls, ours: which ones the shipped scripts reach).
template <typename R, typename... P, typename... A>
R call_named(const char* name, R (*fn)(P...), A... a) {
  if (fn) return fn(a...);
  ++null_calls()[name];
  return R();
}
#define call(fn, ...) call_named(#fn, fn, ##__VA_ARGS__)

// 0x0049bfd0, formatted here (zerr::report takes the finished message).
void report(int line, const char* fmt, ...) {
  char msg[0x400];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  zerr::report(0x200, kFile, line, msg);
}

void print(const char* fmt, ...) {  // printf
  char msg[0x400];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  call(engine().print, static_cast<const char*>(msg));
}

// The x87's degrees to radians: a float argument times the double constant,
// rounded once (crt::x87_mul_f24). Some commands round the argument to a
// float first and some multiply atof's double directly; each says which.
float rad(double deg) { return crt::x87_mul_f24(deg, kDegToRad); }

// Byte offsets into the model being built (+0xa4).
template <typename T> T bget(Build* b, int off) { return b->get<T>(off); }
template <typename T> void bput(Build* b, int off, T v) {
  // FIX (bug): the exe writes vertices and UVs past the block with no bound.
  if (off >= 0 && off + static_cast<int>(sizeof v) <= static_cast<int>(sizeof b->raw)) b->put(off, v);
}

struct Args {
  Interp& in;
  float f() { return static_cast<float>(arg_float(in, 0.0f)); }
  double d() { return arg_float(in, 0.0f); }
  int32_t i() { return arg_int(in, 0); }
  int b() { return arg_bool(in, false) ? 1 : 0; }
  char* s() { return next_arg(in); }
};

// argv[0] against a name: the exe's strncmp with its own length, so some
// names match any longer word that starts with them (docs/interp.md).
bool is(const char* argv0, const char* name, size_t n) { return std::strncmp(argv0, name, n) == 0; }
bool exact(const char* argv0, const char* name) { return std::strcmp(argv0, name) == 0; }

enum Result { kKnown, kMiss };

// The New* commands' shared tail (0x00593ef0): the new node becomes current
// and takes the next argument as its name.
Result new_node(Interp& in, Node* n) {
  in.current = n;
  char* name = next_arg(in);
  call(engine().set_name, in.current, static_cast<const char*>(name));
  return kKnown;
}

Result letter_a(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "AddChild", 8)) {
    char* name = a.s();
    Node* child = call(e.find_node, 6, static_cast<const char*>(name));
    if (!in.current) error(in, "%s %s FAILED because current node is NULL", argv0, name);
    else if (!child) error(in, "%s %s FAILED because node [%s] wasn't found", argv0, name, name);
    else call(e.add_child, in.current, child);
    return kKnown;
  }
  if (is(argv0, "AddClutterTemplates", 0x13)) {
    if (in.argc == 2) add_clutter_template(a.s());
    return kKnown;
  }
  if (is(argv0, "AddEnhancerImage", 0x10)) {
    call(e.texture_add, static_cast<const char*>(a.s()));
    return kKnown;
  }
  if (is(argv0, "AnimSetZBDFile", 0xe)) {
    call(e.anim_set_zbd_path, static_cast<const char*>(a.s()));
    return kKnown;
  }
  if (is(argv0, "AnimSetDebugFrame", 0x11)) {
    call(e.anim_debug_frame);
    return kKnown;
  }
  return kMiss;
}

Result letter_c(Interp& in, const char* argv0) {
  Engine& e = engine();
  Globals& g = globals();
  Args a{in};
  if (is(argv0, "CameraSetRotate", 0xc)) {
    const float x = a.f(), y = a.f(), z = a.f();
    call(e.camera_set_rotate, in.current, x, y, z);
    return kKnown;
  }
  if (exact(argv0, "CameraGetTranslate")) {
    if (!in.current) return kKnown;
    float x = 0, y = 0, z = 0;
    call(e.camera_get_translate, in.current, &x, &y, &z);
    log(in, "%s --> ( %.2f %.2f %.2f )", in.current->name(), x, y, z);
    return kKnown;
  }
  if (is(argv0, "CameraSetActive", 0xf)) {
    const int on = a.b();
    call(e.camera_set_active, in.current, on);
    return kKnown;
  }
  if (is(argv0, "CameraSetDynamicLOD", 0x13)) {
    const float v = a.f();
    call(e.camera_set_dynamic_lod, 1, v);  // ECX is the constant 1
    return kKnown;
  }
  if (is(argv0, "CameraSetFOV", 0xc)) {
    const float h = a.f();  // h is rounded to a float first, v is not
    const double v = a.d();
    const float hr = rad(h), vr = rad(v);
    call(e.camera_set_fov, in.current, hr, vr);
    return kKnown;
  }
  if (is(argv0, "CameraSetHorizonXZ", 0x12)) {
    Node* n = call(e.find_node, 6, static_cast<const char*>(a.s()));
    call(e.camera_set_horizon_xz, in.current, n);
    return kKnown;
  }
  if (is(argv0, "CameraSetHorizon", 0x10)) {
    Node* n = call(e.find_node, 6, static_cast<const char*>(a.s()));
    call(e.camera_set_horizon, in.current, n);
    return kKnown;
  }
  if (is(argv0, "CameraSetLODMultiplier", 0x16)) {
    const float v = a.f();
    call(e.camera_set_lod_multiplier, in.current, v);
    return kKnown;
  }
  if (is(argv0, "CameraSetNearFarClip", 0x14)) {
    const float n = a.f(), f = a.f();
    call(e.camera_set_clip, in.current, n, f);
    return kKnown;
  }
  if (exact(argv0, "CameraSetNearClip")) {
    float n = 0, f = 0;
    call(e.camera_get_clip, in.current, &n, &f);
    n = a.f();
    call(e.camera_set_clip, in.current, n, f);
    return kKnown;
  }
  if (exact(argv0, "CameraSetFarClip")) {
    float n = 0, f = 0;
    call(e.camera_get_clip, in.current, &n, &f);
    f = a.f();
    call(e.camera_set_clip, in.current, n, f);
    return kKnown;
  }
  if (is(argv0, "CameraSetObjectHSETest", 0x16)) {
    call(e.camera_set_object_hse_test, a.b());
    return kKnown;
  }
  if (is(argv0, "CameraSetWindow", 0xf)) {
    Node* n = call(e.find_node, 0xe, static_cast<const char*>(a.s()));
    call(e.camera_set_window, in.current, n);
    return kKnown;
  }
  if (is(argv0, "CameraSetWorld", 0xe)) {
    Node* n = call(e.find_node, 0xd, static_cast<const char*>(a.s()));
    call(e.camera_set_world, in.current, n);
    return kKnown;
  }
  if (is(argv0, "CameraSetTranslate", 0xf)) {
    const float x = a.f(), y = a.f(), z = a.f();
    call(e.camera_set_translate, in.current, x, y, z);
    return kKnown;
  }
  if (is(argv0, "CountCameraNodes", 0xe)) {
    log(in, "# of nodes in camera list = %d\n", call(e.list_count, 8));
    return kKnown;
  }
  if (is(argv0, "CountUsedNodes", 0xe)) {
    log(in, "# of nodes in used list = %d\n", call(e.list_count, 6));
    return kKnown;
  }
  if (is(argv0, "CycleTextureSetLooping", 0x16)) {
    const int on = a.b();
    call(e.cycle_set_looping, g.cycle_model, on);
    return kKnown;
  }
  if (is(argv0, "CycleTextureSetMap", 0x12)) {
    zimg::TextureEntry* t = call(e.texture_add, static_cast<const char*>(a.s()));
    call(e.cycle_set_map, g.cycle_model, t);
    return kKnown;
  }
  if (is(argv0, "CycleTextureSetOn", 0x11)) {
    const int32_t on = a.i();
    if (!in.current) {
      report(0x1ac, "No current node to enable cycle textures.  Take note of preceding \"FindNode\" Error");
      return kKnown;
    }
    call(e.get_model, in.current, &g.model_out);
    g.cycle_model = g.model_out;
    // zerr is an empty stub in the shipped game: neither report stops the
    // command, so a node without a model reaches the cycle setter.
    if (!g.cycle_model) report(0x1b6, "ERROR no GFX data for cycled texture (%s)", in.current->name());
    if (call(e.cycle_set_on, g.cycle_model, on) != 0)
      report(0x1bc, "Node (%s) has no graphics data for cycled texture\n", in.current->name());
    return kKnown;
  }
  if (is(argv0, "CycleTextureSetSpeed", 0x14)) {
    const float v = a.f();
    call(e.cycle_set_speed, g.cycle_model, v);
    return kKnown;
  }
  if (is(argv0, "ClearScreenBuffer", 0x11)) {
    call(e.clear_screen_buffer, a.b());
    return kKnown;
  }
  return kMiss;
}

Result letter_d(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "DeleteChild", 0xb)) {
    char* name = a.s();
    Node* child = call(e.find_in_tree, in.current, static_cast<const char*>(name));
    if (in.current && child) call(e.delete_child, in.current, child);
    else report(0x1da, "interp: DeleteChild (%s, %s) --> NULL NODE", in.current ? in.current->name() : "NULL", name);
    return kKnown;
  }
  if (is(argv0, "DeleteFile", 0xa)) {
    call(e.delete_file, static_cast<const char*>(a.s()));
    return kKnown;
  }
  if (is(argv0, "DeleteTree", 0xa)) {
    char* name = a.s();
    Node* n = call(e.find_node, 6, static_cast<const char*>(name));
    if (!n) report(0x1f2, "interp: DeleteTree (%s) --> NULL NODE", name);
    else call(e.delete_tree, n);
    return kKnown;
  }
  // These five need an exact argument count; with another count the word
  // falls through to the rest of the list and counts as unknown.
  if (exact(argv0, "DirectedLightFluctuate") && in.argc == 2) {
    call(e.clouds_on, static_cast<int32_t>(a.b()));
    return kKnown;
  }
  if (exact(argv0, "DirectedLightFluctuatePeriod") && in.argc == 2) {
    call(e.clouds_period, a.f());
    return kKnown;
  }
  if (exact(argv0, "DirectedLightFluctuateWavelength") && in.argc == 2) {
    call(e.clouds_wavelength, a.f());
    return kKnown;
  }
  if (exact(argv0, "DirectedLightFluctuateBaseAndAmp") && in.argc == 3) {
    const float base = a.f(), amp = a.f();
    call(e.clouds_level, base, amp);
    return kKnown;
  }
  if (exact(argv0, "DirectedLightFluctuateHeading") && in.argc == 2) {
    call(e.clouds_heading, rad(a.d()));
    return kKnown;
  }
  if (is(argv0, "DisplayOrigin", 0xd)) {
    const int32_t x = a.i(), y = a.i();
    call(e.display_origin, in.current, x, y);
    return kKnown;
  }
  if (is(argv0, "DisplayResolution", 0x11)) {
    const int32_t w = a.i(), h = a.i();
    call(e.display_resolution, in.current, w, h);
    return kKnown;
  }
  if (is(argv0, "DisplaySetClearColor", 0x14)) {
    const float r = a.f(), g = a.f(), b = a.f();
    call(e.display_set_clear_color, in.current, r, g, b);
    return kKnown;
  }
  return kMiss;
}

Result letter_f(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "FindNode", 8)) {
    char* name = a.s();
    in.current = call(e.find_node, 6, static_cast<const char*>(name));
    if (!in.current) error(in, "FindNode %s: FAILED", name);
    return kKnown;
  }
  if (is(argv0, "FindSubNode", 0xb)) {
    char* name = a.s();
    in.current = call(e.find_in_tree, in.current, static_cast<const char*>(name));
    if (!in.current) error(in, "FindSubNode %s: FAILED", name);
    return kKnown;
  }
  if (is(argv0, "FogLargeObjects", 0xf)) {
    a.b();  // to 0x005ac5c0, an empty function
    return kKnown;
  }
  if (is(argv0, "FreeNode", 8)) {
    char* name = a.s();
    Node* n = call(e.find_node, 6, static_cast<const char*>(name));
    // FIX (bug): the exe reads the class of a node it did not find and crashes.
    if (!n) return kKnown;
    int r;
    switch (n->cls()) {
      case 1: case 3: case 5: r = call(e.free_node, n); break;
      case 2: r = call(e.free_world, n); break;
      default:
        print("Unrecognized node class = %d\n", n->cls());
        r = 1;
    }
    if (r) print("   Error freeing node %s\n", name);
    return kKnown;
  }
  return kMiss;
}

Result letter_g(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "GameZReadZBDFile", 0x10)) {
    char* path = a.s();
    if (call(e.read_zbd, static_cast<const char*>(path)) != 0) error(in, "%s %s FAILED", argv0, path);
    return kKnown;
  }
  if (is(argv0, "GameZWriteZBDFile", 0x11)) {
    char* path = a.s();
    call(e.write_prepare_a);
    call(e.write_prepare_b);
    call(e.write_zbd, static_cast<const char*>(path));
    return kKnown;
  }
  if (is(argv0, "GetBFETolerance", 0xf)) {
    log(in, "BFE Tolerance: %.7f", static_cast<double>(call(e.get_bfe_tolerance)));
    return kKnown;
  }
  // As DirectedLightFluctuate*: a wrong count falls through to unknown.
  if (is(argv0, "GlobalWindRandomMaxSpeed", 0x18) && in.argc == 2) {
    call(e.wind_max_speed, a.f());
    return kKnown;
  }
  if (is(argv0, "GlobalWindRandomAccel", 0x15) && in.argc == 2) {
    call(e.wind_accel, a.f());
    return kKnown;
  }
  if (is(argv0, "GlobalWindRandomAngVel", 0x16) && in.argc == 2) {
    call(e.wind_ang_vel, rad(a.d()));
    return kKnown;
  }
  if (is(argv0, "GlobalWindStaticVelocity", 0x18) && in.argc == 4) {
    float v[3];
    v[0] = a.f(), v[1] = a.f(), v[2] = a.f();
    call(e.wind_static_velocity, static_cast<const float*>(v));
    return kKnown;
  }
  return kMiss;
}

Result light_bool(Interp& in, int (*fn)(Node*, int)) {
  if (!in.current) {
    // FIX (bug): the exe passes no argument for the %s (the log callback, if
    // any, reads a stray stack word); argv[0] here.
    error(in, "%s Failed: no current node", in.argv[0]);
    return kKnown;
  }
  const int on = arg_bool(in, false) ? 1 : 0;
  call(fn, in.current, on);
  return kKnown;
}

Result letter_l(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "LensFlareTexture", 0x10)) {
    const int32_t index = a.i();
    zimg::TextureEntry* t = call(e.texture_add, static_cast<const char*>(a.s()));
    call(e.lens_flare_texture, index, t);
    return kKnown;
  }
  // Pirate's Moon 1.0 @ 0x005b2d10 (MW3 has no such command): the whole word.
  if (const auto set = zgame::game().zint_light_map_texture; set && exact(argv0, "LightMapTexture")) {
    set(call(e.texture_add, static_cast<const char*>(a.s())));
    return kKnown;
  }
  if (is(argv0, "LightNew", 8)) return new_node(in, call(e.light_create));
  if (is(argv0, "LightSetActive", 0xe)) {  // jumps into Object3DSetActive
    const int on = a.b();
    call(e.set_active, in.current, on);
    return kKnown;
  }
  if (is(argv0, "LightSetAmbient", 0xf)) {
    const float v = a.f();
    call(e.light_set_ambient, in.current, v);
    return kKnown;
  }
  if (is(argv0, "LightSetColor", 0xd)) {
    const float r = a.f(), g = a.f(), b = a.f();
    call(e.light_set_color, in.current, r, g, b);
    return kKnown;
  }
  if (is(argv0, "LightSetDiffuse", 0xf)) {
    const float v = a.f();
    call(e.light_set_diffuse, in.current, v);
    return kKnown;
  }
  if (is(argv0, "LightSetDirectedSource", 0x16)) {
    call(e.light_set_directed, in.current);
    return kKnown;
  }
  if (is(argv0, "LightSetDirectional", 0x13)) {
    const int on = a.b();
    call(e.light_set_directional, in.current, on);
    return kKnown;
  }
  if (is(argv0, "LightSetOrientation", 0x13)) {
    const float x = a.f(), y = a.f();  // the last angle stays a double
    const double z = a.d();
    const float zr = rad(z), yr = rad(y), xr = rad(x);
    call(e.light_set_orientation, in.current, xr, yr, zr);
    return kKnown;
  }
  if (is(argv0, "LightSetPointSource", 0x13)) {
    call(e.light_set_point, in.current);
    return kKnown;
  }
  if (is(argv0, "LightSetRanges", 0xe)) {
    const float n = a.f(), f = a.f();
    call(e.light_set_ranges, in.current, n, f);
    return kKnown;
  }
  if (is(argv0, "LightSetSaturated", 0x11)) return light_bool(in, e.light_set_saturated);
  if (is(argv0, "LightSetStatic", 0xe)) return light_bool(in, e.light_set_static);
  if (is(argv0, "LightSetSubdivide", 0x11)) return light_bool(in, e.light_set_subdivide);
  if (is(argv0, "LightSetTranslate", 0x11)) {
    const float x = a.f(), y = a.f(), z = a.f();
    call(e.light_set_translate, in.current, x, y, z);
    return kKnown;
  }
  if (is(argv0, "LoadSoils", 9)) {
    call(e.load_soils, static_cast<const char*>(a.s()));
    return kKnown;
  }
  if (is(argv0, "LODAddChild", 0xb)) {
    Node* child = call(e.find_node, 6, static_cast<const char*>(a.s()));
    call(e.lod_add_child, in.current, child);
    return kKnown;
  }
  if (is(argv0, "LODSetRange", 0xb)) {
    const float n = a.f(), f = a.f();
    // Squared, into the current node's LOD data. FIX (bug): the exe has no
    // check for a missing node or data.
    if (in.current && in.current->data) {
      auto* lod = static_cast<zcls::Lod*>(in.current->data);
      lod->put<float>(4, n * n);
      lod->put<float>(0xc, f * f);
    }
    return kKnown;
  }
  return kMiss;
}

Result letter_m(Interp& in, const char* argv0) {
  Engine& e = engine();
  Build* b = in.build;
  Args a{in};
  if (exact(argv0, "MakeShadows")) {
    call(e.make_shadows);
    return kKnown;
  }
  if (is(argv0, "MatlFaceColor", 0xd)) {
    bput(b, 4, a.f());
    bput(b, 8, a.f());
    bput(b, 0xc, a.f());
    bput<uint16_t>(b, 2, call(e.pack_rgb, static_cast<const float*>(b->floats(4))));
    return kKnown;
  }
  if (is(argv0, "MatlNew", 7)) {
    call(e.matl_new, b);
    return kKnown;
  }
  if (is(argv0, "MatlTexture", 0xb)) {
    b->texture = call(e.find_texture, static_cast<const char*>(a.s()));
    if (!b->texture) bput<uint16_t>(b, 0, bget<uint16_t>(b, 0) & 0xfeff);
    else b->raw[1] |= 1;
    return kKnown;
  }
  if (is(argv0, "ModelNew", 8)) {
    in.current = call(e.object3d_create);
    char* name = a.s();
    call(e.set_name, in.current, static_cast<const char*>(name));
    b->model = call(e.model_new);
    call(e.set_model, in.current, b->model);
    const char* kind = a.s();
    // FIX (bug): without a second argument the exe's strncmp reads null.
    call(e.model_set_facade, b->model, kind && std::strncmp(kind, "Facade", 6) == 0 ? 1 : 0);
    return kKnown;
  }
  if (is(argv0, "ModelDoShadows", 0xe) && in.argc == 2) {
    if (zrndr::device_fog().active == 0) return kKnown;  // 0x0072f348: hardware only
    call(e.model_do_shadows, a.b());
    return kKnown;
  }
  if (is(argv0, "ModelPolygonBegin", 0x11)) {
    bput<int32_t>(b, 0x28, 0);
    bput<int32_t>(b, 0xa4, 0);
    bput<int32_t>(b, 0xf8, 0);
    return kKnown;
  }
  if (is(argv0, "ModelPolygonEnd", 0xf)) {
    bput<int32_t>(b, 0x1c4, call(e.polygon_material, b));
    call(e.polygon_prepare, static_cast<void*>(b->raw + 0x1d0));
    call(e.polygon_add, b->model, bget<int32_t>(b, 0x28), b->floats(0x2c), 0, b->floats(0xa8), bget<int32_t>(b, 0xf8),
         static_cast<void*>(b->raw + 0xfc), static_cast<void*>(b->raw + 0x174), bget<int32_t>(b, 0x1c4),
         bget<int32_t>(b, 0x1c8), bget<int32_t>(b, 0x1cc), static_cast<void*>(b->raw + 0x1d0), 0);
    return kKnown;
  }
  if (is(argv0, "ModelPolygonUV", 0xe)) {
    const int32_t n = bget<int32_t>(b, 0xa4);
    bput(b, 0xa8 + 8 * n, a.f());
    bput(b, 0xac + 8 * n, a.f());
    bput<int32_t>(b, 0xa4, bget<int32_t>(b, 0xa4) + 1);
    return kKnown;
  }
  if (is(argv0, "ModelPolygonVertex", 0x12)) {
    const int32_t n = bget<int32_t>(b, 0x28);
    bput(b, 0x2c + 12 * n, a.f());
    bput(b, 0x30 + 12 * n, a.f());
    bput(b, 0x34 + 12 * n, a.f());
    bput<int32_t>(b, 0x28, bget<int32_t>(b, 0x28) + 1);
    return kKnown;
  }
  return kMiss;
}

Result letter_n(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "NewCamera", 9)) return new_node(in, call(e.camera_create));
  if (is(argv0, "NewDisplay", 0xa)) return new_node(in, call(e.display_create));
  if (is(argv0, "NewLOD", 6)) return new_node(in, call(e.lod_create));
  if (is(argv0, "NewNode", 7)) return new_node(in, call(e.node_alloc));
  if (is(argv0, "NewObject3D", 0xb)) return new_node(in, call(e.object3d_create));
  if (is(argv0, "NewSEQ", 6)) return new_node(in, call(e.seq_create));
  if (is(argv0, "NewWindow", 9)) return new_node(in, call(e.window_create));
  if (is(argv0, "NewWorld", 8)) return new_node(in, call(e.world_create));
  if (exact(argv0, "NodeDump")) {
    node_dump(in, in.current);
    return kKnown;
  }
  if (is(argv0, "NodeSetActive", 0xd)) {  // jumps into Object3DSetActive
    const int on = a.b();
    call(e.set_active, in.current, on);
    return kKnown;
  }
  if (is(argv0, "NodeSetDescription", 0x12)) {
    char* name = a.s();
    call(e.set_name, in.current, static_cast<const char*>(name));
    return kKnown;
  }
  if (is(argv0, "NodeSetCanModify", 0x10)) {
    const int on = a.b();
    call(e.set_can_modify, in.current, on);
    return kKnown;
  }
  if (is(argv0, "NodeSetLighting", 0xf)) {
    const int on = a.b();
    call(e.set_lighting, in.current, on);
    return kKnown;
  }
  if (is(argv0, "NodeSetOverwrite", 0x10)) {
    const int on = a.b();
    call(e.set_overwrite, in.current, on);
    return kKnown;
  }
  return kMiss;
}

// The node's model into 0x00730c1c and on into one of the model globals.
Model* model_of(Interp& in) {
  Globals& g = globals();
  call(engine().get_model, in.current, &g.model_out);
  return g.model_out;
}

Result letter_o(Interp& in, const char* argv0) {
  Engine& e = engine();
  Globals& g = globals();
  Args a{in};
  if (is(argv0, "Object3DAddChild", 0x10)) {
    char* name = a.s();
    Node* child = call(e.find_node, 6, static_cast<const char*>(name));
    if (in.current && child) call(e.object3d_add_child, in.current, child);
    else report(0x46f, "interp: Object3DAddChild (%s, %s) --> NULL NODE", in.current ? in.current->name() : "NULL", name);
    return kKnown;
  }
  if (exact(argv0, "Object3DGetTranslate")) {
    if (!in.current) return kKnown;
    float x = 0, y = 0, z = 0;
    call(e.object3d_get_translate, in.current, &x, &y, &z);
    log(in, "%s --> ( %.2f %.2f %.2f )", in.current->name(), x, y, z);
    return kKnown;
  }
  if (is(argv0, "Object3DRegisterTexturesToWorld", 0x1f)) {
    const bool on = a.b();
    g.cycle_model = model_of(in);
    // FIX (bug): the exe writes the flags without checking for a model.
    if (g.cycle_model) {
      uint32_t flags;
      std::memcpy(&flags, g.cycle_model->header + 8, 4);
      flags = on ? flags | 4 : flags & ~4u;
      std::memcpy(g.cycle_model->header + 8, &flags, 4);
    }
    return kKnown;
  }
  if (is(argv0, "Object3DRotate", 0xe)) {
    const float x = a.f(), y = a.f();  // the last angle stays a double
    const double z = a.d();
    const float zr = rad(z), yr = rad(y), xr = rad(x);
    call(e.object3d_rotate, in.current, xr, yr, zr);
    return kKnown;
  }
  if (is(argv0, "Object3DScale", 0xd)) {
    const float x = a.f(), y = a.f(), z = a.f();
    call(e.object3d_scale, in.current, x, y, z);
    return kKnown;
  }
  if (is(argv0, "Object3DSetActionPriority", 0x19)) {
    const int32_t p = a.i();
    call(e.set_action_priority, in.current, p);
    return kKnown;
  }
  if (is(argv0, "Object3DSetActive", 0x11)) {
    const int on = a.b();
    call(e.set_active, in.current, on);
    return kKnown;
  }
  if (is(argv0, "Object3DSetColor", 0x10)) {
    const int32_t c = a.i();
    g.object_model = model_of(in);
    call(e.model_set_color, g.object_model, c);
    return kKnown;
  }
  if (is(argv0, "Object3DSetFacade", 0x11)) {
    g.cycle_model = model_of(in);
    call(e.model_set_facade, g.cycle_model, 1);
    return kKnown;
  }
  if (is(argv0, "Object3DSetOpacityIsSet", 0x17)) {
    const int32_t v = a.i();
    call(e.object3d_set_opacity_is_set, in.current, v);
    return kKnown;
  }
  if (is(argv0, "Object3DSetOpacity", 0x12)) {
    const float v = a.f();
    call(e.object3d_set_opacity, in.current, v);
    return kKnown;
  }
  if (is(argv0, "Object3DSetPoints", 0x11)) {
    g.cycle_model = model_of(in);
    call(e.model_set_facade, g.cycle_model, 2);
    return kKnown;
  }
  if (is(argv0, "Object3DSetPriority", 0x13)) {
    g.object_model = model_of(in);
    const int32_t p = a.i();
    call(e.model_set_priority, g.object_model, p);
    return kKnown;
  }
  if (is(argv0, "Object3DSetScrollAlways", 0x17)) {
    const bool on = a.b();
    const float u = a.f(), v = a.f();
    if (!on) scroll_off(in.current);
    else if (!scroll_on(in.current, u, v, true))
      report(0x4de, "Object3DSetScrollAlways on: FAILED  (node=0x%08x) (gfx=0x%08x)",
             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(in.current)),
             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g.cycle_model)));
    return kKnown;
  }
  if (is(argv0, "Object3DSetScroll", 0x11)) {
    const bool on = a.b();
    const float u = a.f(), v = a.f();
    if (!in.current)
      error(in, "%s %s %.1f %.1f Failed: current_node is NULL", argv0, on ? "ON" : "OFF", static_cast<double>(u),
            static_cast<double>(v));
    else if (on) scroll_on(in.current, u, v, false);
    else scroll_off(in.current);
    return kKnown;
  }
  if (is(argv0, "Object3DSetShowBackFace", 0x17)) {
    g.object_model = model_of(in);
    const int on = a.b();
    call(e.model_set_show_backface, g.object_model, on);
    return kKnown;
  }
  if (is(argv0, "Object3DSetTextureWorldBaseCoordinates", 0x26)) {
    const float u = a.f(), v = a.f();
    call(e.texture_world_base, u, v);
    return kKnown;
  }
  if (is(argv0, "Object3DSetTextureWorldTexturesPerMeter", 0x25)) {
    const float u = a.f(), v = a.f();
    call(e.texture_world_per_meter, u, v);
    return kKnown;
  }
  if (is(argv0, "Object3DSetMorphVertex", 0x16)) {
    const float v = a.f();
    g.cycle_model = model_of(in);
    float local[3] = {};  // the exe passes an uninitialised stack slot
    call(e.model_set_morph, g.cycle_model, static_cast<void*>(local), v, 0, 6);
    // FIX (bug): the exe writes 1.0 at +0x24 without checking for a model.
    if (g.cycle_model) {
      const float one = 1.0f;
      std::memcpy(g.cycle_model->header + 0x24, &one, 4);
    }
    return kKnown;
  }
  if (is(argv0, "Object3DTranslate", 0x11)) {
    const float x = a.f(), y = a.f(), z = a.f();
    call(e.object3d_translate, in.current, x, y, z);
    return kKnown;
  }
  return kMiss;
}

Result letter_p(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "PerspectiveTexture", 0x12)) {
    // 0x0074a3cc, which every model draw overwrites and nothing reads.
    zmod::draw_state().default_model = a.b();
    return kKnown;
  }
  if (is(argv0, "PrintNodeCount", 0xe)) {
    char* name = a.s();
    call(e.walk_prefix, static_cast<const char*>(name), 6);
    int32_t n = 0;
    while (call(e.walk_prefix, static_cast<const char*>(nullptr), 0)) ++n;
    print("Node count for %s = %d\n", name, n);
    return kKnown;
  }
  if (exact(argv0, "PrintTree")) {
    if (!in.current) error(in, "No current node");
    print_tree(in, in.current, 2);
    return kKnown;
  }
  if (is(argv0, "PrintUsedNodes", 0xe)) {
    call(e.list_print, 6);
    return kKnown;
  }
  return kMiss;
}

Result letter_r(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "RdrAddPath", 0xa)) {
    call(e.rdr_add_path, static_cast<const char*>(a.s()));
    return kKnown;
  }
  if (is(argv0, "RdrSetPath", 0xa)) {
    call(e.rdr_set_path, static_cast<const char*>(a.s()));
    return kKnown;
  }
  return kMiss;
}

// Anything starting "SEQ" is taken here, known or not.
Result letter_seq(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "SEQAddChild", 0xb)) {
    char* name = a.s();
    const int32_t frame = a.i();
    const float t = a.f();
    Node* child = call(e.find_node, 6, static_cast<const char*>(name));
    call(e.seq_add_child, in.current, child, frame, t);
  } else if (is(argv0, "SEQNew", 6)) {
    new_node(in, call(e.seq_create));
  } else if (is(argv0, "SEQSetActive", 0xc)) {
    const int32_t v = a.i();
    call(e.seq_set_active, in.current, v);
  } else if (is(argv0, "SEQSetLoop", 0xa)) {
    const int32_t v = a.i();
    call(e.seq_set_loop, in.current, v);
  } else if (is(argv0, "SEQSetPause", 0xc)) {
    const int32_t v = a.i();
    call(e.seq_set_pause, in.current, v);
  } else if (is(argv0, "SEQSetRepeat", 0xc)) {
    const int32_t v = a.i();
    call(e.seq_set_repeat, in.current, v);
  }
  return kKnown;
}

Result node_bool(Interp& in, int (*fn)(Node*, int)) {
  const int on = arg_bool(in, false) ? 1 : 0;
  call(fn, in.current, on);
  return kKnown;
}

Result letter_s(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "SEQ", 3)) return letter_seq(in, argv0);
  if (is(argv0, "SetAltitudeSurface", 0x12)) return node_bool(in, e.set_altitude_surface);
  if (is(argv0, "SetBFETolerance", 0xf)) {
    if (check_args(in, 1, 0, nullptr)) call(e.bfe_tolerance, a.f());
    return kKnown;
  }
  if (is(argv0, "SetCoplanarTolerance", 0x14)) {
    call(e.coplanar_tolerance, a.f());
    return kKnown;
  }
  if (is(argv0, "SetColinearTolerance", 0x14)) {
    call(e.colinear_tolerance, a.f());
    return kKnown;
  }
  if (is(argv0, "SetGameZNodeArraySize", 0x15)) {
    a.i();  // to 0x005ac5c0, an empty function
    return kKnown;
  }
  if (is(argv0, "SetMaterialArraySize", 0x14)) {
    call(e.material_array_size, a.i());
    return kKnown;
  }
  if (is(argv0, "SetModel3DArraySize", 0x13)) {
    call(e.model3d_array_size, a.i());
    return kKnown;
  }
  if (is(argv0, "SetIntersectBBOX", 0x10)) return node_bool(in, e.set_intersect_bbox);
  if (is(argv0, "SetIntersectSurface", 0x13)) return node_bool(in, e.set_intersect_surface);
  if (is(argv0, "SetLandmark", 0xb)) return node_bool(in, e.set_landmark);
  if (is(argv0, "SetPaletteName", 0xe)) {
    call(e.palette_name, static_cast<const char*>(a.s()));
    return kKnown;
  }
  if (is(argv0, "SetPaletteShading", 0x11)) {
    call(e.palette_shading, a.b());
    return kKnown;
  }
  if (is(argv0, "SetPerspectiveAdaptiveCorrection", 0x20)) {
    const int32_t x = a.i(), y = a.i();
    const float z = a.f();
    call(e.perspective_adaptive, x, y, z);
    return kKnown;
  }
  if (is(argv0, "SetPerspectiveTextureDeltaX", 0x1b)) {
    call(e.perspective_delta_x, a.i());
    return kKnown;
  }
  if (is(argv0, "SetInverseZTolerance", 0x14)) {
    call(e.inverse_z_tolerance, a.f());
    return kKnown;
  }
  if (is(argv0, "SetPerspectiveInverseZTolerance", 0x14)) {
    call(e.perspective_inverse_z, a.f());
    return kKnown;
  }
  if (is(argv0, "SetPerspectiveTextureFarZ", 0x19)) {
    call(e.perspective_far_z, a.f());
    return kKnown;
  }
  if (is(argv0, "SetProximity", 0xc)) return node_bool(in, e.set_proximity);
  if (is(argv0, "SetSmallPolygonRejectArea", 0x19)) {
    call(e.small_polygon_reject, a.f());
    return kKnown;
  }
  if (is(argv0, "SetTextureDirectory", 0x13)) {
    call(e.texture_directory, static_cast<const char*>(a.s()));
    return kKnown;
  }
  if (is(argv0, "SetVertexShading", 0x10)) {
    call(e.vertex_shading, a.b());
    return kKnown;
  }
  return kMiss;
}

Result letter_v(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  if (is(argv0, "Verbose", 7)) {
    globals().verbose = a.b();
    return kKnown;
  }
  if (argv0_is(in, "VideoSetDither")) {
    call(e.video_dither, a.b());
    return kKnown;
  }
  if (argv0_is(in, "VideoSetWireFrame")) {
    call(e.video_wireframe, a.b());
    return kKnown;
  }
  return kMiss;
}

Result letter_w(Interp& in, const char* argv0) {
  Engine& e = engine();
  Args a{in};
  // The commands that check their node and count first (0x005962d0) end
  // quietly, as known, when the check fails.
  if (is(argv0, "WindowAddClearPolygonVertex", 0x1b)) {
    if (!check_args(in, 3, 3, in.current)) return kKnown;
    float v[3];
    v[0] = a.f(), v[1] = a.f(), v[2] = a.f();
    call(e.window_add_clear_vertex, in.current, static_cast<const float*>(v));
    return kKnown;
  }
  if (is(argv0, "WindowBuffer", 0xf)) {
    if (!check_args(in, 1, 3, in.current)) return kKnown;
    const int32_t v = a.i();
    call(e.window_buffer, in.current, v);
    return kKnown;
  }
  if (is(argv0, "WindowCloseClearPolygon", 0x17)) {
    call(e.window_close_clear_polygon, in.current);
    return kKnown;
  }
  if (is(argv0, "WindowOrigin", 0xc)) {
    const int32_t x = a.i(), y = a.i();
    call(e.window_origin, in.current, x, y);
    return kKnown;
  }
  if (is(argv0, "WindowResolution", 0x10)) {
    const int32_t w = a.i(), h = a.i();
    call(e.window_resolution, in.current, w, h);
    return kKnown;
  }
  if (is(argv0, "WindowSetClearPolygon", 0x15)) {
    const int on = a.b();
    call(e.window_set_clear_polygon, in.current, on);
    return kKnown;
  }
  if (is(argv0, "WorldAddLight", 0xd)) {
    Node* light = call(e.find_node, 9, static_cast<const char*>(a.s()));
    call(e.world_add_light, in.current, light);
    return kKnown;
  }
  if (is(argv0, "WorldExtents", 0xc)) {
    const float x = a.f(), z = a.f();
    call(e.world_extents, in.current, x, z);
    return kKnown;
  }
  if (is(argv0, "WorldOrigin", 0xb)) {
    const float x = a.f(), z = a.f();
    call(e.world_origin, in.current, x, z);
    return kKnown;
  }
  if (is(argv0, "WorldPartitionInclusionTolerance", 0x20)) {
    const float x = a.f(), z = a.f();
    call(e.world_partition_tolerance, in.current, x, z);
    return kKnown;
  }
  if (is(argv0, "WorldPartitionMaxDECFeatureCount", 0x20)) {
    const int32_t n = a.i();
    call(e.world_partition_max_dec, in.current, n);
    return kKnown;
  }
  if (is(argv0, "WorldPartition", 0xe)) {
    const float x = a.f(), z = a.f();
    call(e.world_partition, in.current, x, z);
    return kKnown;
  }
  if (is(argv0, "WorldSetFogAltitude", 0x13)) {
    if (!check_args(in, 2, 2, in.current)) return kKnown;
    const float lo = a.f(), hi = a.f();
    call(e.world_set_fog_altitude, in.current, lo, hi);
    return kKnown;
  }
  if (is(argv0, "WorldSetFogColor", 0x10)) {
    const float r = a.f(), g = a.f(), b = a.f();
    call(e.world_set_fog_color, in.current, r, g, b);
    return kKnown;
  }
  if (argv0_is(in, "WorldGetFogColor")) {
    if (!check_args(in, 0, 2, in.current)) return kKnown;
    float r = 0, g = 0, b = 0;
    call(e.world_get_fog_color, in.current, &r, &g, &b);
    log(in, "Fog Color: [%s] [ %.2f, %.2f %.2f ]", in.current->name(), r, g, b);
    return kKnown;
  }
  if (is(argv0, "WorldSetFogDensity", 0x12)) {
    const float v = a.f();
    call(e.world_set_fog_density, in.current, v);
    return kKnown;
  }
  if (argv0_is(in, "WorldSetFogRange")) {
    if (!check_args(in, 2, 2, in.current)) return kKnown;
    const float n = a.f(), f = a.f();
    call(e.world_set_fog_range, in.current, n, f);
    return kKnown;
  }
  if (argv0_is(in, "WorldSetFogRangeNear")) {
    if (!check_args(in, 1, 2, in.current)) return kKnown;
    float n = 0, f = 0;
    call(e.world_get_fog_range, in.current, &n, &f);
    n = a.f();
    call(e.world_set_fog_range, in.current, n, f);
    return kKnown;
  }
  if (argv0_is(in, "WorldSetFogRangeFar")) {
    if (!check_args(in, 1, 2, in.current)) return kKnown;
    float n = 0, f = 0;
    call(e.world_get_fog_range, in.current, &n, &f);
    f = a.f();
    call(e.world_set_fog_range, in.current, n, f);
    return kKnown;
  }
  if (argv0_is(in, "WorldGetFogRange")) {
    if (!check_args(in, 0, 2, in.current)) return kKnown;
    float n = 0, f = 0;
    call(e.world_get_fog_range, in.current, &n, &f);
    log(in, "Fog Range: [%s] [ %.2f, %.2f ]", in.current->name(), n, f);
    return kKnown;
  }
  if (is(argv0, "WorldSetFogState", 0x10)) {
    if (!check_args(in, 1, 2, in.current)) return kKnown;
    const char* s = a.s();
    if (std::strncmp(s, "linear", 6) == 0) call(e.world_set_fog_type, in.current, 1u);
    else if (std::strncmp(s, "exponential", 0xb) == 0) call(e.world_set_fog_type, in.current, 2u);
    else if (std::strncmp(s, "off", 3) == 0) call(e.world_set_fog_type, in.current, 0u);
    else print("Did not understand: %s\n", s);
    return kKnown;
  }
  if (is(argv0, "WorldSetVirtualPartitionMapSize", 0x1f)) {
    const int32_t w = a.i(), h = a.i();
    call(e.world_set_virtual_map_size, in.current, w, h);
    return kKnown;
  }
  if (is(argv0, "WorldSetVirtualPartition", 0x18)) {
    const int on = a.b();
    call(e.world_set_virtual_partition, in.current, on);
    return kKnown;
  }
  if (is(argv0, "WriteTextureSetType", 0x13)) {
    call(e.write_texture_type, a.i());
    return kKnown;
  }
  if (is(argv0, "WriteTextureSetMap", 0x12)) {
    zimg::TextureEntry* t = call(e.texture_add, static_cast<const char*>(a.s()));
    call(e.write_texture_map, t);
    return kKnown;
  }
  return kMiss;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00590d80. `ifdef A || B && C`: strictly left to right,
// no precedence; any other word after a name ends the chain. Names are not
// macro-expanded.
bool condition(Interp& in) {
  if (in.argc == 1) return false;
  if (in.argc == 2) return macro_defined(in, in.argv[1]);
  int op = 0;
  bool v = false;
  for (uint32_t i = 1; i < in.argc;) {
    const char* name = i < in.argc ? in.argv[i] : nullptr;
    ++i;
    if (op == 0) v = macro_defined(in, name);
    else if (op == 1) v = v | macro_defined(in, name);
    else if (op == 2) v = v & macro_defined(in, name);
    if (i < in.argc) {
      const char* word = in.argv[i];
      ++i;
      if (std::strncmp(word, "||", 2) == 0) op = 1;
      else if (std::strncmp(word, "&&", 2) == 0) op = 2;
      else return v;
    }
  }
  return v;
}

// Mech3.exe 1.2 @ 0x00590e80. False when the line was its own (or skipped).
// While a block is skipped only `endif` is looked at, so an ifdef nested in
// a skipped block is not counted and its endif closes the outer one.
bool preprocess(Interp& in, const char* argv0) {
  if (std::strncmp(argv0, "endif", 5) == 0) {
    if (in.skip_depth != 0) --in.skip_depth;
    return false;
  }
  if (in.skip_depth != 0) return false;
  if (std::strncmp(argv0, "ifdef", 5) == 0) {
    if (!condition(in)) ++in.skip_depth;
    return false;
  }
  if (std::strncmp(argv0, "ifndef", 6) == 0) {
    if (condition(in)) ++in.skip_depth;
    return false;
  }
  if (std::strncmp(argv0, "mkdir", 5) == 0) {
    const char* path = next_arg(in);
    const int err = call(engine().make_dir, path);
    if (err != 0 && err != EEXIST) error(in, "%s %s FAILED (errno == %d)", argv0, path, err);
    return false;
  }
  if (std::strcmp(argv0, "Quit") == 0) {
    in.quit = 1;
    return false;
  }
  if (std::strncmp(argv0, "set", 3) == 0) {
    const char* name = next_arg(in);
    const char* value = next_arg(in);
    macro_set(in, name, value);
    return false;
  }
  if (std::strncmp(argv0, "source", 6) == 0) {
    if (const char* arg = next_arg(in)) {
      char* name = static_cast<char*>(std::malloc(std::strlen(arg) + 1));
      std::strcpy(name, arg);
      // The exe frees the tokens here (see exec_line's FIX).
      in.pending_free.push_back(in.line);
      in.line = nullptr;
      source(in, name);
      std::free(name);
    }
    in.quit = 0;  // a Quit in the sourced file ends that file only
    return false;
  }
  if (std::strncmp(argv0, "who", 3) == 0) {
    log(in, "%d Macros", static_cast<int>(in.macros.size()));
    for (const Macro& m : in.macros) log(in, "%12s: %s", m.name.c_str(), m.value.c_str());
    log(in, "%d Variables", static_cast<int>(in.vars.size()));
    for (const Variable& v : in.vars) var_print(in, &v);
    return false;
  }
  if (std::strncmp(argv0, "var", 3) != 0) return true;
  const char* name = next_arg(in);
  Variable* v = var_find(in, name ? name : "");
  if (!v) {
    log(in, "Can't find variable ( %s )", name);
    return false;
  }
  // FIX (bug): with no operator the exe's strncmp reads null.
  const char* op = next_arg(in);
  if (!op || (std::strncmp(op, "set", 3) != 0 && std::strncmp(op, "=", 1) != 0)) {
    miss(in);
    return false;
  }
  if (v->type == 0) {
    *static_cast<int32_t*>(v->value) = arg_int(in, 0);
  } else if (v->type == 1) {
    *static_cast<float*>(v->value) = static_cast<float>(arg_float(in, 0.0f));
  } else if (v->type == 2) {
    log(in, "Can't modify string variables (yet!)");
  }
  var_print(in, v);
  return false;
}

// Mech3.exe 1.2 @ 0x00591290
void print_tree(Interp& in, Node* node, int indent) {
  if (!node) return;
  log(in, "%*s%s", indent, " ", node->name());
  for (int32_t i = 0; i < node->child_count(); ++i) print_tree(in, node->children[i], indent + 2);
}

// Mech3.exe 1.2 @ 0x00591300. A switch on the first letter, then the names
// in the exe's order (docs/interp.md). Always true; an unknown word counts a
// miss, except under 'I' (no commands) and the SEQ table.
bool dispatch(Interp& in, const char* argv0) {
  Result r = kMiss;
  switch (argv0[0]) {
    case 'A': r = letter_a(in, argv0); break;
    case 'C': r = letter_c(in, argv0); break;
    case 'D': r = letter_d(in, argv0); break;
    case 'E': r = is(argv0, "Echo", 4) ? (echo(in), kKnown) : kMiss; break;
    case 'e': r = is(argv0, "echo", 4) ? (echo(in), kKnown) : kMiss; break;
    case 'F': r = letter_f(in, argv0); break;
    case 'G': r = letter_g(in, argv0); break;
    case 'I': r = kKnown; break;
    case 'L': r = letter_l(in, argv0); break;
    case 'M': case 'm': r = letter_m(in, argv0); break;
    case 'N': r = letter_n(in, argv0); break;
    case 'O': r = letter_o(in, argv0); break;
    case 'P': r = letter_p(in, argv0); break;
    case 'R': r = letter_r(in, argv0); break;
    case 'S': r = letter_s(in, argv0); break;
    case 'T':
      r = is(argv0, "TextureAdd", 0xa) ? (call(engine().texture_add, static_cast<const char*>(next_arg(in))), kKnown)
                                       : kMiss;
      break;
    case 'V': r = letter_v(in, argv0); break;
    case 'W': r = letter_w(in, argv0); break;
    default: break;
  }
  if (r == kMiss) miss(in);
  return true;
}

}  // namespace zint
