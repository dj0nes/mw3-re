#include "mech3/mech_anim.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "gamez/zutil/zutl_path.h"
#include <cstdlib>
#include <cstring>

#include "gamez/zerror/zerr.h"
#include "gamez/zutil/zutl_zar.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
const char kAnimFile[] = "C:\\zipper\\Mech3\\mech_anim.cpp";
const char kScriptFile[] = "C:\\zipper\\GameZMech3\\zSIScript\\si_script.cpp";
zutl_zar::Archive* g_motions = nullptr;  // 0x00736588

// The part names the constructors look up (0x005e23c0..0x005e2374).
const char* const kFeet[2] = {"lfoot", "rfoot"};
const char* const kArms[2] = {"larm", "rarm"};
const char* const kHands[2] = {"lhand", "rhand"};

// A part's script borrowed for a blend's sample (0x00421510), and returned
// (0x00421520).
struct Lend {
  zsis::Part* pa;
  zsis::Part* pb;
  MechAnim* a;
  MechAnim* b;
  Lend(zsis::Part* pa_, zsis::Part* pb_, BlendAnim* blend, MechAnim* a_, MechAnim* b_)
      : pa(pa_), pb(pb_), a(a_), b(b_) {
    pa->script = blend;
    pb->script = blend;
  }
  ~Lend() {
    pa->script = a;
    pb->script = b;
  }
};
}  // namespace

namespace {
std::string g_motions_game_path;  // the path open_motions was given (the archive's own may be freed)
std::set<std::string> g_broken;    // motions some copy has, broken, and none has whole
}  // namespace

// Mech3.exe 1.2 @ 0x005a9820. A failed open leaves no archive.
bool open_motions(const char* path) {
  close_motions();
  g_motions_game_path = path ? path : "";
  g_broken.clear();
  g_motions = new zutl_zar::Archive;
  zutl_zar::init(*g_motions);
  if (zutl_zar::open(*g_motions, path)) return true;
  close_motions();
  return false;
}

// Mech3.exe 1.2 @ 0x005a98a0
void close_motions() {
  if (g_motions) {
    zutl_zar::destroy(*g_motions);
    delete g_motions;
  }
  g_motions = nullptr;
}

namespace {

// One motion from one archive, bounded by its entry; `broken` when it is
// there and refused.
bool read_from(zutl_zar::Archive& a, zsis::Script& s, const char* name, bool* broken) {
  *broken = false;
  const int i = zutl_zar::find(a, name);
  if (i < 0) return false;  // the exe seeks to entry -1 and reads whatever is there
  uint32_t length = 0;
  std::FILE* f = zutl_zar::seek(a, i, &length);
  if (!f) return false;
  const long end = std::ftell(f) + static_cast<long>(length);
  if (s.read(f, end)) return true;
  *broken = s.part_count == 0 && !s.parts;
  return false;
}
}  // namespace

// Mech3.exe 1.2 @ 0x005a9ad0. The base name goes to the script's name.
bool load_motion(zsis::Script& s, const char* name) {
  const char* base = name;
  for (const char* p = name; *p; ++p)
    if (*p == '\\' || *p == '/' || *p == ':') base = p + 1;
  size_t n = 0;
  while (base[n] && base[n] != '.' && n + 1 < sizeof s.name) ++n;
  std::memcpy(s.name, base, n);
  s.name[n] = 0;
  if (!g_motions) return false;
  bool broken = false;
  if (read_from(*g_motions, s, name, &broken)) return true;
  // Ours (docs/modding.md, M3): a motion the open archive (a mod's) has
  // broken, or lacks, is taken from the next mount's copy of the archive
  // (normally the install's), in mount order; said in the log. Without
  // mods there is one copy and the motion is missing, as in the exe.
  const std::string first = zutl::find(g_motions_game_path.c_str());
  for (const std::string& host : zutl::find_all(g_motions_game_path.c_str())) {
    if (host == first) continue;
    zutl_zar::Archive a;
    zutl_zar::init(a);
    bool other_broken = false;
    const bool ok = zutl_zar::open(a, host.c_str()) && read_from(a, s, name, &other_broken);
    zutl_zar::destroy(a);
    broken = broken || other_broken;
    if (ok) {
      std::printf("mw3: %s: its motion %s is %s; %s's instead\n", first.c_str(), name, broken ? "broken" : "missing",
                  host.c_str());
      return true;
    }
  }
  if (broken) {
    std::printf("mw3: %s: its motion %s is broken (a count or keys past its end), and no other copy has it\n",
                first.empty() ? "motion.zbd" : first.c_str(), name);
    g_broken.insert(name);
  }
  return false;
}

