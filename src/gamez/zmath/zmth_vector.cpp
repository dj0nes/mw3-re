#include "gamez/zmath/zmth_vector.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "crt/msvc_math.h"

#pragma STDC FP_CONTRACT OFF

namespace zmth {

// Mech3.exe 1.2 @ 0x004020d0
float normalize(Vec3& v) {
  const float len = std::sqrt((v.x * v.x + v.z * v.z) + v.y * v.y);
  uint32_t bits;
  std::memcpy(&bits, &len, 4);
  if (bits & 0x7fffffff) {
    const float inv = 1.0f / len;
    v.x = inv * v.x;
    v.y = inv * v.y;
    v.z = inv * v.z;
  }
  return len;
}

// Mech3.exe 1.2 @ 0x0051e210. 0 stays 0; out may be v.
void flat_normalize(const Vec3& v, Vec3& out) {
  const float len = std::sqrt((v.x * v.x + 0.0f * 0.0f) + v.z * v.z);
  const float inv = len == 0.0f ? len : 1.0f / len;
  out.x = inv * v.x;
  out.z = v.z * inv;
}

// Mech3.exe 1.2 @ 0x0051e3d0
float direction(const Vec3& from, const Vec3& to, Vec3& out) {
  out = {to.x - from.x, to.y - from.y, to.z - from.z};
  return normalize(out);
}

// Mech3.exe 1.2 @ 0x00525840: 2w (q x v) + 2 (q.v) q + (2w^2 - 1) v, summed
// in the original's order (z adds its terms the other way round).
void quat_rotate(const Quat& q, const Vec3& v, Vec3& out) {
  const float d = (q.x * v.x + q.y * v.y) + q.z * v.z;
  const float d2 = d + d, w2 = q.w + q.w;
  const float c = w2 * q.w - 1.0f;
  out.x = ((v.z * q.y - q.z * v.y) * w2 + d2 * q.x) + c * v.x;
  out.y = ((v.x * q.z - v.z * q.x) * w2 + d2 * q.y) + c * v.y;
  out.z = ((v.y * q.x - v.x * q.y) * w2 + c * v.z) + d2 * q.z;
}

// Mech3.exe 1.2 @ 0x0051dfa0
float distance_sq(const Vec3& a, const Vec3& b) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return (dx * dx + dy * dy) + dz * dz;
}

// Mech3.exe 1.2 @ 0x0051e740: a unit vector at right angles to v in the xy
// plane; the length comes from the bit-trick square root.
void perpendicular(const Vec3& v, Vec3* out) {
  out->z = 0.0f;
  if (v.x == 0.0f) {
    out->x = 1.0f;
    out->y = 0.0f;
    return;
  }
  const float sq = v.y * v.y + v.x * v.x;
  int32_t bits;
  std::memcpy(&bits, &sq, 4);
  bits = (bits >> 1) + 0x1fc00000;
  float len;
  std::memcpy(&len, &bits, 4);
  const float inv = 1.0f / len;
  out->x = inv * v.y;
  out->y = -(inv * v.x);
}

// Mech3.exe 1.2 @ 0x0051e0e0
void add_scaled(const Vec3& base, const Vec3& dir, float s, Vec3* out) {
  *out = {s * dir.x + base.x, dir.y * s + base.y, dir.z * s + base.z};
}

// Mech3.exe 1.2 @ 0x0051e000
float distance(const Vec3& a, const Vec3& b) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt((dx * dx + dy * dy) + dz * dz);
}

// Mech3.exe 1.2 @ 0x0051e280. out may be v.
void reflect(const Vec3& n, const Vec3& v, Vec3* out) {
  const float d = (n.x * v.x + n.y * v.y) + n.z * v.z;
  if (d == 0.0f) {
    *out = {v.x * -1.0f, v.y * -1.0f, v.z * -1.0f};
    return;
  }
  const float k = -d;
  const Vec3 t{k * n.x, n.y * k, n.z * k};
  const Vec3 m{v.x + t.x, v.y + t.y, v.z + t.z};
  *out = {t.x + m.x, t.y + m.y, t.z + m.z};
}

