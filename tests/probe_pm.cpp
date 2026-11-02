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
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zvideo/zvid_format.h"
#include "pm/gamez/zclass/cls_zbd.h"
#include "pm/gamez/zimage/zimg_texture.h"
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
  if (with_models == 1) {
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
        if (n && (q.flags & 0x1000)) put(q.normal_indices, n * 4);
        for (int32_t j = 0; j < q.material_count; ++j) {
          const int32_t mi = mat_index(q.materials[j]);
          put(&mi, 4);
          if (q.uvs) put(q.uvs[j], n * 8);
        }
        put(q.colors, n * 12);
      }
      for (int32_t k = 0; k < m.count(0x5c); ++k) {
        const pm::zmod::MaterialRef& r = m.refs[k];
        const int32_t mi = mat_index(r.material), first = r.polygon ? static_cast<int32_t>(r.polygon - m.polygons) : -1;
        put(&mi, 4), put(&r.usage_count, 4), put(&first, 4);
      }
    }
  }
  if (with_nodes) {
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
  if (static_cast<int>(b.size()) > cap) return -1;
  std::memcpy(out, b.data(), b.size());
  return static_cast<int>(b.size());
}

}  // extern "C"
