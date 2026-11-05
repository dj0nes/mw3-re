#include "gamez/zeffect/zeff_anim_puffer.h"

#include <cstdio>
#include <cstring>

#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_puff.h"

namespace zeff {

namespace {
const char kInitFile[] = "C:\\zipper\\GameZMech3\\zEffect\\zeff_anim_init.c";

template <typename T>
T get(const uint8_t* p, int off) {
  T v;
  std::memcpy(&v, p + off, sizeof v);
  return v;
}
const float* vec(const uint8_t* p, int off) { return reinterpret_cast<const float*>(p + off); }
}  // namespace

// Mech3.exe 1.2 @ 0x00509260. Ref 0 is the list's unused first entry.
int32_t anim_find_puffer(const AnimDef& def, const char* name) {
  const uint8_t count = def.count(0x10c);
  if (count < 2) return -1;
  for (int32_t i = 1; i < count; ++i) {
    const CreatedRef& r = def.puffers[i];
    if (r.made && std::strcmp(reinterpret_cast<const char*>(r.raw), name) == 0) return i;
  }
  return -1;
}

// Mech3.exe 1.2 @ 0x00509520. The puffer is made before the overflow check,
// so a full list leaks one. The list grows by realloc in the exe.
int32_t anim_puffer_index(AnimDef& def, const char* name) {
  const int32_t found = anim_find_puffer(def, name);
  if (found > 0) return found;
  void* made = anim_hooks().create_puffer ? anim_hooks().create_puffer() : nullptr;
  if (!made) return -1;
  if (!def.puffers) {
    def.puffers = new CreatedRef[1]{};
    def.raw[0x10c] = static_cast<uint8_t>(def.raw[0x10c] + 1);
  }
  const uint8_t count = def.count(0x10c);
  if (count == 0xff) {
    char msg[96];
    std::snprintf(msg, sizeof msg, "Puffer list overflow.\n  Animation: %s\n", def.name());
    zerr::report(0x400, kInitFile, 0x290, msg);
    return -1;
  }
  auto* grown = new CreatedRef[count + 1]{};
  for (int i = 0; i < count; ++i) grown[i] = def.puffers[i];
  delete[] def.puffers;
  def.puffers = grown;
  CreatedRef& r = grown[count];
  std::strncpy(reinterpret_cast<char*>(r.raw), name, 0x23);
  r.made = made;
  def.raw[0x10c] = static_cast<uint8_t>(count + 1);
  return count;
}

// Mech3.exe 1.2 @ 0x005035b0 (ECX def, EDX event). Each flag calls one setter
// (docs/puffers.md has the table); the node is set last.
// Pirate's Moon 1.0 @ 0x0051bbf0: nothing when the def has no puffers (PM makes them at load), the
// priority, and node ref -200 moves the translation by the def's start offset (+0x78 here).
int32_t anim_puffer_state(AnimDef& def, uint8_t* ev, Node* def_node) {
  using E = PufferStateEvent;
  if (def.count(0x10c) == 0) return 2;
  int32_t index = get<int32_t>(ev, E::kIndex);
  if (index < 1) {
    index = anim_puffer_index(def, reinterpret_cast<const char*>(ev + E::kName));
    std::memcpy(ev + E::kIndex, &index, 4);
    if (index < 1) return 2;
  }
  auto* p = static_cast<zmod::Puffer*>(def.puffers[index].made);
  const uint32_t f = get<uint32_t>(ev, E::kFlags);
  if (f & kPuffActive) zmod::puffer_set_active(p, get<int32_t>(ev, E::kActive));
  if (f & kPuffLocalVelocity) zmod::puffer_set_local_velocity(p, vec(ev, 0x48));
  if (f & kPuffWorldVelocity) zmod::puffer_set_world_velocity(p, vec(ev, 0x54));
  if (f & kPuffMinRandom) zmod::puffer_set_min_random(p, vec(ev, 0x60));
  if (f & kPuffMaxRandom) zmod::puffer_set_max_random(p, vec(ev, 0x6c));
  if (f & kPuffAccel) zmod::puffer_set_accel(p, vec(ev, 0x78));
  if (f & (kPuffIntervalType | kPuffInterval)) {
    zmod::puffer_set_by_distance(p, get<int32_t>(ev, 0x84));
    zmod::puffer_set_interval(p, get<float>(ev, 0x88));
  }
  if (f & kPuffSize) zmod::puffer_set_size_min(p, get<float>(ev, 0x8c)), zmod::puffer_set_size_max(p, get<float>(ev, 0x90));
  if (f & kPuffLife) zmod::puffer_set_life_min(p, get<float>(ev, 0x94)), zmod::puffer_set_life_max(p, get<float>(ev, 0x98));
  if (f & kPuffStartAge) zmod::puffer_set_age_min(p, get<float>(ev, 0x9c)), zmod::puffer_set_age_max(p, get<float>(ev, 0xa0));
  if (f & kPuffDeviation) zmod::puffer_set_deviation(p, get<float>(ev, 0xa4));
  if (f & kPuffNearFade) zmod::puffer_set_near_fade(p, get<float>(ev, 0xa8), get<float>(ev, 0xac));
  if (f & kPuffFarFade) zmod::puffer_set_far_fade(p, get<float>(ev, 0xb0), get<float>(ev, 0xb4));
  if (f & kPuffFriction) zmod::puffer_set_friction(p, get<float>(ev, 0xb8));
  if (f & kPuffWindFactor) zmod::puffer_set_wind_factor(p, get<float>(ev, 0xbc));
  if (f & kPuffPriority) zmod::puffer_set_priority(p, get<float>(ev, E::kPriority));
  if (f & kPuffNumber) zmod::puffer_set_number(p, get<int32_t>(ev, E::kNumber));
  if (f & kPuffTextures) {
    zmod::puffer_clear_textures(p);
    const bool timed = get<int32_t>(ev, E::kTextures) != 0;
    for (int i = 0; i < 6; ++i) {
      const int off = E::kTextures + 8 + 0x24 * i;
      const char* name = reinterpret_cast<const char*>(ev + off);
      if (!*name) break;
      zimg::TextureEntry* t = zimg::find_texture(name);
      if (!t) t = zimg::default_entry();
      if (timed)
        zmod::puffer_add_timed_texture(p, t, get<float>(ev, off - 4));
      else
        zmod::puffer_add_texture(p, t);
    }
  }
  // FIX (bug): the exe reads as many keys as the count says, past the six
  // slots into whatever follows; the shipped counts are 1..5 (docs/fixes.md).
  if (f & kPuffColors) {
    zmod::puffer_clear_colors(p);
    const int32_t n = get<int32_t>(ev, E::kColors);
    for (int32_t i = 0; i < n && i < 6; ++i) {
      const float* c = vec(ev, E::kColors + 4 + 0x14 * i);
      zmod::puffer_add_color(p, c[0], c[1], c[2], c[3], c[4]);
    }
  }
  if (f & kPuffGrowth) {  // the exe runs this block twice; the second changes nothing
    zmod::puffer_clear_growth(p);
    const int32_t n = get<int32_t>(ev, E::kGrowth);
    for (int32_t i = 0; i < n && i < 6; ++i) {
      const float* g = vec(ev, E::kGrowth + 4 + 8 * i);
      zmod::puffer_add_growth(p, g[0], g[1]);
    }
  }
  float translate[3] = {0.0f, 0.0f, 0.0f};
  bool moved = (f & kPuffTranslate) != 0;
  if (moved) std::memcpy(translate, vec(ev, E::kTranslate), 12);
  if (f & kPuffAtNode) {
    const int32_t ni = get<int32_t>(ev, E::kNode);
    Node* n = ni > 0 ? def.nodes[ni].node : nullptr;
    if (!n && ni == -200) {
      n = def_node;
      for (int i = 0; i < 3; ++i) translate[i] += def.get<float>(0x78 + 4 * i);
      moved = true;
    }
    zmod::puffer_set_node(p, n);
  }
  if (moved) zmod::puffer_set_translate(p, translate);  // after the node, in Pirate's Moon
  return 2;
}

}  // namespace zeff
