// Pirate's Moon's ported functions behind a C interface, for the differential
// tests run against PM's exe (tests/differential, MW3_EXE=pm). PM's table is
// set on load; MW3's probe is tests/probe.cpp.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <string>

#include "crt/msvc_rand.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "platform/render3d.h"
#include "pm/gamez/zvideo/zvid_tl.h"
#include "pm/gamez/zclass/cls_clutter.h"
#include "pm/gamez/zclass/light.h"
#include "pm/mech3/mission_world.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/camera.h"
#include "gamez/zmodel/gmod_init.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "gamez/zmodel/gmod_bake.h"
#include "pm/gamez/zmodel/gmod_projected.h"
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
#include "pm/gamez/zmodel/gmod_cull.h"
#include "pm/gamez/zmodel/gmod_matl.h"
#include "pm/gamez/zreader/zrdr_main.h"
#include "pm/gamez/zutil/zutl_zar.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zrender/zrndr_fog.h"
#include "pm/gamez/zmodel/gmod_draw.h"
#include "pm/pm_game.h"

namespace {
// The whole probe library tests the original's path, as tests/probe.cpp: the zfix fixes off.
const bool g_fixes_off = (zfix::options().per_second = false, zfix::options().bugs = false,
                          zfix::options().any_resolution = false, zfix::options().nested_lock = false,
                          zfix::options().fpu53 = false, true);
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
        put(l->raw, 0x34), put(l->pm_tail, 0xc), put(l->raw + 0x34, 0x1c);
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

// test_pm_modeldraw.py: a world loaded (nodes and models, as the exe's), a view
// set, then the draw's pieces on one node.
int probe_pm_draw_load(const char* world) {
  static std::vector<uint8_t> scratch(1 << 27);
  return probe_pm_gamez_stack(world, 5, 6, 5, 2, scratch.data(), static_cast<int>(scratch.size())) < 0 ? -1 : 0;
}
void probe_pm_draw_view(const float* cam, const float* proj, float near_z, const float* rect, float yaw, const float* views) {
  zrndr::device_fog().active = 1;
  zmth::Mat34 w;
  std::memcpy(w.m, cam, 48);
  zmth::set_view(w);
  zmth::set_projection(proj[0], proj[1], proj[2], proj[3], proj[4], proj[5], proj[6], proj[7]);
  std::memcpy(zmth::view().view.m, views, 48);  // the exe's, so the view's own sums are not compared here
  std::memcpy(zmth::view().camera.m, views + 12, 48);
  pm::zmod::clip3d_state().near_z = near_z;
  ::zmod::DrawState& s = ::zmod::draw_state();
  std::memcpy(s.point_rect, rect, 16);
  static float camera[0x1e8 / 4];
  camera[0x3c / 4] = yaw;
  s.camera_data = camera;
}
void probe_pm_model_view(const float* below, float* out) {
  zmth::Mat34 b, t;
  std::memcpy(b.m, below, 48);
  zmth::MatrixStack& st = zmth::matrix_stack();
  const zmth::MatrixStack saved = st;
  st.top = 0, st.slot[0] = &b, st.identity[0] = 0;
  zmth::push(&t);
  zmth::load_model_view();
  std::memcpy(out, t.m, 48);
  st = saved;
}
// test_pm_nodebox.py: a node's model box 0x004fef40 and its box's corners
// 0x004fda40 under `world` (null: no matrix): flags, the two boxes, the model's
// sphere, the corners.
int probe_pm_node_box(int node, const float* world, uint8_t* out) {
  ::zcls::Node* n = ::zcls::node_at(node);
  const int ret = ::zcls::node_model_box(n);
  zmth::MatrixStack& st = zmth::matrix_stack();
  const zmth::MatrixStack saved = st;
  zmth::Mat34 m;
  st.top = 0;
  if (world) {
    std::memcpy(m.m, world, 48);
    st.slot[0] = &m, st.identity[0] = 0;
  } else {
    st.slot[0] = nullptr, st.identity[0] = 1;
  }
  float corners[8][3] = {};
  const int status = ::zcls::node_box_corners(n, corners);
  st = saved;
  const uint32_t flags = n->flags();
  std::memcpy(out, &ret, 4), std::memcpy(out + 4, &status, 4), std::memcpy(out + 8, &flags, 4);
  std::memcpy(out + 12, n->raw + 0x8c, 24);
  std::memset(out + 36, 0, 16);
  if (n->pm_model) std::memcpy(out + 36, n->pm_model->header + 0x48, 16);
  std::memcpy(out + 52, corners, sizeof corners);
  return 52 + sizeof corners;
}
// test_pm_walk.py: the world render (0x0050a9e0) with the model draw recording:
// node, mask, the stack's top, the fade, the walk's morph, the model's flag 8 and
// factor.
extern "C++" {
namespace {
std::string g_walk_log;
template <typename T> void walk_put(T v) { g_walk_log.append(reinterpret_cast<const char*>(&v), sizeof v); }
int32_t walk_node_index(const ::zcls::Node* n) {
  const ::zcls::NodePool& p = ::zcls::nodes();
  for (size_t b = 0; b < p.blocks.size(); ++b) {
    const ::zcls::Node* base = p.blocks[b].get();
    if (n >= base && n < base + p.block_size) return static_cast<int32_t>(b * p.block_size + (n - base));
  }
  return -1;
}
int32_t walk_record(::zcls::Node* node, uint32_t mask) {
  walk_put(walk_node_index(node)), walk_put(mask);
  g_walk_log.append(reinterpret_cast<const char*>(zmth::matrix_stack().matrix().m), 48);
  walk_put(::zmod::draw_state().opacity), walk_put(::zcls::scene_walk().morph);
  const uint8_t* h = node->pm_model->header;
  uint32_t flags;
  float factor;
  std::memcpy(&flags, h + 8, 4), std::memcpy(&factor, h + 0x24, 4);
  walk_put(flags), walk_put(factor);
  auto* w = static_cast<::zcls::World*>(::zcls::camera_world(::zcls::list_head(::zcls::kCameras)->node)->data);
  for (int32_t i = 0; i < w->get<int32_t>(0xa0); ++i) walk_put(static_cast<::zcls::Light*>(w->light_data[i])->get<uint32_t>(0xb0));
  return 0;
}
::zcls::Node* walk_camera() { return ::zcls::list_head(::zcls::kCameras)->node; }
}  // namespace
}  // extern "C++"
int probe_pm_walk_load(const char* world) {
  if (probe_pm_draw_load(world) != 0) return -1;
  ::zcls::scene_walk() = ::zcls::SceneWalk{};
  ::zmod::walk_state() = ::zmod::WalkState{};
  ::zmod::walk_state().draw = walk_record;
  ::zmod::zones() = ::zmod::Zones{};
  ::zmod::shadow_casters() = ::zmod::ShadowCasters{};
  ::zmod::draw_state() = ::zmod::DrawState{};
  ::zmod::lighting().tint_on = 0;
  ::zmod::cull_hooks() = ::zmod::CullHooks{};
  ::zmod::cull_hooks().hidden_surface = [](void*, const float c[3], float) -> int32_t {
    const int32_t gx = static_cast<int32_t>(std::floor(c[0] / 37.0f)), gz = static_cast<int32_t>(std::floor(c[2] / 37.0f));
    return ((gx * 7 + gz * 13) % 5 + 5) % 5 != 0;
  };
  ::zcls::world_render_hooks() = ::zcls::WorldRenderHooks{};
  ::zcls::update_hooks() = ::zcls::UpdateHooks{};
  zrndr::device_fog().active = 1;
  zmth::reset_stack();
  return walk_node_index(walk_camera());
}
// test_pm_records.py: the state 0x00543b10 reads, as the exe's setters leave it.
void probe_pm_records_state(int fog_on, float fog_near, float fog_far, float alt_high, float alt_low, int shading,
                            int shadows, float opacity, int tint, float far_z, float slope_x, float slope_y) {
  ::zmod::fog_set_near(fog_near), ::zmod::fog_set_far(fog_far);
  ::zmod::fog_set_alt_high(alt_high), ::zmod::fog_set_alt_low(alt_low);
  ::zmod::fog().on = fog_on;
  std::memcpy(::zmod::fog_calc().camera_to_world, zmth::view().camera.m, 48);
  ::zmod::draw_state().vertex_shading = shading, ::zmod::draw_state().shadows = shadows, ::zmod::draw_state().opacity = opacity;
  ::zmod::lighting().tint_on = tint;
  pm::zmod::CullState& c = pm::zmod::cull_state();
  c.far_z = far_z, c.slope_x = slope_x, c.slope_y = slope_y;
}
// test_pm_lighting.py: lights as the exe holds them (0x100 bytes each), in the list;
// the clouds; then the draw as far as each ref's lighting.
void probe_pm_lights(int count, const uint8_t* pm, int clouds, float scale, float u, float v, float amp, float base, float tint) {
  ::zmod::LightList& list = ::zmod::light_list();
  list.count = count, list.any = count > 0;
  static uint32_t mask = 1;
  ::zmod::lighting().mask = &mask;
  ::zmod::lighting().tint_weight = tint;
  for (int i = 0; i < count; ++i) {
    auto* l = new ::zcls::Light{};
    std::memcpy(l->raw, pm + i * 0x100, 0xb0);
    std::memcpy(l->pm_colors, pm + i * 0x100 + 0xb0, 0x30);
    std::memcpy(l->raw + 0xb0, pm + i * 0x100 + 0xe0, 0x20);
    list.list[i] = ::zmod::LightEntry{};
    list.list[i].data = l;
  }
  zfix::options().fpu53 = false;  // the emulator's 24 bits: the clouds' index, CLAUDE.md
  ::zmod::clouds_set_on(clouds);
  ::zmod::Clouds& c = ::zmod::clouds();
  c.scale = scale, c.u = u, c.v = v, c.amplitude = amp, c.base = base;
}
int probe_pm_lighting(int node, const float* world, uint32_t mask, uint8_t* out, int cap) {
  static std::string b, mixes;
  b.clear(), mixes.clear();
  pm::zmod::draw_state().mix_fog = [](const float rgb[3]) { mixes.append(reinterpret_cast<const char*>(rgb), 12); };
  const auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  const auto i32 = [&](int32_t v) { put(&v, 4); };
  std::memset(::zmod::bake_state().stale, 0, 12);  // the test zeroes the exe's stack alike
  ::zcls::Node* n = ::zcls::node_at(node);
  pm::zmod::Model& m = *n->pm_model;
  zmth::MatrixStack& st = zmth::matrix_stack();
  const zmth::MatrixStack saved = st;
  zmth::Mat34 base, local;
  std::memcpy(base.m, world, 48);
  st.top = 0, st.slot[0] = &base, st.identity[0] = 0;
  zmth::push(&local);
  float centre[3] = {};
  pm::zmod::ModelPass pass;
  int ok = pm::zmod::prepare_model(m, centre);
  if (ok) {
    ok = pm::zmod::begin_model(*n, m, mask, pass);
    if (ok) {
      pm::zmod::RefPass ref;
      for (int32_t r = 0; r < m.count(0x5c); ++r) {
        if (!pm::zmod::build_records(m, m.refs[r], pass, ref)) continue;
        pm::zmod::light_records(m, pass, ref);
        for (const pm::zmod::Record& q : ref.records) {
          i32(static_cast<int32_t>(q.polygon - m.polygons)), i32(r), i32(static_cast<int32_t>(q.flags)), i32(q.count);
          put(q.lit, 12);
          for (int32_t k = 0; k < q.count; ++k) put(q.rec + k * 8, 12), put(q.rec + k * 8 + 4, 4);
        }
      }
    }
    zmth::pop();
  }
  st = saved;
  if (static_cast<int>(b.size() + mixes.size()) + 16 > cap) return -1;
  uint32_t head[4] = {static_cast<uint32_t>(ok), pass.needs, static_cast<uint32_t>(pass.shadows) | pass.light_maps << 1 | pass.projected << 2 | pass.bake << 3 | pass.cull << 4,
                      static_cast<uint32_t>(mixes.size() / 12)};
  // What the lights reached, and the fog colour last sent to the device.
  const ::zmod::LightList& list = ::zmod::light_list();
  for (int32_t i = 0; i < list.count; ++i) i32(list.list[i].full), i32(list.list[i].reach);
  put(zrndr::device_fog().sent, 12);
  std::memcpy(out, head, 16), std::memcpy(out + 16, mixes.data(), mixes.size());
  std::memcpy(out + 16 + mixes.size(), b.data(), b.size());
  return static_cast<int>(b.size() + mixes.size()) + 16;
}
// A model's bake: each polygon's scratch (0x0055fb30's results), from `values`, a
// float a vertex in polygon order.
void probe_pm_set_bake(int node, const float* values) {
  pm::zmod::Model& m = *::zcls::node_at(node)->pm_model;
  for (int32_t i = 0; i < m.count(0x10); ++i) {
    const uint32_t n = m.polygons[i].flags & 0x3ff;
    std::memcpy(m.polygons[i].scratch, values, n * 4);
    values += n;
  }
}
void probe_pm_set_fog_color(const float* rgb) { ::zmod::fog_set_color(rgb); }
// The largest model counts among the loaded nodes: vertices, morphs, polygons,
// polygons in one ref, extra materials on a polygon; polygons under 3 vertices.
void probe_pm_max_counts(int32_t* out) {
  std::memset(out, 0, 24);
  for (int32_t i = 0; i < ::zcls::nodes().capacity; ++i) {
    const pm::zmod::Model* m = ::zcls::node_at(i)->pm_model;
    if (!m) continue;
    out[0] = std::max(out[0], m->count(0x14)), out[1] = std::max(out[1], m->count(0x1c)), out[2] = std::max(out[2], m->count(0x10));
    for (int32_t r = 0; r < m->count(0x5c); ++r) out[3] = std::max(out[3], m->refs[r].usage_count);
    for (int32_t k = 0; k < m->count(0x10); ++k) {
      out[4] = std::max(out[4], m->polygons[k].material_count - 1);
      if ((m->polygons[k].flags & 0x3ff) < 3) ++out[5];
    }
  }
}
// test_pm_model_lights.py: a model's lights, from PM's record bytes with their
// vertices, drawn: each point (x, y, z, colour, alpha), each flare (world, screen, colour).
void probe_pm_set_model_lights(int node, int count, const uint8_t* records, const float* vertices, int per_light) {
  pm::zmod::Model& m = *::zcls::node_at(node)->pm_model;
  auto* ls = new ::zmod::Light[static_cast<size_t>(count)]{};
  for (int i = 0; i < count; ++i) {
    std::memcpy(ls[i].raw, records + i * 0x4c, 0x4c);
    std::memcpy(&ls[i].color, records + i * 0x4c + 0x28, 2);
    ls[i].vertices = new float[static_cast<size_t>(per_light) * 3];
    std::memcpy(ls[i].vertices, vertices + i * per_light * 3, static_cast<size_t>(per_light) * 12);
  }
  m.lights = ls;
}
int probe_pm_model_lights(int node, const float* world, float dt, uint8_t* out, int cap) {
  static std::string b;
  b.clear();
  ::zcls::Node* n = ::zcls::node_at(node);
  pm::zmod::Model& m = *n->pm_model;
  ::zmod::DrawTable& t = ::zmod::draw_table();
  const ::zmod::DrawTable keep = t;
  t.point = [](void*, const float screen[3], uint32_t color, int32_t alpha) {
    const int32_t kind = 1;
    b.append(reinterpret_cast<const char*>(&kind), 4), b.append(reinterpret_cast<const char*>(screen), 12);
    b.append(reinterpret_cast<const char*>(&color), 4), b.append(reinterpret_cast<const char*>(&alpha), 4);
  };
  pm::zmod::draw_state().flare = [](const float w[3], const float s[3], uint32_t color, const uint8_t*) {
    const int32_t kind = 2;
    b.append(reinterpret_cast<const char*>(&kind), 4), b.append(reinterpret_cast<const char*>(w), 12);
    b.append(reinterpret_cast<const char*>(s), 12), b.append(reinterpret_cast<const char*>(&color), 4);
  };
  ztimer::state().dt = dt;
  zmth::MatrixStack& st = zmth::matrix_stack();
  const zmth::MatrixStack saved = st;
  zmth::Mat34 base, local;
  std::memcpy(base.m, world, 48);
  st.top = 0, st.slot[0] = &base, st.identity[0] = 0;
  zmth::push(&local);
  float centre[3] = {};
  if (pm::zmod::prepare_model(m, centre)) {
    pm::zmod::draw_model_lights(m);
    zmth::pop();
  }
  st = saved;
  t = keep;
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}
// test_pm_bake.py: the model's camera-space vertices and normals, the vector the exe's
// stack holds; the bake; each polygon's scratch, then the vector.
int probe_pm_bake(int node, const float* cam, const float* normals, float* stale, uint8_t* out, int cap) {
  pm::zmod::Model& m = *::zcls::node_at(node)->pm_model;
  pm::zmod::DrawState& d = pm::zmod::draw_state();
  const size_t nv = static_cast<size_t>(m.count(0x14)), nn = static_cast<size_t>(m.count(0x18));
  d.cam.assign(cam, cam + nv * 3), d.normals.assign(normals, normals + nn * 3);
  std::memcpy(::zmod::bake_state().stale, stale, 12);
  pm::zmod::bake_static_light(m);
  std::memcpy(stale, ::zmod::bake_state().stale, 12);
  int at = 0;
  for (int32_t i = 0; i < m.count(0x10); ++i) {
    const pm::zmod::Polygon& p = m.polygons[i];
    const int k = static_cast<int>(p.flags & 0x3ff);
    if (p.flags & 0x400) continue;
    if (at + k * 4 > cap) return -1;
    std::memcpy(out + at, p.scratch, static_cast<size_t>(k) * 4);
    at += k * 4;
  }
  return at;
}
// The light list's full and reach, as 0x0055ebf0 would leave them.
void probe_pm_light_reach(int i, int full, int reach) {
  ::zmod::light_list().list[i].full = full, ::zmod::light_list().list[i].reach = reach;
}
void probe_pm_set_frames(uint32_t frame, uint32_t update) { ::zcls::frames().frame = frame, ::zcls::frames().update_frame = update; }
// test_pm_sets.py: the casters (0x8c bytes each, the exe's layout), the light map's
// surface; then the draw to the end of its sets, every record.
void probe_pm_casters(int count, const uint8_t* raw, uint32_t texture_base) {
  static std::vector<::zmod::ShadowCaster> keep;
  static std::vector<::zmod::ShadowCaster*> list;
  keep.assign(static_cast<size_t>(count), ::zmod::ShadowCaster{});
  list.clear();
  for (int i = 0; i < count; ++i) {
    ::zmod::ShadowCaster& c = keep[static_cast<size_t>(i)];
    std::memcpy(c.position, raw + i * 0x8c + 4, 12);
    std::memcpy(&c.ground_y, raw + i * 0x8c + 0x1c, 4);
    std::memcpy(c.slope, raw + i * 0x8c + 0x20, 8);
    std::memcpy(&c.slope_len, raw + i * 0x8c + 0x28, 4);
    std::memcpy(c.box_min, raw + i * 0x8c + 0x2c, 12), std::memcpy(c.box_max, raw + i * 0x8c + 0x38, 12);
    std::memcpy(c.scale, raw + i * 0x8c + 0x44, 8), std::memcpy(c.slope_cam, raw + i * 0x8c + 0x4c, 12);
    c.texture = reinterpret_cast<void*>(static_cast<uintptr_t>(texture_base + static_cast<uint32_t>(i)));
    list.push_back(&c);
  }
  ::zmod::ShadowCasters& s = ::zmod::shadow_casters();
  s.list = list.data(), s.count = count;
}
void probe_pm_light_map_surface(uint32_t surface) {
  static ::zimg::TextureEntry entry{};
  entry.surface = reinterpret_cast<void*>(static_cast<uintptr_t>(surface));
  pm::zimg::set_light_map(&entry);
}
int probe_pm_sets(int node, const float* world, uint32_t mask, int fpu53, uint8_t* out, int cap) {
  zfix::options().fpu53 = fpu53 != 0;
  static std::string b;
  b.clear();
  const auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  const auto i32 = [&](int32_t v) { put(&v, 4); };
  std::memset(::zmod::bake_state().stale, 0, 12);
  ::zcls::Node* n = ::zcls::node_at(node);
  pm::zmod::Model& m = *n->pm_model;
  zmth::MatrixStack& st = zmth::matrix_stack();
  const zmth::MatrixStack saved = st;
  zmth::Mat34 base, local;
  std::memcpy(base.m, world, 48);
  st.top = 0, st.slot[0] = &base, st.identity[0] = 0;
  zmth::push(&local);
  float centre[3] = {};
  pm::zmod::ModelPass pass;
  int ok = pm::zmod::prepare_model(m, centre);
  if (ok) {
    ok = pm::zmod::begin_model(*n, m, mask, pass);
    if (ok) {
      pm::zmod::RefPass ref;
      for (int32_t r = 0; r < m.count(0x5c); ++r) {
        if (!pm::zmod::build_records(m, m.refs[r], pass, ref)) continue;
        pm::zmod::light_records(m, pass, ref);
        const ::zmod::Material& mat = *m.refs[r].polygon->materials[0];
        pm::zmod::split_strips(mat, pass, ref);
        pm::zmod::split_fans(pass, ref);
        pm::zmod::shadow_sets(mat, pass, ref);
        pm::zmod::light_map_sets(ref);
        for (const pm::zmod::Record& q : ref.records) {
          i32(static_cast<int32_t>(q.polygon - m.polygons)), i32(r), i32(static_cast<int32_t>(q.flags)), i32(q.count);
          i32(static_cast<int32_t>(q.codes));
          if (q.count == 0) continue;
          put(q.pos, static_cast<size_t>(q.count) * 12);
          for (int32_t k = 0; k < q.count; ++k) put(q.rec + k * 8, 32), put(q.tl + k * 8 + 6, 8);
          const int32_t sets = q.shadows ? q.shadows->count : 0;
          static const float kNone[3] = {};
          put(q.normal ? q.normal : kNone, 12);
          i32(sets);
          for (int32_t k = 0; k < sets; ++k) {
            const uint32_t tex = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(q.shadows->texture[k]));
            put(&tex, 4), i32(q.shadows->kind[k]);
            put(q.shadows->uv[k].data(), static_cast<size_t>(q.count) * 8);
            if (q.shadows->kind[k] == 2) put(q.shadows->xyz[k].data(), static_cast<size_t>(q.count) * 12), put(&q.shadows->fade[k], 4);
          }
        }
      }
    }
    zmth::pop();
  }
  st = saved;
  if (static_cast<int>(b.size()) + 12 > cap) return -1;
  uint32_t head[3] = {static_cast<uint32_t>(ok), pass.needs, static_cast<uint32_t>(pass.shadows) | pass.light_maps << 1 | pass.projected << 2 | pass.bake << 3 | pass.cull << 4};
  std::memcpy(out, head, 12), std::memcpy(out + 12, b.data(), b.size());
  return static_cast<int>(b.size()) + 12;
}
// test_pm_projected.py: a record made up from the test's arrays: rec and tl (0x20 bytes
// a vertex), positions, sets (kind, then UVs, then a map set's colours). 0x00562620's
// answer; 0x00562840's, with each triangle: its TL, fog, set UVs and colours.
int probe_pm_projected(int node, int poly, uint32_t flags, int count, float* pos, float* rec, float* tl, int sets,
                       const int32_t* kinds, const float* uvs, const float* xyz, int* reach, uint8_t* out, int cap) {
  static std::string b;
  b.clear();
  pm::zmod::Model& m = *::zcls::node_at(node)->pm_model;
  pm::zmod::Record r;
  r.polygon = &m.polygons[poly], r.flags = flags, r.count = count, r.pos = pos, r.rec = rec, r.tl = tl;
  pm::zmod::ClipShadows s;
  if (sets >= 0) {
    s.count = sets;
    for (int k = 0; k < sets; ++k) {
      s.kind[k] = kinds[k];
      s.uv[k].assign(uvs + k * count * 2, uvs + (k + 1) * count * 2);
      s.xyz[k].assign(xyz + k * count * 3, xyz + (k + 1) * count * 3);
    }
    r.shadows = &s;
  }
  *reach = pm::zmod::projected_reach(r);
  const int ret = pm::zmod::draw_projected(r, [](const pm::zmod::ProjectedTriangle& t, const ::zmod::Material&) {
    b.append(reinterpret_cast<const char*>(t.v), sizeof t.v);
    const int32_t n = t.sets ? t.sets->count : 0;
    for (int k = 0; k < n; ++k) {
      b.append(reinterpret_cast<const char*>(t.sets->uv[k].data()), 24);
      if (t.sets->kind[k] == 2) b.append(reinterpret_cast<const char*>(t.sets->xyz[k].data()), 36);
    }
  });
  if (static_cast<int>(b.size()) + 4 > cap) return -1;
  std::memcpy(out, &ret, 4), std::memcpy(out + 4, b.data(), b.size());
  return static_cast<int>(b.size()) + 4;
}
void probe_pm_set_fpu53(int on) { zfix::options().fpu53 = on != 0; }
void probe_pm_set_bugs(int on) { zfix::options().bugs = on != 0; }
// test_pm_submit.py: the whole draw, every device call: kind, count, cull, primitive,
// shade, fade, overlay, its TL vertices, then its sets' UVs and colours.
float probe_pm_face(const float* p, float* n, int fpu53) {
  zfix::options().fpu53 = fpu53 != 0;
  return pm::zmod::face(p, n);
}

