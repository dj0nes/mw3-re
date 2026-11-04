// Pirate's Moon's ported functions behind a C interface, for the differential
// tests run against PM's exe (tests/differential, MW3_EXE=pm). PM's table is
// set on load; MW3's probe is tests/probe.cpp.
#include <cstdint>
#include <cstring>
#include <string>

#include "gamez/zgame/zgame_table.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/sound.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zutil/zfix.h"
#include <vector>
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zvideo/zvid_format.h"
#include "gamez/zclass/cls_mechlib.h"
#include "pm/gamez/zclass/cls_mechlib.h"
#include "gamez/zinterp/zinterp.h"
#include "pm/gamez/zclass/cls_zbd.h"
#include "pm/gamez/zeffect/zeff_anim_init.h"
#include "pm/gamez/zimage/zimg_texture.h"
#include "pm/gamez/zmodel/gmod_clip3d.h"
#include "pm/gamez/zmodel/gmod_const.h"
#include "pm/gamez/zmodel/gmod_matl.h"
#include "pm/gamez/zreader/zrdr_main.h"
#include "pm/gamez/zutil/zutl_zar.h"
#include "pm/pm_game.h"

namespace {
struct UsePm {
  UsePm() { zgame::set_game(pm::table()); }
} g_use_pm;

void mount_only(const char* dir) {
  zutl::mounts() = {};
  zutl::mount(dir);
  zutl::set_write_dir(dir);
}

zrdr::Node* g_root = nullptr;

// test_pm_zreader.py's form: I<int> F<bits> S<len>:<text> L<count>[...]
void dump(const zrdr::Node& n, std::string& s) {
  char b[32];
  switch (n.type) {
    case zrdr::Int: std::snprintf(b, sizeof b, "I%d ", n.i), s += b; break;
    case zrdr::Float: {
      uint32_t u;
      std::memcpy(&u, &n.f, 4);
      std::snprintf(b, sizeof b, "F%08x ", u), s += b;
      break;
    }
    case zrdr::String: s += "S" + std::to_string(std::strlen(n.s)) + ":" + n.s + " "; break;
    case zrdr::List:
      s += "L" + std::to_string(n.list[0].i) + "[";
      for (int32_t i = 1; i < n.list[0].i; ++i) dump(n.list[i], s);
      s += "] ";
      break;
    default: s += "? ";
  }
}

// The index path of `target` under `n` ("1/3/2"), one past a list's end included.
bool path_to(const zrdr::Node* n, const zrdr::Node* target, std::string& out) {
  if (n->type != zrdr::List) return false;
  for (int32_t i = 1; i <= n->list[0].i; ++i) {
    if (&n->list[i] == target) return out = std::to_string(i), true;
    std::string sub;
    if (i < n->list[0].i && path_to(&n->list[i], target, sub)) return out = std::to_string(i) + "/" + sub, true;
  }
  return false;
}
std::string where(const zrdr::Node* hit) {
  std::string p;
  if (hit && path_to(g_root, hit, p)) return p;
  return hit ? "?" : "";
}
uint32_t bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
}  // namespace

