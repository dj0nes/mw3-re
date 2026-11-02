#pragma once
#include <cstdint>

#include "gamez/zsiscript/si_script.h"
#include "mech3/mech_parts.h"

// A mech's animations (docs/movement.md, The animation objects; sampling in
// docs/motion.md). Original file: mech3\mech_anim.cpp. MechAnim (vtable
// 0x005bd1f0, 0xdc bytes) is a zSIScript motion with the mech's parts looked
// up by name; the blends (vtable 0x005bd240, 0xe8) mix two of them, the
// one-shot leg actions (0x005bd290, 0xf0) play once at 15 frames a second.
// Virtuals in vtable order (offsets in the comments).
namespace mech3 {

class MechAnim : public zsis::Script {
 public:
  float t = 0.0f;                     // +0x88  time, before the rate
  float stride_speed = 0.0f;          // +0x8c
  zsis::Part* feet[2] = {};           // +0x90  lfoot, rfoot
  zsis::Part* hip_part = nullptr;     // +0x98
  zsis::Part* torso_part = nullptr;   // +0x9c
  zsis::Part* arms[2] = {};           // +0xa0  larm, rarm
  zsis::Part* hands[2] = {};          // +0xa8  lhand, rhand
  zsis::Part* toes[2][4] = {};        // +0xb0  ltoe01..04, rtoe01..04
  float events[3] = {};               // +0xd0  left and right footfall, a quarter cycle (footfalls 0x0041fd30)

  MechAnim();                         // 0x0041f100 (0x0041ed10, 0x0041f800): an empty script, active
  virtual ~MechAnim();                // +0x00
  virtual void set_speed(float speed);    // +0x08, 0x0041ee80
  virtual void set_time(float v) { t = v; }  // +0x0c, 0x0041eee0
  virtual void advance(float dt);         // +0x10, 0x0041ef60
  virtual void set_blend(float) {}        // +0x14, 0x004a5bc0 (blends only)
  virtual void hip_frame(const float* m);              // +0x18, 0x0041fc10
  virtual void ski_frame(int leg, const float* m);     // +0x1c, 0x0041fbe0: a foot sampled in its ski's frame
  virtual void velocity(Vec3* v);                      // +0x20, 0x0041f8d0: the hip's speed into v->z
  virtual void foot(int leg, float pos[3], float q[4]);        // +0x24, 0x0041f930
  virtual bool arm(int side, float* pos, float* q);            // +0x28, 0x0041f9d0; false without the part
  virtual bool hand(int side, float* pos, float* q);           // +0x2c, 0x0041fa40
  virtual void hip(float pos[3], float q[4]);                  // +0x30, 0x0041f980
  virtual void torso(float pos[3], float q[4]);                // +0x34, 0x0041fae0
  virtual void torso_rotation(float q[4]);                     // +0x38, 0x0041fab0
  virtual bool toe(int leg, int toe, float q[4]);              // +0x3c, 0x0041fb70
  virtual void mirror_init(float stride_speed, uint32_t frames);  // +0x40, 0x0041f680: the other leg's motion
  virtual void seek(int event);                                // +0x44, 0x0041fce0
  virtual void init(float stride_speed, uint32_t frames);      // +0x48, 0x0041f580

  // The loading constructor 0x0041f340 after its script is read: the parts
  // by name; without both feet, both arms... the first four, it reports and
  // deactivates. The copy constructor 0x0041f180 shares another's buffers.
  void bind_parts(const char* motion_name);
  void find_parts();                      // the lookups alone (0x0041f180 copies without the check)
  void copy_from(const MechAnim& from);   // 0x0041f180 (with 0x0041edd0)
  void copy_phase(const MechAnim& from);  // 0x0041ef00
  void set_stride_speed(float s);         // 0x0041ef90
  bool start_at_left_forward();           // 0x0041fc40: rotates the keys
  float footfalls();                      // 0x0041fd30: events from the feet; returns the best mismatch
};

// The loading factory 0x0041f000 (and 0x00420d00 for the one-shots): the
// motion `name` from the motion archive at 30 frames a second, its parts
// bound, then init(stride_speed, frames). Null when it fails to load.
MechAnim* load_anim(const char* name, float stride_speed, uint32_t frames);

// Two animations mixed (vtable 0x005bd240, 0xe8 bytes; 0x0041ffc0): the
// blend's own timing samples both, its loop time between theirs by the
// weight. Made by 0x0041ff30 (null when either is missing).
class BlendAnim : public MechAnim {
 public:
  float weight = 0.0f;       // +0xdc
  MechAnim* a = nullptr;     // +0xe0
  MechAnim* b = nullptr;     // +0xe4

