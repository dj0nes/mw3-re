#pragma once
#include <cstdint>

#include "gamez/zeffect/zeff_anim_init.h"

// The animations' puffers: the PUFFER_STATE event (type 42) the animation run
// applies (0x005035b0, between zdec_qsand.cpp and zeff_anim_run.c, most
// likely zeff_anim_run.c), and the def's puffer list it fills on demand
// (0x00509260, 0x00509520, zeff_anim_init.c). docs/puffers.md, format.
namespace zeff {

// The event as it sits in a sequence blob: the 12-byte header (type, size
// +4, time +8), then 0x244 bytes of data; offsets from the header.
struct PufferStateEvent {
  static constexpr int kSize = 0x250;
  static constexpr int kName = 0x0c;         // char[32], the puffer ref's name
  static constexpr int kIndex = 0x2c;        // i32: the ref, < 1 until looked up (cached here)
  static constexpr int kFlags = 0x30;        // u32, below
  static constexpr int kNode = 0x34;         // i32: node ref, -200 the def's +0x74
  static constexpr int kActive = 0x38;       // i32
  static constexpr int kTranslate = 0x3c;    // vec3; then +0x48 local velocity, +0x54 world
                                             // velocity, +0x60 / +0x6c random min / max, +0x78 accel
  // Pirate's Moon (0x0051bbf0) inserts the priority at +0xc0: the number is at +0xc4 and what
  // follows four bytes on (MW3: textures +0xc4, colours +0x1a0, growth +0x21c).
  static constexpr int kPriority = 0xc0;     // f32, flag 0x400000
  static constexpr int kNumber = 0xc4;       // i32
  static constexpr int kTextures = 0xc8;     // i32 timed, then 6 x {f32 time, char[32] name}
  static constexpr int kColors = 0x1a4;      // i32 count, then 6 x {time, r, g, b, a}
  static constexpr int kGrowth = 0x220;      // i32 count, then 6 x {time, growth}
};

// Flags at +0x30 and what each sets (the fields after +0x84 are f32).
enum PufferStateFlag : uint32_t {
  kPuffTranslate = 0x1,       // +0x3c
  kPuffAtNode = 0x2,          // +0x34, applied last
  kPuffActive = 0x4,          // +0x38
  kPuffLocalVelocity = 0x8,   // +0x48
  kPuffWorldVelocity = 0x10,  // +0x54
  kPuffMinRandom = 0x20,      // +0x60
  kPuffMaxRandom = 0x40,      // +0x6c
  kPuffIntervalType = 0x80,   // with 0x100 either one sets both: i32 +0x84 (1 distance), +0x88
  kPuffInterval = 0x100,
  kPuffSize = 0x200,          // +0x8c, +0x90
  kPuffLife = 0x400,          // +0x94, +0x98
  kPuffDeviation = 0x800,     // +0xa4
  kPuffFarFade = 0x1000,      // +0xb0, +0xb4
  kPuffGrowth = 0x2000,       // +0x21c
  kPuffTextures = 0x4000,     // +0xc4
  kPuffStartAge = 0x8000,     // +0x9c, +0xa0
  kPuffAccel = 0x10000,       // +0x78
  kPuffFriction = 0x20000,    // +0xb8
  kPuffColors = 0x40000,      // +0x1a0
  kPuffNearFade = 0x80000,    // +0xa8, +0xac
  kPuffWindFactor = 0x100000, // +0xbc
  kPuffNumber = 0x200000,     // +0xc4 in Pirate's Moon, an i32
  kPuffPriority = 0x400000,   // Pirate's Moon: +0xc0
};

// The ref named `name` among 1..count-1 with a puffer, else -1.
int32_t anim_find_puffer(const AnimDef& def, const char* name);  // 0x00509260
// Found, or a new puffer (anim_hooks().create_puffer) added to the list under
// that name; -1 when it cannot be made or the list holds 255.
int32_t anim_puffer_index(AnimDef& def, const char* name);       // 0x00509520
// ECX def, EDX the event. `def_node` is the def's +0x74 (set at run time; the
// node for node ref -200). Returns 2.
int32_t anim_puffer_state(AnimDef& def, uint8_t* event, Node* def_node);  // 0x005035b0

}  // namespace zeff
