#pragma once
#include <cstdint>

// The matrix stack, the camera's view and projection, and the point transforms
// the scene walk and the model draw use (docs/drafts/matrix.md, docs/render.md
// sections 0-3). Original file name unknown: an unnamed module at
// 0x51b170..0x526920, between zinput\zin_init.cpp and zmodel\gmod_init.c, likely
// the engine's maths library; named zmth_* after the zXxx convention.
namespace zmth {

// 3x4, row vectors: m[0..2], m[3..5], m[6..8] are the local x, y, z axes in
// the parent and m[9..11] the translation, so p' = p * M.
struct Mat34 {
  float m[12];
};
struct Vec3 {
  float x, y, z;
};
static_assert(sizeof(Mat34) == 48 && sizeof(Vec3) == 12, "the exe's layouts");

// The stack holds callers' buffers, not copies. Slot 0 starts empty (the base
// is a null pointer in the exe): the first use must push a buffer.
constexpr int kStackDepth = 32;  // 0x0063d760..0x0063d7e0; no bound check in the original
struct MatrixStack {
  Mat34* slot[kStackDepth] = {};  // 0x0063d760
  int32_t identity[kStackDepth] = {};  // 0x0063d7e8  1: the slot holds the identity
  int top = 0;                    // 0x005eda90 -> slot[top], 0x005eda8c -> identity[top]
  // FIX (bug), ours (W59): pushes refused past the last slot, counted so their
  // pops are too; an empty slot gives a scratch matrix. docs/fixes.md.
  int overflow = 0;
  const void* pushed_by[kStackDepth] = {};  // each slot's push's return address, for the reports
  Mat34& matrix() { return slot[top] ? *slot[top] : empty_slot(top); }
  int32_t& is_identity() { return identity[top]; }
  // The slot under the top (the parent's matrix).
  Mat34& below() { return top > 0 && slot[top - 1] ? *slot[top - 1] : empty_slot(top - 1); }
  int32_t below_identity() const { return top > 0 ? identity[top - 1] : 1; }
  static Mat34& empty_slot(int at);
};
MatrixStack& matrix_stack();

// The camera, set once per frame (docs/render.md, camera matrices).
struct View {
  Mat34 view;    // 0x0073fbc0  world -> camera (x right, y down, z forward)
  Mat34 camera;  // 0x0073fc00  camera -> world, its y and z rows negated
};
View& view();

// Projection constants, set by set_projection 0x521c60.
struct Projection {
  float origin_x = 0, origin_y = 0;    // 0x0063d338 / 0x0063d33c  window left, top
  float half_w = 0, half_h = 0;        // 0x0063d748 / 0x0063d74c
  float cot_h = 0, cot_v = 0;          // 0x0063d750 / 0x0063d754  cot(fov / 2)
  float inv_cot_h = 0, inv_cot_v = 0;  // 0x0063d740 / 0x0063d744
  float inv_fx = 0, inv_fy = 0;        // 0x0063d758 / 0x0063d75c
  float fov_h = 0, fov_v = 0;          // 0x0063d7e0 / 0x0063d7e4  set_fov 0x521c10
  float near_z = 0;                    // 0x0073fb80
  float cx = 0, cy = 0;                // 0x0073fb88 / 0x0073fb8c  screen centre
  float inv_near = 0;                  // 0x0073fb90  1 / near on Direct3D, else 1
  float far_z = 0;                     // 0x0073fb94
  float fy_near = 0;                   // 0x0073fb98  fy * inv_near
  float fx = 0, fy = 0;                // 0x0073fb9c / 0x0073fba0  pixels per unit at z = 1
  float fx_near = 0;                   // 0x0073fc30  fx * inv_near
};
Projection& projection();

// --- The stack ---
void reset_stack();                      // 0x0051e8b0 (unreferenced; the exe's data starts so)
void push(Mat34* buffer);                // 0x0051e9d0  contents untouched, not identity
void push_copy(Mat34* buffer);           // 0x0051e930  copies the top and its flag
void pop();                              // 0x0051ea00
// Ours (W59): the stack back to empty at a frame's world draw, logged once
// when something left it unbalanced. FIX (bug), docs/fixes.md.
void check_stack_at_frame();
// Ours (W59, for the tests): the times each guard fired.
struct StackFaults { int over_push = 0, under_pop = 0, empty = 0, frame = 0; };
StackFaults& stack_faults();
void load(const Mat34& m);               // 0x0051fd40
void load_rotation(const Mat34& m);      // 0x0051fd70  3x3 only, translation kept
void load_identity();                    // 0x0051fde0
void load_view();                        // 0x0051ea30  top = view().view
void load_camera();                      // 0x0051ea60  top = view().camera
void load_model_view();                  // 0x0051fb30  top = below * view
void multiply(const Mat34& local, int mode);  // 0x0051fee0  top = local * top; mode 2 rotation only
void scale(float x, float y, float z);   // 0x00520330  top = S * top
void translate(float x, float y, float z);  // 0x00520480  top = T * top
void rotate_x(float a);                  // 0x00520610  top = Rx * top
void rotate_y(float a);                  // 0x005207b0
void rotate_z(float a);                  // 0x00520960
void euler(const float rot[3], const float pos[3], const float scale[3]);  // 0x00521670

// --- Matrices ---
void matrix_to_euler(const Mat34& m, float out[3]);  // 0x00523bc0
// At 53 bits, the reference runtime's precision (test_matrix.py): for callers
// on the full-precision path (render-matrix.md).
bool invert(const Mat34& m, Mat34& out, bool translation);  // 0x00523c50, false if singular
// The same at 24 bits (every operation rounded to a float), for play code
// checked in the emulator at Direct3D's setting: the shot queries' model test.
bool invert_single(const Mat34& m, Mat34& out, bool translation);  // 0x00523c50
void euler_to_quat(float x, float y, float z, float q[4]);  // 0x00525340  q = (w, x, y, z)
void quat_matrix(const float q[4], float r[9]);           // 0x00525740  unit quaternion to 3x3
void quat_two_angles(float a, float b, float q[4]);       // 0x00525280  euler_to_quat(b, a, 0)'s way
void quat_angles(const float q[4], float out[3]);         // 0x00525f80  through 0x00525740 and 0x00523bc0
void quat_exp(const float v[3], float q[4]);               // 0x00525900  axis-angle vector to (w, x, y, z)
void quat_mul(const float a[4], const float b[4], float out[4]);  // 0x005255d0  a * b
// ECX a, EDX b, stack out: (pitch, yaw, 0) of b seen from a (atan2 of the
// differences; y counts up).
void direction_angles(const float a[3], const float b[3], float out[3]);  // 0x00523ac0
float direction_pitch(const float a[3], const float b[3]);                // 0x00523b40

// e^-x from a 256-entry table in steps of 1/51 (x truncated to a step): 0 above
// 5, 1 below 0 or NaN. Built on the first call (docs/puffers.md, drag).
float exp_neg(float x);                                   // 0x00523f70
// Ours: exp_neg(rate * dt) for the frame time dt, as a 30 fps frame's per
// second (FIX (B) behind per_second; the table without it).
float exp_neg_dt(float x, float dt);

// --- Camera and projection ---
void set_view(const Mat34& camera_world);  // 0x00520b00
void set_fov(float h, float v);           // 0x00521c10
void set_projection(float x, float y, float half_w, float half_h, float cot_h, float cot_v,
                    float near_z, float far_z);  // 0x00521c60
void focal(float out[2]);                 // 0x0051e8d0  (fx, fy)
void scale_by_cot(Vec3* v, uint32_t count);    // 0x00521db0  x *= cot_h, y *= cot_v
void scale_by_tan(Vec3* v, uint32_t count);    // 0x00521de0  x /= cot_h, y /= cot_v
void project(const Vec3* cam, Vec3* screen, uint32_t count);      // 0x005234f0  z' = 1/z
void project_d3d(const Vec3* cam, Vec3* screen, uint32_t count);  // 0x00523540  z' = near/z
void unproject_d3d(const Vec3* screen, Vec3* cam, int32_t count); // 0x00523590
void unproject(const Vec3* screen, Vec3* cam, int32_t count);     // 0x005235f0
void unproject_world(const Vec3* screen, Vec3* out, int32_t count);  // 0x00523930: then by view().camera

// --- Points through the top of the stack ---
void morph(Vec3* dst, const Vec3* base, const Vec3* delta, uint32_t count, float f);  // 0x005220e0
void rotate_points(Vec3* v, uint32_t count);                  // 0x005225b0  in place, 3x3
void rotate_points_transposed(Vec3* v, int32_t count);        // 0x00522640  in place, by the inverse 3x3
void rotate_points_to(const Vec3* src, Vec3* dst, uint32_t count);  // 0x005226e0  normals
void transform_points(Vec3* v, uint32_t count);               // 0x005227a0  in place
// ECX matrix, EDX box (min xyz, max xyz), stack out: the box through m,
// axis aligned again (each term takes min or max by the sign of m's entry).
void transform_box(const float* m, const float box[6], float out[6]);  // 0x005231b0
// The same in x and z only: out[1] and out[4] are left as they were.
void transform_box_xz(const float* m, const float box[6], float out[6]);  // 0x00523040
void inverse_transform_points_to(const Vec3* src, Vec3* dst, int32_t count);  // 0x00522910
void transform_points_to(const Vec3* src, Vec3* dst, uint32_t count);  // inlined in 0x0053f2e0
// ECX m (12 floats), EDX box (min x, y, z, max x, y, z), stack out: the
// eight corners through m, in the order of zcls::node_box_corners.
void box_corners(const float* m, const float box[6], float out[8][3]);  // 0x00522ab0
// The (x, z) of a box's (min xyz, max xyz) eight corners through m, in the
// order of zcls::node_box_xz; reads m's x and z columns only.
void box_corners_xz(const float* m, const float box[6], float out[8][2]);  // 0x00522d60

}  // namespace zmth