// PM's device functions (pm/gamez/zvideo/zvid_tl) into a recording Render3D.
namespace device_probe {
std::vector<int32_t> g_log;
zvid::D3DTexture g_surfaces[8];
zimg::TextureEntry g_entries[8];
zimg::Texture g_images[8];
class Recorder : public platform::Render3D {
 public:
  int32_t create_texture(int, int, const uint8_t*, uint8_t) override { return 0; }
  void release_texture(int32_t) override {}
  void update_texture(int32_t, int, int, const uint8_t*) override {}
  void set_viewport(int, int, int, int) override {}
  void clear(bool, uint8_t, uint8_t, uint8_t) override {}
  void begin_scene() override {}
  void end_scene() override {}
  void draw(platform::Primitive type, const platform::TLVertex* v, int count, const platform::DrawState& s) override {
    g_log.insert(g_log.end(), {1, static_cast<int32_t>(type), count, s.texture, s.address_u, s.address_v, s.blend, s.multiply,
                               s.additive, s.depth_write, s.depth_always, s.alpha_modulate, s.fog, s.flat, s.color_select});
    const auto* w = reinterpret_cast<const int32_t*>(v);
    g_log.insert(g_log.end(), w, w + 8 * count);
  }
  void draw_light_map(platform::Primitive type, const platform::LightMapVertex* v, int count, int32_t map, const platform::DrawState& s) override {
    g_log.insert(g_log.end(), {2, static_cast<int32_t>(type), count, s.texture, s.address_u, s.address_v, s.blend, s.multiply,
                               s.additive, s.depth_write, s.depth_always, s.alpha_modulate, s.fog, s.flat, s.color_select, map});
    const auto* w = reinterpret_cast<const int32_t*>(v);
    g_log.insert(g_log.end(), w, w + 10 * count);
  }
  void set_fog_color(uint8_t r, uint8_t g, uint8_t b) override { g_log.insert(g_log.end(), {6, r, g, b}); }
  void present() override {}
};
Recorder g_recorder;
int take(int32_t* out, int cap) {
  const int n = static_cast<int>(g_log.size());
  if (n > cap) return -1;
  std::memcpy(out, g_log.data(), g_log.size() * 4);
  g_log.clear();
  return n;
}
}  // namespace device_probe