  BlendAnim(MechAnim* a, MechAnim* b);  // 0x0041ffc0
  void set_speed(float speed) override;   // 0x00420cb0
  void advance(float dt) override;        // 0x00420260
  void set_blend(float w) override;       // 0x004200c0
  void hip_frame(const float* m) override;            // 0x00420370
  void ski_frame(int leg, const float* m) override;   // 0x00420320
  void velocity(Vec3* v) override;                    // 0x004203c0
  void foot(int leg, float pos[3], float q[4]) override;   // 0x00420490
  bool arm(int side, float* pos, float* q) override;       // 0x004205d0
  bool hand(int side, float* pos, float* q) override;      // 0x004207b0
  void hip(float pos[3], float q[4]) override;             // 0x00420af0
  void torso_rotation(float q[4]) override;                // 0x00420bf0
  bool toe(int leg, int toe, float q[4]) override;         // 0x00420990
  void seek(int event) override;                           // 0x004202c0
  void scale_rates(bool keep_phase);                       // 0x004201b0
};
BlendAnim* make_blend(MechAnim* a, MechAnim* b);  // 0x0041ff30

// A one-shot leg action (vtable 0x005bd290, 0xf0 bytes): fallf, fallb,
// landf, landb, getupf, getupb, collapsel, collapser (docs/movement-jump.md).
// No wrap: it plays from the first key to the last and stops.
class MechAction : public MechAnim {
 public:
  zsis::Part* thighs[2] = {};  // +0xdc  lthigh, rthigh (0x00420ec0)
  zsis::Part* calves[2] = {};  // +0xe4  lcalf, rcalf
  bool done = false;      // +0xec
  bool reversed = false;  // +0xed  running backwards (Stop)

  MechAction();           // test and blend use; the loader is load_action
  void advance(float dt) override;  // +0x10, 0x00420f80: clamps to 0..loop and sets done
  void mirror_init(float stride_speed, uint32_t frames) override;  // +0x40, 0x00421140
  void init(float stride_speed, uint32_t frames) override;         // +0x48, 0x00421120
  virtual void calf(int leg, float pos[3], float q[4]);   // 0x00421080 (not in the vtable; virtual for tests)
  virtual void thigh(int leg, float pos[3], float q[4]);  // 0x004210d0
  void bind_leg_parts();            // 0x00420ec0
  void reset();                     // 0x00420f40
  bool started() const;             // 0x00421000
  void stop();                      // 0x00421060
};
MechAction* load_action(const char* name, uint32_t frames);  // 0x00420d00 (0x00420df0)

// The motion archive (motion.zbd, 0x00736588), opened at mission load.
bool open_motions(const char* path);   // 0x005a9820
void close_motions();                  // 0x005a98a0
// 0x005a9ad0: the script `name` from the archive. The developers' loose
// <name>.zan override (0x005a98d0, 0x005a9e10) is not ported.
bool load_motion(zsis::Script& s, const char* name);
// Ours (docs/modding.md, M3): load_motion found `name` broken in a copy and
// whole in none, as against missing from every copy (the shipped data's
// elemental_limp: the exe ran with that gait null).
bool motion_broken(const char* name);
void script_free_parts(zsis::Script& s);  // 0x005a9dc0, the script's destructor

}  // namespace mech3
