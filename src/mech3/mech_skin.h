#pragma once
#include <cstdint>
#include <list>
#include <vector>

namespace zrdr { struct Node; }
namespace zimg { struct Texture; struct TextureEntry; }

// mech3\mech_skin.cpp: a mech's camos, each a list of texture slots
// (docs/mechdef-skins.md).
namespace mech3 {

struct SkinEntry {                 // 0x1c bytes
  void* texture = nullptr;         // +0x00  0 until loaded
  uint8_t cached = 0;              // +0x04  texture came from the zImage cache
  char* name = nullptr;            // +0x08
  char* alt = nullptr;             // +0x0c  the same pointer as name when none was given
  SkinEntry* damage = nullptr;     // +0x10
  uint8_t b14 = 0;                 // +0x14
  int32_t refs = 1;                // +0x18
  SkinEntry(const char* name, const char* alt);  // 0x00473a30
  explicit SkinEntry(const SkinEntry& from);     // 0x00473bb0: no texture, +0x14 set, the damage link kept
  ~SkinEntry();                    // 0x00473ca0
  void unload();                   // 0x00473e10
  // 0x00473d40: the texture by name (or by the alternate name with use_alt or
  // for `vult_d03_c`): by_name a texture-table entry (found, else loaded in
  // group 3), else zImage's cached image (+0x04 set). True when it has one.
  bool load(bool by_name, bool use_alt);
  void link_damage(SkinEntry* damage);  // 0x00474740: +0x10, unless the two images' sizes differ
  void* image() const;             // the zimg::Texture: the cached one, or the table entry's
};
bool release(SkinEntry* entry);    // 0x00473c60, true when it was the last reference

struct Skin {                      // 0x18 bytes
  char* name = nullptr;            // +0x00
  std::vector<SkinEntry*> entries; // +0x08
  uint8_t loaded = 0;              // +0x14
  Skin(const char* name, uint32_t count);              // 0x00473e50
  void add_entry(const char* name, const char* alt);   // 0x00474000
  Skin* derive(const char* name, const char* suffix);  // 0x00474500
  void unload();                   // 0x004744a0
  ~Skin();                         // 0x00473f40
  bool load_textures(bool by_name, bool use_alt);     // 0x00474430: every entry, +0x14 set
  void push(SkinEntry* entry, bool copy);              // 0x00474210: a copy, else the entry with one more reference
  SkinEntry* find(const char* name, bool alt) const;   // 0x00474bd0: by name (+0x08) or alternate name (+0x0c)
  SkinEntry* find_texture(const void* entry) const;    // 0x00474cd0: by its texture-table entry (none for a cached one)
  int32_t index_of(const SkinEntry* entry) const;      // 0x00474b70: -1 when not there
  // 0x00475870 (ECX this, EDX the damage skin): a skin of the same name and
  // entries for one mech; with HOLES, an entry the damage skin has a match
  // for is copied (the holes get painted into the copy), the rest shared.
  Skin* clone(const Skin* damage) const;
  // 0x004748b0: the model tree's materials (each polygon's) get the soil
  // "mech" and this skin's textures by name; the first skin's entries take
  // the model's own textures when they have none.
  bool apply_first(void* node);
  // 0x004749e0: each textured material's texture, found in this skin by name
  // or alternate name, swapped for `to`'s entry of the same index.
  void apply(void* node, Skin* to) const;
};

struct SkinSet {                   // 0x14 bytes
  std::vector<Skin*> skins;        // +0x04  every skin but "damage", in file order
  Skin* damage = nullptr;          // +0x10
  explicit SkinSet(int32_t count); // 0x00474f60
  bool add(Skin* skin);            // 0x004752e0
  Skin* named(const char* name) const;  // 0x00475b00
  ~SkinSet();                      // 0x004751a0
  // 0x00475520: with HOLES, the damage skin loaded (cached images) once and
  // every skin's entries linked to it; the damage skin, else null.
  Skin* link_damage();
  void apply_first(void* node);                    // 0x00475780: the first skin on a model tree
  // 0x004757f0 (on the definition's skins): the mech's skin (+0x3e8) made
  // again from `skin` and swapped onto both its models (+0x694, +0x698).
  bool set_mech_skin(void** mech_skin, void* model, void* lod_model, Skin* skin);
  // 0x00475a80: skin `index`'s copy for one model tree, swapped onto it.
  Skin* mech_skin(int32_t index, void* node);
  // 0x00474d90 (ECX this, the definition's; the mech's skin +0x3e8, the hit's
  // material): a random bullet hole (0x006061ac) stamped from the damage
  // skin into the hit texture at the captured UV (zmod::cookie 0x00542730),
  // the device's copy refreshed ([0x0072f374]). docs/damage-decals.md.
  void damage_decal(const Skin* mech_skin, const void* material);
};

struct SkinGlobals {
  std::list<SkinEntry*> entries;   // 0x006061bc  every live entry
  int32_t entry_count = 0;         // 0x006061c0
  std::vector<void*> holes;        // 0x006061a8  the eight bullet-hole textures
  int32_t sets_built = 0;          // 0x006061c8
  uint8_t damage_mode = 0;         // 0x006061cc  set at mission load
  void* (*fetch_texture)(const char* name) = nullptr;  // 0x005179d0, zImage's cache (unset: zimg::cache_fetch)
  void (*release_cached)(void* texture) = nullptr;     // 0x00517b10 (unset: zimg::cache_release)
  void (*release_texture)(void* texture) = nullptr;    // 0x00518b30 (unset: zimg::entry_free)
  // The texture table (zimg; unset: its ports) and the soil names.
  void* (*find_texture)(const char* name) = nullptr;   // 0x00518220
  void* (*load_named)(const char* name, int32_t group, int32_t free_pixels) = nullptr;  // 0x00518d10
  int32_t (*soil_index)(const char* name) = nullptr;   // 0x00535f20
  // Test hook: the decal's cookie (unset: zmod::cookie, 0x00542730).
  bool (*cookie)(zimg::TextureEntry* target, const zimg::Texture* from, const zimg::Texture* hole) = nullptr;
};
SkinGlobals& skin_globals();

// The skins block of the mech definition reader (0x0046ba40, mech_reader.cpp,
// 0x0046be07..0x0046bfd8), until that reader is ported: MechDfn +0x1c.
SkinSet* read_skins(zrdr::Node* dfn);

}  // namespace mech3