void probe_pm_device_start(const int32_t* surfaces, const int32_t* format) {
  using namespace device_probe;
  zvid::set_render3d(&g_recorder);
  zvid::draw_state() = platform::DrawState{};
  zvid::device_state() = zvid::DeviceState{};
  zvid::device_state().z_write = 1;
  zvid::transparent_queue().count = zvid::transparent_queue().vertex_count = 0;
  zvid::overlay_list().count = 0, zvid::overlay_list().set_limit(zvid::OverlayList::kMaxPm);
  zvid::set_pixel_format(format[0], format[1], format[2]);
  zvid::channel_masks() = {static_cast<uint32_t>(format[3]), static_cast<uint32_t>(format[4]), static_cast<uint32_t>(format[5])};
  zrndr::device_fog().active = 1;
  for (int i = 0; i < 8; ++i) {
    zvid::D3DTexture& t = g_surfaces[i];
    t.handle = surfaces[i * 5], t.type = surfaces[i * 5 + 1], t.address_u = surfaces[i * 5 + 2], t.address_v = surfaces[i * 5 + 3];
    g_images[i].stretch = static_cast<int16_t>(surfaces[i * 5 + 4]);
    g_entries[i].image = &g_images[i], g_entries[i].surface = &t;
  }
  g_log.clear();
}

void probe_pm_device_fog(const float* colour) {
  std::memcpy(zrndr::device_fog().sent, colour, 12);
  zvid::send_fog_color();
}

int probe_pm_device_call(int kind, int count, const float* tl, int cull, int primitive, int rgb, int alpha, int tex, float fade, int overlay,
                         int shade, int nsets, const int32_t* set_kinds, const int32_t* set_tex, const float* uvs, const float* xyz, const uint32_t* fades, int32_t* out, int cap) {
  using namespace device_probe;
  ::zmod::Material m{};
  m.alpha = static_cast<uint8_t>(alpha), m.rgb = static_cast<uint16_t>(rgb);
  m.texture = tex >= 0 ? &g_entries[tex] : nullptr;
  pm::zmod::ClipShadows sets;
  sets.count = nsets;
  for (int k = 0; k < nsets; ++k) {
    sets.kind[k] = set_kinds[k], sets.texture[k] = &g_surfaces[set_tex[k]];
    sets.uv[k].assign(uvs + k * count * 2, uvs + (k + 1) * count * 2);
    if (set_kinds[k] == 2) sets.xyz[k].assign(xyz + k * count * 3, xyz + (k + 1) * count * 3), sets.fade[k] = fades[k];
  }
  pm::zmod::DeviceCall d;
  d.kind = static_cast<pm::zmod::DeviceCall::Kind>(kind);
  d.tl = tl, d.count = count, d.cull = static_cast<uint32_t>(cull), d.primitive = primitive, d.material = &m;
  d.fade = fade, d.overlay = overlay, d.shade = shade, d.sets = nsets >= 0 ? &sets : nullptr;
  pm::zvid::device_call(d);
  return take(out, cap);
}

