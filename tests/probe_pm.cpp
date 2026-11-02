// Pirate's Moon's ported functions behind a C interface, for the differential
// tests run against PM's exe (tests/differential, MW3_EXE=pm). PM's table is
// set on load; MW3's probe is tests/probe.cpp.
#include <cstdint>
#include <cstring>
#include <string>

#include "gamez/zgame/zgame_table.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
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

}  // extern "C"
