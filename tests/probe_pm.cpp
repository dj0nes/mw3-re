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

}  // extern "C"