// Mech3.exe 1.2 @ 0x0051e460. out may be a. The sines and the arc are the
// x87's at 24 bits in the original; here the CRT's, rounded once.
void slerp(const Vec3& a, const Vec3& b, float t, Vec3* out) {
  if (!(t > 0.0f)) {
    *out = a;
    return;
  }
  if (!(t < 1.0f)) {
    *out = b;
    return;
  }
  const float c = (a.x * b.x + a.y * b.y) + a.z * b.z;
  if (!(c >= -0.96f)) {
    Vec3 p;
    perpendicular(a, &p);
    const float ang = t * 3.14159274f;
    const float s = static_cast<float>(std::sin(static_cast<double>(ang)));
    const float co = static_cast<float>(std::cos(static_cast<double>(ang)));
    p = {p.x * s, p.y * s, p.z * s};
    const Vec3 o{co * a.x, a.y * co, a.z * co};
    *out = {p.x + o.x, p.y + o.y, p.z + o.z};
    return;
  }
  if (c > 0.96f) {
    const float u = 1.0f - t;
    const Vec3 o{u * a.x, a.y * u, a.z * u};
    const Vec3 q{t * b.x, b.y * t, b.z * t};
    *out = {o.x + q.x, o.y + q.y, o.z + q.z};
    return;
  }
  const float s = std::sqrt(1.0f - c * c);
  const float th = static_cast<float>(std::atan2(static_cast<double>(s), static_cast<double>(c)));
  const float k1 = static_cast<float>(std::sin(static_cast<double>((1.0f - t) * th)));
  const Vec3 o{k1 * a.x, a.y * k1, a.z * k1};
  const float k2 = static_cast<float>(std::sin(static_cast<double>(th * t)));
  const Vec3 q{k2 * b.x, b.y * k2, b.z * k2};
  const Vec3 r{o.x + q.x, o.y + q.y, o.z + q.z};
  const float inv = 1.0f / s;
  *out = {inv * r.x, inv * r.y, inv * r.z};
}

// Mech3.exe 1.2 @ 0x00524247: solves |at + vel t - from| = speed t. With d =
// from - at the answer is vel - d k for the k of the nearer root; the square
// root is the bit trick's.
int32_t intercept(const Vec3& from, const Vec3& at, float speed, const Vec3& vel, Vec3* out) {
  Vec3 d{from.x - at.x, from.y - at.y, from.z - at.z};
  const float dd = (d.x * d.x + d.y * d.y) + d.z * d.z;
  if (dd == 0.0f) return 0;
  const float b = (vel.x * d.x + vel.y * d.y) + vel.z * d.z;
  const float vv = (vel.x * vel.x + vel.y * vel.y) + vel.z * vel.z;
  float a = vv - speed * speed;
  float k;
  if (a == 0.0f) {
    if (!(b > 0.0f)) return 0;
    k = (b + b) / dd;
  } else {
    const float disc = b * b - dd * a;
    if (!(disc >= 0.0f)) return 0;
    int32_t bits;
    std::memcpy(&bits, &disc, 4);
    bits = (bits >> 1) + 0x1fc00000;
    float r;
    std::memcpy(&r, &bits, 4);
    float bb = b;
    if (a < 0.0f) {
      a = -a;
      bb = -b;
    }
    if (bb > r) {
      k = a / (bb - r);
    } else {
      const float s = bb + r;
      if (!(s > 0.0f)) return 0;
      k = a / s;
    }
  }
  d = {k * d.x, k * d.y, k * d.z};
  *out = {vel.x - d.x, vel.y - d.y, vel.z - d.z};
  normalize(*out);
  return 1;
}