int probe_pm_device_flush(int which, int32_t* out, int cap) {
  if (which == 0) zvid::flush_transparent();
  else if (which == 1) zvid::flush_overlay();
  return device_probe::take(out, cap);
}

// PM's light setters on a light given by PM's offsets (0x100 bytes): the result and the bytes,
// and how many times the world's bake was marked stale (frames().update_frame's move).
int probe_pm_light_setter(int which, uint8_t* pm_bytes, float a, float b, float c) {
  static ::zcls::Node node;
  static ::zcls::Light light;
  const auto copy_in = [&] {
    std::memcpy(light.raw, pm_bytes, 0xb0);
    std::memcpy(light.pm_colors, pm_bytes + 0xb0, 0x30);
    std::memcpy(light.raw + 0xb0, pm_bytes + 0xe0, 0x20);
  };
  copy_in();
  node.data = &light;
  ::zcls::frames() = ::zcls::Frames{};
  ::zcls::frames().update_frame = 0;
  int r = 0;
  switch (which) {
    case 0: r = pm::zcls::light_set_orientation(&node, a, b, c); break;
    case 1: r = pm::zcls::light_set_ambient(&node, a); break;
    case 2: r = pm::zcls::light_set_diffuse(&node, a); break;
    case 3: r = pm::zcls::light_set_diffuse_color(&node, a, b, c); break;
    case 4: r = pm::zcls::light_set_ambient_color(&node, a, b, c); break;
    default: r = pm::zcls::light_set_own_colors(&node, static_cast<int>(a)); break;
  }
  std::memcpy(pm_bytes, light.raw, 0xb0);
  std::memcpy(pm_bytes + 0xb0, light.pm_colors, 0x30);
  std::memcpy(pm_bytes + 0xe0, light.raw + 0xb0, 0x20);
  return r | (::zcls::frames().update_frame != 0) << 8;
}

// A material's texture says it has an alpha plane, or the material takes back what it had.
void probe_pm_material_alpha(int node, int ref, int on) {
  static zimg::Texture image;
  static std::remove_pointer_t<decltype(::zmod::Material::texture)> record{};
  static decltype(::zmod::Material::texture) saved = nullptr;
  ::zmod::Material& m = *::zcls::node_at(node)->pm_model->refs[ref].polygon->materials[0];
  image.flags = 2;
  record.image = &image;
  if (on) saved = m.texture, m.texture = &record;
  else m.texture = saved;
}

int probe_pm_submit(int node, const float* world, uint32_t mask, int fpu53, const float* rect, int overlay, uint8_t* out, int cap) {
  zfix::options().fpu53 = fpu53 != 0;
  static std::string b;
  b.clear();
  std::memset(::zmod::bake_state().stale, 0, 12);
  ::zmod::draw_state().overlay = overlay;
  pm::zmod::Clip3dState& a = pm::zmod::clip3d_state();
  a.left = rect[0], a.top = rect[1], a.right = rect[2], a.bottom = rect[3];
  zmth::MatrixStack& st = zmth::matrix_stack();
  const zmth::MatrixStack saved = st;
  zmth::Mat34 base;
  std::memcpy(base.m, world, 48);
  st.top = 0, st.slot[0] = &base, st.identity[0] = 0;
  pm::zmod::draw_model(::zcls::node_at(node), mask, [](const pm::zmod::DeviceCall& d) {
    const int32_t head[7] = {d.kind, d.count, static_cast<int32_t>(d.cull), d.primitive, d.kind == pm::zmod::DeviceCall::kProjected ? static_cast<int32_t>(d.flags) : d.shade, d.overlay, d.sets ? d.sets->count : -1};
    b.append(reinterpret_cast<const char*>(head), sizeof head);
    b.append(reinterpret_cast<const char*>(&d.fade), 4);
    b.append(reinterpret_cast<const char*>(d.tl), static_cast<size_t>(d.count) * 32);
    if (d.sets) {
      for (int32_t k = 0; k < d.sets->count; ++k) {
        b.append(reinterpret_cast<const char*>(&d.sets->kind[k]), 4);
        b.append(reinterpret_cast<const char*>(d.sets->uv[k].data()), static_cast<size_t>(d.count) * 8);
        if (d.sets->kind[k] == 2) b.append(reinterpret_cast<const char*>(d.sets->xyz[k].data()), static_cast<size_t>(d.count) * 12);
      }
    }
  });
  st = saved;
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}
void probe_pm_set_node(int node, int off, uint32_t v) { ::zcls::node_at(node)->put<uint32_t>(off, v); }
// The light map's gate: a texture or none, the option (0x0040b270's value).
void probe_pm_records_light_map(int on, int option) {
  static ::zimg::TextureEntry entry{};
  pm::zimg::set_light_map(on ? &entry : nullptr);
  pm::zmod::draw_state().light_map_option = option;
}
// Polygon `poly` given k more materials' UVs (k + 1 materials, the first kept):
// the same material again for each, as the UVs alone are what is read here.
void probe_pm_set_sets(int node, int poly, int k, const float* uvs) {
  pm::zmod::Polygon& p = ::zcls::node_at(node)->pm_model->polygons[poly];
  const int n = static_cast<int>(p.flags & 0x3ff);
  auto** mats = new ::zmod::Material*[static_cast<size_t>(k) + 1];
  auto** arr = new float*[static_cast<size_t>(k) + 1];
  mats[0] = p.materials[0], arr[0] = p.uvs ? p.uvs[0] : nullptr;
  for (int i = 0; i < k; ++i) {
    mats[i + 1] = p.materials[0];
    arr[i + 1] = new float[static_cast<size_t>(n) * 2];
    std::memcpy(arr[i + 1], uvs + i * n * 2, static_cast<size_t>(n) * 8);
  }
  p.materials = mats, p.uvs = arr, p.material_count = k + 1;
}
// test_pm_records.py: 0x00543b10's model part and each ref's records, as offsets
// into the arenas, and what they hold. The caller loaded and set a view
// (probe_pm_draw_load, probe_pm_draw_view).
int probe_pm_records(int node, const float* world, uint32_t mask, uint8_t* out, int cap) {
  static std::string b;
  b.clear();
  const auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  const auto i32 = [&](int32_t v) { put(&v, 4); };
  ::zcls::Node* n = ::zcls::node_at(node);
  pm::zmod::Model& m = *n->pm_model;
  zmth::MatrixStack& st = zmth::matrix_stack();
  const zmth::MatrixStack saved = st;
  zmth::Mat34 base, local;
  std::memcpy(base.m, world, 48);
  st.top = 0, st.slot[0] = &base, st.identity[0] = 0;
  zmth::push(&local);
  float centre[3] = {};
  pm::zmod::ModelPass pass;
  int ok = pm::zmod::prepare_model(m, centre);
  if (ok) {
    ok = pm::zmod::begin_model(*n, m, mask, pass);
    if (ok) {
      pm::zmod::RefPass ref;
      for (int32_t r = 0; r < m.count(0x5c); ++r) {
        pm::zmod::build_records(m, m.refs[r], pass, ref);
        for (const pm::zmod::Record& q : ref.records) {
          i32(static_cast<int32_t>(q.polygon - m.polygons)), i32(r), i32(static_cast<int32_t>(q.flags)), i32(q.count);
          i32(static_cast<int32_t>(q.codes));
          put(q.pos, static_cast<size_t>(q.polygon->flags & 0x3ff) * 12);
          const uint32_t sets = q.shadows ? static_cast<uint32_t>(q.shadows->count) : 0;
          i32(q.normal != nullptr), i32(q.normals != nullptr), i32(static_cast<int32_t>(sets));
          if (q.normal && pass.cull) put(q.normal, 12);
          if (q.normals) put(q.normals, static_cast<size_t>(q.polygon->flags & 0x3ff) * 12);
          for (uint32_t k = 0; k < sets; ++k) put(q.shadows->uv[k].data(), q.shadows->uv[k].size() * 4);
          const bool textured = (m.refs[r].polygon->materials[0]->flags & 1) != 0;
          for (uint32_t k = 0; k < (q.polygon->flags & 0x3ff); ++k) {
            if (q.flags & 8 || (pass.needs & 1)) put(q.rec + k * 8 + 4, 4);
            if (textured) put(q.rec + k * 8 + 5, 12), put(q.tl + k * 8 + 6, 8);
          }
        }
      }
    }
    zmth::pop();
  }
  st = saved;
  if (static_cast<int>(b.size()) + 12 > cap) return -1;
  uint32_t head[3] = {static_cast<uint32_t>(ok), pass.needs, static_cast<uint32_t>(pass.shadows) | pass.light_maps << 1 | pass.projected << 2 | pass.bake << 3 | pass.cull << 4};
  std::memcpy(out, head, 12), std::memcpy(out + 12, b.data(), b.size());
  return static_cast<int>(b.size()) + 12;
}
// A LoD's data as PM's exe holds it (0x5c bytes), through the reader's translation.
void probe_pm_lod_set(int node, const uint8_t* pm) {
  pm::zcls::translate_lod(*static_cast<::zcls::Lod*>(::zcls::node_at(node)->data), pm);
}
void probe_pm_light_edit(uint32_t flags, float x, float y, float z, float range) {
  auto* w = static_cast<::zcls::World*>(::zcls::camera_world(walk_camera())->data);
  auto* l = static_cast<::zcls::Light*>(w->light_data[0]);
  l->put<uint32_t>(0xb0, flags);
  l->put<float>(0x60, x), l->put<float>(0x64, y), l->put<float>(0x68, z), l->put<float>(0xb8, range);
}
int probe_pm_walk_frame(const float* pos, const float* rot, uint32_t frame, float dt, int hse, uint8_t* out, int cap) {
  ::zcls::scene_walk().hidden_surface = hse;
  ::zcls::Node* c = walk_camera();
  ::zcls::camera_set_rotation(c, rot[0], rot[1], rot[2]);
  ::zcls::camera_set_position(c, pos[0], pos[1], pos[2]);
  ::zcls::frames().frame = frame;
  ztimer::state().dt = dt;
  g_walk_log.clear();
  ::zcls::render_world(c, 0);
  auto* w = static_cast<::zcls::World*>(::zcls::camera_world(c)->data);
  for (int32_t i = 0; i < w->get<int32_t>(0xa0); ++i) {
    walk_put(static_cast<::zcls::Light*>(w->light_data[i])->get<uint32_t>(0xb0));
  }
  if (static_cast<int>(g_walk_log.size()) > cap) return -1;
  std::memcpy(out, g_walk_log.data(), g_walk_log.size());
  return static_cast<int>(g_walk_log.size());
}
void probe_pm_flip_flags(int node, uint32_t mask) {
  ::zcls::Node* n = ::zcls::node_at(node);
  n->set_flags(n->flags() ^ mask);
}
void probe_pm_set_light(int node, const float* points, int count) {
  auto* l = new ::zmod::Light{};
  std::memcpy(l->raw + 0xc, &count, 4);
  l->vertices = new float[static_cast<size_t>(count) * 3];
  std::memcpy(l->vertices, points, static_cast<size_t>(count) * 12);
  ::zcls::node_at(node)->pm_model->lights = l;
}
void probe_pm_set_model(int node, int off, uint32_t v) {
  pm::zmod::Model* m = ::zcls::node_at(node)->pm_model;
  if (m) std::memcpy(m->header + off, &v, 4);
}
void probe_pm_set_morphs(int node, const float* deltas, int count) {
  static std::vector<float> keep;
  keep.assign(deltas, deltas + count * 3);
  pm::zmod::Model* m = ::zcls::node_at(node)->pm_model;
  if (m) m->morphs = keep.data();
}
// 0x00542a70 on the node's model. Out: result, default flag, the stack's top and
// the one below, the centre, the camera-space vertices (points only), the points.
int probe_pm_prepare(int node, const float* world, uint8_t* out, int cap) {
  static std::string b;
  b.clear();
  auto put = [&](const void* p, size_t n) { b.append(static_cast<const char*>(p), n); };
  static zmth::Mat34 w, local;
  std::memcpy(w.m, world, 48);
  zmth::MatrixStack& st = zmth::matrix_stack();
  st.top = 0, st.slot[0] = &w, st.identity[0] = 0;
  zmth::push(&local);
  static std::string points;
  points.clear();
  ::zmod::DrawTable& table = ::zmod::draw_table();
  const ::zmod::DrawTable keep = table;
  table.point = [](void*, const float screen[3], uint32_t color, int32_t alpha) {
    points.append(reinterpret_cast<const char*>(screen), 12);
    const uint32_t c = color & 0xffff;
    points.append(reinterpret_cast<const char*>(&c), 4);
    points.append(reinterpret_cast<const char*>(&alpha), 4);
  };
  pm::zmod::Model& m = *::zcls::node_at(node)->pm_model;
  float centre[3] = {};
  const int32_t r = pm::zmod::prepare_model(m, centre);
  const int32_t d = pm::zmod::draw_state().default_model;
  const int32_t depth = st.top;  // what prepare left: read the matrices by address, as the exe's side
  put(&r, 4), put(&d, 4), put(&depth, 4), put(local.m, 48), put(w.m, 48), put(centre, 12);
  const int32_t np = static_cast<int32_t>(points.size() / 20);
  put(&np, 4);
  b += points;
  if (m.count(0) == 2 && m.count(0x14) > 0) put(pm::zmod::draw_state().cam.data(), static_cast<size_t>(m.count(0x14)) * 12);
  if (st.top > 0) zmth::pop();
  table = keep;
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}

