#include "gamez/zmodel/gmod_matl.h"

#include <cstdlib>
#include <cctype>
#include <cstring>

#include "gamez/zerror/zerr.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zvideo/zvid_format.h"

namespace zmod {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zModel\\gmod_matl.c";
constexpr int kBlock = 1000;  // 0x005edd50
MaterialTable g_table;

bool read4(std::FILE* f, void* out) { return std::fread(out, 4, 1, f) == 1; }

template <typename T>
T at(const uint8_t* p, int off) {
  T v;
  std::memcpy(&v, p + off, sizeof v);
  return v;
}
}  // namespace

MaterialTable& materials() { return g_table; }

int32_t MaterialTable::index_of(const Material* m) const {
  for (size_t b = 0; m && b < blocks.size(); ++b)
    if (m >= blocks[b].get() && m < blocks[b].get() + kBlock)
      return static_cast<int32_t>(b) * kBlock + static_cast<int32_t>(m - blocks[b].get());
  return -1;
}

// Mech3.exe 1.2 @ 0x00536070. FIX (A): one past the table is no material
// (the exe had no upper bound; a peer's crater can name any index, fixes.md).
Material* take_material(int32_t index) {
  const MaterialTable& t = g_table;
  if (index < 0 || index >= t.capacity || index / kMaterialBlock >= static_cast<int32_t>(t.blocks.size())) return nullptr;
  Material& m = g_table.at(index);
  m.flags |= 0x10;
  return &m;
}