bool motion_broken(const char* name) { return name && g_broken.count(name) != 0; }

// Mech3.exe 1.2 @ 0x0041f100
MechAnim::MechAnim() { active = true; }

// Mech3.exe 1.2 @ 0x005a9dc0 (the script's destructor)
void script_free_parts(zsis::Script& s) {
  if (!s.parts) return;
  for (uint32_t i = 0; i < s.part_count; ++i) {
    if (!s.parts[i]) continue;
    s.parts[i]->destroy();
    delete s.parts[i];
  }
  std::free(s.parts);
}

MechAnim::~MechAnim() { script_free_parts(*this); }

// Mech3.exe 1.2 @ 0x0041ee80. Keeps the phase when the rate changes.
void MechAnim::set_speed(float speed) {
  const float f = mult * speed;
  if (f != 0.0f) {
    const float rt = rate * t;
    t = rt / f;
  }
  rate = f;
}

// Mech3.exe 1.2 @ 0x0041ef60
void MechAnim::advance(float dt) { t = t + dt; }

// Mech3.exe 1.2 @ 0x0041fc10
void MechAnim::hip_frame(const float* m) { hip_part->set_matrix(m, false); }

// Mech3.exe 1.2 @ 0x0041fbe0
void MechAnim::ski_frame(int leg, const float* m) { feet[leg]->set_matrix(m, false); }

// Mech3.exe 1.2 @ 0x0041f8d0. Only z is written; beyond 1000 it is dropped.
void MechAnim::velocity(Vec3* v) {
  float s[3];
  hip_part->velocity(t, s);
  v->z = s[2];
  if (std::fabs(v->z) > 1000.0f) v->z = 0.0f;  // 0x005bd1e0
}

// Mech3.exe 1.2 @ 0x0041f930
void MechAnim::foot(int leg, float pos[3], float q[4]) {
  feet[leg]->translation(t, pos);
  feet[leg]->rotation(t, q);
}

// Mech3.exe 1.2 @ 0x0041f9d0
bool MechAnim::arm(int side, float* pos, float* q) {
  zsis::Part* p = arms[side];
  if (!p) return false;
  if (pos) p->translation(t, pos);
  if (q) p->rotation(t, q);
  return true;
}

// Mech3.exe 1.2 @ 0x0041fa40
bool MechAnim::hand(int side, float* pos, float* q) {
  zsis::Part* p = hands[side];
  if (!p) return false;
  if (pos) p->translation(t, pos);
  if (q) p->rotation(t, q);
  return true;
}

// Mech3.exe 1.2 @ 0x0041f980
void MechAnim::hip(float pos[3], float q[4]) {
  hip_part->translation(t, pos);
  hip_part->rotation(t, q);
}

// Mech3.exe 1.2 @ 0x0041fae0
void MechAnim::torso(float pos[3], float q[4]) {
  torso_part->translation(t, pos);
  torso_part->rotation(t, q);
}

// Mech3.exe 1.2 @ 0x0041fab0
void MechAnim::torso_rotation(float q[4]) { torso_part->rotation(t, q); }