// test_pm_cull.py: 0x54faa0, 0x54fc60 and 0x5439e0, on the state given.
int probe_pm_plane_codes(uint32_t planes, float near_z, float far_z, float sx, float sy, const float* pos, int n,
                         uint32_t* codes) {
  pm::zmod::CullState& c = pm::zmod::cull_state();
  c.planes = planes, c.far_z = far_z, c.slope_x = sx, c.slope_y = sy;
  pm::zmod::clip3d_state().near_z = near_z;
  return pm::zmod::plane_codes(pos, n, codes);
}
int probe_pm_polygon_codes(const uint32_t* table, int n, const int32_t* vertices, uint32_t* out) {
  std::memcpy(pm::zmod::cull_state().codes, table, sizeof pm::zmod::cull_state().codes);
  return pm::zmod::polygon_codes(n, vertices, out);
}
void probe_pm_tl_colours(int fpu53, float fade, int count, float* rec, float* tl) {
  const bool keep = zfix::options().fpu53;
  zfix::options().fpu53 = fpu53 != 0;
  pm::zmod::set_fade(fade);
  pm::zmod::ClipPoly p;
  p.count = count, p.rec = rec, p.tl = tl;
  pm::zmod::tl_colours(p);
  zfix::options().fpu53 = keep;
}

// test_pm_clip3d.py: one polygon through clipper `which` (0..5, the exe's
// order), the buffers rewritten in place. Out: the result, the count, whether
// each of pos, rec, tl, extra moved to its arena, and how far each arena went.
// The fog of a polygon with sets, near cut, bugs on: FIX (bug) on 0x0054a6f0. 1 if it matches
// the plain textured clipper's, which carries it; 0 if not.
int probe_pm_clip3d_fog_fix() {
  float pos[8 * 3] = {-1, -1, 5, 1, -1, 5, 1, 1, -5, -1, 1, -5};
  float rec[2][8 * 8] = {}, tl[2][8 * 8] = {};
  uint8_t extra[8] = {};
  float arena_pos[8 * 3], arena_rec[8 * 8], arena_tl[8 * 8];
  uint8_t arena_extra[8];
  for (int v = 0; v < 4; ++v) rec[0][v * 8 + 4] = rec[1][v * 8 + 4] = 0.1f * static_cast<float>(v + 1);
  float out[2][4];
  pm::zmod::ClipShadows none;
  zfix::options().bugs = true;
  for (int k = 0; k < 2; ++k) {
    float p[8 * 3];
    std::memcpy(p, pos, sizeof p);
    pm::zmod::Clip3dState& st = pm::zmod::clip3d_state();
    st.near_z = 1.0f, st.arena_pos = arena_pos, st.arena_rec = arena_rec, st.arena_tl = arena_tl, st.arena_extra = arena_extra;
    pm::zmod::ClipPoly c;
    c.pos = p, c.rec = rec[k], c.tl = tl[k], c.extra = extra, c.count = 4, c.flags = 8, c.codes = 0x10;
    if (k) c.shadows = &none;
    if (!(k ? pm::zmod::clip3d_textured_shadow(c) : pm::zmod::clip3d_textured(c)) || c.count != 4) return 0;
    for (int v = 0; v < 4; ++v) out[k][v] = c.rec[v * 8 + 4];
  }
  zfix::options().bugs = false;
  return std::memcmp(out[0], out[1], sizeof out[0]) == 0;
}

int probe_pm_clip3d(int which, float near_z, int count, uint32_t flags, float* pos, float* rec, float* tl, uint8_t* extra,
                    void* shadows, float* a_pos, float* a_rec, float* a_tl, uint8_t* a_extra, int32_t* out) {
  pm::zmod::ClipPoly p;
  p.pos = pos, p.rec = rec, p.tl = tl, p.extra = extra, p.flags = flags, p.count = count;
  // The exe's record (0x2358 bytes) to the port's growing sets, and back after.
  auto* raw = static_cast<uint8_t*>(shadows);
  pm::zmod::ClipShadows sets;
  std::memcpy(&sets.count, raw, 4);
  std::memcpy(sets.kind, raw + 0x20, 0x1c);
  for (int k = 0; k < 7; ++k) {
    sets.xyz[k].resize(64 * 3), sets.uv[k].resize(64 * 2);
    std::memcpy(sets.xyz[k].data(), raw + 0x3c + k * 0x300, 0x300);
    std::memcpy(sets.uv[k].data(), raw + 0x1558 + k * 0x200, 0x200);
  }
  p.shadows = &sets;
  pm::zmod::Clip3dState& s = pm::zmod::clip3d_state();
  s.near_z = near_z, s.arena_pos = a_pos, s.arena_rec = a_rec, s.arena_tl = a_tl, s.arena_extra = a_extra;
  int (*const fn[6])(pm::zmod::ClipPoly&) = {pm::zmod::clip3d_plain,           pm::zmod::clip3d_lit,
                                             pm::zmod::clip3d_textured,        pm::zmod::clip3d_textured_lit,
                                             pm::zmod::clip3d_textured_shadow, pm::zmod::clip3d_textured_lit_shadow};
  out[0] = fn[which](p);
  for (int k = 0; k < 7; ++k) {
    std::memcpy(raw + 0x3c + k * 0x300, sets.xyz[k].data(), 0x300);
    std::memcpy(raw + 0x1558 + k * 0x200, sets.uv[k].data(), 0x200);
  }
  out[1] = p.count;
  out[2] = p.pos != pos, out[3] = p.rec != rec, out[4] = p.tl != tl, out[5] = p.extra != extra;
  out[6] = static_cast<int32_t>(s.arena_pos - a_pos), out[7] = static_cast<int32_t>(s.arena_rec - a_rec);
  out[8] = static_cast<int32_t>(s.arena_tl - a_tl), out[9] = static_cast<int32_t>(s.arena_extra - a_extra);
  return 0;
}

