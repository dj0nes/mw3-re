#include "gamez/zmath/zmth_matrix.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long, unsigned long, void**,
                                                                                   unsigned long*);
extern "C" const char __ImageBase;  // the linker's, the image's start
#else
#include <dlfcn.h>
#include <execinfo.h>
#endif

#include "gamez/zrender/zrndr_fog.h"
#include "gamez/zutil/zfix.h"

namespace zmth {

namespace {
MatrixStack g_stack;
View g_view;
Projection g_proj;

// r = l * t for 3x4 row-vector matrices (l applied first). The sums in the x87 code's order (0x0051fee0, 0x0051fb30 and 0x00521670
// share it): the z column adds its terms from the last, and the z
// translation adds the first term to t[11] before the others.
void mul34(const float* l, const float* t, float* r, bool translation) {
  for (int row = 0; row < 3; ++row) {
    const float* a = l + 3 * row;
    r[3 * row] = a[0] * t[0] + a[1] * t[3] + a[2] * t[6];
    r[3 * row + 1] = a[0] * t[1] + a[1] * t[4] + a[2] * t[7];
    r[3 * row + 2] = (a[2] * t[8] + a[1] * t[5]) + a[0] * t[2];
  }
  if (!translation) return;
  r[9] = l[9] * t[0] + l[10] * t[3] + l[11] * t[6] + t[9];
  r[10] = l[9] * t[1] + l[10] * t[4] + l[11] * t[7] + t[10];
  r[11] = (l[11] * t[8] + l[10] * t[5]) + (l[9] * t[2] + t[11]);
}

// x87 compares: an unordered (NaN) result sets C3 like "equal", so the
// original's "!= c" branches skip NaN too.
bool differs(double x, double c) { return x < c || x > c; }
}  // namespace

MatrixStack& matrix_stack() { return g_stack; }
View& view() { return g_view; }
Projection& projection() { return g_proj; }

namespace {
StackFaults g_faults;
Mat34 g_scratch;

// The callers, as image offsets a linker map finds (Owen's v1.0 crash at
// load+0x1d, W59): a few frames, once per kind of fault.
void print_address(const void* a) {
#if defined(_WIN32)
  std::fprintf(stderr, " mw3.exe+%#llx", static_cast<unsigned long long>(static_cast<const char*>(a) - &__ImageBase));
#else
  Dl_info d{};
  if (dladdr(a, &d) && d.dli_fbase)
    std::fprintf(stderr, " %s+%#lx", d.dli_sname ? d.dli_sname : "?",
                 static_cast<unsigned long>(static_cast<const char*>(a) -
                                            static_cast<const char*>(d.dli_saddr ? d.dli_saddr : d.dli_fbase)));
  else std::fprintf(stderr, " %p", a);
#endif
}

void report(const char* what, int at) {
  std::fprintf(stderr, "mw3: matrix stack: %s (top %d) (logged once; docs/fixes.md, W59), from:", what, at);
  void* frames[6] = {};
#if defined(_WIN32)
  const int n = RtlCaptureStackBackTrace(1, 6, frames, nullptr);
  for (int i = 0; i < n; ++i) print_address(frames[i]);
#else
  const int n = backtrace(frames, 6);
  for (int i = 1; i < n; ++i) print_address(frames[i]);
#endif
  std::fprintf(stderr, "; pushed by:");
  for (int i = 1; i <= g_stack.top && i < kStackDepth; ++i) print_address(g_stack.pushed_by[i]);
  std::fprintf(stderr, "\n");
}
}  // namespace

StackFaults& stack_faults() { return g_faults; }

Mat34& MatrixStack::empty_slot(int at) {
  if (g_faults.empty++ == 0) report("a matrix asked of an empty slot", at);
  return g_scratch;
}

// Mech3.exe 1.2 @ 0x0051e8b0
void reset_stack() { g_stack.top = 0, g_stack.overflow = 0; }

void check_stack_at_frame() {
  if (g_stack.top == 0 && g_stack.overflow == 0) return;
  if (g_faults.frame++ == 0) report("left unbalanced by the last frame", g_stack.top + g_stack.overflow);
  reset_stack();
}

// The bounds below are FIX (bug), W59: the exe's run past its 32 slots (into
// the FOV at 0x0063d7e0) or under them (reading 0x0063d75c as a slot).
// Mech3.exe 1.2 @ 0x0051e9d0
void push(Mat34* buffer) {
  if (g_stack.overflow || g_stack.top + 1 >= kStackDepth) {
    if (g_faults.over_push++ == 0) report("a push past the last slot, refused", g_stack.top);
    ++g_stack.overflow;
    return;
  }
  ++g_stack.top;
  g_stack.slot[g_stack.top] = buffer;
  g_stack.pushed_by[g_stack.top] = __builtin_return_address(0);
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x0051e930
void push_copy(Mat34* buffer) {
  if (g_stack.overflow || g_stack.top + 1 >= kStackDepth) {
    if (g_faults.over_push++ == 0) report("a push past the last slot, refused", g_stack.top);
    ++g_stack.overflow;
    return;
  }
  ++g_stack.top;
  g_stack.identity[g_stack.top] = g_stack.identity[g_stack.top - 1];
  g_stack.slot[g_stack.top] = buffer;
  g_stack.pushed_by[g_stack.top] = __builtin_return_address(0);
  *buffer = g_stack.below();
}

// Mech3.exe 1.2 @ 0x0051ea00
void pop() {
  if (g_stack.overflow) {
    --g_stack.overflow;  // a refused push's pop
    return;
  }
  if (g_stack.top <= 0) {
    if (g_faults.under_pop++ == 0) report("a pop with nothing pushed, refused", g_stack.top);
    return;
  }
  --g_stack.top;
}

// Mech3.exe 1.2 @ 0x0051fd40
void load(const Mat34& m) {
  g_stack.matrix() = m;
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x0051fd70
void load_rotation(const Mat34& m) {
  std::memcpy(g_stack.matrix().m, m.m, 9 * sizeof(float));
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x0051fde0
void load_identity() {
  g_stack.matrix() = Mat34{{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}};
  g_stack.is_identity() = 1;
}

// Mech3.exe 1.2 @ 0x0051ea30
void load_view() {
  g_stack.matrix() = g_view.view;
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x0051ea60
void load_camera() {
  g_stack.matrix() = g_view.camera;
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x0051fb30. Model-view: the slot below (the node's world
// matrix) times the view. The identity test after the load is dead (the flag
// was just cleared), so the below slot's own flag is never consulted.
void load_model_view() {
  load_view();
  float r[12];
  mul34(g_stack.below().m, g_stack.matrix().m, r, true);
  std::memcpy(g_stack.matrix().m, r, sizeof r);
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x0051fee0. EDX mode 2 multiplies the 3x3 only; the identity
// shortcut copies all of local in every mode.
void multiply(const Mat34& local, int mode) {
  Mat34& top = g_stack.matrix();
  if (g_stack.is_identity()) {
    top = local;
    g_stack.is_identity() = 0;
    return;
  }
  // FIX (bug): mode 2 copied the stack temp's unwritten row 3 (leftover stack
  // bytes) into the top's translation; zero here (docs/drafts/matrix.md).
  // The x87's order: columns 0 and 1 sum left to right, column 2 as
  // a0 t2 + (a1 t5 + a2 t8); the translation's z as (l9 t2 + t11) + (l10 t5
  // + l11 t8) (docs/render-shadows.md, the shadow box checks it bit for bit).
  float r[12] = {};
  const float* l = local.m;
  const float* t = top.m;
  for (int row = 0; row < 3; ++row) {
    const float* a = l + 3 * row;
    const float x0 = a[0] * t[0], x1 = a[1] * t[3], x2 = a[2] * t[6];
    r[3 * row] = (x0 + x1) + x2;
    const float y0 = a[0] * t[1], y1 = a[1] * t[4], y2 = a[2] * t[7];
    r[3 * row + 1] = (y0 + y1) + y2;
    const float z0 = a[0] * t[2], z1 = a[1] * t[5], z2 = a[2] * t[8];
    r[3 * row + 2] = z0 + (z1 + z2);
  }
  if (mode != 2) {
    const float x0 = l[9] * t[0], x1 = l[10] * t[3], x2 = l[11] * t[6];
    r[9] = ((x0 + x1) + x2) + t[9];
    const float y0 = l[9] * t[1], y1 = l[10] * t[4], y2 = l[11] * t[7];
    r[10] = ((y0 + y1) + y2) + t[10];
    const float z0 = l[9] * t[2], z1 = l[10] * t[5], z2 = l[11] * t[8];
    r[11] = (z0 + t[11]) + (z1 + z2);
  }
  std::memcpy(top.m, r, sizeof r);
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x00520330. The identity shortcut writes the diagonal only.
void scale(float x, float y, float z) {
  float* t = g_stack.matrix().m;
  if (g_stack.is_identity()) {
    t[0] = x, t[4] = y, t[8] = z;
  } else {
    for (int j = 0; j < 3; ++j) t[j] *= x, t[3 + j] *= y, t[6 + j] *= z;
  }
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x00520480
void translate(float x, float y, float z) {
  float* t = g_stack.matrix().m;
  if (g_stack.is_identity()) {
    t[9] = x, t[10] = y, t[11] = z;
  } else {
    for (int j = 0; j < 3; ++j) t[9 + j] = t[j] * x + t[6 + j] * z + t[3 + j] * y + t[9 + j];
  }
  g_stack.is_identity() = 0;
}

namespace {
// Rows i, k of the top become (c*ri + s*rk, c*rk - s*ri): the three axis
// rotations of 0x520610.. share this shape.
void rotate_rows(int i, int k, float c, float s) {
  float* t = g_stack.matrix().m;
  for (int j = 0; j < 3; ++j) {
    const float a = t[3 * i + j], b = t[3 * k + j];
    t[3 * i + j] = c * a + s * b;
    t[3 * k + j] = c * b - s * a;
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x00520610. Rx = [[1,0,0],[0,c,s],[0,-s,c]].
void rotate_x(float a) {
  const float c = std::cos(a), s = std::sin(a);
  float* t = g_stack.matrix().m;
  if (g_stack.is_identity()) {
    t[4] = c, t[5] = s, t[7] = -s, t[8] = c;
  } else {
    rotate_rows(1, 2, c, s);
  }
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x005207b0. Ry = [[c,0,-s],[0,1,0],[s,0,c]].
void rotate_y(float a) {
  const float c = std::cos(a), s = std::sin(a);
  float* t = g_stack.matrix().m;
  if (g_stack.is_identity()) {
    t[0] = c, t[2] = -s, t[6] = s, t[8] = c;
  } else {
    rotate_rows(0, 2, c, -s);
  }
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x00520960. Rz = [[c,s,0],[-s,c,0],[0,0,1]].
void rotate_z(float a) {
  const float c = std::cos(a), s = std::sin(a);
  float* t = g_stack.matrix().m;
  if (g_stack.is_identity()) {
    t[0] = c, t[1] = s, t[3] = -s, t[4] = c;
  } else {
    rotate_rows(0, 1, c, s);
  }
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x00521670. M = S * Rz(c) * Rx(a) * Ry(b), translation pos,
// then multiplied onto the top (docs/render.md section 0). Translation parts
// equal to 0 and scales equal to 1 are skipped, which only shows with NaN.
void euler(const float rot[3], const float pos[3], const float scl[3]) {
  const float sa = std::sin(rot[0]), ca = std::cos(rot[0]);
  const float sb = std::sin(rot[1]), cb = std::cos(rot[1]);
  const float sc = std::sin(rot[2]), cc = std::cos(rot[2]);
  const float cc_cb = cc * cb, sb_sa = sb * sa, sc_cb = sc * cb;
  float l[12] = {
      sb_sa * sc + cc_cb, sc * ca,  sc_cb * sa - cc * sb,
      sb_sa * cc - sc_cb, cc * ca,  cc_cb * sa + sc * sb,
      sb * ca,            -sa,      cb * ca,
      0, 0, 0};
  for (int i = 0; i < 3; ++i)
    if (differs(pos[i], 0.0)) l[9 + i] = pos[i];
  for (int i = 0; i < 3; ++i)
    if (differs(scl[i], 1.0))
      for (int j = 0; j < 3; ++j) l[3 * i + j] *= scl[i];
  Mat34& top = g_stack.matrix();
  if (g_stack.is_identity()) {
    std::memcpy(top.m, l, sizeof l);
  } else {
    float r[12];
    mul34(l, top.m, r, true);
    std::memcpy(top.m, r, sizeof r);
  }
  g_stack.is_identity() = 0;
}

// Mech3.exe 1.2 @ 0x00523bc0. The inverse of euler's rotation for unit scale.
// At |m[7]| >= 1 (pitch +-90 degrees) roll is 0 and yaw takes it all; the
// constant is the exe's pi/2, a few ulps over (0x3ff921fb54442eea).
void matrix_to_euler(const Mat34& mat, float out[3]) {
  const float* m = mat.m;
  if (!(std::fabs(m[7]) >= 1.0)) {  // NaN takes this branch, as on the x87
    out[0] = static_cast<float>(std::asin(-double{m[7]}));
    out[1] = static_cast<float>(std::atan2(double{m[6]}, double{m[8]}));
    out[2] = static_cast<float>(std::atan2(double{m[1]}, double{m[4]}));
  } else {
    out[0] = static_cast<float>(-std::copysign(1.570796326795, double{m[7]}));
    out[2] = 0.0f;
    out[1] = static_cast<float>(std::atan2(-double{m[2]}, double{m[0]}));
  }
}

// Mech3.exe 1.2 @ 0x00525340. Half angles; x's and y's sine and cosine are
// stored as floats, z's stay on the x87 stack. Taken as floats here too, as
// the emulator computes them (the x87's 64-bit fsin can differ in the last
// bit of a product).
void euler_to_quat(float x, float y, float z, float q[4]) {
  const float hx = x * 0.5f, hy = y * 0.5f, hz = z * 0.5f;
  const auto sa = static_cast<float>(std::sin(double{hx})), ca = static_cast<float>(std::cos(double{hx}));
  const auto sb = static_cast<float>(std::sin(double{hy})), cb = static_cast<float>(std::cos(double{hy}));
  const float a = cb * ca, b = sb * ca, c = cb * sa, d = sb * sa;
  const auto sc = static_cast<float>(std::sin(double{hz})), cc = static_cast<float>(std::cos(double{hz}));
  q[0] = a * cc + d * sc;
  q[1] = b * cc + c * sc;
  q[2] = c * cc - b * sc;
  q[3] = a * sc - d * cc;
}

// Mech3.exe 1.2 @ 0x00525280 (ECX q): euler_to_quat with z = 0, the first
// angle `a` (about y) and the second `b` (about x): (cb ca, sb ca, cb sa,
// -(sb sa)).
void quat_two_angles(float a, float b, float q[4]) {
  const float ha = a * 0.5f, hb = b * 0.5f;
  const auto sa = static_cast<float>(std::sin(double{ha})), ca = static_cast<float>(std::cos(double{ha}));
  const auto sb = static_cast<float>(std::sin(double{hb})), cb = static_cast<float>(std::cos(double{hb}));
  q[0] = cb * ca;
  q[1] = sb * ca;
  q[2] = cb * sa;
  q[3] = -(sb * sa);
}

// Mech3.exe 1.2 @ 0x00525740. ECX quaternion (w, x, y, z), EDX the 3x3.
void quat_matrix(const float q[4], float r[9]) {
  const float w = q[0], x = q[1], y = q[2], z = q[3];
  const float sx = x + x, sy = y + y, sz = z + z;
  const float a = sx * x, b = sy * y, c = sz * z;
  const float d = sy * x, e = sz * y, f = sx * z;
  const float g = sx * w, h = sy * w, i = sz * w;
  float t = 1.0f - b;
  r[0] = t - c;
  r[1] = i + d;
  r[2] = f - h;
  r[3] = d - i;
  t = 1.0f - c;
  r[4] = t - a;
  r[5] = g + e;
  r[6] = h + f;
  r[7] = e - g;
  t = 1.0f - a;
  r[8] = t - b;
}

// Mech3.exe 1.2 @ 0x00525f80: the rotation's angles. Its 0x30-byte stack
// matrix gets the 3x3 only; the angles read nothing else.
void quat_angles(const float q[4], float out[3]) {
  Mat34 m{};
  quat_matrix(q, m.m);
  matrix_to_euler(m, out);
}

// Mech3.exe 1.2 @ 0x00525900. A zero vector is the identity. The sine is
// taken in double and divided by the length there, as the x87 keeps it.
void quat_exp(const float v[3], float q[4]) {
  const float len = static_cast<float>(std::sqrt(double{(v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]}));
  if (!(len < 0.0f || len > 0.0f)) {
    q[0] = 1.0f, q[1] = q[2] = q[3] = 0.0f;
    return;
  }
  q[0] = static_cast<float>(std::cos(double{len}));
  const float s = static_cast<float>(std::sin(double{len}) / len);
  q[1] = s * v[0], q[2] = v[1] * s, q[3] = v[2] * s;
}

// Mech3.exe 1.2 @ 0x005255d0. The x87's order of the sums.
void quat_mul(const float a[4], const float b[4], float out[4]) {
  out[0] = ((b[0] * a[0] - a[1] * b[1]) - a[2] * b[2]) - a[3] * b[3];
  out[1] = ((a[0] * b[1] + b[0] * a[1]) + b[3] * a[2]) - a[3] * b[2];
  out[2] = ((a[0] * b[2] + b[0] * a[2]) + a[3] * b[1]) - b[3] * a[1];
  out[3] = ((a[0] * b[3] + b[0] * a[3]) + b[2] * a[1]) - a[2] * b[1];
}

// Mech3.exe 1.2 @ 0x00523ac0
void direction_angles(const float a[3], const float b[3], float out[3]) {
  const float dx = a[0] - b[0], dy = b[1] - a[1], dz = a[2] - b[2];
  const float yaw = static_cast<float>(std::atan2(double{dx}, double{dz}));
  const float flat = static_cast<float>(std::sqrt(double{dx * dx + dz * dz}));
  out[0] = static_cast<float>(std::atan2(double{dy}, double{flat}));
  out[1] = yaw;
  out[2] = 0.0f;
}

// Mech3.exe 1.2 @ 0x00523b40. Returned on the x87 stack; the callers store it.
float direction_pitch(const float a[3], const float b[3]) {
  const float dx = a[0] - b[0], dy = b[1] - a[1], dz = a[2] - b[2];
  const float flat = static_cast<float>(std::sqrt(double{dx * dx + dz * dz}));
  return static_cast<float>(std::atan2(double{dy}, double{flat}));
}

// Mech3.exe 1.2 @ 0x00523f70. The table (0x0063d340, built once: flag
// 0x005eda94) holds e^(-i * 0.019607844) as the x87 makes it at 24-bit
// precision: t = -(i * c) * log2(e) rounded to float, then 2^frac(t) scaled.
float exp_neg(float x) {
  static float table[256];
  static bool built = false;
  if (!built) {
    for (int i = 0; i < 256; ++i) {
      const float t = static_cast<float>(-(static_cast<float>(i) * 0.019607844f) * 1.4426950408889634);
      const float n = std::nearbyint(t);
      const double frac = std::exp2(double{t - n}) - 1.0;  // f2xm1
      table[i] = std::ldexp(static_cast<float>(1.0 + frac), static_cast<int>(n));
    }
    built = true;
  }
  if (x > 5.0f) return 0.0f;
  if (!(x >= 0.0f)) return 1.0f;
  return table[static_cast<int32_t>(51.0f * x)];
}

// FIX (B), W50: every caller eases by e^-(rate dt), and the table steps by
// 1/51, so on frames shorter than 1 / (51 rate) it reads 1: above about
// 150 fps for a rate of 3 nothing eased at all (the turrets froze). The
// 30 fps frame's value, raised to this frame's share of it: at 1/30 s
// exactly the table (play as the original at 30 fps), and at any other
// frame time the same rate per second. No frame time (the first frame, a
// pause) or no rate: the table, as the exe (fixes.md, Frame rate).
float exp_neg_dt(float x, float dt) {
  if (!zfix::options().per_second || !(dt > 1e-6f) || !(x > 0.0f)) return exp_neg(x);
  constexpr float kRef = 1.0f / zfix::kReferenceFps;
  if (dt == kRef) return exp_neg(x);
  return static_cast<float>(std::pow(double{exp_neg(x * (kRef / dt))}, double{dt / kRef}));
}

// Mech3.exe 1.2 @ 0x00523c50. General 3x3 inverse by cofactors; with
// `translation`, row 3 becomes -t * inverse. A determinant of exactly 0 (or
// NaN) returns false and leaves out alone. The x87 keeps products, c2, det,
// 1/det and c unrounded; double does too, which decides "exactly 0".
bool invert(const Mat34& mat, Mat34& out, bool translation) {
  const float* m = mat.m;
  float* o = out.m;
  const auto c0 = static_cast<float>(double{m[8]} * m[4] - double{m[5]} * m[7]);
  const auto c1 = static_cast<float>(double{m[6]} * m[5] - double{m[3]} * m[8]);
  const double c2 = double{m[3]} * m[7] - double{m[6]} * m[4];
  const double det = double{c1} * m[1] + c2 * m[2] + double{m[0]} * c0;
  if (!differs(det, 0.0)) return false;
  const double inv = 1.0 / det;
  const float a = static_cast<float>(m[0] * inv), b = static_cast<float>(inv * m[1]);
  const double c = inv * m[2];
  o[0] = static_cast<float>(inv * c0);
  o[1] = static_cast<float>(c * m[7] - b * m[8]);
  o[2] = static_cast<float>(b * m[5] - c * m[4]);
  o[3] = static_cast<float>(inv * c1);
  o[4] = static_cast<float>(a * m[8] - c * m[6]);
  o[5] = static_cast<float>(c * m[3] - a * m[5]);
  o[6] = static_cast<float>(inv * c2);
  o[7] = b * m[6] - a * m[7];
  o[8] = a * m[4] - b * m[3];
  if (translation) {
    const float t0 = -m[9], t1 = -m[10], t2 = -m[11];
    for (int j = 0; j < 3; ++j) o[9 + j] = t0 * o[j] + t1 * o[3 + j] + t2 * o[6 + j];
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00523c50 at 24 bits: invert's operations in floats.
bool invert_single(const Mat34& mat, Mat34& out, bool translation) {
  const float* m = mat.m;
  float* o = out.m;
  const float c0 = m[8] * m[4] - m[5] * m[7];
  const float c1 = m[6] * m[5] - m[3] * m[8];
  const float c2 = m[3] * m[7] - m[6] * m[4];
  const float det = (c1 * m[1] + c2 * m[2]) + m[0] * c0;
  if (!(det < 0.0f || det > 0.0f)) return false;
  const float inv = 1.0f / det;
  const float a = m[0] * inv, b = inv * m[1], c = inv * m[2];
  o[0] = inv * c0;
  o[1] = c * m[7] - b * m[8];
  o[2] = b * m[5] - c * m[4];
  o[3] = inv * c1;
  o[4] = a * m[8] - c * m[6];
  o[5] = c * m[3] - a * m[5];
  o[6] = inv * c2;
  o[7] = b * m[6] - a * m[7];
  o[8] = a * m[4] - b * m[3];
  if (translation) {
    const float t0 = -m[9], t1 = -m[10], t2 = -m[11];
    for (int j = 0; j < 3; ++j) o[9 + j] = t0 * o[j] + t1 * o[3 + j] + t2 * o[6 + j];
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00520b00. camera = the camera's world matrix with rows y
// and z negated (camera space looks down +z, y down); view = its rigid inverse.
void set_view(const Mat34& w) {
  float* c = g_view.camera.m;
  float* v = g_view.view.m;
  std::memcpy(c, w.m, sizeof w.m);
  for (int j = 3; j < 9; ++j) c[j] = -c[j];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) v[3 * i + j] = c[3 * j + i];
  const float t0 = -c[9], t1 = -c[10], t2 = -c[11];
  for (int j = 0; j < 3; ++j) v[9 + j] = t0 * v[j] + t1 * v[3 + j] + t2 * v[6 + j];
}

// Mech3.exe 1.2 @ 0x00521c10. The effective fov (camera +0xe8/+0xec); its
// getter 0x521c30 has no callers.
void set_fov(float h, float v) {
  g_proj.fov_h = h;
  g_proj.fov_v = v;
}

// Mech3.exe 1.2 @ 0x00521c60, from 0x526e10: window origin, half its size,
// cot(fov/2) from camera +0x1d4/+0x1d8, near and far. Reads the Direct3D flag
// 0x72f348: there the projection's x/y factors carry 1/near (see project_d3d).
void set_projection(float x, float y, float half_w, float half_h, float cot_h, float cot_v,
                    float near_z, float far_z) {
  Projection& p = g_proj;
  p.fx = cot_h * half_w;
  p.fy = cot_v * half_h;
  p.cot_h = cot_h, p.cot_v = cot_v;
  p.inv_cot_h = 1.0f / cot_h, p.inv_cot_v = 1.0f / cot_v;
  p.inv_fx = 1.0f / p.fx, p.inv_fy = 1.0f / p.fy;
  p.half_w = half_w, p.half_h = half_h;
  p.origin_x = x, p.origin_y = y;
  p.cx = half_w + x, p.cy = half_h + y;
  p.near_z = near_z, p.far_z = far_z;
  if (zrndr::device_fog().active) {
    p.inv_near = 1.0f / near_z;
    p.fx_near = p.inv_near * p.fx;
    p.fy_near = p.inv_near * p.fy;
  } else {
    p.inv_near = 1.0f;
    p.fx_near = p.fx;
    p.fy_near = p.fy;
  }
}

// Mech3.exe 1.2 @ 0x0051e8d0. Returned in EAX:EDX.
void focal(float out[2]) {
  out[0] = g_proj.fx;
  out[1] = g_proj.fy;
}

// Mech3.exe 1.2 @ 0x00521db0
void scale_by_cot(Vec3* v, uint32_t count) {
  for (; count != 0; --count, ++v) v->x *= g_proj.cot_h, v->y = g_proj.cot_v * v->y;
}

// Mech3.exe 1.2 @ 0x00521de0
void scale_by_tan(Vec3* v, uint32_t count) {
  for (; count != 0; --count, ++v) v->x *= g_proj.inv_cot_h, v->y = g_proj.inv_cot_v * v->y;
}

// Mech3.exe 1.2 @ 0x005234f0. Count is a do-while: 0 would run 2^32 times
// (every caller passes 1 or more). cam and screen may be the same array.
void project(const Vec3* cam, Vec3* screen, uint32_t count) {
  do {
    const float rz = 1.0f / cam->z;
    screen->z = rz;
    screen->x = cam->x * g_proj.fx * rz + g_proj.cx;
    screen->y = cam->y * g_proj.fy * screen->z + g_proj.cy;
    ++cam, ++screen;
  } while (--count != 0);
}

// Mech3.exe 1.2 @ 0x00523540. Same screen x/y as project; z' = near/z (the
// rhw Direct3D gets, 1 at the near plane). Do-while like project.
void project_d3d(const Vec3* cam, Vec3* screen, uint32_t count) {
  do {
    const float rz = g_proj.near_z / cam->z;
    screen->z = rz;
    screen->x = cam->x * g_proj.fx_near * rz + g_proj.cx;
    screen->y = cam->y * g_proj.fy_near * screen->z + g_proj.cy;
    ++cam, ++screen;
  } while (--count != 0);
}

// Mech3.exe 1.2 @ 0x00523590. The inverse of project_d3d for software factors:
// z = near / z', x = (sx - cx) / fx * z.
void unproject_d3d(const Vec3* screen, Vec3* cam, int32_t count) {
  for (; count > 0; --count, ++screen, ++cam) {
    const float z = g_proj.near_z / screen->z;
    cam->z = z;
    cam->x = (screen->x - g_proj.cx) * g_proj.inv_fx * z;
    cam->y = (screen->y - g_proj.cy) * g_proj.inv_fy * cam->z;
  }
}

// Mech3.exe 1.2 @ 0x005235f0. The inverse of project.
void unproject(const Vec3* screen, Vec3* cam, int32_t count) {
  for (; count > 0; --count, ++screen, ++cam) {
    const float z = 1.0f / screen->z;
    cam->z = z;
    cam->x = (screen->x - g_proj.cx) * g_proj.inv_fx * z;
    cam->y = (screen->y - g_proj.cy) * g_proj.inv_fy * cam->z;
  }
}

// Mech3.exe 1.2 @ 0x00523930: screen points (x, y, 1/z) into the world
// through the camera's matrix (the one view() keeps; the exe pushes a copy).
void unproject_world(const Vec3* screen, Vec3* out, int32_t count) {
  const float* m = view().camera.m;
  for (; count > 0; --count, ++screen, ++out) {
    Vec3 c;
    unproject(screen, &c, 1);
    const Vec3 r = {c.z * m[6] + c.y * m[3] + c.x * m[0] + m[9], c.z * m[7] + c.y * m[4] + c.x * m[1] + m[10],
                    c.z * m[8] + c.y * m[5] + c.x * m[2] + m[11]};
    *out = r;
  }
}

// Mech3.exe 1.2 @ 0x005220e0. dst = base + delta * f (LoD morph; the model draw
// passes its morph count +0x1c and factor +0x24).
void morph(Vec3* dst, const Vec3* base, const Vec3* delta, uint32_t count, float f) {
  for (; count != 0; --count, ++dst, ++base, ++delta) {
    const float dx = delta->x * f, dy = delta->y * f, dz = delta->z * f;
    dst->x = base->x + dx;
    dst->y = base->y + dy;
    dst->z = base->z + dz;
  }
}

namespace {
Vec3 rotate(const Vec3& v, const float* t) {
  return {v.x * t[0] + v.y * t[3] + v.z * t[6], v.x * t[1] + v.y * t[4] + v.z * t[7],
          v.x * t[2] + v.y * t[5] + v.z * t[8]};
}
Vec3 transform(const Vec3& v, const float* t) {
  const Vec3 r = rotate(v, t);
  return {r.x + t[9], r.y + t[10], r.z + t[11]};
}
}  // namespace

// Mech3.exe 1.2 @ 0x00522d60. The shared products and their sums in the
// x87 code's order.
void box_corners_xz(const float* m, const float b[6], float out[8][2]) {
  const float r = m[0] * b[0], t = m[0] * b[3], p = m[3] * b[4], u = m[3] * b[1];
  const float s = m[2] * b[0], w = m[2] * b[3], q = m[5] * b[4], v = m[5] * b[1];
  const float a_lo = b[2] * m[6] + m[9], c_lo = m[8] * b[2] + m[11];
  const float a_hi = m[6] * b[5] + m[9], c_hi = m[8] * b[5] + m[11];
  const float ur = u + r, ut = u + t, pr = p + r, pt = p + t;
  const float vs = v + s, vw = v + w, qs = q + s, qw = q + w;
  const float x[8] = {a_hi + ur, a_hi + ut, a_lo + ut, a_lo + ur, pr + a_hi, pt + a_hi, pt + a_lo, pr + a_lo};
  const float z[8] = {c_hi + vs, c_hi + vw, c_lo + vw, c_lo + vs, qs + c_hi, qw + c_hi, qw + c_lo, qs + c_lo};
  for (int i = 0; i < 8; ++i) out[i][0] = x[i], out[i][1] = z[i];
}

// Mech3.exe 1.2 @ 0x005225b0. Nothing to do when the top is the identity.
void rotate_points(Vec3* v, uint32_t count) {
  if (g_stack.is_identity()) return;
  for (; count != 0; --count, ++v) *v = rotate(*v, g_stack.matrix().m);
}

// Mech3.exe 1.2 @ 0x00522640. v' = (row0.v, row1.v, row2.v): the inverse
// rotation when the 3x3 is orthonormal.
void rotate_points_transposed(Vec3* v, int32_t count) {
  if (g_stack.is_identity() || count <= 0) return;
  for (; count != 0; --count, ++v) {
    const float* t = g_stack.matrix().m;
    const Vec3 p = *v;
    *v = {(t[2] * p.z + t[1] * p.y) + t[0] * p.x, (t[5] * p.z + t[4] * p.y) + t[3] * p.x,
          (t[8] * p.z + t[7] * p.y) + t[6] * p.x};  // the x87's order: z first
  }
}

// Mech3.exe 1.2 @ 0x005226e0. The model draw's normals into camera space.
void rotate_points_to(const Vec3* src, Vec3* dst, uint32_t count) {
  if (g_stack.is_identity()) {
    std::memcpy(dst, src, count * sizeof(Vec3));
    return;
  }
  for (; count != 0; --count, ++src, ++dst) *dst = rotate(*src, g_stack.matrix().m);
}

// Mech3.exe 1.2 @ 0x005227a0
void transform_points(Vec3* v, uint32_t count) {
  if (g_stack.is_identity()) return;
  for (; count != 0; --count, ++v) *v = transform(*v, g_stack.matrix().m);
}

namespace {
void transform_box_columns(const float* m, const float box[6], float out[6], int step) {
  for (int c = 0; c < 3; c += step) {
    float lo, hi;
    for (int r = 0; r < 3; ++r) {
      const float e = m[3 * r + c];
      const float a = box[r] * e, b = box[3 + r] * e;
      const bool pos = !(e < 0.0f);
      if (r == 0) {
        lo = pos ? a : b, hi = pos ? b : a;
      } else {
        lo = (pos ? a : b) + lo, hi = (pos ? b : a) + hi;
      }
    }
    out[c] = lo + m[9 + c];
    out[3 + c] = hi + m[9 + c];
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x005231b0. A zero entry counts as positive.
void transform_box(const float* m, const float box[6], float out[6]) { transform_box_columns(m, box, out, 1); }

// Mech3.exe 1.2 @ 0x00523040
void transform_box_xz(const float* m, const float box[6], float out[6]) { transform_box_columns(m, box, out, 2); }

// Mech3.exe 1.2 @ 0x00522910. v' = (v - t) * transpose(R): the inverse of a
// rigid top, each sum in the x87's order (y and z start from their outer
// terms).
void inverse_transform_points_to(const Vec3* src, Vec3* dst, int32_t count) {
  if (g_stack.is_identity()) {
    std::memcpy(dst, src, static_cast<uint32_t>(count) * sizeof(Vec3));
    return;
  }
  for (; count > 0; --count, ++src, ++dst) {
    const float* t = g_stack.matrix().m;
    const float x = src->x - t[9], y = src->y - t[10], z = src->z - t[11];
    *dst = {(x * t[0] + t[1] * y) + t[2] * z, (t[3] * x + t[5] * z) + t[4] * y,
            (t[8] * z + t[7] * y) + t[6] * x};
  }
}

// Mech3.exe 1.2 @ 0x0053f2e0 (inlined at 0x53f3a9 and 0x53f8fb): the model's
// vertices into camera space, buffer [0x74639c]. A do-while: the point-model
// path (0x53f37d) does not check for 0 vertices first.
void transform_points_to(const Vec3* src, Vec3* dst, uint32_t count) {
  if (g_stack.is_identity()) {
    std::memcpy(dst, src, count * sizeof(Vec3));
    return;
  }
  do {
    *dst++ = transform(*src++, g_stack.matrix().m);
  } while (--count != 0);
}

// Mech3.exe 1.2 @ 0x00522ab0. Each corner is (x r0 + y r1) + (z r2 + t).
void box_corners(const float* m, const float box[6], float out[8][3]) {
  static const int kPick[8][3] = {{0, 1, 5}, {3, 1, 5}, {3, 1, 2}, {0, 1, 2},
                                  {0, 4, 5}, {3, 4, 5}, {3, 4, 2}, {0, 4, 2}};
  for (int i = 0; i < 8; ++i) {
    const float x = box[kPick[i][0]], y = box[kPick[i][1]], z = box[kPick[i][2]];
    for (int c = 0; c < 3; ++c) {
      const float xy = x * m[c] + y * m[3 + c];
      const float zt = z * m[6 + c] + m[9 + c];
      out[i][c] = xy + zt;
    }
  }
}

}  // namespace zmth