namespace {
// The shared tail of 0x00536ad0 and 0x00537830: a full table grows by a block
// whose slots all go on the free list; the free head moves to the front of
// the used list, takes m's first 0x24 bytes and is referenced; a cycle is
// copied with its own texture list.
Material* new_material(const Material& m) {
  MaterialTable& t = g_table;
  if (t.free_head < 0) {
    t.blocks.emplace_back(new Material[kBlock]{});
    const int32_t first = t.capacity;
    t.free_head = first;
    t.capacity += kBlock;
    for (int32_t i = first; i < t.capacity; ++i) {
      Material& s = t.at(i);
      s.prev = static_cast<int16_t>(i == first ? -1 : i - 1);
      s.next = static_cast<int16_t>(i == t.capacity - 1 ? -1 : i + 1);
      s.flags |= 0x20;
    }
  }
  const int32_t index = t.free_head;
  Material& e = t.at(index);
  const int16_t prev = e.prev, next = e.next;
  t.free_head = next;
  if (prev >= 0) t.at(prev).next = next;
  if (t.free_head >= 0) t.at(t.free_head).prev = prev;
  e.prev = -1;
  e.next = static_cast<int16_t>(t.used_head);
  e.flags &= ~0x20;
  if (t.used_head >= 0) t.at(t.used_head).prev = static_cast<int16_t>(index);
  ++t.count;
  t.used_head = index;
  e.alpha = m.alpha, e.flags = m.flags, e.rgb = m.rgb;
  e.r = m.r, e.g = m.g, e.b = m.b;
  e.texture = m.texture, e.texture_index = m.texture_index;
  std::memcpy(e.unk_14, m.unk_14, sizeof e.unk_14);
  e.soil = m.soil;
  e.file_cycle = 0;  // +0x24 is the new cycle or 0
  e.flags |= 0x10;
  e.cycle = nullptr;
  if (m.flags & 4) {
    auto* c = static_cast<Cycle*>(std::malloc(sizeof(Cycle)));
    *c = *m.cycle;
    c->textures = static_cast<zimg::TextureEntry**>(std::calloc(c->texture_count, sizeof(void*)));
    std::memcpy(c->textures, m.cycle->textures, c->texture_count * sizeof(void*));
    e.cycle = c;
  }
  return &e;
}

// The exe compares +0x10 as one dword: the texture once resolved, the file's
// index for a coloured material.
bool same_texture(const Material& a, const Material& b) {
  if (a.texture || b.texture) return a.texture == b.texture;
  return a.texture_index == b.texture_index;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00536f10. The first 0x20 bytes with flag bits 0x10..0x80
// ignored, then the soils.
int material_differs(Material& entry, Material& m) {
  if (!same_texture(entry, m)) return 1;
  const bool same = entry.alpha == m.alpha && (entry.flags & 0x0f) == (m.flags & 0x0f) && entry.rgb == m.rgb &&
                    std::memcmp(&entry.r, &m.r, 4) == 0 && std::memcmp(&entry.g, &m.g, 4) == 0 &&
                    std::memcmp(&entry.b, &m.b, 4) == 0 && std::memcmp(entry.unk_14, m.unk_14, 12) == 0;
  if (!same) return 1;
  if (entry.soil == m.soil) return 0;
  if (entry.soil == 0) return entry.soil = m.soil, 0;
  if (m.soil == 0) return m.soil = entry.soil, 0;
  return 1;
}

// Mech3.exe 1.2 @ 0x00536ab0
int material_changes(const Material& m) {
  // +0x24: a cycle, or the file's stale pointer on a world material that has none.
  return (m.flags & 6) != 0 || m.cycle != nullptr || m.file_cycle != 0 ? 1 : 0;
}

// Mech3.exe 1.2 @ 0x00536ad0
Material* material_add(Material& m) {
  MaterialTable& t = g_table;
  if (t.last && !(t.last->flags & 0x20) && material_differs(*t.last, m) == 0) {
    t.last->flags |= 0x10;
    return t.last;
  }
  for (int32_t i = t.used_head; i >= 0; i = t.at(i).next) {
    Material& e = t.at(i);
    if (material_differs(e, m) == 0) {
      e.flags |= 0x10;
      return &e;
    }
  }
  t.last = new_material(m);
  return t.last;
}

Material& default_material() {
  static Material m{};
  return m;
}

// Mech3.exe 1.2 @ 0x00536a70. The exe keeps flag bits 3..7 of whatever the
// material held (callers pass an uninitialised one on the stack); 0 here.
void material_init(Material& m) {
  m.cycle = nullptr;
  m.texture = nullptr;
  m.alpha = 0xff;
  m.flags &= 0xf8;
  m.r = m.g = m.b = 255.0f;
  m.unk_14[0] = 0, m.unk_14[1] = 0.5f, m.unk_14[2] = 0.5f;
  m.rgb = 0x7fff;
  m.soil = 0;
}

// Mech3.exe 1.2 @ 0x005375c0
int material_set_soil(Material* m, uint32_t soil) {
  if (!m) return 0;
  m->soil = soil;
  return 1;
}

// Mech3.exe 1.2 @ 0x005375d0
int cycle_create(Material& m, int32_t count) {
  if (&m == &default_material()) return 0;
  m.flags |= 5;
  if (m.cycle && count <= static_cast<int32_t>(m.cycle->texture_count)) return 0;
  // The exe reallocs the cycle and drops its old texture list. It sets every
  // field but the frame (+0x04): a cycle made again keeps it (a new one gets
  // realloc's leftovers; here 0), so it steps once a frame all the same.
  const bool fresh = m.cycle == nullptr;
  auto* c = static_cast<Cycle*>(std::realloc(m.cycle, sizeof(Cycle)));
  const uint32_t frame = fresh ? 0 : c->frame;
  *c = Cycle{};
  c->frame = frame;
  c->speed = 15.0f;
  c->texture_count = static_cast<uint32_t>(count);
  c->textures = static_cast<zimg::TextureEntry**>(std::malloc(count > 0 ? count * sizeof(void*) : 1));
  for (int32_t i = 0; i < count; ++i) c->textures[i] = zimg::default_entry();
  m.cycle = c;
  return 1;
}

// Mech3.exe 1.2 @ 0x00537680
int cycle_add(Material& m, zimg::TextureEntry* t) {
  if (!(m.flags & 4)) return 0;
  Cycle* c = m.cycle;
  if (!c->textures || c->added >= c->texture_count) return 0;
  c->textures[c->added++] = t;
  return 1;
}

// Mech3.exe 1.2 @ 0x005377a0
int cycle_set_loop(Material* m, uint32_t v) {
  if (!m) return 0;
  if ((m->flags & 4) && m->cycle) return m->cycle->looping = v, 1;
  zerr::report(0x200, kFile, 0x727, "SetCycleTextureLoop:  Texture not cycled");
  return 0;
}

// Mech3.exe 1.2 @ 0x005377e0
int cycle_set_speed(Material* m, float v) {
  if (!m) return 0;
  if ((m->flags & 4) && m->cycle) return m->cycle->speed = v, 1;
  zerr::report(0x200, kFile, 0x73b, "SetCycleTextureSpeed:  Texture not cycled");
  return 0;
}

// Mech3.exe 1.2 @ 0x00537be0
Material* material_find_texture(zimg::TextureEntry* t) {
  if (!t) return nullptr;
  MaterialTable& tb = g_table;
  for (int32_t i = tb.used_head; i >= 0; i = tb.at(i).next) {
    Material& m = tb.at(i);
    if (m.texture == t) return m.flags |= 0x10, &m;
  }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00537830. The exe writes through null for a null
// material; every caller passes one.
Material* material_copy(const Material& m) { return new_material(m); }

// Mech3.exe 1.2 @ 0x00537070: a material back on the free list: its cycle
// freed, everything but the links cleared, then 0x10 (as the exe) and 0x20,
// unlinked from the used list to the free list's front. The free head
// itself is left as it is.
void material_free(Material* m) {
  if (!m) return;
  MaterialTable& t = g_table;
  if ((m->flags & 4) && m->cycle) {
    std::free(m->cycle->textures);
    std::free(m->cycle);
  }
  const int16_t prev = m->prev, next = m->next;
  *m = Material{};
  m->prev = prev, m->next = next;
  m->flags |= 0x10;
  const int32_t index = t.index_of(m);
  if (index < 0 || index == t.free_head) return;
  if (prev >= 0) t.at(prev).next = next;
  if (next >= 0) t.at(next).prev = prev;
  if (t.used_head == index) t.used_head = next;
  m->prev = -1;
  m->next = static_cast<int16_t>(t.free_head);
  if (t.free_head >= 0) t.at(t.free_head).prev = static_cast<int16_t>(index);
  m->flags |= 0x20;
  --t.count;
  t.free_head = index;
}

// Mech3.exe 1.2 @ 0x00537e50: every material a model's polygon uses marked
// (0x40), then each one with none of 0x80, 0x40, 0x20 (free), 0x08 freed and
// the others' two top bits cleared. It also resets [0x005edd5c], the cache
// of 0x00537f20 (not ported). Answers how many it freed.
int32_t materials_purge() {
  for (Model& md : models().slots)
    for (uint32_t i = 0; md.polygons && i < md.count(0x10); ++i)
      if (Material* m = md.polygons[i].material) m->flags = static_cast<uint8_t>((m->flags & 0x3f) | 0x40);
  MaterialTable& t = g_table;
  int32_t freed = 0;
  for (int32_t i = 0; i < t.capacity; ++i) {
    Material& m = t.at(i);
    if ((m.flags & 0xe8) == 0) {
      material_free(&m);
      ++freed;
    } else {
      m.flags &= 0x3f;
    }
  }
  return freed;
}

// Mech3.exe 1.2 @ 0x005363d0. FIX (A): owned blocks (MaterialTable); the
// capacity is rounded up to whole blocks.
int read_materials(std::FILE* f) {
  MaterialTable& t = g_table;
  int32_t size, count, free_head, used_head;
  if (!read4(f, &size) || !read4(f, &count) || !read4(f, &free_head) || !read4(f, &used_head)) {
    zerr::report(0x200, kFile, 0, "Error reading GameZ Material buffer data.");
    return -1;
  }
  t.capacity = size, t.count = count, t.free_head = free_head, t.used_head = used_head;
  if (size == 0) return 0;
  const int blocks = (size + kBlock - 1) / kBlock;
  t.blocks.clear();
  for (int b = 0; b < blocks; ++b) t.blocks.emplace_back(new Material[kBlock]{});
  for (int i = 0; i < size; ++i) {
    uint8_t raw[0x2c];
    if (std::fread(raw, 0x2c, 1, f) != 1) return -1;
    Material& m = t.at(i);
    m.alpha = raw[0], m.flags = raw[1], m.rgb = at<uint16_t>(raw, 2);
    m.r = at<float>(raw, 4), m.g = at<float>(raw, 8), m.b = at<float>(raw, 0xc);
    m.texture_index = at<uint32_t>(raw, 0x10);
    for (int k = 0; k < 3; ++k) m.unk_14[k] = at<float>(raw, 0x14 + 4 * k);
    m.soil = at<uint32_t>(raw, 0x20);
    m.file_cycle = at<uint32_t>(raw, 0x24);
    m.prev = at<int16_t>(raw, 0x28), m.next = at<int16_t>(raw, 0x2a);
  }
  // The rest of a partial last block goes on the free list (never in the data).
  for (int i = blocks * kBlock - 1; i >= size; --i) {
    Material& m = t.at(i);
    m.flags = 0x20;
    m.next = static_cast<int16_t>(t.free_head);
    m.prev = -1;
    if (t.free_head >= 0) t.at(t.free_head).prev = static_cast<int16_t>(i);
    t.free_head = i;
  }
  t.capacity = blocks * kBlock;
  for (int i = t.used_head; i != -1; i = t.at(i).next) {
    Material& m = t.at(i);
    if (m.flags & 1) {
      m.texture = zimg::texture_at(static_cast<int32_t>(m.texture_index));
    } else {
      const float rgb[3] = {m.r, m.g, m.b};
      m.rgb = zvid::pack_rgb(rgb);
    }
    if (m.flags & 4) {
      auto* c = static_cast<Cycle*>(std::calloc(1, sizeof(Cycle)));
      uint8_t raw[0x1c];
      if (std::fread(raw, 0x1c, 1, f) != 1) return -1;
      c->looping = at<uint32_t>(raw, 0), c->frame = at<uint32_t>(raw, 4);
      c->index = at<float>(raw, 8), c->speed = at<float>(raw, 0xc);
      c->texture_count = at<uint32_t>(raw, 0x10), c->added = at<uint32_t>(raw, 0x14);
      c->textures = static_cast<zimg::TextureEntry**>(std::malloc(c->texture_count * sizeof(void*)));
      for (uint32_t k = 0; k < c->texture_count; ++k) {
        int32_t idx;
        if (!read4(f, &idx)) {
          zerr::report(0x200, kFile, 0x3c0, "Error reading GameZ Material cycle texture data.");
          return -1;
        }
        c->textures[k] = zimg::texture_at(idx);
      }
      m.cycle = c;
    }
  }
  return t.capacity;
}

namespace {
std::vector<std::string> g_soils = {"default", "water", "seafloor", "quicksand", "lava", "fire"};

// The name test 0x00535f20 and 0x00537cb0 share: `name` starts with the soil
// and ends there or at a space.
bool soil_matches(const char* name, const std::string& soil) {
  const size_t n = soil.size();
  if (std::strlen(name) < n) return false;
  const unsigned char c = static_cast<unsigned char>(name[n]);
  if (c && !std::isspace(c)) return false;
  for (size_t i = 0; i < n; ++i)
    if (std::tolower(static_cast<unsigned char>(name[i])) != std::tolower(static_cast<unsigned char>(soil[i])))
      return false;
  return true;
}
}  // namespace

std::vector<std::string>& soil_names() { return g_soils; }

// Mech3.exe 1.2 @ 0x00535f20
int32_t soil_index(const char* name) {
  if (!name) return -1;
  for (size_t i = 0; i < g_soils.size(); ++i)
    if (soil_matches(name, g_soils[i])) return static_cast<int32_t>(i);
  return -1;
}

// Mech3.exe 1.2 @ 0x00537cb0: the names in the file's first list not known
// yet are added, up to 100 in all. The items are taken as strings unchecked.
int load_soils(const char* file) {
  if (!file) return 0;
  zrdr::Node* root = zrdr::load(file);
  if (!root) return 0;
  zrdr::Node* list = root->list[1].list;
  if (list) {
    const int32_t n = zrdr::list_count(&root->list[1]);
    for (int32_t i = 1; i < n; ++i) {
      const char* name = list[i].s;
      if (name && soil_index(name) != -1) continue;
      if (g_soils.size() >= 100) break;
      g_soils.emplace_back(name ? name : "");
    }
  }
  zrdr::free_node(root);
  return 0;
}

// Mech3.exe 1.2 @ 0x00537270
int reset_soils() {
  g_soils.resize(6);
  return 0;
}

// Mech3.exe 1.2 @ 0x00537f20 (ECX step), each Instant Action and multiplayer
// frame with 10 (player.cpp, [0x005e8118]): the materials no model uses
// freed, in steps. Clears every mark first (the cursor [0x005edd5c] -1),
// then marks (+0x01 0x10) the materials of `step` models a call; once past
// the last model, frees each material without 0x08, 0x10 or 0x20 (0x00537070)
// and clears the marks, the freed count returned. The materials' slots past
// the count are swept too ([0x0063fbc4], the capacity).
namespace {
int32_t g_sweep = -1;  // [0x005edd5c]
}
void material_sweep_reset() { g_sweep = -1; }
int32_t material_sweep(int32_t step) {
  MaterialTable& t = g_table;
  ModelPool& pool = models();
  if (g_sweep < 0) {
    for (int32_t i = 0; i < t.capacity; ++i) t.at(i).flags &= static_cast<uint8_t>(~0x10);
    g_sweep = 0;
  }
  int32_t at = g_sweep;
  if (pool.size <= g_sweep) {
    g_sweep = -1;
    return 0;
  }
  while (g_sweep = at, step > 0) {
    const Model& m = pool.slots[static_cast<size_t>(at)];
    for (uint32_t k = 0; m.polygons && k < m.count(0x10); ++k)
      if (Material* mat = m.polygons[k].material) mat->flags |= 0x10;
    --step;
    if (pool.size <= ++at) step = 0;
  }
  if (pool.size > at) return 0;
  int32_t freed = 0;
  for (int32_t i = 0; i < t.capacity; ++i) {
    Material& mat = t.at(i);
    if ((mat.flags & 0x38) == 0) {
      material_free(&mat);
      ++freed;
    } else {
      mat.flags &= static_cast<uint8_t>(~0x10);
    }
  }
  g_sweep = 0;
  return freed;
}

}  // namespace zmod