// test_pm_clip2d.py: one polygon through a side clipper (0..4), as probe_pm_clip3d.
int probe_pm_clip2d(int which, uint32_t codes, const float* sides, int count, float* rec, float* tl, uint8_t* extra,
                    void* shadows, float* a_rec, float* a_tl, uint8_t* a_extra, int32_t* out) {
  pm::zmod::ClipPoly p;
  p.rec = rec, p.tl = tl, p.extra = extra, p.count = count, p.codes = codes;
  auto* raw = static_cast<uint8_t*>(shadows);
  pm::zmod::ClipShadows sets;
  std::memcpy(&sets.count, raw, 4);
  std::memcpy(sets.kind, raw + 0x20, 0x1c);
  for (int k = 0; k < 7; ++k) {
    sets.xyz[k].resize(64 * 3), sets.uv[k].resize(64 * 2);
    std::memcpy(sets.xyz[k].data(), raw + 0x3c + k * 0x300, 0x300);
    std::memcpy(sets.uv[k].data(), raw + 0x1558 + k * 0x200, 0x200);
  }
  p.shadows = &sets;
  pm::zmod::Clip3dState& s = pm::zmod::clip3d_state();
  s.left = sides[0], s.right = sides[1], s.top = sides[2], s.bottom = sides[3];
  s.arena_rec = a_rec, s.arena_tl = a_tl, s.arena_extra = a_extra;
  int (*const fn[5])(pm::zmod::ClipPoly&) = {pm::zmod::clip2d_plain, pm::zmod::clip2d_lit, pm::zmod::clip2d_textured,
                                             pm::zmod::clip2d_textured_lit, pm::zmod::clip2d_textured_lit_shadow};
  out[0] = fn[which](p);
  for (int k = 0; k < 7; ++k) {
    std::memcpy(raw + 0x3c + k * 0x300, sets.xyz[k].data(), 0x300);
    std::memcpy(raw + 0x1558 + k * 0x200, sets.uv[k].data(), 0x200);
  }
  out[1] = p.count;
  out[2] = p.rec != rec, out[3] = p.tl != tl, out[4] = p.extra != extra;
  out[5] = static_cast<int32_t>(s.arena_rec - a_rec), out[6] = static_cast<int32_t>(s.arena_tl - a_tl);
  out[7] = static_cast<int32_t>(s.arena_extra - a_extra);
  return 0;
}

// test_pm_light_map.py: script lines ('|'-separated, words by spaces) through
// the dispatcher with PM's table. texture_add answers 0x70000000 + 0x10 * call
// number. Out, per line: the light map, the cursor, the misses, the names asked.
// The script's TextureAdditiveTransparent and MipBias: per line the cursor and the known images' +0x0c.
int probe_pm_additive(const char* lines, int16_t* images, int count, int32_t* cursors) {
  static ::zimg::Texture image[2];
  static ::zimg::TextureEntry entry[3];
  zgame::set_game(pm::table());
  ::zint::Engine keep = ::zint::engine();
  entry[0].image = &image[0], entry[1].image = &image[1], entry[2].image = nullptr;
  ::zint::engine().find_texture = [](const char* name) -> ::zimg::TextureEntry* {
    if (!std::strcmp(name, "one")) return &entry[0];
    if (!std::strcmp(name, "two")) return &entry[1];
    if (!std::strcmp(name, "noimage")) return &entry[2];
    return nullptr;
  };
  image[0].stretch = 3, image[1].stretch = 0;
  std::string all = lines;
  int n = 0;
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
    cursors[n++] = static_cast<int32_t>(in.cursor);
  }
  images[0] = image[0].stretch, images[1] = image[1].stretch;
  ::zint::engine() = keep;
  return count == n ? 0 : -1;
}

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

extern "C" {

// pm::mech3::world_read_params on a small scene (test_pm_world_params.py): `names` is the list's
// nodes joined by \n, the skies being those from index 3 on. Words out: the world's +0x00..+0x33,
// the camera's clip and its stale flag, the light's 0x100 bytes, each node's flags, the clouds, stale.
// cfg: sun option, has sun, has horizon, light flags. Returns the words, or -1 if the archive fails.
int probe_pm_world_params(const char* dir, const char* archive, const char* key, const char* names,
                          const int32_t* cfg, uint32_t* out) {
  mount_only(dir);
  zrdr::close_archives(true);
  zrdr::init();
  if (!zrdr::open_archive(archive, true)) return -1;
  zvid::set_pixel_format(5, 6, 5);
  ::zcls::reset_lists();
  ::zerr::set_sink(nullptr);
  static ::zcls::Node nodes[32], *kids[32], horizon, sun, world, camera;
  static ::zcls::Light light;
  static ::zcls::World wd;
  static ::zcls::Camera cd;
  const auto setup = [](::zcls::Node& n, const char* name, int cls, void* data) {
    n = ::zcls::Node{};
    std::strcpy(reinterpret_cast<char*>(n.raw), name);
    n.put<int32_t>(0x34, cls);
    n.set_flags(0x10);
    n.data = data;
  };
  std::string all(names);
  int count = 0;
  for (size_t at = 0; at < all.size();) {
    size_t end = all.find('\n', at);
    if (end == std::string::npos) end = all.size();
    setup(nodes[count], all.substr(at, end - at).c_str(), 5, nullptr);
    if (cfg[2]) nodes[count].set_flags(count % 2 ? 4 : 0x10);
    ::zcls::push_back(::zcls::kAllNodes, &nodes[count]);
    kids[count] = &nodes[count];
    ++count;
    at = end + 1;
  }
  setup(horizon, "horizon", 5, nullptr);
  horizon.put<int32_t>(0x5c, count), horizon.children = kids;
  wd = ::zcls::World{};
  for (int o = 4; o < 0x34; o += 4) wd.put<uint32_t>(o, 0x3f800000u + o * 0x1000);
  setup(world, "world1", 13, &wd);
  cd = ::zcls::Camera{};
  cd.put<float>(0xb0, 0.5f), cd.put<float>(0xb4, 777.0f);
  cd.nodes[2] = cfg[2] ? &horizon : nullptr;
  setup(camera, "camera1", 1, &cd);
  uint8_t bytes[0x100];
  for (int i = 0; i < 0x100; i += 4) {
    const float v = 0.125f + 0.0625f * (i / 4 % 16);
    std::memcpy(bytes + i, &v, 4);
  }
  std::memcpy(bytes + 0xe0, &cfg[3], 4);
  std::memcpy(light.raw, bytes, 0xb0);
  std::memcpy(light.pm_colors, bytes + 0xb0, 0x30);
  std::memcpy(light.raw + 0xb0, bytes + 0xe0, 0x20);
  setup(sun, "sunlight", 9, &light);
  sun.set_flags(cfg[1] == 2 ? 0x10 : 0x14);  // 2: starts inactive
  if (cfg[1]) ::zcls::push_back(::zcls::kAllNodes, &sun);
  ::zmod::clouds() = {7, 1.0f, 2.0f, 0.25f, 0.125f, 0.5f, 0.75f, 0, 0};
  ::zcls::frames() = ::zcls::Frames{};
  ::zcls::frames().update_frame = 0;
  pm::mech3::world_read_params(&world, &camera, key[0] ? key : nullptr, cfg[0] != 0);
  int n = 0;
  for (int o = 0; o < 0x34; o += 4) out[n++] = wd.get<uint32_t>(o);
  out[n++] = cd.get<uint32_t>(0xb0), out[n++] = cd.get<uint32_t>(0xb4), out[n++] = cd.get<uint32_t>(0xf8);
  std::memcpy(bytes, light.raw, 0xb0);
  std::memcpy(bytes + 0xb0, light.pm_colors, 0x30);
  std::memcpy(bytes + 0xe0, light.raw + 0xb0, 0x20);
  std::memcpy(out + n, bytes, 0x100), n += 0x40;
  for (int i = 0; i < count; ++i) out[n++] = nodes[i].flags();
  out[n++] = sun.flags();
  const ::zmod::Clouds& c = ::zmod::clouds();
  out[n++] = static_cast<uint32_t>(c.on);
  for (float f : {c.speed, c.scale, c.base, c.amplitude, c.heading_sin, c.heading_cos}) std::memcpy(out + n++, &f, 4);
  out[n++] = ::zcls::frames().update_frame != 0;
  zrdr::close_archives(true);
  return n;
}

}  // extern "C"