namespace {
// Every node in PM's layout (test_pm_gamez.py's node_dump): the slot, its
// box and model, the live ones' links and class data, the class lists.
// `item_set`: +0xcc as 0 or 1 (mechlib's nodes keep their list-7 item).
void put_world_nodes(std::string& b, int32_t n_unread, bool item_set = false) {
  auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  ::zcls::NodePool& np = ::zcls::nodes();
  put(&np.capacity, 4), put(&np.live, 4), put(&np.free_head, 4);
  auto node_index = [&](const ::zcls::Node* n) -> int32_t {
    if (!n) return -1;
    for (size_t k = 0; k < np.blocks.size(); ++k)
      if (n >= np.blocks[k].get() && n < np.blocks[k].get() + np.block_size)
        return static_cast<int32_t>(k) * np.block_size + static_cast<int32_t>(n - np.blocks[k].get());
    return -2;
  };
  auto model_index = [&](const pm::zmod::Model* m) -> int32_t {
    if (!m) return -1;
    pm::zmod::ModelPool& mp = pm::zmod::models();
    for (size_t k = 0; k < mp.blocks.size(); ++k)
      if (m >= mp.blocks[k].get() && m < mp.blocks[k].get() + mp.block)
        return static_cast<int32_t>(k) * mp.block + static_cast<int32_t>(m - mp.blocks[k].get());
    return -2;
  };
  auto nodes = [&](::zcls::Node* const* list, int32_t n) {
    for (int32_t k = 0; k < n; ++k) {
      const int32_t i = node_index(list ? list[k] : nullptr);
      put(&i, 4);
    }
  };
  for (int32_t i = 0; i < np.capacity; ++i) {
    const ::zcls::Node& n = *::zcls::node_at(i);
    uint8_t r[0xd4] = {};  // back into PM's layout
    std::memcpy(r, n.raw, 0x4c);
    const int16_t ax = static_cast<int16_t>(n.get<int32_t>(0x4c)), az = static_cast<int16_t>(n.get<int32_t>(0x50));
    const int16_t pc = static_cast<int16_t>(n.parent_count()), cc = static_cast<int16_t>(n.child_count());
    std::memcpy(r + 0x4c, &ax, 2), std::memcpy(r + 0x4e, &az, 2), std::memcpy(r + 0x50, n.pm_virtual, 4);
    std::memcpy(r + 0x54, &pc, 2), std::memcpy(r + 0x56, &cc, 2);
    std::memcpy(r + 0x60, n.raw + 0x64, 0x10);
    std::memcpy(r + 0x74, n.raw + 0x74, 0xd0 - 0x74);
    std::memcpy(r + 0xd0, &n.link, 4);
    if (item_set) {
      uint32_t raw_item;
      std::memcpy(&raw_item, r + 0xcc, 4);
      const uint32_t set = n.changed || raw_item ? 1 : 0;
      std::memcpy(r + 0xcc, &set, 4);
    }
    std::memset(r + 0x38, 0, 8), std::memset(r + 0x48, 0, 4), std::memset(r + 0x58, 0, 8), std::memset(r + 0x70, 0, 4);
    const int32_t sel = !n.pm_box ? -1 : n.pm_box == reinterpret_cast<const float*>(n.raw + 0x74) ? 0
                        : n.pm_box == reinterpret_cast<const float*>(n.raw + 0x8c) ? 1 : 2;
    const int32_t mi = model_index(n.pm_model);
    put(r, 0xd4);
    if (i >= n_unread) continue;
    put(&sel, 4), put(&mi, 4);
    if (!(n.link & ::zcls::kNodeLive)) continue;
    nodes(n.parents, pc), nodes(n.children, cc);
    if (!n.data) continue;
    switch (n.cls()) {
      case 1: {
        auto* c = static_cast<::zcls::Camera*>(n.data);
        uint8_t d[0x1e8];
        std::memcpy(d, c->raw, sizeof d), std::memset(d, 0, 16);
        put(d, sizeof d), nodes(c->nodes, 4);
        break;
      }
      case 2: {
        auto* w = static_cast<::zcls::World*>(n.data);
        uint8_t d[0xcc] = {};
        std::memcpy(d, w->raw, 0x4c), std::memcpy(d + 0x4c, w->pm_area2, 0x10), std::memcpy(d + 0x5c, w->raw + 0x4c, 0xbc - 0x4c);
        for (int off : {0xa0, 0xb4, 0xb8, 0xc0, 0xc4}) std::memset(d + off, 0, 4);
        put(d, sizeof d);
        const int32_t nl = w->get<int32_t>(0xa0), ns = w->get<int32_t>(0xac);
        nodes(w->lights, nl), nodes(w->sounds, ns);
        for (int32_t k = 0; k < nl; ++k) {
          const int32_t same = w->light_data[k] == w->lights[k]->data ? 1 : 0;
          put(&same, 4);
        }
        const int32_t rows = w->get<int32_t>(0x8c), cols = w->get<int32_t>(0x88);
        for (int32_t z = 0; z < rows; ++z)
          for (int32_t x = 0; x < cols; ++x) {
            const ::zcls::Partition& pr = w->rows[z][x];
            uint8_t q[0x58] = {};
            std::memcpy(q, pr.raw, 0x48), std::memcpy(q + 0x48, pr.pm_tail, 0x10), std::memset(q + 0x3c, 0, 4);
            put(q, sizeof q);
            for (int16_t k = 0; k < pr.get<int16_t>(0x3a); ++k) {
              const int32_t i2 = node_index(pr.nodes[k]);
              put(&i2, 4), put(pr.pm_y[k], 8);
            }
          }
        break;
      }
      case 3: put(static_cast<::zcls::Window*>(n.data)->raw, 0xf8); break;
      case 4: put(static_cast<::zcls::Display*>(n.data)->raw, 0x1c); break;
      case 5: put(static_cast<::zcls::Object3d*>(n.data)->raw, 0x90); break;
      case 6: {
        auto* l = static_cast<::zcls::Lod*>(n.data);
        put(l->raw, 0x50), put(l->pm_tail, 0xc);
        break;
      }
      case 9: {
        auto* l = static_cast<::zcls::Light*>(n.data);
        uint8_t d[0x100] = {};
        std::memcpy(d, l->raw, 0xb0), std::memcpy(d + 0xb0, l->pm_colors, 0x30), std::memcpy(d + 0xe0, l->raw + 0xb0, 0x20);
        std::memset(d + 0xfc, 0, 4);
        put(d, sizeof d), nodes(l->nodes, l->get<int32_t>(0xc8));
        break;
      }
      case 10: {
        auto* s = static_cast<::zcls::Sound*>(n.data);
        uint8_t d[0x94];
        std::memcpy(d, s->raw, sizeof d), std::memset(d + 0x90, 0, 4);
        put(d, sizeof d), nodes(s->nodes, s->get<int32_t>(0x8c));
        break;
      }
      default: break;
    }
  }
  for (int k : {6, 8, 9, 10, 13, 14, 15}) {
    std::vector<int32_t> seq;
    for (::zcls::ListItem* it = ::zcls::lists().list[k].head; it; it = it->next) seq.push_back(node_index(it->node));
    const int32_t len = static_cast<int32_t>(seq.size());
    put(&len, 4);
    if (len) put(seq.data(), seq.size() * 4);
  }
}

// The texture table and every material slot, the cycles (test_pm_gamez.py's
// tables); past `unresolved`, a slot's flags and links only.
void put_tables(std::string& b, int32_t unresolved) {
  auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  ::zimg::TextureTable& tt = ::zimg::textures();
  put(&tt.count, 4);
  for (int i = 0; i < tt.count; ++i) {
    const ::zimg::TextureEntry& e = tt.entries[i];
    const int32_t mip = e.mip ? 1 : 0;
    char name[20] = {};
    std::memcpy(name, e.name, strnlen(e.name, 20));  // a C string: what follows its end is unread
    put(name, 20), put(&e.state, 4), put(&e.group, 4), put(&mip, 4);
  }
  const ::zmod::MaterialTable& t = ::zmod::materials();
  put(&t.capacity, 4), put(&t.count, 4), put(&t.free_head, 4), put(&t.used_head, 4);
  auto index_of = [&](const ::zimg::TextureEntry* e) { return e ? static_cast<int32_t>(e - tt.entries) : -1; };
  for (int i = 0; i < t.capacity; ++i) {
    const ::zmod::Material& m = t.at(i);
    if (i >= unresolved) {  // a failed read (which ends the load): its flags and links only
      put(&m.alpha, 1), put(&m.flags, 1), put(&m.prev, 2), put(&m.next, 2);
      continue;
    }
    put(&m.alpha, 1), put(&m.flags, 1), put(&m.rgb, 2), put(&m.r, 4), put(&m.g, 4), put(&m.b, 4);
    const int32_t tex = (!(m.flags & 0x20) && (m.flags & 1)) ? index_of(m.texture) : static_cast<int32_t>(m.texture_index);
    put(&tex, 4), put(m.unk_14, 12), put(&m.soil, 4), put(&m.prev, 2), put(&m.next, 2);
  }
  for (int i = 0; i < t.capacity; ++i) {
    const ::zmod::Material& m = t.at(i);
    if ((m.flags & 0x20) || !(m.flags & 4) || i >= unresolved) continue;  // the exe's cycles past a failed read are half read
    const ::zmod::Cycle* c = m.cycle;
    put(&c->looping, 4), put(&c->frame, 4), put(&c->index, 4), put(&c->speed, 4);
    put(&c->texture_count, 4), put(&c->added, 4);
    for (uint32_t k = 0; k < c->texture_count; ++k) {
      const int32_t idx = index_of(c->textures[k]);
      put(&idx, 4);
    }
  }
}

// Every model slot in PM's pool (test_pm_gamez.py's model_dump); past
// `m_unread`, a slot's header only.
void put_models(std::string& b, int32_t m_unread) {
  auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  const ::zmod::MaterialTable& t = ::zmod::materials();
  pm::zmod::ModelPool& mp = pm::zmod::models();
  put(&mp.capacity, 4), put(&mp.count, 4), put(&mp.free_head, 4);
  auto mat_index = [&](const ::zmod::Material* m) { return m ? t.index_of(m) : -1; };
  for (int i = 0; i < mp.capacity; ++i) {
    const pm::zmod::Model& m = mp.at(i);
    put(m.header, 0x34), put(m.header + 0x48, 0x18), put(&m.link, 4);  // not the pointers
    if (i >= m_unread) continue;
    auto arr = [&](const void* p, int32_t n, size_t each) { if (n > 0) put(p, static_cast<size_t>(n) * each); };
    arr(m.vertices, m.count(0x14), 12), arr(m.normals, m.count(0x18), 12), arr(m.morphs, m.count(0x1c), 12);
    for (int32_t k = 0; k < m.count(0x20); ++k) {
      const ::zmod::Light& l = m.lights[k];
      put(l.raw, 0x28), put(&l.color, 2), put(l.raw + 0x30, 0x1c);
      int32_t n;
      std::memcpy(&n, l.raw + 0xc, 4);
      arr(l.vertices, n, 12);
    }
    for (int32_t k = 0; k < m.count(0x10); ++k) {
      const pm::zmod::Polygon& q = m.polygons[k];
      const uint32_t n = q.flags & 0x3ff;
      const int32_t uvs = q.file_uvs ? 1 : 0;
      put(&q.flags, 4), put(&q.priority, 4), put(&q.material_count, 4), put(&uvs, 4), put(&q.zone_set, 4);
      if (n) put(q.vertex_indices, n * 4);
      if (n && (q.flags & 0x1000) && q.normal_indices) put(q.normal_indices, n * 4);
      for (int32_t j = 0; j < q.material_count; ++j) {
        const int32_t mi = mat_index(q.materials[j]);
        put(&mi, 4);
        if (q.uvs) put(q.uvs[j], n * 8);
      }
      if (q.colors) put(q.colors, n * 12);  // mechlib's: only with the file's +0x1c
    }
    for (int32_t k = 0; k < m.count(0x5c); ++k) {
      const pm::zmod::MaterialRef& r = m.refs[k];
      const int32_t mi = mat_index(r.material), first = r.polygon ? static_cast<int32_t>(r.polygon - m.polygons) : -1;
      put(&mi, 4), put(&r.usage_count, 4), put(&first, 4);
    }
  }
}

}  // namespace