// Mech3.exe 1.2 @ 0x0041fb70
bool MechAnim::toe(int leg, int i, float q[4]) {
  zsis::Part* p = toes[leg][i];
  if (p) p->rotation(t, q);
  return p != nullptr;
}

// Mech3.exe 1.2 @ 0x0041fce0. -1 is the start.
void MechAnim::seek(int event) { t = event == -1 ? 0.0f : rate * events[event]; }

// Mech3.exe 1.2 @ 0x0041ef00: the same fraction of the cycle as `from`.
void MechAnim::copy_phase(const MechAnim& from) {
  const float f = rate * from.loop_time;
  if (f != 0.0f) t = from.rate * from.t * loop_time / f;
}

// Mech3.exe 1.2 @ 0x0041ef90
void MechAnim::set_stride_speed(float s) {
  stride_speed = s;
  if (stride > 1.0f) {
    const float a = stride_speed * loop_time;
    mult = a / stride;
  } else {
    mult = 1.0f;
  }
}

// Mech3.exe 1.2 @ 0x0041f340, after the script's load: the parts by name.
void MechAnim::find_parts() {
  feet[0] = find_part(kFeet[0], true);
  feet[1] = find_part(kFeet[1], true);
  hip_part = find_part("hip", true);
  torso_part = find_part("torso", true);
  arms[0] = find_part(kArms[0], true);
  arms[1] = find_part(kArms[1], true);
  hands[0] = find_part(kHands[0], true);
  hands[1] = find_part(kHands[1], true);
  char n[16];
  for (int i = 0; i < 4; ++i) {
    std::snprintf(n, sizeof n, "ltoe%02d", i + 1);
    toes[0][i] = find_part(n, true);
    std::snprintf(n, sizeof n, "rtoe%02d", i + 1);
    toes[1][i] = find_part(n, true);
  }
}

void MechAnim::bind_parts(const char* motion_name) {
  if (!active) return;
  find_parts();
  if (!feet[0] || !feet[1] || !hip_part || !torso_part) {
    auto low = [](const void* p) { return static_cast<unsigned>(reinterpret_cast<uintptr_t>(p)); };
    zerr::reportf(0x400, kAnimFile, 0xb4, "Motion (%s):  feet=( 0x%08x, 0x%08x ) hip=0x%08x, torso=0x%08x",
                  motion_name ? motion_name : "", low(feet[0]), low(feet[1]), low(hip_part), low(torso_part));
    active = false;
  }
}

// Mech3.exe 1.2 @ 0x0041f180 (0x0041edd0): another's buffers, its time and
// footfalls.
void MechAnim::copy_from(const MechAnim& from) {
  share(from);
  stride_speed = from.stride_speed;
  t = from.t;
  find_parts();
  events[0] = from.events[0];
  events[1] = from.events[1];
  events[2] = from.events[2];
}

// Mech3.exe 1.2 @ 0x0041f580 (vtable +0x48). A stride left at the file's
// -1 is the hip's travel over the loop (0 under 1 m); a stride speed gives
// the hip velocity keys and the multiplier. Unused parts are freed, the
// keys start with the left foot furthest forward, then resampled.
void MechAnim::init(float speed, uint32_t frames) {
  if (stride < 0.0f) {
    const float* k0 = hip_part->key(0, nullptr);
    const float* kn = hip_part->key(frame_count - 1, nullptr);
    const float dz = std::fabs(k0[2] - kn[2]);
    stride = dz < 1.0f ? 0.0f : dz;  // 0x005bd1d8
  }
  if (speed > 0.0f) {
    zsis::make_velocities(hip_part);
    set_stride_speed(speed);
  }
  free_unused();
  start_at_left_forward();
  set_frames(frames);
}