extern "C" {

// pm::zcls::clutter_read_settings on a scene (test_pm_clutter.py): `names` the list's nodes, each
// with two children. Out: the words as the test reads them, the templates' names joined by \n.
int probe_pm_clutter(const char* dir, const char* archive, const char* file, const char* names, uint32_t* out,
                     int cap, char* text, int text_cap) {
  mount_only(dir);
  zrdr::close_archives(true);
  zrdr::init();
  if (!zrdr::open_archive(archive, true)) return -1;
  ::zcls::reset_lists();
  static std::vector<std::unique_ptr<::zcls::Node>> nodes;
  static std::vector<::zcls::Node*> kids;
  nodes.clear(), kids.clear();
  kids.reserve(4096);
  const auto make = [](const std::string& name) {
    nodes.push_back(std::make_unique<::zcls::Node>());
    ::zcls::Node* n = nodes.back().get();
    std::strncpy(reinterpret_cast<char*>(n->raw), name.c_str(), 35);
    n->put<int32_t>(0x34, 5);
    n->set_flags(0x10);
    return n;
  };
  std::string all(names);
  for (size_t at = 0; at < all.size();) {
    size_t end = all.find('\n', at);
    if (end == std::string::npos) end = all.size();
    const std::string name = all.substr(at, end - at);
    ::zcls::Node* n = make(name);
    ::zcls::Node* a = make(name + "_a");
    ::zcls::Node* b = make(name + "_b");
    ::zcls::push_back(::zcls::kAllNodes, n);
    kids.push_back(a), kids.push_back(b);
    n->put<int32_t>(0x5c, 2), n->children = &kids[kids.size() - 2];
    at = end + 1;
  }
  static uint32_t* log_at;
  static int log_n;
  static uint32_t log[4096];
  log_n = 0;
  log_at = log;
  const auto id = [](const ::zcls::Node* n) -> uint32_t {
    for (size_t i = 0; i < nodes.size(); ++i)
      if (nodes[i].get() == n) return static_cast<uint32_t>(i + 1);
    return 0;
  };
  static auto s_id = +id;
  pm::zcls::ClutterHooks& h = pm::zcls::clutter_hooks();
  h.anim_find = [](const char* name) -> void* {
    if (!name) return nullptr;
    uint32_t v = 2166136261u;
    for (const char* p = name; *p; ++p) v = (v ^ static_cast<uint8_t>(*p)) * 16777619u;
    log_at[log_n++] = 0xa0000000u, log_at[log_n++] = v;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(v));
  };
  h.watch_hits = [](::zcls::Node* n) { log_at[log_n++] = 0xb0000000u, log_at[log_n++] = s_id(n); };
  ::zerr::set_sink([](int level, const char*, int at, const char*) {
    log_at[log_n++] = 0xc0000000u, log_at[log_n++] = static_cast<uint32_t>(level) << 16 | static_cast<uint32_t>(at);
  });
  pm::zcls::clutter_templates().clear();
  pm::zcls::clutter_read_settings(file);
  ::zerr::set_sink(nullptr);
  int n = 0;
  const auto word = [&](uint32_t v) { if (n < cap) out[n] = v; ++n; };
  const auto fl = [&](float f) { uint32_t v; std::memcpy(&v, &f, 4); word(v); };
  word(static_cast<uint32_t>(log_n));
  for (int i = 0; i < log_n; ++i) word(log[i]);
  word(static_cast<uint32_t>(pm::zcls::clutter_templates().size()));
  std::string names_out;
  for (const auto& t : pm::zcls::clutter_templates()) {
    names_out += t->name + "\n";
    fl(t->scale[0]), fl(t->scale[1]);
    fl(t->uv_low[0]), fl(t->uv_low[1]), fl(t->uv_high[0]), fl(t->uv_high[1]);
    fl(t->fade_low[0]), fl(t->fade_low[1]), fl(t->fade_high[0]), fl(t->fade_high[1]);
    word(t->align_normal);
    for (float f : t->rotation) fl(f);
    fl(t->max_slope), fl(t->min_slope);
    word(t->on_weapon_hit);
    for (const pm::zcls::ClutterEvent* e : {&t->weapon_hit, &t->crater, &t->collide}) {
      fl(e->health);
      word(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e->anim)));
      word(s_id(e->model));
    }
    word(static_cast<uint32_t>(t->substitutes.size()));
    for (const auto& s : t->substitutes) fl(s.weight), word(s_id(s.node));
  }
  word(static_cast<uint32_t>(nodes.size()));
  for (const auto& p : nodes) word(p->flags());
  std::snprintf(text, static_cast<size_t>(text_cap), "%s", names_out.c_str());
  zrdr::close_archives(true);
  return n;
}

}  // extern "C"

extern "C" {

// pm::zcls::clutter_load_templates on a world (test_pm_clutter.py). `settings` and `names` are
// joined by \n. Out: each error's level << 16 | line, then the groups: each its count, then u, v,
// node index, settings index + 1. Groups in the map's order; the test sorts them.
int probe_pm_clutter_nodes(const char* world, const char* settings, const char* names, uint32_t* out, int cap) {
  const int r = probe_pm_walk_load(world);
  if (r < 0) return -1;
  const auto split = [](const std::string& all) {
    std::vector<std::string> v;
    for (size_t at = 0; at < all.size();) {
      size_t end = all.find('\n', at);
      if (end == std::string::npos) end = all.size();
      v.push_back(all.substr(at, end - at));
      at = end + 1;
    }
    return v;
  };
  pm::zcls::clutter_templates().clear();
  pm::zcls::clutter_pieces().clear();
  for (const std::string& s : split(settings)) {
    auto t = std::make_unique<pm::zcls::ClutterTemplate>();
    t->name = s;
    pm::zcls::clutter_templates().push_back(std::move(t));
  }
  std::list<std::string> list;
  for (const std::string& s : split(names)) list.push_back(s);
  static std::vector<uint32_t> errors;
  errors.clear();
  ::zerr::set_sink([](int level, const char*, int at, const char*) {
    errors.push_back(static_cast<uint32_t>(level) << 16 | static_cast<uint32_t>(at));
  });
  pm::zcls::clutter_load_templates(list);
  ::zerr::set_sink(nullptr);
  int n = 0;
  const auto word = [&](uint32_t v) { if (n < cap) out[n] = v; ++n; };
  word(static_cast<uint32_t>(errors.size()));
  for (uint32_t e : errors) word(e);
  word(static_cast<uint32_t>(pm::zcls::clutter_pieces().size()));
  for (const auto& [texture, pieces] : pm::zcls::clutter_pieces()) {
    word(static_cast<uint32_t>(pieces.size()));
    const std::string texture_name = texture ? texture->name : "";
    word(static_cast<uint32_t>(texture_name.size()));
    for (const char c : texture_name) word(static_cast<uint8_t>(c));
    for (const pm::zcls::ClutterPiece& p : pieces) {
      uint32_t v;
      std::memcpy(&v, &p.u, 4), word(v);
      std::memcpy(&v, &p.v, 4), word(v);
      word(static_cast<uint32_t>(walk_node_index(p.node)));
      uint32_t which = 0;
      for (size_t i = 0; i < pm::zcls::clutter_templates().size(); ++i)
        if (pm::zcls::clutter_templates()[i].get() == p.settings) which = static_cast<uint32_t>(i + 1);
      word(which);
    }
  }
  return n;
}

}  // extern "C"

extern "C" {

// test_pm_clutter.py's crafted template: a node "tmpl" with one model (n vertices, `polys` polygons,
// the first of `mats` materials with `mat_flags`), children c0.. at `kids`. `wrap_node`: tmpl holds
// no model, a child does. Out as probe_pm_clutter_nodes, node indexes being the child's number.
int probe_pm_clutter_scene(const float* verts, const float* uvs, int n, const float* kids, int n_kids, int polys,
                           int mats, int mat_flags, int wrap_node, uint32_t* out, int cap) {
  ::zcls::reset_lists();
  static ::zcls::Node tmpl, holder;
  static std::vector<::zcls::Node> children;
  static std::vector<std::array<uint8_t, 0x100>> objects;
  static std::vector<::zcls::Node*> kid_list;
  static ::zcls::Node* holder_kids[1];
  children.assign(static_cast<size_t>(n_kids), ::zcls::Node{});
  objects.assign(static_cast<size_t>(n_kids), {});
  kid_list.clear();
  const auto name = [](::zcls::Node& node, const std::string& s) { std::strcpy(reinterpret_cast<char*>(node.raw), s.c_str()); };
  tmpl = ::zcls::Node{}, holder = ::zcls::Node{};
  for (int i = 0; i < n_kids; ++i) {
    ::zcls::Node& c = children[static_cast<size_t>(i)];
    name(c, "c" + std::to_string(i));
    c.put<int32_t>(0x34, 5);
    c.set_flags(0x80010);
    std::memcpy(objects[static_cast<size_t>(i)].data() + 0x84, kids + i * 3, 12);
    c.data = objects[static_cast<size_t>(i)].data();
    kid_list.push_back(&c);
  }
  static pm::zmod::Model model;
  static pm::zmod::Polygon poly;
  static ::zmod::Material material;
  static ::zmod::Material* material_list[2];
  static std::vector<float> vertex_store, uv_store;
  static std::vector<uint32_t> index_store;
  static float* uv_list[2];
  model = pm::zmod::Model{}, poly = pm::zmod::Polygon{}, material = ::zmod::Material{};
  vertex_store.assign(verts, verts + n * 3);
  uv_store.assign(uvs, uvs + n * 2);
  index_store.resize(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) index_store[static_cast<size_t>(i)] = static_cast<uint32_t>(i);
  std::memcpy(model.header + 0x10, &polys, 4);
  model.polygons = &poly, model.vertices = vertex_store.data();
  poly.flags = static_cast<uint32_t>(n);
  poly.vertex_indices = index_store.data();
  poly.material_count = mats;
  material.flags = static_cast<uint8_t>(mat_flags);
  material_list[0] = material_list[1] = &material;
  poly.materials = material_list;
  uv_list[0] = uv_list[1] = uv_store.data();
  poly.uvs = uv_list;
  name(tmpl, "tmpl");
  tmpl.put<int32_t>(0x34, 5);
  tmpl.set_flags(0x10);
  ::zcls::Node* owner = &tmpl;
  if (wrap_node) {
    name(holder, "holder");
    holder.put<int32_t>(0x34, 5);
    holder.set_flags(0x10);
    holder_kids[0] = &holder;
    tmpl.put<int32_t>(0x5c, 1), tmpl.children = holder_kids;
    owner = &holder;
  }
  owner->pm_model = &model;
  owner->put<int32_t>(0x5c, n_kids), owner->children = kid_list.data();
  ::zcls::push_back(::zcls::kAllNodes, &tmpl);
  pm::zcls::clutter_templates().clear();
  pm::zcls::clutter_pieces().clear();
  auto t = std::make_unique<pm::zcls::ClutterTemplate>();
  t->name = "c1";
  pm::zcls::clutter_templates().push_back(std::move(t));
  static std::vector<uint32_t> errors;
  errors.clear();
  ::zerr::set_sink([](int level, const char*, int at, const char*) {
    errors.push_back(static_cast<uint32_t>(level) << 16 | static_cast<uint32_t>(at));
  });
  pm::zcls::clutter_load_templates({"tmpl"});
  ::zerr::set_sink(nullptr);
  int w = 0;
  const auto word = [&](uint32_t v) { if (w < cap) out[w] = v; ++w; };
  word(static_cast<uint32_t>(errors.size()));
  for (uint32_t e : errors) word(e);
  for (const auto& [texture, pieces] : pm::zcls::clutter_pieces()) {
    for (const pm::zcls::ClutterPiece& p : pieces) {
      uint32_t v;
      std::memcpy(&v, &p.u, 4), word(v);
      std::memcpy(&v, &p.v, 4), word(v);
      word(static_cast<uint32_t>(p.node - children.data()));
      word(p.settings != nullptr);
    }
  }
  return w;
}

}  // extern "C"

