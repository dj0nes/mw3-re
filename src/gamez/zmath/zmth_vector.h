#pragma once

#include "gamez/zmath/zmth_matrix.h"

// Vector and quaternion helpers the movement and weapon code use
// (docs/movement.md, docs/weapons.md).
// The direction and the quaternion rotation sit in the unnamed maths module
// of zmth_matrix (0x51b170..0x526920); the normalise at 0x004020d0 is a copy
// in the first, unnamed game module (before mech3\briefing.cpp).
namespace zmth {

// Quaternions are (w, x, y, z), Hamilton products.
struct Quat {
  float w, x, y, z;
};

float normalize(Vec3& v);                                // 0x004020d0, returns the length; 0 stays 0
float direction(const Vec3& from, const Vec3& to, Vec3& out);  // 0x0051e3d0, normalize(to - from)
void flat_normalize(const Vec3& v, Vec3& out);           // 0x0051e210: x and z to unit length; out.y untouched
void quat_rotate(const Quat& q, const Vec3& v, Vec3& out);     // 0x00525840, q v q*
float distance_sq(const Vec3& a, const Vec3& b);         // 0x0051dfa0 (keeps a - b in 0x0063d328)
void perpendicular(const Vec3& v, Vec3* out);            // 0x0051e740: in the xy plane, by the fast square root
void quat_axis_angle(const Vec3& axis, float angle, Quat* q);  // 0x00525490
// Mech3.exe 1.2 @ 0x00523e70 (ECX out, EDX in, may be the same): `in` turned
// about y by `angle`: x' = x cos + z sin, z' = z cos - x sin.
void rotate_y(float out[3], const float in[3], float angle);
void poly_normal(const float a[3], const float b[3], const float c[3], float out[3]);  // 0x00524020: unit (b - a) x (c - a)
// The gradient of a value t over a triangle in a plane: t - t0 = ga (a - a0) +
// gb (b - b0) through (a0, b0, t0), (a1, b1, t1), (a2, b2, t2); both 0 for
// a degenerate triangle.
void plane_gradient(float a0, float b0, float a1, float b1, float a2, float b2, float t0, float t1, float t2,
                    float* ga, float* gb);  // 0x005240d7
void quat_pitch(float angle, Quat& out);                 // 0x00525130: about x
void quat_yaw(float angle, Quat& out);                   // 0x005251a0: about y
void quat_roll(float angle, Quat& out);                  // 0x00525210: about z
void quat_conjugate(const Quat& q, Quat& out);           // 0x00525580
// q's rotation angle times f about the same axis (0x00525be0); a zero axis
// leaves q. The axis length from the bit-trick square root.
void quat_scale(const Quat& q, float f, Quat& out);
void quat_normalize(const Quat& q, Quat& out);           // 0x00525500: bit-trick square root
void quat_mul(const Quat& a, const Quat& b, Quat& out);  // 0x005255d0: a b
void quat_exp(const Vec3& v, Quat& out);                 // 0x00525900: (cos |v|, sin |v| v / |v|)
void quat_log(const float q[4], float out[3]);           // 0x005259c0: its inverse
// The rotation turning unit `from` onto unit `to` through their half vector;
// opposite vectors take a turn about a perpendicular in the xy plane.
void quat_between(const float from[3], const float to[3], float q[4]);  // 0x00525a50
// a to b by t along the shorter arc; linear above a cosine of 0.95, the sine's
// divisor from the bit-trick square root. out may be a or b.
void quat_slerp(const float a[4], const float b[4], float t, float out[4]);  // 0x00525ce0

void add_scaled(const Vec3& base, const Vec3& dir, float s, Vec3* out);  // 0x0051e0e0: base + dir s
float distance(const Vec3& a, const Vec3& b);            // 0x0051e000 (keeps a - b in 0x0063d328)
void reflect(const Vec3& n, const Vec3& v, Vec3* out);   // 0x0051e280: v - 2 (n.v) n; -v when n.v is 0
// 0x0051e460: from a toward b by t (0..1) over the arc of two unit vectors:
// linear when they are close (dot > 0.96), round a perpendicular when nearly
// opposite (dot < -0.96).
void slerp(const Vec3& a, const Vec3& b, float t, Vec3* out);
// 0x00524247: the direction to shoot from `from` at `speed` to meet a target
// at `at` moving at `vel`; 0 when it cannot.
int32_t intercept(const Vec3& from, const Vec3& at, float speed, const Vec3& vel, Vec3* out);

// The same two with pointer outputs, as the weapon code calls them.
inline float direction_to(const Vec3& from, const Vec3& to, Vec3* out) { return direction(from, to, *out); }
inline void quat_rotate(const Quat& q, const Vec3& v, Vec3* out) { quat_rotate(q, v, *out); }

}  // namespace zmth