// Mech3.exe 1.2 @ 0x0041fc40: the key with the least left-foot z (the
// furthest forward; the first of equals) becomes key 0.
bool MechAnim::start_at_left_forward() {
  uint32_t best = 0;
  float b[3];
  feet[0]->key(0, b);
  for (uint32_t i = 1; i < frame_count; ++i) {
    const float* k = feet[0]->key(i, nullptr);
    if (k[2] < b[2]) {
      b[0] = k[0], b[1] = k[1], b[2] = k[2];
      best = i;
    }
  }
  rotate_keys(best);
  return true;
}

// Mech3.exe 1.2 @ 0x0041fd30. Steps of 0.01 s at rate 1: where the feet are
// equally far fore and aft (within 0.2, or the best so far) with the right
// foot highest is the left footfall; the right one is half a loop away,
// the quarter between.
float MechAnim::footfalls() {
  const float step = 0.01f;
  float best = 10.0f, top = -10.0f;
  set_speed(1.0f);
  phase_at(step, nullptr, nullptr, nullptr);
  const float loop = loop_time;
  for (float s = 0.0f; !(s > loop); s = s + step) {
    float l[3], r[3];
    feet[0]->translation(s, l);
    feet[1]->translation(s, r);
    const float d = std::fabs(std::fabs(l[2]) - std::fabs(r[2]));
    if (!(d < best) && !(0.2f > d)) continue;  // 0x0041fef0 with 0x3e4ccccd
    if (r[1] > top) {
      best = d;
      top = r[1];
      events[0] = s;
    }
  }
  const float half = loop * 0.5f;
  if (events[0] + half > loop) {
    events[1] = events[0] - half;
    events[2] = half * 0.5f + events[1];
  } else {
    events[1] = events[0] + half;
    events[2] = half * 0.5f + events[0];
  }
  return best;
}

// Mech3.exe 1.2 @ 0x0041f680 (vtable +0x40): left and right exchanged, then
// init. The exe mirrors both arms unchecked (no arm crashes it); here a
// missing part is skipped.
void MechAnim::mirror_init(float speed, uint32_t frames) {
  zsis::Part* order[8] = {feet[0], feet[1], arms[0], arms[1], hands[0], hands[1], hip_part, torso_part};
  for (zsis::Part* p : order)
    if (p) p->mirror(frames);
  zsis::swap_parts(&feet[0], &feet[1]);
  if (arms[0] && arms[1]) zsis::swap_parts(&arms[0], &arms[1]);
  if (hands[0] && hands[1]) zsis::swap_parts(&hands[0], &hands[1]);
  for (int i = 0; i < 4; ++i)
    if (toes[0][i] && toes[1][i]) zsis::swap_parts(&toes[0][i], &toes[1][i]);
  init(speed, frames);
}

// Mech3.exe 1.2 @ 0x0041f000. The loader 0x0041f340 is the script's load by
// name (0x005a9990: 30 frames a second) and bind_parts.
MechAnim* load_anim(const char* name, float speed, uint32_t frames) {
  auto* a = new MechAnim;
  a->active = load_motion(*a, name);
  if (!a->active) zerr::reportf(0x400, kScriptFile, 0x75, "Script (%s) not loaded", name);
  else a->set_fps(30.0f);
  a->bind_parts(name);
  if (!a->active) {
    delete a;
    return nullptr;
  }
  a->init(speed, frames);
  return a;
}

// Mech3.exe 1.2 @ 0x0041ffc0
BlendAnim::BlendAnim(MechAnim* a_, MechAnim* b_) : a(a_), b(b_) {
  frame_count = a->frame_count;
  set_loop_time(a->loop_time);
  set_stride_speed(a->stride_speed <= b->stride_speed ? b->stride_speed : a->stride_speed);
  set_blend(0.0f);
}

// Mech3.exe 1.2 @ 0x0041ff30
BlendAnim* make_blend(MechAnim* a, MechAnim* b) { return a && b ? new BlendAnim(a, b) : nullptr; }

// Mech3.exe 1.2 @ 0x00420cb0
void BlendAnim::set_speed(float speed) {
  a->set_speed(speed);
  b->set_speed(speed);
  MechAnim::set_speed(speed);
}