// Mech3.exe 1.2 @ 0x00525490. Angles beyond 9.22e18 take the other branch
// (fsin and fcos apart); nothing passes such angles.
void quat_axis_angle(const Vec3& axis, float angle, Quat* q) {
  const float h = angle * 0.5f;
  q->w = static_cast<float>(std::cos(static_cast<double>(h)));
  const float s = static_cast<float>(std::sin(static_cast<double>(h)));
  q->x = s * axis.x;
  q->y = axis.y * s;
  q->z = axis.z * s;
}

// Mech3.exe 1.2 @ 0x00523e70
void rotate_y(float out[3], const float in[3], float angle) {
  const float s = static_cast<float>(std::sin(double{angle}));
  const float c = static_cast<float>(std::cos(double{angle}));
  const float x = in[0], z = in[2];
  out[1] = in[1];
  out[2] = c * z - x * s;
  out[0] = x * c + s * z;
}

// Mech3.exe 1.2 @ 0x00524020 (zMath): the unit normal (b - a) x (c - a), ECX a,
// EDX b, stack c. A zero-length normal is stored as (y, z, x), as the x87
// stack leaves it (all zero anyway).
void poly_normal(const float a[3], const float b[3], const float c[3], float out[3]) {
  const float px = c[0] - a[0], py = c[1] - a[1], pz = c[2] - a[2];
  const float qx = b[0] - a[0], qy = b[1] - a[1], qz = b[2] - a[2];
  const float x = qy * pz - py * qz;
  const float y = qz * px - pz * qx;
  const float z = qx * py - qy * px;
  const float len = std::sqrt(x * x + y * y + z * z);
  if (len == 0.0f) {
    out[0] = y, out[1] = z, out[2] = x;
    return;
  }
  const float inv = 1.0f / len;
  out[0] = x * inv, out[1] = y * inv, out[2] = z * inv;
}

// Mech3.exe 1.2 @ 0x005240d7. ECX ga, EDX gb; differences taken from vertex 1.
void plane_gradient(float a0, float b0, float a1, float b1, float a2, float b2, float t0, float t1, float t2,
                    float* ga, float* gb) {
  const float p = a2 - a1, q = b2 - b1, r = a0 - a1, s = b0 - b1;
  const float d = q * r - p * s;
  if (d == 0.0f) {
    *ga = 0.0f;
    *gb = 0.0f;
    return;
  }
  const float inv = 1.0f / d;
  const float b = t2 - t1, a = t0 - t1;
  *ga = -((b * s - q * a) * inv);
  *gb = -((p * a - b * r) * inv);
}

// Mech3.exe 1.2 @ 0x00525210. The sine and cosine of the half angle as the
// emulator computes them (in double, then rounded).
void quat_roll(float angle, Quat& out) {
  const float h = angle * 0.5f;
  out.z = static_cast<float>(std::sin(double{h}));
  out.x = out.y = 0.0f;
  out.w = static_cast<float>(std::cos(double{h}));
}

// Mech3.exe 1.2 @ 0x00525130 (about x) and 0x005251a0 (about y), as quat_roll.
void quat_pitch(float angle, Quat& out) {
  const float h = angle * 0.5f;
  out.x = static_cast<float>(std::sin(double{h}));
  out.y = out.z = 0.0f;
  out.w = static_cast<float>(std::cos(double{h}));
}

void quat_yaw(float angle, Quat& out) {
  const float h = angle * 0.5f;
  out.y = static_cast<float>(std::sin(double{h}));
  out.x = out.z = 0.0f;
  out.w = static_cast<float>(std::cos(double{h}));
}

