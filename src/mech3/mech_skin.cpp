#include "mech3/mech_skin.h"
#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_hit.h"
#include "gamez/zmodel/gmod_matl.h"

#include <cstring>
#include <string>

#include "gamez/zclass/cls_node.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_msg.h"

namespace mech3 {

namespace {
SkinGlobals g_skins;

char* dup(const char* s) {
  char* d = new char[std::strlen(s) + 1];
  std::strcpy(d, s);
  return d;
}
}  // namespace

SkinGlobals& skin_globals() { return g_skins; }

// Mech3.exe 1.2 @ 0x00473a30. With no alternate the two names share one copy.
SkinEntry::SkinEntry(const char* n, const char* a) {
  if (n) name = dup(n);
  alt = a ? dup(a) : name;
  g_skins.entries.push_back(this);
  ++g_skins.entry_count;
}

// Mech3.exe 1.2 @ 0x00473ca0. Every list item naming this entry is dropped.
SkinEntry::~SkinEntry() {
  unload();
  if (alt && alt != name) delete[] alt;
  delete[] name;
  name = alt = nullptr;
  for (auto it = g_skins.entries.begin(); it != g_skins.entries.end();) {
    if (*it == this) {
      it = g_skins.entries.erase(it);
      --g_skins.entry_count;
    } else {
      ++it;
    }
  }
}

// Mech3.exe 1.2 @ 0x00473e10. Only while +0x14 is set.
void SkinEntry::unload() {
  if (!texture || !b14) return;
  void (*fn)(void*) = cached ? g_skins.release_cached : g_skins.release_texture;
  if (fn) fn(texture);
  else if (cached) zimg::cache_release(static_cast<zimg::Texture*>(texture));  // 0x00517b10
  else zimg::entry_free(*static_cast<zimg::TextureEntry*>(texture));         // 0x00518b30
  texture = nullptr;
}

// Mech3.exe 1.2 @ 0x00473c60
bool release(SkinEntry* e) {
  if (!e) return true;
  if (--e->refs != 0) return false;
  delete e;
  return true;
}

// Mech3.exe 1.2 @ 0x00473e50. No entries yet; `count` is only reserved.
Skin::Skin(const char* n, uint32_t count) : name(dup(n)) {
  entries.reserve(count);
}

// Mech3.exe 1.2 @ 0x00474000
void Skin::add_entry(const char* n, const char* a) { entries.push_back(new SkinEntry(n, a)); }

// Mech3.exe 1.2 @ 0x004744a0. The exe passes an argument the entries ignore.
void Skin::unload() {
  if (entries.empty() || !loaded) return;
  for (SkinEntry* e : entries) e->unload();
  loaded = 0;
}

// Mech3.exe 1.2 @ 0x00473f40
Skin::~Skin() {
  unload();
  for (SkinEntry*& e : entries) release(e), e = nullptr;
  entries.clear();
  delete[] name;
  name = nullptr;
}

// Mech3.exe 1.2 @ 0x00474500. Each base name plus the suffix (64-byte buffers
// in the exe, unchecked); an alternate only where the base has its own.
Skin* Skin::derive(const char* n, const char* suffix) {
  if (entries.empty()) return nullptr;
  Skin* s = new Skin(n, static_cast<uint32_t>(entries.size()));
  for (const SkinEntry* e : entries) {
    const std::string main = std::string(e->name) + suffix;
    if (e->alt != e->name) {
      s->add_entry(main.c_str(), (std::string(e->alt) + suffix).c_str());
    } else {
      s->add_entry(main.c_str(), nullptr);
    }
  }
  return s;
}

// Mech3.exe 1.2 @ 0x00474f60. The first set built in damage mode fetches the
// bullet-hole textures.
SkinSet::SkinSet(int32_t count) {
  if (count != 0) skins.reserve(count > 0 ? count : 0);
  if (g_skins.damage_mode && g_skins.holes.empty()) {
    static const char* const kHoles[] = {"hole_8x8",   "hole_8x8a",   "hole_8x8b",   "hole_8x8c",
                                         "hole_16x16", "hole_16x16a", "hole_16x16b", "hole_16x16c"};
    for (const char* h : kHoles)
      if (void* t = g_skins.fetch_texture ? g_skins.fetch_texture(h) : zimg::cache_fetch(h)) g_skins.holes.push_back(t);
  }
  ++g_skins.sets_built;
}

// Mech3.exe 1.2 @ 0x004751a0. The last set gone releases the bullet holes.
SkinSet::~SkinSet() {
  for (Skin*& s : skins) delete s, s = nullptr;
  skins.clear();
  delete damage;
  damage = nullptr;
  if (--g_skins.sets_built == 0) {
    for (void* t : g_skins.holes)
      if (g_skins.release_cached) g_skins.release_cached(t);
      else zimg::cache_release(static_cast<zimg::Texture*>(t));
    g_skins.holes.clear();
  }
}

// Mech3.exe 1.2 @ 0x00475b00: the first skin of that name.
Skin* SkinSet::named(const char* name) const {
  for (Skin* k : skins)
    if (k && k->name && name && std::strcmp(name, k->name) == 0) return k;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x004752e0. A second "damage" skin replaces the first.
bool SkinSet::add(Skin* skin) {
  if (!skin || !skin->name) return false;
  if (std::strcmp(skin->name, "damage") == 0) {
    damage = skin;
    return true;
  }
  skins.push_back(skin);
  return true;
}

// The first pair is the full skin whatever its name; every later one except
// "damage" is the first skin plus its suffix. Values are not type-checked.
SkinSet* read_skins(zrdr::Node* dfn) {
  zrdr::Node* found = zrdr::find(dfn, "skins");
  if (!found) return nullptr;
  zrdr::Node* list = found->list;
  const int32_t pairs = (list[0].i - 1) / 2;
  auto* set = new SkinSet(pairs);
  for (int32_t i = 0; i < pairs; ++i) {
    const char* name = zutl::localize(list[1 + 2 * i].s);
    zrdr::Node* items = list[2 + 2 * i].list;
    if (i == 0 || std::strcmp(name, "damage") == 0) {
      int32_t n = items[0].i - 1;
      int32_t first = 1;
      // FIX (bug): the Mad Cat's damage list has two zeros too many and the
      // Supernova's one, shifting every mask off its texture; leading empty
      // slots are dropped until the list is as long as the first skin's.
      if (zfix::options().bugs && i != 0 && !set->skins.empty()) {
        const auto want = static_cast<int32_t>(set->skins[0]->entries.size());
        while (n > want && items[first].type == zrdr::Int) ++first, --n;
      }
      Skin* skin = new Skin(name, static_cast<uint32_t>(n));
      set->add(skin);
      for (int32_t k = first; k < first + n; ++k) {
        const zrdr::Node& item = items[k];
        if (item.type == zrdr::Int) skin->add_entry(nullptr, nullptr);
        else if (item.type == zrdr::String) skin->add_entry(item.s, nullptr);
        else if (item.type == zrdr::List) skin->add_entry(item.list[1].s, item.list[2].s);
      }
    } else {
      // The exe calls derive on a null base when the set is empty (never in the data).
      Skin* base = set->skins.empty() ? nullptr : set->skins[0];
      if (Skin* skin = base ? base->derive(name, items[1].s) : nullptr) set->add(skin);
    }
  }
  return set;
}


// ---------------------------------------------------------------------------
// The skins on a mech's models (docs/mechdef-skins.md, The skins in play).

namespace {
zcls::Node* nd(void* n) { return static_cast<zcls::Node*>(n); }
void* find_texture(const char* n) { return g_skins.find_texture ? g_skins.find_texture(n) : zimg::find_texture(n); }
void* load_named(const char* n) {
  return g_skins.load_named ? g_skins.load_named(n, 3, 0) : zimg::load_named(n, 3, 0);
}
int32_t soil_of(const char* n) { return g_skins.soil_index ? g_skins.soil_index(n) : zmod::soil_index(n); }
// The polygons' materials of a node's model (0x24-byte polygons, +0x1c).
template <typename F>
void each_material(zcls::Node* n, F&& f) {
  zmod::Model* m = n->model;
  if (!m) return;
  for (uint32_t k = 0; k < m->count(0x10); ++k)
    if (zmod::Material* mat = m->polygons[k].material) f(*mat);
}
}  // namespace

// Mech3.exe 1.2 @ 0x00473bb0
SkinEntry::SkinEntry(const SkinEntry& from) {
  b14 = 1;
  name = dup(from.name);
  alt = from.name != from.alt ? dup(from.alt) : name;
  damage = from.damage;
  g_skins.entries.push_back(this);
  ++g_skins.entry_count;
}

// Mech3.exe 1.2 @ 0x00473d40
bool SkinEntry::load(bool by_name, bool use_alt) {
  if (texture) return true;
  if (!name) return false;
  const bool use = std::strcmp(name, "vult_d03_c") == 0 ? true : use_alt;  // 0x005e5790
  const char* n = use ? alt : name;
  if (!by_name) {
    texture = g_skins.fetch_texture ? g_skins.fetch_texture(n) : zimg::cache_fetch(n);  // 0x005179d0
    cached = 1;
    return texture != nullptr;
  }
  if (use) texture = find_texture(n);
  if (!texture) texture = load_named(n);
  return texture != nullptr;
}

void* SkinEntry::image() const {
  if (cached) return texture;
  return texture ? static_cast<zimg::TextureEntry*>(texture)->image : nullptr;
}

// Mech3.exe 1.2 @ 0x00474740. The images' width (+0x04) and height (+0x06)
// must match; a damage entry not loaded, or a mismatch, links nothing.
void SkinEntry::link_damage(SkinEntry* d) {
  if (!d || !d->name) return;
  if (!d->texture) {
    zerr::reportf(0x200, "C:\\zipper\\Mech3\\mech_skin.cpp", 0xde, "Failed to link damage to texture %s %s%s", name,
                 d->name, " not loaded");
    damage = nullptr;
    return;
  }
  if (const auto* a = static_cast<const zimg::Texture*>(image())) {
    const auto* b = static_cast<const zimg::Texture*>(d->image());
    if (a->width != b->width || a->height != b->height) {
      zerr::reportf(0x200, "C:\\zipper\\Mech3\\mech_skin.cpp", 0xed, "Damage Skin size error: skin %dx%d damage %dx%d",
                   a->width, a->height, b->width, b->height);
      damage = nullptr;
      return;
    }
  }
  damage = d;
}

// Mech3.exe 1.2 @ 0x00474430
bool Skin::load_textures(bool by_name, bool use_alt) {
  if (entries.empty()) return false;
  for (SkinEntry* e : entries) e->load(by_name, use_alt);
  loaded = 1;
  return true;
}

// Mech3.exe 1.2 @ 0x00474210
void Skin::push(SkinEntry* e, bool copy) {
  if (copy) e = new SkinEntry(*e);
  else ++e->refs;
  entries.push_back(e);
}

// Mech3.exe 1.2 @ 0x00474bd0
SkinEntry* Skin::find(const char* n, bool by_alt) const {
  for (SkinEntry* e : entries) {
    if (!e) continue;
    const char* k = by_alt ? e->alt : e->name;
    if (k && std::strcmp(k, n) == 0) return e;
  }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00474cd0
SkinEntry* Skin::find_texture(const void* entry) const {
  for (SkinEntry* e : entries)
    if (e && (e->cached ? nullptr : e->texture) == entry) return e;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00474b70
int32_t Skin::index_of(const SkinEntry* e) const {
  if (entries.empty() || !e) return -1;
  for (size_t k = 0; k < entries.size(); ++k)
    if (entries[k] == e) return static_cast<int32_t>(k);
  return -1;
}

// Mech3.exe 1.2 @ 0x00475870
Skin* Skin::clone(const Skin* d) const {
  if (entries.empty()) return nullptr;
  auto* out = new Skin(name, static_cast<uint32_t>(entries.size()));
  for (size_t k = 0; k < entries.size(); ++k) {
    const SkinEntry* de = d && k < d->entries.size() ? d->entries[k] : nullptr;
    out->push(entries[k], g_skins.damage_mode && de && de->name);
  }
  return out;
}

// Mech3.exe 1.2 @ 0x004748b0
bool Skin::apply_first(void* node) {
  zcls::Node* n = nd(node);
  if (!n) return false;
  const bool holes = g_skins.damage_mode != 0;
  each_material(n, [&](zmod::Material& mat) {
    mat.soil = static_cast<uint32_t>(soil_of("mech"));  // 0x005e5840
    zimg::TextureEntry* t = mat.texture;
    SkinEntry* e = t ? find(t->name, false) : nullptr;
    if (!e) return;
    if (!e->texture) {
      if (!holes && e->name != e->alt) {
        e->load(true, true);
        mat.texture = e->cached ? nullptr : static_cast<zimg::TextureEntry*>(e->texture);
      } else {
        e->texture = t;
      }
    }
    if (holes && e->damage) mat.flags |= 2;
  });
  for (int32_t k = 0; k < n->child_count(); ++k) apply_first(n->children[k]);
  return true;
}

// Mech3.exe 1.2 @ 0x004749e0
void Skin::apply(void* node, Skin* to) const {
  zcls::Node* n = nd(node);
  if (!n) return;
  const bool holes = g_skins.damage_mode != 0;
  each_material(n, [&](zmod::Material& mat) {
    if (!(mat.flags & 1) || !mat.texture) return;
    SkinEntry* e = find(mat.texture->name, false);
    if (!e) e = find(mat.texture->name, true);
    if (!e) return;
    const int32_t k = index_of(e);
    SkinEntry* target = k >= 0 && k < static_cast<int32_t>(to->entries.size()) ? to->entries[k] : nullptr;
    if (!target) return;
    if (!(target->cached ? nullptr : target->texture)) target->load(true, !holes);
    mat.texture = target->cached ? nullptr : static_cast<zimg::TextureEntry*>(target->texture);
  });
  for (int32_t k = 0; k < n->child_count(); ++k) apply(n->children[k], to);
}

// Mech3.exe 1.2 @ 0x00475520
Skin* SkinSet::link_damage() {
  if (!damage) return nullptr;
  if (!g_skins.damage_mode) return nullptr;
  if (!damage->loaded) {
    damage->load_textures(false, false);
    for (Skin* s : skins)
      for (size_t j = 0; j < s->entries.size(); ++j)
        s->entries[j]->link_damage(j < damage->entries.size() ? damage->entries[j] : nullptr);
  }
  return damage;
}

// Mech3.exe 1.2 @ 0x00475780
void SkinSet::apply_first(void* node) {
  Skin* first = skins.empty() ? nullptr : skins[0];
  link_damage();
  if (first) first->apply_first(node);
}

// Mech3.exe 1.2 @ 0x004757f0
bool SkinSet::set_mech_skin(void** slot, void* model, void* lod_model, Skin* skin) {
  auto* old = slot ? static_cast<Skin*>(*slot) : nullptr;
  if (!old || !skin) return false;
  Skin* made = skin->clone(link_damage());
  if (!made) return false;
  old->apply(model, made);
  old->apply(lod_model, made);
  *slot = made;
  delete old;  // 0x00473f40, then the free
  return true;
}

// Mech3.exe 1.2 @ 0x00475a80
Skin* SkinSet::mech_skin(int32_t index, void* node) {
  Skin* s = index >= 0 && index < static_cast<int32_t>(skins.size()) ? skins[index] : nullptr;
  Skin* made = s ? s->clone(link_damage()) : nullptr;
  if (s && made) s->apply(node, made);
  return made;
}

// Mech3.exe 1.2 @ 0x00474d90
void SkinSet::damage_decal(const Skin* skin, const void* material) {
  if (!skin || !material) return;
  if (!link_damage()) return;  // 0x00475520
  std::vector<void*>& holes = g_skins.holes;
  if (holes.empty()) return;
  const auto* m = static_cast<const zmod::Material*>(material);
  SkinEntry* e = skin->find_texture(m->texture);
  if (!e || !e->damage || !e->damage->texture) return;
  const int32_t n = static_cast<int32_t>(holes.size());
  const int32_t pick = crt::msvc_ftol(static_cast<float>(crt::msvc_rand()) * static_cast<float>(n - 1) * 3.0518509e-05f);  // 0x005e5848
  auto* hole = static_cast<const zimg::Texture*>(holes[static_cast<size_t>(pick)]);
  auto* target = e->cached ? nullptr : static_cast<zimg::TextureEntry*>(e->texture);
  const auto* from = static_cast<const zimg::Texture*>(e->damage->image());
  if (!(g_skins.cookie ? g_skins.cookie(target, from, hole) : zmod::cookie(target, from, hole))) return;
  if (target && target->surface && zimg::texture_hooks().reload)
    zimg::texture_hooks().reload(target->surface, static_cast<zimg::Texture*>(target->image));
}

}  // namespace mech3