extern "C" {

// zArchive 2 (pm/gamez/zutil/zutl_zar): opens dir/name through the mounts.
// out: [0] the open's result, [1] count, [2] checksum (+0x18), [3]
// archive_crc, [4] checksum_ok, [5] file_checksum_ok(name). The table's
// bytes go to `table` (up to `cap`), each entry's span to `spans` (up to
// `cap_spans`). With `crc` 0 the CRCs are left out (the sounds are 150 MB).
int probe_pm_zar_open(const char* dir, const char* name, int crc, uint32_t* out, uint8_t* table, int cap,
                      uint32_t* spans, int cap_spans) {
  mount_only(dir);
  zutl_zar::Archive a;
  zutl_zar::init(a);
  std::memset(out, 0, 6 * 4);
  out[0] = zutl_zar::open(a, name);
  out[1] = static_cast<uint32_t>(a.count);
  out[2] = a.checksum;
  if (out[0]) {
    const int n = a.count * 0x94 < cap ? a.count * 0x94 : cap;
    std::memcpy(table, a.entries, static_cast<size_t>(n));
    for (int i = 0; i < a.count && i < cap_spans; ++i) spans[i] = pm::zar::span(a, i);
    if (crc) {
      out[3] = pm::zar::archive_crc(a);
      out[4] = pm::zar::checksum_ok(a);
    }
  }
  zutl_zar::close(a);
  zutl_zar::destroy(a);
  if (crc) out[5] = pm::zar::file_checksum_ok(name);
  return static_cast<int>(out[0]);
}

// The writer: dir/name created, `count` entries added (names "e<i>", data
// from `data` at `offsets`), the checksum set, closed (0x00597570 writes the
// table).
int probe_pm_zar_write(const char* dir, const char* name, int count, const uint8_t* data, const uint32_t* offsets,
                       const uint32_t* lengths, uint32_t checksum) {
  mount_only(dir);
  zutl_zar::Archive a;
  zutl_zar::init(a);
  if (!zutl_zar::create(a, name)) return 0;
  for (int i = 0; i < count; ++i) {
    const std::string n = "e" + std::to_string(i);
    zutl_zar::add(a, n.c_str(), data + offsets[i], lengths[i]);
  }
  a.checksum = checksum;
  zutl_zar::close(a);
  zutl_zar::destroy(a);
  return 1;
}

// The reader (gamez/zreader) under PM's table: dir mounted, `archive` opened
// as the current one, `entry` loaded. 1 with a tree, 0 without.
int probe_pm_zrdr_load(const char* dir, const char* archive, const char* entry) {
  mount_only(dir);
  if (g_root) zrdr::free_node(g_root), g_root = nullptr;
  zrdr::close_archives(true);
  zrdr::init();
  if (!zrdr::open_archive(archive, true)) return -1;
  g_root = zrdr::load(entry);
  return g_root != nullptr;
}

// A text file through pm::zrdr::parse_text, dir mounted (where its #include
// files are found), the #ifdef depth given and returned: 1 with the tree
// loaded, 0 when it did not balance, -1 no file.
int probe_pm_zrdr_text(const char* dir, const char* path, int32_t* depth) {
  mount_only(dir);
  if (g_root) zrdr::free_node(g_root), g_root = nullptr;
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  zrdr::state().if_depth = *depth;
  g_root = pm::zrdr::parse_text(f);
  std::fclose(f);
  *depth = zrdr::state().if_depth;
  return g_root != nullptr;
}

int probe_pm_zrdr_dump(char* buf, int size) {
  std::string s;
  if (g_root) dump(*g_root, s);
  if (static_cast<int>(s.size()) >= size) return -1;
  std::memcpy(buf, s.c_str(), s.size() + 1);
  return static_cast<int>(s.size());
}

// Every getter on `key`, as test_pm_zreader.py's QUERY lists them. Outputs
// start as `fill`, so one left alone shows. Paths and strings go to `text`,
// separated by '|'.
int probe_pm_zrdr_query(const char* key, uint32_t fill, uint32_t* out, char* text, int size) {
  zrdr::Node* r = g_root;
  std::string t;
  zrdr::Node* hit = zrdr::find(r, key);
  t += where(hit) + "|" + where(zrdr::find_flat(r, key, 1)) + "|";
  const char* s = zrdr::get_string(r, key);
  t += std::string(s ? s : "(null)") + "|";
  s = zrdr::get_string_flat(r, key, 1);
  t += std::string(s ? s : "(null)") + "|";
  int k = 0;
  auto f32 = [&](bool ok, const float* v, int n) {
    out[k++] = ok;
    for (int i = 0; i < n; ++i) out[k++] = bits(v[i]);
  };
  auto i32 = [&](bool ok, const int32_t* v, int n) {
    out[k++] = ok;
    for (int i = 0; i < n; ++i) out[k++] = static_cast<uint32_t>(v[i]);
  };
  float f[3];
  int32_t iv[3];
  const auto fillf = [&] { for (float& x : f) std::memcpy(&x, &fill, 4); };
  const auto filli = [&] { for (int32_t& x : iv) x = static_cast<int32_t>(fill); };
  fillf(), f32(zrdr::get_float(r, key, f), f, 1);
  filli(), i32(zrdr::get_int(r, key, iv), iv, 1);
  filli(), zrdr::get_int_if(r, key, iv), out[k++] = static_cast<uint32_t>(iv[0]);
  uint8_t byte = static_cast<uint8_t>(fill);
  zrdr::get_byte_if(r, key, &byte), out[k++] = byte;
  fillf(), f32(pm::zrdr::get_float_value(r, key, f), f, 1);
  const char* sv = reinterpret_cast<const char*>(1);
  const bool sok = pm::zrdr::get_string_value(r, key, &sv);
  out[k++] = sok;
  t += std::string(sv == reinterpret_cast<const char*>(1) ? "(left)" : sv ? sv : "(null)") + "|";
  fillf(), f32(zrdr::get_vec3(r, key, f), f, 3);
  fillf(), f32(zrdr::get_vec2(r, key, f), f, 2);
  filli(), i32(zrdr::get_ivec3(r, key, iv), iv, 3);
  filli(), i32(zrdr::get_ivec2(r, key, iv), iv, 2);
  filli(), i32(zrdr::get_bool_int(r, key, iv), iv, 1);
  bool b = (fill & 1) != 0;
  const bool bok = zrdr::get_bool(r, key, &b);
  out[k++] = bok, out[k++] = b;
  filli(), i32(zrdr::get_tristate(hit, iv), iv, 1);
  filli(), i32(pm::zrdr::get_int_value(hit, iv), iv, 1);
  if (static_cast<int>(t.size()) >= size) return -1;
  std::memcpy(text, t.c_str(), t.size() + 1);
  return k;
}

// gamez.zbd files read in turn as PM's loader 0x00516bc0 reads their
// textures and materials (docs/pm-map.md, gamez.zbd 41): `paths` split by
// '|'. Out: per file the two results, then the texture table and the
// material table (every slot; a used textured one's texture as an index),
// then the cycles in index order. The bytes written, -1 when too small.
int probe_pm_gamez_stack(const char* paths, int rb, int gb, int bb, int with_models, uint8_t* out, int cap) {
  const bool with_nodes = with_models > 1;  // 2: the nodes too (then the models are not dumped)
  zvid::set_pixel_format(rb, gb, bb);
  ::zimg::TextureTable& tt = ::zimg::textures();
  tt.count = 0, tt.group = 0;
  ::zmod::materials() = ::zmod::MaterialTable{};
  pm::zmod::models() = pm::zmod::ModelPool{};
  ::zcls::nodes() = ::zcls::NodePool{};
  ::zcls::lists() = ::zcls::Lists{};
  pm::zcls::ZbdState& z = pm::zcls::zbd_state();
  z = pm::zcls::ZbdState{};
  std::string b;
  auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  std::string all = paths;
  int32_t unresolved = 1 << 30, m_unread = 1 << 30, n_unread = 1 << 30;
  for (size_t at = 0; at <= all.size();) {
    size_t bar = all.find('|', at);
    if (bar == std::string::npos) bar = all.size();
    const std::string path = all.substr(at, bar - at);
    at = bar + 1;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return -2;
    pm::zcls::ZbdFile e;
    e.path = path;
    if (std::fread(e.header, 0x28, 1, f) != 1) return -2;
    z.files.push_back(e);
    z.current = static_cast<int32_t>(z.files.size() - 1), z.file = f;
    int32_t tex = 0, mat = 0;
    if (tt.count == 0) {
      std::fseek(f, e.get(0x10), SEEK_SET);
      tex = pm::zimg::read_texture_directory(e.get(0xc), f);
      z.own_textures = true;
    }
    std::fseek(f, e.get(0x14), SEEK_SET);
    const int32_t before = ::zmod::materials().capacity;
    if (tex >= 0) mat = pm::zmod::read_materials(f);
    if (mat < 0) unresolved = before;  // a failed read's materials keep the file's indices
    int32_t mod = 0;
    if (with_models && tex >= 0 && mat >= 0) {
      std::fseek(f, e.get(0x18), SEEK_SET);
      const int32_t mbefore = pm::zmod::models().capacity;
      mod = pm::zmod::read_models(f, mat);
      if (mod < 0) m_unread = mbefore;  // a failed read's slots: their headers only
    }
    int32_t nod = 0;
    if (with_nodes && tex >= 0 && mat >= 0 && mod >= 0) {
      std::fseek(f, e.get(0x24), SEEK_SET);
      const int32_t nbefore = ::zcls::nodes().capacity;
      nod = pm::zcls::read_nodes(e.get(0x1c), f, mod);
      if (nod < 0) n_unread = nbefore;  // a failed read's slots only
    }
    put(&tex, 4), put(&mat, 4);
    if (with_models) put(&mod, 4);
    if (with_nodes) put(&nod, 4);
    std::fclose(f);
    z.file = nullptr, z.current = -1, z.own_textures = false;
    if (tex < 0 || mat < 0) break;
  }
  put_tables(b, unresolved);
  if (with_models == 1) put_models(b, m_unread);
  if (with_nodes) put_world_nodes(b, n_unread);
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}

// test_pm_mechlib.py: a world loaded as probe_pm_gamez_stack does (or none:
// empty pools), then mechlib.zbd opened through the game table and the trees
// `names` ('|'-separated) loaded in order. Out: the open's result, each
// load's root, the library's materials and trees, then the tables, models
// and nodes.
int probe_pm_mechlib(const char* world, const char* path, const char* names, uint8_t* out, int cap) {
  static std::vector<uint8_t> scratch(1 << 27);
  if (world && *world) {
    if (probe_pm_gamez_stack(world, 5, 6, 5, 2, scratch.data(), static_cast<int>(scratch.size())) < 0) return -3;
  } else {
    zvid::set_pixel_format(5, 6, 5);
    ::zimg::textures().count = 0, ::zimg::textures().group = 0;
    ::zmod::materials() = ::zmod::MaterialTable{};
    pm::zmod::models() = pm::zmod::ModelPool{};
    ::zcls::nodes() = ::zcls::NodePool{};
    ::zcls::lists() = ::zcls::Lists{};
  }
  static ::zcls::MechLib* lib = nullptr;
  if (lib) ::zcls::mechlib_close(*lib);  // the archive closed, the materials freed
  delete lib;
  lib = new ::zcls::MechLib{};
  std::string b;
  auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  ::zcls::NodePool& np = ::zcls::nodes();
  auto node_index = [&](const ::zcls::Node* n) -> int32_t {
    if (!n) return -1;
    for (size_t k = 0; k < np.blocks.size(); ++k)
      if (n >= np.blocks[k].get() && n < np.blocks[k].get() + np.block_size)
        return static_cast<int32_t>(k) * np.block_size + static_cast<int32_t>(n - np.blocks[k].get());
    return -2;
  };
  const int32_t opened = ::zcls::mechlib_open(*lib, path) ? 1 : 0;
  put(&opened, 4);
  std::string all = names;
  for (size_t at = 0; opened && at < all.size();) {
    size_t bar = all.find('|', at);
    if (bar == std::string::npos) bar = all.size();
    const int32_t root = node_index(::zcls::mechlib_load(*lib, all.substr(at, bar - at).c_str()));
    put(&root, 4);
    at = bar + 1;
  }
  const int32_t nm = static_cast<int32_t>(lib->materials.size());
  put(&nm, 4);
  for (const ::zcls::Material* m : lib->materials) {
    const int32_t resolved = m->resolved ? 1 : 0, world_index = m->resolved ? ::zmod::materials().index_of(m->world) : -1;
    const int32_t named = m->texture ? 1 : 0;
    put(&resolved, 4), put(&world_index, 4), put(&named, 4);
  }
  const int32_t nt = static_cast<int32_t>(lib->trees.size());
  put(&nt, 4);
  for (const ::zcls::Node* n : lib->trees) {
    const int32_t i = node_index(n);
    put(&i, 4);
  }
  put_tables(b, 1 << 30);
  put_models(b, 1 << 30);
  put_world_nodes(b, 1 << 30, true);
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}

// test_pm_clip3d.py: one polygon through clipper `which` (0..5, the exe's
// order), the buffers rewritten in place. Out: the result, the count, whether
// each of pos, rec, tl, extra moved to its arena, and how far each arena went.
int probe_pm_clip3d(int which, float near_z, int count, uint32_t flags, float* pos, float* rec, float* tl, uint8_t* extra,
                    void* shadows, float* a_pos, float* a_rec, float* a_tl, uint8_t* a_extra, int32_t* out) {
  pm::zmod::ClipPoly p;
  p.pos = pos, p.rec = rec, p.tl = tl, p.extra = extra, p.flags = flags, p.count = count;
  p.shadows = static_cast<pm::zmod::ClipShadows*>(shadows);
  pm::zmod::Clip3dState& s = pm::zmod::clip3d_state();
  s.near_z = near_z, s.arena_pos = a_pos, s.arena_rec = a_rec, s.arena_tl = a_tl, s.arena_extra = a_extra;
  int (*const fn[6])(pm::zmod::ClipPoly&) = {pm::zmod::clip3d_plain,           pm::zmod::clip3d_lit,
                                             pm::zmod::clip3d_textured,        pm::zmod::clip3d_textured_lit,
                                             pm::zmod::clip3d_textured_shadow, pm::zmod::clip3d_textured_lit_shadow};
  out[0] = fn[which](p);
  out[1] = p.count;
  out[2] = p.pos != pos, out[3] = p.rec != rec, out[4] = p.tl != tl, out[5] = p.extra != extra;
  out[6] = static_cast<int32_t>(s.arena_pos - a_pos), out[7] = static_cast<int32_t>(s.arena_rec - a_rec);
  out[8] = static_cast<int32_t>(s.arena_tl - a_tl), out[9] = static_cast<int32_t>(s.arena_extra - a_extra);
  return 0;
}

// test_pm_light_map.py: script lines ('|'-separated, words by spaces) through
// the dispatcher with PM's table. texture_add answers 0x70000000 + 0x10 * call
// number. Out, per line: the light map, the cursor, the misses, the names asked.
int probe_pm_light_map(const char* lines, uint8_t* out, int cap) {
  static std::string asked;
  static int calls;
  asked.clear(), calls = 0;
  zgame::set_game(pm::table());
  ::zint::Engine keep = ::zint::engine();
  ::zint::engine().texture_add = [](const char* name) {
    asked += name ? name : "(none)", asked += '\n';
    return reinterpret_cast<::zimg::TextureEntry*>(static_cast<uintptr_t>(0x70000000u + 0x10u * calls++));
  };
  pm::zimg::set_light_map(nullptr);
  std::string b, all = lines;
  auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  for (size_t at = 0; at < all.size();) {
    size_t bar = all.find('|', at);
    if (bar == std::string::npos) bar = all.size();
    std::string line = all.substr(at, bar - at);
    at = bar + 1;
    ::zint::Interp in;
    for (size_t i = 0; i < line.size() && in.argc < 16; ++i) {
      if (line[i] == ' ') { line[i] = 0; continue; }
      if (i == 0 || line[i - 1] == 0) in.argv[in.argc++] = &line[i];
    }
    in.cursor = 1;
    ::zint::dispatch(in, in.argv[0]);
    const uint32_t lm = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pm::zimg::light_map()));
    put(&lm, 4), put(&in.cursor, 4), put(&in.misses, 4);
  }
  b += asked;
  ::zint::engine() = keep;
  pm::zimg::set_light_map(nullptr);
  zgame::set_game(zgame::mw3_table());
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}

}  // extern "C"