// Mech3.exe 1.2 @ 0x00420260
void BlendAnim::advance(float dt) {
  t = t + dt;
  a->advance(dt);
  b->advance(dt);
}

// Mech3.exe 1.2 @ 0x004200c0: loop time and stride between the two's.
void BlendAnim::set_blend(float w) {
  weight = w;
  const float loop = (b->loop_time - a->loop_time) * w + a->loop_time;
  if (loop_time != 0.0f) {
    t = t * loop / loop_time;
    set_loop_time(loop);
  }
  stride = (b->stride - a->stride) * w + a->stride;
  scale_rates(true);
}

// Mech3.exe 1.2 @ 0x004201b0: the multiplier for the blended stride, the
// rate and time rescaled to keep the phase.
void BlendAnim::scale_rates(bool keep_phase) {
  float m = 1.0f;
  if (stride > 1.0f) {
    const float a_ = stride_speed * loop_time;
    m = a_ / stride;
    if (keep_phase) {
      const float r = rate / mult;
      rate = r * m;
      t = mult / m * t;
    }
  }
  mult = m;
}

// Mech3.exe 1.2 @ 0x00420370
void BlendAnim::hip_frame(const float* m) {
  a->hip_frame(m);
  b->hip_frame(m);
}

// Mech3.exe 1.2 @ 0x00420320
void BlendAnim::ski_frame(int leg, const float* m) {
  a->ski_frame(leg, m);
  b->ski_frame(leg, m);
}

// Mech3.exe 1.2 @ 0x004203c0. Unlike the base, x and y are zeroed and z is
// not clamped.
void BlendAnim::velocity(Vec3* v) {
  float s[3];
  {
    Lend l(a->hip_part, b->hip_part, this, a, b);
    a->hip_part->blend_velocity(t, *b->hip_part, weight, s);
  }
  v->x = 0.0f;
  v->y = 0.0f;
  v->z = s[2];
}

// Mech3.exe 1.2 @ 0x00420490. A's ski frame applies to the mix.
void BlendAnim::foot(int leg, float pos[3], float q[4]) {
  {
    Lend l(a->feet[leg], b->feet[leg], this, a, b);
    a->feet[leg]->blend_translation(t, *b->feet[leg], weight, pos);
    a->feet[leg]->blend_rotation(t, *b->feet[leg], weight, q);
  }
  a->feet[leg]->transform(pos);
}

// Mech3.exe 1.2 @ 0x004205d0. With one side's part missing, the other plays
// on its own timing.
bool BlendAnim::arm(int side, float* pos, float* q) {
  zsis::Part* pa = a->arms[side];
  zsis::Part* pb = b->arms[side];
  if (!pa && !pb) return false;
  if (!pa) return b->arm(side, pos, q);
  if (!pb) return a->arm(side, pos, q);
  Lend l(pa, pb, this, a, b);
  if (pos) pa->blend_translation(t, *pb, weight, pos);
  if (q) pa->blend_rotation(t, *pb, weight, q);
  return true;
}

// Mech3.exe 1.2 @ 0x004207b0
bool BlendAnim::hand(int side, float* pos, float* q) {
  zsis::Part* pa = a->hands[side];
  zsis::Part* pb = b->hands[side];
  if (!pa && !pb) return false;
  if (!pa) return b->hand(side, pos, q);
  if (!pb) return a->hand(side, pos, q);
  Lend l(pa, pb, this, a, b);
  if (pos) pa->blend_translation(t, *pb, weight, pos);
  if (q) pa->blend_rotation(t, *pb, weight, q);
  return true;
}

// Mech3.exe 1.2 @ 0x00420af0
void BlendAnim::hip(float pos[3], float q[4]) {
  Lend l(a->hip_part, b->hip_part, this, a, b);
  a->hip_part->blend_translation(t, *b->hip_part, weight, pos);
  a->hip_part->blend_rotation(t, *b->hip_part, weight, q);
}

