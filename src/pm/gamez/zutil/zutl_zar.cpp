#include "pm/gamez/zutil/zutl_zar.h"

#include <cstdio>
#include <vector>

// FIX (A), as MW3's (gamez/zutil/zutl_zar.cpp): the Win32 file calls become C stdio.
namespace pm::zar {
namespace {
long file_size(std::FILE* f) {
  const long here = std::ftell(f);
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, here, SEEK_SET);
  return size;
}
uint32_t read_bytes(std::FILE* f, void* buffer, uint32_t n) { return static_cast<uint32_t>(std::fread(buffer, 1, n, f)); }
}  // namespace

// Pirate's Moon 1.0 @ 0x00597610. MW3's 0x005767b0 with the checksum after the
// count (read before the version is checked) and version 2.
bool read_table(zutl_zar::Archive& a) {
  const long size = file_size(a.file);
  if (static_cast<unsigned long>(size) < 12) return false;
  int32_t version = 0, count = 0;
  std::fseek(a.file, -12, SEEK_END);
  read_bytes(a.file, &version, 4);
  read_bytes(a.file, &count, 4);
  read_bytes(a.file, &a.checksum, 4);
  if (version != 2) return false;
  // FIX (bug), as MW3's: a count the file cannot hold fails the open.
  if (count < 0 || static_cast<int64_t>(count) * 0x94 > size - 12) return false;
  zutl_zar::reserve(a, count);
  std::fseek(a.file, -static_cast<long>(count) * 0x94 - 12, SEEK_END);
  read_bytes(a.file, a.entries, static_cast<uint32_t>(count) * 0x94);
  std::fseek(a.file, 0, SEEK_SET);
  a.count = count;
  return true;
}

// Pirate's Moon 1.0 @ 0x00597570. The checksum written is whatever +0x18
// holds: 0 for an archive the game made (its saved loadouts).
bool write_table(zutl_zar::Archive& a) {
  std::fseek(a.file, 0, SEEK_END);
  std::fwrite(a.entries, sizeof(zutl_zar::Entry), static_cast<size_t>(a.count), a.file);
  const uint32_t tail[3] = {2, static_cast<uint32_t>(a.count), a.checksum};
  std::fwrite(tail, 4, 3, a.file);
  a.dirty = false;
  return true;
}

// Pirate's Moon 1.0 @ 0x00597a30. One buffer, grown to the longest entry so
// far (0x00596520, a realloc); an entry that does not read is left out.
uint32_t archive_crc(zutl_zar::Archive& a) {
  uint32_t crc = 0;
  std::vector<uint8_t> buffer;
  for (int i = 0; i < a.count; ++i) {
    const uint32_t length = a.entries[i].length;
    if (buffer.size() < length) buffer.resize(length);
    uint32_t got = static_cast<uint32_t>(buffer.size());
    if (zutl_zar::read(a, i, buffer.data(), &got) == 0) crc = zutl_zar::crc32(crc, buffer.data(), got);
  }
  return crc;
}

// Pirate's Moon 1.0 @ 0x00597a10
bool checksum_ok(zutl_zar::Archive& a) { return archive_crc(a) == a.checksum; }

// Pirate's Moon 1.0 @ 0x00597af0. False when the file does not open.
bool file_checksum_ok(const char* path) {
  zutl_zar::Archive a;
  zutl_zar::init(a);
  bool ok = false;
  if (zutl_zar::open(a, path)) {
    ok = checksum_ok(a);
    zutl_zar::close(a);
  }
  zutl_zar::destroy(a);
  return ok;
}

uint32_t span(const zutl_zar::Archive& a, int index) {
  if (index < 0 || a.count <= index || !a.file) return 0;
  const uint32_t start = a.entries[index].start;
  const long table = file_size(a.file) - 12 - static_cast<long>(a.count) * 0x94;
  uint32_t end = table > 0 ? static_cast<uint32_t>(table) : 0;
  for (int i = 0; i < a.count; ++i)
    if (a.entries[i].start > start && a.entries[i].start < end) end = a.entries[i].start;
  return end > start ? end - start : 0;
}

}  // namespace pm::zar