namespace {
// test_pm_anim.py: the calls into parts not ported answer fake ids
// (0x70000000 + 0x10 * call number), or 0 (-1 for an effect) on `g_fail_at`.
std::string g_trace;
int32_t g_calls = 0, g_fail_at = -1, g_anim_fix = 0;
std::vector<int32_t> g_reports;

uint32_t next_fake() {
  const int32_t k = g_calls++;
  return k == g_fail_at ? 0 : 0x70000000u + 0x10u * static_cast<uint32_t>(k);
}
void trace_u32(uint32_t v) { g_trace.append(reinterpret_cast<const char*>(&v), 4); }
void trace_str(const char* s) { g_trace.append(s, std::strlen(s) + 1); }
void* fake(uint32_t v) { return reinterpret_cast<void*>(static_cast<uintptr_t>(v)); }
uint32_t id(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

int32_t node_index(const ::zcls::Node* n) {
  if (!n) return -1;
  const ::zcls::NodePool& p = ::zcls::nodes();
  for (size_t k = 0; k < p.blocks.size(); ++k)
    if (n >= p.blocks[k].get() && n < p.blocks[k].get() + p.block_size)
      return static_cast<int32_t>(k) * p.block_size + static_cast<int32_t>(n - p.blocks[k].get());
  return -2;
}

void install_anim_hooks() {
  ::zeff::AnimHooks& h = ::zeff::anim_hooks();
  h.copy_tree = [](::zcls::Node* n) {
    trace_u32(3), trace_u32(static_cast<uint32_t>(node_index(n)));
    return static_cast<::zcls::Node*>(fake(next_fake()));
  };
  h.create_puffer = [] { trace_u32(7); return fake(next_fake()); };
  h.destroy_puffer = [](void* p) { trace_u32(8), trace_u32(id(p)); };
  h.find_sound = [](const char* name) { trace_u32(10), trace_str(name); return fake(next_fake()); };
  h.effect_index = [](const char* name) {
    trace_u32(11), trace_str(name);
    const uint32_t v = next_fake();
    return v ? static_cast<int32_t>(v) : -1;
  };
  ::zcls::set_find_sound(h.find_sound);
}

// The read in PM's layout (test_pm_anim.py's dump): the block, each def's
// 0x10c record (pointers 0; +0xd0/+0xd4 as 0/1), its nodes as pool slots,
// its lists, sequences and events; the scripts.
void put_anim(std::string& b) {
  auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  auto i32 = [&](int32_t v) { put(&v, 4); };
  const ::zeff::AnimState& a = ::zeff::anim();
  ::zcls::Raw<0x6c> blk = a.pm_block;  // PM-only bytes as read; MW3's from `block`
  std::memcpy(blk.raw, a.block.raw, 0x10);
  blk.put(0x18, a.block.get<int32_t>(0x10));
  static constexpr int kFields[][2] = {{0x18, 0x20}, {0x1c, 0x24}, {0x20, 0x28}, {0x24, 0x2c}, {0x28, 0x30},
                                       {0x2c, 0x44}, {0x30, 0x48}, {0x34, 0x4c}, {0x38, 0x64}, {0x3c, 0x3c},
                                       {0x40, 0x68}};
  for (const auto& m : kFields) blk.put(m[1], a.block.get<uint32_t>(m[0]));
  blk.put(0x10, a.pm_script_count);
  for (int off : {0x0c, 0x14, 0x1c, 0x20}) blk.put<uint32_t>(off, 0);
  put(blk.raw, 0x6c);
  const ::zeff::AnimDef* defs = a.defs;
  for (int32_t i = 0; i < a.def_count(); ++i) {
    const ::zeff::AnimDef& d = defs[i];
    const bool fixed = i != 0 && d.raw[0x98] != 5;
    uint8_t r[0x10c] = {};
    std::memcpy(r, d.raw, 0x20), std::memcpy(r + 0x24, &d.pm_path_count, 4);
    std::memcpy(r + 0x28, d.raw + 0x20, 0xa4), std::memcpy(r + 0xd8, d.raw + 0x108, 0xc);
    std::memcpy(r + 0x108, d.raw + 0x138, 4);
    std::memset(r + 0x48, 0, 4), std::memset(r + 0x6c, 0, 4);
    r[0xd0] = d.pm_reset, r[0xd4] = d.pm_seq2 != nullptr;
    put(r, sizeof r);
    i32(fixed ? node_index(d.node) : 0), i32(fixed ? node_index(d.root) : 0);
    for (int32_t k = 0; k < d.pm_path_count; ++k) {
      uint8_t e[0x24];
      std::memcpy(e, d.pm_path[k].raw, 0x20), std::memset(e + 0x20, 0, 4);
      put(e, sizeof e), i32(fixed ? node_index(d.pm_path[k].node) : 0);
    }
    for (int k = 0; k < d.count(0x109); ++k) {
      const ::zeff::ObjectRef& o = d.objects[k];
      uint8_t e[0x5c];
      std::memcpy(e, o.raw, 0x24), std::memset(e + 0x24, 0, 4);
      std::memcpy(e + 0x28, &o.pm_flags, 2), std::memcpy(e + 0x2a, &o.pm_parent, 2), std::memcpy(e + 0x2c, o.raw + 0x30, 0x30);
      put(e, sizeof e), i32(fixed && k ? node_index(o.node) : 0);
    }
    for (int k = 0; k < d.count(0x10a); ++k) {
      const ::zeff::NodeRef40& o = d.nodes[k];
      uint8_t e[0x2c];
      std::memcpy(e, &o.pm_flags, 2), std::memcpy(e + 2, &o.pm_parent, 2), std::memcpy(e + 4, o.raw, 0x24);
      std::memset(e + 0x28, 0, 4);
      put(e, sizeof e), i32(fixed && k ? node_index(o.node) : 0);
    }
    const ::zeff::CreatedRef* made[] = {d.lights, d.puffers, d.sounds};
    for (int c = 0; c < 3; ++c)
      for (int k = 0; k < d.count(0x10b + c); ++k) {
        uint8_t e[0x2c];
        std::memcpy(e, made[c][k].raw, 0x2c), std::memset(e + 0x24, 0, 4);
        const void* m = made[c][k].made;
        put(e, sizeof e);
        i32(!fixed || !k ? 0 : c == 1 ? static_cast<int32_t>(id(m)) : node_index(static_cast<const ::zcls::Node*>(m)));
      }
    for (int k = 0; k < d.count(0x10e); ++k) {
      uint8_t e[0x24];
      std::memcpy(e, d.static_sounds[k].raw, 0x24), std::memset(e + 0x20, 0, 4);
      put(e, sizeof e), i32(fixed && k ? static_cast<int32_t>(id(d.static_sounds[k].sound)) : 0);
    }
    for (int k = 0; k < d.count(0x10f); ++k) put(d.effects[k].raw, 0x24);
    for (int k = 0; k < d.count(0x110); ++k) {
      const ::zeff::Prerequisite& q = d.prereqs[k];
      uint8_t e[0x30];
      std::memcpy(e, q.raw, 0x30), std::memset(e + 0x28, 0, 8);
      put(e, sizeof e);
      if (!fixed) continue;
      if (q.raw[4] == 1) i32(q.def ? static_cast<int32_t>(q.def - defs) : -1);
      if (q.raw[4] == 2 || q.raw[4] == 3) i32(node_index(q.node));
    }
    for (int k = 0; k < d.count(0x112); ++k) put(d.anim_refs[k].raw, 0x48);
    auto seq = [&](const uint8_t* raw, const uint8_t* events) {
      uint8_t e[0x40];
      std::memcpy(e, raw, 0x40), std::memset(e + 0x38, 0, 4);
      put(e, sizeof e);
      int32_t n;
      std::memcpy(&n, raw + 0x3c, 4);
      if (n > 0) put(events, n);
    };
    if (d.pm_reset) seq(d.raw + 0xc8, d.reset_events);
    if (d.pm_seq2) seq(d.pm_seq2->raw, d.pm_seq2->events);
    for (int k = 0; k < d.count(0x108); ++k) seq(d.seqs[k].raw, d.seqs[k].events);
  }
  for (int32_t k = 0; k < a.pm_script_count; ++k) {
    const ::zeff::SiScript& sc = a.pm_scripts[k];
    uint8_t e[0x1c];
    std::memcpy(e, sc.raw, 0x1c), std::memset(e, 0, 8), std::memset(e + 0x18, 0, 4);
    put(e, sizeof e);
    if (sc.name) put(sc.name, sc.raw[8]);
    if (sc.object) put(sc.object, sc.raw[9]);
    if (sc.data) put(sc.data, sc.get<int32_t>(0x14));
  }
  for (int32_t k = 0; k < a.message_count(); ++k) put(a.messages[k].raw, 0x24);
}
}  // namespace

extern "C" {

void probe_pm_anim_fix(int on) { g_anim_fix = on; }

// PM's 0x00523140 on one anim.zbd against its world, read as
// probe_pm_gamez_stack reads it: the result, the calls, the reports, the
// trace; with `full`, the read and the whole node pool after.
int probe_pm_anim(const char* world, const char* path, int fail_at, int full, uint8_t* out, int cap) {
  std::vector<uint8_t> scratch(64u << 20);
  if (probe_pm_gamez_stack(world, 5, 6, 5, 2, scratch.data(), static_cast<int>(scratch.size())) < 0) return -2;
  g_reports.clear();
  zerr::set_sink([](int level, const char*, int line, const char*) { g_reports.push_back(level), g_reports.push_back(line); });
  ::zcls::list_walk() = ::zcls::ListWalk{};
  ::zeff::anim() = ::zeff::AnimState{};
  ::zeff::anim().fix_prerequisites = g_anim_fix != 0;
  const bool bugs = zfix::options().bugs;
  zfix::options().bugs = g_anim_fix != 0;
  ::zeff::anim_allow_loading(1);
  ::zeff::anim_set_zbd_path(path);
  install_anim_hooks();
  g_trace.clear(), g_calls = 0, g_fail_at = fail_at;
  const int32_t r = ::zeff::anim_read_zbd();
  zfix::options().bugs = bugs;
  zerr::set_sink(nullptr);
  std::string b;
  auto i32 = [&](int32_t v) { b.append(reinterpret_cast<const char*>(&v), 4); };
  i32(r), i32(g_calls), i32(static_cast<int32_t>(g_reports.size() / 2));
  for (int32_t v : g_reports) i32(v);
  b += g_trace;
  if (full && r == 0) {
    put_anim(b);
    put_world_nodes(b, 1 << 30);
  }
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}

// After probe_pm_anim: def i's prerequisite count | the count to meet << 8.
int probe_pm_anim_prereqs(int i) {
  const ::zeff::AnimDef& d = ::zeff::anim().defs[i];
  return d.count(0x110) | d.count(0x111) << 8;
}

}  // extern "C"