// Mech3.exe 1.2 @ 0x00420bf0
void BlendAnim::torso_rotation(float q[4]) {
  Lend l(a->torso_part, b->torso_part, this, a, b);
  a->torso_part->blend_rotation(t, *b->torso_part, weight, q);
}

// Mech3.exe 1.2 @ 0x00420990: both or nothing.
bool BlendAnim::toe(int leg, int i, float q[4]) {
  zsis::Part* pa = a->toes[leg][i];
  zsis::Part* pb = b->toes[leg][i];
  if (!pa || !pb) return false;
  Lend l(pa, pb, this, a, b);
  pa->blend_rotation(t, *pb, weight, q);
  return true;
}

// Mech3.exe 1.2 @ 0x004202c0
void BlendAnim::seek(int event) {
  a->seek(event);
  b->seek(event);
  t = a->t;
}

MechAction::MechAction() { wraps = false; }

// Mech3.exe 1.2 @ 0x00420f80
void MechAction::advance(float dt) {
  if (done) return;
  float v = t + (reversed ? -dt : dt);
  done = true;  // 0x00409980: clamp, true when it clamped
  if (v > loop_time) v = loop_time;
  else if (v < 0.0f) v = 0.0f;
  else done = false;
  t = v;
}

// Mech3.exe 1.2 @ 0x00420ec0
void MechAction::bind_leg_parts() {
  calves[0] = find_part("lcalf", true);
  calves[1] = find_part("rcalf", true);
  thighs[0] = find_part("lthigh", true);
  thighs[1] = find_part("rthigh", true);
  wraps = false;
  reset();
}

// Mech3.exe 1.2 @ 0x00421080
void MechAction::calf(int leg, float pos[3], float q[4]) {
  calves[leg]->translation(t, pos);
  calves[leg]->rotation(t, q);
}

// Mech3.exe 1.2 @ 0x004210d0
void MechAction::thigh(int leg, float pos[3], float q[4]) {
  thighs[leg]->translation(t, pos);
  thighs[leg]->rotation(t, q);
}

// Mech3.exe 1.2 @ 0x00421120 (vtable +0x48): no resample, no rotation.
void MechAction::init(float, uint32_t) {
  stride = 0.0f;
  free_unused();
}

// Mech3.exe 1.2 @ 0x00421140
void MechAction::mirror_init(float speed, uint32_t frames) {
  zsis::Part* order[4] = {calves[0], calves[1], thighs[0], thighs[1]};
  for (zsis::Part* p : order)
    if (p) p->mirror(frames);
  if (calves[0] && calves[1]) zsis::swap_parts(&calves[0], &calves[1]);
  if (thighs[0] && thighs[1]) zsis::swap_parts(&thighs[0], &thighs[1]);
  MechAnim::mirror_init(speed, frames);
}

// Mech3.exe 1.2 @ 0x00420f40
void MechAction::reset() {
  t = 0.0f;
  done = false;
  reversed = false;
}

// Mech3.exe 1.2 @ 0x00421000
bool MechAction::started() const { return reversed || t > 0.0f; }

// Mech3.exe 1.2 @ 0x00421060
void MechAction::stop() {
  reversed = true;
  done = false;
}

// Mech3.exe 1.2 @ 0x00420d00 (0x00420df0): 15 frames a second.
MechAction* load_action(const char* name, uint32_t frames) {
  auto* a = new MechAction;
  a->wraps = true;
  a->active = load_motion(*a, name);
  if (!a->active) zerr::reportf(0x400, kScriptFile, 0x75, "Script (%s) not loaded", name);
  else a->set_fps(30.0f);
  a->bind_parts(name);
  a->bind_leg_parts();
  a->set_fps(15.0f);
  if (!a->active) {
    delete a;
    return nullptr;
  }
  a->init(0.0f, frames);
  return a;
}

}  // namespace mech3