// Mech3.exe 1.2 @ 0x00525be0. The angle from fpatan stays on the x87 stack;
// fsincos, the sine through a float before the division (as quat_exp).
void quat_scale(const Quat& q, float f, Quat& out) {
  const float n2 = (q.x * q.x + q.y * q.y) + q.z * q.z;
  if (n2 == 0.0f) {
    out = q;
    return;
  }
  int32_t bits;
  std::memcpy(&bits, &n2, 4);
  bits = (bits >> 1) + 0x1fc00000;
  float s;
  std::memcpy(&s, &bits, 4);
  const double angle = q.w < 0.0f ? -std::atan2(double{s}, -double{q.w}) : std::atan2(double{s}, double{q.w});
  const double a = crt::x87_mul_f24(angle, f);
  const float k = static_cast<float>(std::sin(a)) / s;
  const Quat in = q;
  out.w = static_cast<float>(std::cos(a));
  out.x = in.x * k;
  out.y = in.y * k;
  out.z = in.z * k;
}

// Mech3.exe 1.2 @ 0x00525580: the sign bits of x, y and z flipped.
void quat_conjugate(const Quat& q, Quat& out) { out = {q.w, -q.x, -q.y, -q.z}; }

// Mech3.exe 1.2 @ 0x00525500. The length is the float's bits halved
// (sqrt's exponent), not a true square root: the result is only near unit.
void quat_normalize(const Quat& q, Quat& out) {
  float n2 = (q.x * q.x + q.y * q.y) + q.z * q.z;
  n2 = q.w * q.w + n2;
  int32_t bits;
  std::memcpy(&bits, &n2, 4);
  bits = (bits >> 1) + 0x1fc00000;
  float len;
  std::memcpy(&len, &bits, 4);
  const float inv = 1.0f / len;
  out.w = inv * q.w;
  out.x = q.x * inv;
  out.y = q.y * inv;
  out.z = q.z * inv;
}

// Mech3.exe 1.2 @ 0x005255d0
void quat_mul(const Quat& a, const Quat& b, Quat& out) {
  const Quat r{((a.w * b.w - a.x * b.x) - a.y * b.y) - a.z * b.z,
               ((a.w * b.x + a.x * b.w) + a.y * b.z) - a.z * b.y,
               ((a.w * b.y + a.y * b.w) + a.z * b.x) - a.x * b.z,
               ((a.w * b.z + a.z * b.w) + a.x * b.y) - a.y * b.x};
  out = r;
}

// Mech3.exe 1.2 @ 0x005259c0: quat_exp undone, the axis by the half angle
// (x, y, z) atan2(|v|, w) / |v|, the angle's sign by w's; no axis gives 0.
void quat_log(const float q[4], float out[3]) {
  const float n2 = q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
  if (n2 == 0.0f) {
    out[0] = out[1] = out[2] = 0.0f;
    return;
  }
  const double n = std::sqrt(static_cast<double>(n2));
  const double a = (q[0] < 0.0f ? -std::atan2(n, -static_cast<double>(q[0])) : std::atan2(n, static_cast<double>(q[0]))) / n;
  out[0] = static_cast<float>(q[1] * a);
  out[1] = static_cast<float>(q[2] * a);
  out[2] = static_cast<float>(q[3] * a);
}

// Mech3.exe 1.2 @ 0x00525900. fsincos; the sine goes through a float
// before the division (beyond 2^63 a separate fsin keeps it on the stack).
void quat_exp(const Vec3& v, Quat& out) {
  const float a = std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
  if (a == 0.0f) {
    out = {1.0f, 0.0f, 0.0f, 0.0f};
    return;
  }
  const auto s = static_cast<float>(std::sin(double{a}));
  out.w = static_cast<float>(std::cos(double{a}));
  const float k = s / a;
  out.x = k * v.x;
  out.y = v.y * k;
  out.z = v.z * k;
}