extern "C" {

// The whole clutter load on a world, as the exe's (test_pm_clutter.py, class Laying): the settings,
// the templates, the laying, with the same made-up ground. Out: rand calls, cell changes, the count,
// then each record as the test reads it.
int probe_pm_clutter_lay(const char* world, const char* dir, const char* archive, const char* names, int skip_ground,
                         uint32_t* out, int cap) {
  if (probe_pm_walk_load(world) < 0) return -1;
  pm::zcls::clutter_skip_ground_test(skip_ground != 0);
  mount_only(dir);
  zrdr::close_archives(true);
  zrdr::init();
  if (!zrdr::open_archive(archive, true)) return -2;
  static int s_changed;
  s_changed = 0;
  pm::zcls::ClutterHooks& h = pm::zcls::clutter_hooks();
  h = {};
  h.cell_changed = [](::zcls::Node*, ::zcls::Partition*) { ++s_changed; };
  h.ground_heights = [](::zcls::Node*, float x, float y, float z, float* heights, int) -> int {
    uint32_t bx, by, bz;
    std::memcpy(&bx, &x, 4), std::memcpy(&by, &y, 4), std::memcpy(&bz, &z, 4);
    const uint32_t v = (bx * 31u + bz * 17u) >> 3;
    const int n = static_cast<int>(v % 4);
    for (int i = 0; i < n; ++i) heights[i] = (v >> (8 + i) & 1) ? y - 1.5f : y - 4.0f;
    return n;
  };
  crt::msvc_srand(1);
  pm::zcls::clutter_templates().clear();
  pm::zcls::clutter_pieces().clear();
  pm::zcls::clutter_read_settings("templates.zrd");
  std::list<std::string> list;
  std::string all(names);
  for (size_t at = 0; at < all.size();) {
    size_t end = all.find('\n', at);
    if (end == std::string::npos) end = all.size();
    list.push_back(all.substr(at, end - at));
    at = end + 1;
  }
  ::zerr::set_sink([](int, const char*, int, const char*) {});
  pm::zcls::clutter_load_templates(list);
  ::zcls::Node* wn = ::zcls::camera_world(walk_camera());
  pm::zcls::clutter_lay(wn);
  ::zerr::set_sink(nullptr);
  auto* w = static_cast<::zcls::World*>(wn->data);
  int n = 3, count = 0;
  const auto word = [&](uint32_t v) { if (n < cap) out[n] = v; ++n; };
  const auto fl = [&](float f) { uint32_t v; std::memcpy(&v, &f, 4); word(v); };
  for (int z = 0; z < w->get<int32_t>(0x8c); ++z) {
    for (int x = 0; x < w->get<int32_t>(0x88); ++x) {
      ::zcls::Partition& cell = w->rows[z][x];
      if (!cell.pm_clutter) continue;
      for (const pm::zcls::ClutterRecord& r : *pm::zcls::cell_clutter(cell)) {
        ++count;
        word(static_cast<uint32_t>(x)), word(static_cast<uint32_t>(z));
        word(static_cast<uint32_t>(walk_node_index(r.node)));
        for (float f : r.matrix.m) fl(f);
        fl(r.near_sq), fl(r.far_sq), fl(r.fade_scale);
        word(r.zone), word(r.flags);
        fl(r.weapon_health), fl(r.collide_health);
        uint32_t which = 0;
        for (size_t i = 0; i < pm::zcls::clutter_templates().size(); ++i)
          if (pm::zcls::clutter_templates()[i].get() == r.settings) which = static_cast<uint32_t>(i + 1);
        word(which);
      }
    }
  }
  out[0] = static_cast<uint32_t>(crt::msvc_rand()), out[1] = static_cast<uint32_t>(s_changed), out[2] = static_cast<uint32_t>(count);
  return n;
}

}  // extern "C"

extern "C" {

// test_pm_clutter.py's crafted world (Laying.scene): a 2 x 2 grid of cells 16 wide, one child for each
// row of `rows` (12 doubles: class, flags, +0x2c, +0x30, data flags, data +4, x, y, z, 0: from the
// local matrix 1: from the cached one, the two polygons' zone_set). Out: each record's cell, zone
// and point, cells in order; the count.
int probe_pm_clutter_scene_lay(const double* rows, int count, uint32_t* out, int cap) {
  ::zcls::reset_lists();
  zmth::reset_stack();
  zmth::Mat34 base;
  zmth::push(&base);
  zmth::load_identity();
  static ::zcls::Node world, piece_node;
  static ::zcls::World wd;
  static std::array<::zcls::Partition, 2> row0, row1;
  static ::zcls::Partition* row_list[2];
  static std::vector<::zcls::Node> nodes;
  static std::vector<::zcls::Node*> kids;
  static std::vector<std::array<uint8_t, 0x100>> data;
  static std::vector<pm::zmod::Model> models;
  static std::vector<std::array<pm::zmod::Polygon, 2>> polys;
  static float verts[12] = {0, 0, 0, 8, 0, 0, 8, 0, 8, 0, 0, 8};
  static float uvs[8] = {0, 0, 2, 0, 2, 2, 0, 2};
  static float* uv_list[2] = {uvs, uvs};
  static uint32_t indices[3][3] = {{0, 1, 2}, {0, 2, 3}, {0, 1, 2}};
  static ::zmod::Material tex_mat, other_mat;
  static ::zmod::Material* first[2] = {&tex_mat, &other_mat};
  static ::zmod::Material* second[2] = {&other_mat, &tex_mat};
  static ::zimg::TextureEntry tex_entry, other_entry;
  tex_mat.texture = &tex_entry, other_mat.texture = &other_entry;
  world = ::zcls::Node{}, wd = ::zcls::World{}, piece_node = ::zcls::Node{};
  row0 = {}, row1 = {};
  row_list[0] = row0.data(), row_list[1] = row1.data();
  wd.rows = row_list;
  wd.put<float>(0x34, 0.0f), wd.put<float>(0x38, 0.0f), wd.put<float>(0x74, 0.0625f), wd.put<float>(0x78, 0.0625f);
  wd.put<int32_t>(0x88, 2), wd.put<int32_t>(0x8c, 2);
  world.data = &wd;
  nodes.assign(static_cast<size_t>(count), ::zcls::Node{});
  kids.clear();
  data.assign(static_cast<size_t>(count), {});
  models.assign(static_cast<size_t>(count), pm::zmod::Model{});
  polys.assign(static_cast<size_t>(count), {});
  const int two = 2;
  for (int i = 0; i < count; ++i) {
    const double* r = rows + i * 12;
    const auto u = static_cast<size_t>(i);
    ::zcls::Node& n = nodes[u];
    n.put<int32_t>(0x34, static_cast<int32_t>(r[0]));
    n.set_flags(static_cast<uint32_t>(r[1]));
    n.put<uint32_t>(0x2c, static_cast<uint32_t>(r[2]));
    n.raw[0x30] = static_cast<uint8_t>(r[3]);
    auto* d = data[u].data();
    const uint32_t f = static_cast<uint32_t>(r[4]);
    if (r[0] == 5) std::memcpy(d, &f, 4);
    const float f4 = static_cast<float>(r[5]);
    if (r[0] == 6) std::memcpy(d + 4, &f4, 4);
    const float m[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, static_cast<float>(r[6]), static_cast<float>(r[7]), static_cast<float>(r[8])};
    std::memcpy(d + 0x30, m, 48), std::memcpy(d + 0x60, m, 48);
    n.data = d;
    pm::zmod::Model& model = models[u];
    std::memcpy(model.header + 0x10, &two, 4);
    model.polygons = polys[u].data(), model.vertices = verts;
    for (int p = 0; p < 2; ++p) {
      pm::zmod::Polygon& poly = polys[u][static_cast<size_t>(p)];
      poly.flags = 3, poly.vertex_indices = indices[p], poly.material_count = 2;
      poly.materials = p ? second : first, poly.uvs = uv_list, poly.file_uvs = 1;
      poly.zone_set = static_cast<uint32_t>(r[10 + p]);
    }
    n.pm_model = &model;
    kids.push_back(&n);
  }
  world.put<int32_t>(0x5c, count), world.children = kids.data();
  pm::zcls::clutter_hooks() = {};
  pm::zcls::clutter_templates().clear();
  pm::zcls::clutter_pieces().clear();
  pm::zcls::clutter_pieces()[&tex_entry].push_back({0.5f, 0.25f, &piece_node, nullptr});
  pm::zcls::clutter_lay(&world);
  int n = 1, records = 0;
  const auto word = [&](uint32_t v) { if (n < cap) out[n] = v; ++n; };
  for (int z = 0; z < 2; ++z) {
    for (int x = 0; x < 2; ++x) {
      ::zcls::Partition& cell = row_list[z][x];
      word(cell.get<uint32_t>(0));
      word(static_cast<uint32_t>(cell.get<int32_t>(0x40)));
      if (!cell.pm_clutter) continue;
      for (const pm::zcls::ClutterRecord& r : *pm::zcls::cell_clutter(cell)) {
        ++records;
        word(static_cast<uint32_t>(x)), word(static_cast<uint32_t>(z)), word(r.zone);
        for (int k = 9; k < 12; ++k) {
          uint32_t v;
          std::memcpy(&v, &r.matrix.m[k], 4);
          word(v);
        }
      }
      delete pm::zcls::cell_clutter(cell);
      cell.pm_clutter = nullptr;
    }
  }
  out[0] = static_cast<uint32_t>(records);
  zmth::pop();
  return n;
}

}  // extern "C"

extern "C" {

// After probe_pm_clutter_lay: a crater, then every record's flags, cells in the test's order.
int probe_pm_clutter_crater(float x, float y, float z, float radius, uint32_t* out, int cap) {
  ::zcls::Node* wn = ::zcls::camera_world(walk_camera());
  const float point[3] = {x, y, z};
  pm::zcls::clutter_crater(wn, point, radius);
  auto* w = static_cast<::zcls::World*>(wn->data);
  int n = 0;
  for (int zi = 0; zi < w->get<int32_t>(0x8c); ++zi) {
    for (int xi = 0; xi < w->get<int32_t>(0x88); ++xi) {
      ::zcls::Partition& cell = w->rows[zi][xi];
      if (!cell.pm_clutter) continue;
      for (const pm::zcls::ClutterRecord& r : *pm::zcls::cell_clutter(cell)) {
        if (n < cap) out[n] = r.flags;
        ++n;
      }
    }
  }
  return n;
}

}  // extern "C"