// Mech3.exe 1.2 @ 0x00525ce0. ECX a, EDX b. The angle from fpatan and the
// two fsin stay on the x87 stack; each product with them rounds once to a
// float (24-bit precision).
void quat_slerp(const float a_in[4], const float b_in[4], float t, float out[4]) {
  if (t == 0.0f) {
    for (int i = 0; i < 4; ++i) out[i] = a_in[i];
    return;
  }
  if (t == 1.0f) {
    for (int i = 0; i < 4; ++i) out[i] = b_in[i];
    return;
  }
  float a[4], b[4];
  for (int i = 0; i < 4; ++i) a[i] = a_in[i], b[i] = b_in[i];
  const float part = (a[1] * b[1] + a[2] * b[2]) + a[3] * b[3];
  float c = b[0] * a[0] + part;
  if (c < 0.0f) {
    for (float& v : b) v = -v;
    c = -c;
  }
  if (static_cast<double>(c) > 0.95) {  // 0x005ce0c0
    const float u = 1.0f - t;
    for (int i = 0; i < 4; ++i) out[i] = a[i] * u + b[i] * t;
    return;
  }
  const float x = 1.0f - c * c;
  uint32_t bits;
  std::memcpy(&bits, &x, 4);
  bits = static_cast<uint32_t>((static_cast<int32_t>(bits) >> 1) + 0x1fc00000);
  float s;
  std::memcpy(&s, &bits, 4);
  const double angle = std::atan2(double{s}, double{c});
  const double sa = std::sin(double{crt::x87_mul_f24(1.0f - t, angle)});
  const double sb = std::sin(double{crt::x87_mul_f24(angle, t)});
  for (int i = 0; i < 4; ++i) out[i] = crt::x87_mul_f24(a[i], sa) + crt::x87_mul_f24(sb, b[i]);
  const float inv = 1.0f / s;
  for (int i = 0; i < 4; ++i) out[i] = inv * out[i];
}

namespace {
// x87 compares: an unordered operand reads as equal (zero) or below.
bool zeroish(float x) { return !(x < 0.0f || x > 0.0f); }
bool below(float a, float b) { return !(a >= b); }

// 0x0051e740 as 0x00525a50 reaches it: a NaN x counts as zero.
void perpendicular_xy(const float v[3], float out[3]) {
  out[2] = 0.0f;
  if (zeroish(v[0])) {
    out[0] = 1.0f, out[1] = 0.0f;
    return;
  }
  const float yy = v[1] * v[1], xx = v[0] * v[0];
  const float s = yy + xx;
  int32_t bits;
  std::memcpy(&bits, &s, 4);
  bits = (bits >> 1) + 0x1fc00000;
  float root;
  std::memcpy(&root, &bits, 4);
  const float r = 1.0f / root;
  out[0] = r * v[1];
  out[1] = -(r * v[0]);
}
}  // namespace

// Mech3.exe 1.2 @ 0x00525a50. ECX from, EDX to (unit), stack out: the
// quaternion (w, x, y, z) turning `from` onto `to`, through the half vector.
void quat_between(const float u[3], const float d[3], float q[4]) {
  Vec3 hv{u[0] + d[0], u[1] + d[1], u[2] + d[2]};
  if (zeroish(hv.x) && zeroish(hv.y) && zeroish(hv.z)) {
    q[0] = 0.0f;
    return perpendicular_xy(u, q + 1);
  }
  normalize(hv);
  const float h[3] = {hv.x, hv.y, hv.z};
  const float a = u[1] * h[2], b = u[2] * h[1];
  const float c0 = a - b;
  const float e = u[2] * h[0], f = u[0] * h[2];
  const float c1 = e - f;
  const float g = u[0] * h[1], k = u[1] * h[0];
  const float c2 = g - k;
  if (zeroish(c0) && zeroish(c1) && zeroish(c2)) {
    const float p0 = u[0] * d[0], p1 = u[1] * d[1], p2 = u[2] * d[2];
    const float s = p0 + p1;
    const float dot = s + p2;
    if (below(dot, 0.0f)) {
      q[0] = 0.0f;
      return perpendicular_xy(u, q + 1);
    }
  }
  const float p0 = u[0] * h[0], p1 = u[1] * h[1], p2 = u[2] * h[2];
  const float s = p0 + p1;
  q[0] = s + p2;
  q[1] = c0, q[2] = c1, q[3] = c2;
}

}  // namespace zmth
