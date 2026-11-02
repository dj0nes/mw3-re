#include "gamez/zutil/zutl_zar.h"

#include <cstdlib>
#include <cstring>

#include "crt/msvc_string.h"
#include "gamez/zgame/zgame_table.h"
#include "gamez/zutil/zutl_path.h"

// FIX (A), whole file: CreateFileA / ReadFile / SetFilePointer / GetFileSize /
// CloseHandle become C stdio, which every platform has. The calls map one to one
// and keep their order. FILE_FLAG_SEQUENTIAL_SCAN, a caching hint, is dropped.
namespace zutl_zar {

namespace {
uint32_t read_bytes(std::FILE* f, void* buffer, uint32_t n) {
  return static_cast<uint32_t>(std::fread(buffer, 1, n, f));
}

// SetFilePointer(start, FILE_BEGIN): its distance is a signed LONG, so a
// start of 2 GB or more (a damaged table) is a negative move that fails and
// keeps the position; the read then takes what follows it.
void seek_to(std::FILE* f, uint32_t start) {
  if (start <= 0x7fffffffu) std::fseek(f, static_cast<long>(start), SEEK_SET);
}

long file_size(std::FILE* f) {
  long here = std::ftell(f);
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, here, SEEK_SET);
  return size;
}
}  // namespace

void host_path(const char* path, char* out, size_t size) {
  size_t i = 0;
  for (; path[i] && i + 1 < size; ++i) out[i] = path[i] == '\\' ? '/' : path[i];
  out[i] = '\0';
}

// Mech3.exe 1.2 @ 0x005764e0
void init(Archive& a) {
  a.path = nullptr;
  a.file = nullptr;
  a.dirty = false;
  a.count = 0;
  a.capacity = 0;
  a.entries = nullptr;
  a.checksum = 0;  // PM's 0x00597300
}

// Mech3.exe 1.2 @ 0x00576500
void destroy(Archive& a) {
  close(a);
  // Quirk kept: reset() inside close() already freed the path when there was a
  // table, and set it to null, so this only frees a path left by a failed open.
  if (a.path) std::free(a.path);
}

// Mech3.exe 1.2 @ 0x00576520. Opens for reading (GENERIC_READ, FILE_SHARE_READ,
// OPEN_EXISTING). The original formats GetLastError() on failure and frees the
// text without using it; the rebuild skips that.
bool open(Archive& a, const char* path) {
  a.file = zutl::open(path, "rb");  // FIX (A): the mounts (zutl_path.h), not CreateFileA on a Windows path
  if (!a.file) return false;
  bool ok = zgame::game().zar_read_table(a);
  a.path = strdup(path);
  return ok;
}

// Mech3.exe 1.2 @ 0x005767b0
bool read_table(Archive& a) {
  long size = file_size(a.file);
  if (static_cast<unsigned long>(size) < 8) return false;
  int32_t version = 0, count = 0;
  std::fseek(a.file, -8, SEEK_END);
  read_bytes(a.file, &version, 4);
  read_bytes(a.file, &count, 4);
  if (version != 1) return false;
  // FIX (bug): a count the file cannot hold (a damaged save) read the table
  // from wherever the seek failed to, into a buffer of that size, and the
  // entries' names from it crashed zSave (docs/save.md, fixes.md).
  if (count < 0 || static_cast<int64_t>(count) * 0x94 > size - 8) return false;
  reserve(a, count);
  std::fseek(a.file, -static_cast<long>(count) * 0x94 - 8, SEEK_END);
  read_bytes(a.file, a.entries, static_cast<uint32_t>(count) * 0x94);
  std::fseek(a.file, 0, SEEK_SET);
  a.count = count;
  return true;
}

// Mech3.exe 1.2 @ 0x00576690: grows to at least count + 1, doubling.
void reserve(Archive& a, int count) {
  if (a.capacity <= count) {
    int doubled = a.capacity * 2;
    int want = count + 1;
    if (want < doubled) want = doubled;
    a.capacity = want;
    a.entries = static_cast<Entry*>(std::realloc(a.entries, static_cast<size_t>(want) * 0x94));
  }
}

// Mech3.exe 1.2 @ 0x00576650
bool close(Archive& a) {
  if (a.dirty) zgame::game().zar_write_table(a);
  if (a.file) std::fclose(a.file);
  a.file = nullptr;
  reset(a);
  return true;
}

// Mech3.exe 1.2 @ 0x005766d0. Quirk kept: does nothing when there is no table.
void reset(Archive& a) {
  if (a.entries) {
    std::free(a.entries);
    a.entries = nullptr;
    if (a.path) std::free(a.path);
    a.path = nullptr;
    a.file = nullptr;
    a.capacity = 0;
    a.count = 0;
    a.entries = nullptr;
    a.dirty = false;
  }
}

// Mech3.exe 1.2 @ 0x00576a80
int find(const Archive& a, const char* name) {
  if (a.count < 1) return -1;
  for (int i = 0; i < a.count; ++i)
    if (crt::msvc_stricmp(name, a.entries[i].name) == 0) return i;
  return -1;
}

uint32_t length_span(const Archive& a, int index) {
  return index < 0 || a.count <= index ? 0 : a.entries[index].length;
}
uint32_t span(const Archive& a, int index) { return zgame::game().zar_span(a, index); }

// Mech3.exe 1.2 @ 0x00576ae0
std::FILE* seek(Archive& a, int index, uint32_t* length) {
  const Entry* e = (index < 0 || a.count <= index) ? nullptr : &a.entries[index];
  if (!e) return nullptr;
  if (length) *length = e->length;
  seek_to(a.file, e->start);
  return a.file;
}

// Mech3.exe 1.2 @ 0x00576b30
uint32_t read(Archive& a, int index, void* buffer, uint32_t* size) {
  const Entry* e = (index < 0 || a.count <= index) ? nullptr : &a.entries[index];
  if (!e) return 0x10001;
  uint32_t have = *size;
  *size = e->length;
  if (have < e->length) return 0x10002;
  if (buffer) {
    seek_to(a.file, e->start);
    read_bytes(a.file, buffer, e->length);
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x00576460
// FIX (bug): a length past the file's end (a damaged save) made the exe
// allocate up to 4 GB and crash when that failed; the buffer here stops at the
// file's end, which holds the same string (docs/fixes.md).
uint32_t read_string(std::FILE* file, char** out) {
  int32_t length = 0;
  uint32_t total = read_bytes(file, &length, 4);
  const long left = file_size(file) - std::ftell(file);
  const size_t size = length < 0 ? 0 : length > left ? static_cast<size_t>(left) : static_cast<size_t>(length);
  char* s = static_cast<char*>(std::malloc(size + 1));
  *out = s;
  std::memset(s, 0, size + 1);
  if (size > 0) total += read_bytes(file, s, static_cast<uint32_t>(size));
  return total;
}

// Mech3.exe 1.2 @ 0x00576c10, table 0x00576bc0
uint32_t crc32(uint32_t crc, const void* data, size_t n) {
  static uint32_t table[256];
  static bool built = false;  // 0x0072f2c0
  if (!built) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t v = i << 24;
      for (int b = 0; b < 8; ++b) v = (v & 0x80000000u) ? (v << 1) ^ 0x04c11db7u : v << 1;
      table[i] = v;
    }
    built = true;
  }
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; ++i) crc = table[(crc >> 24) ^ p[i]] ^ (crc << 8);
  return crc;
}

// Mech3.exe 1.2 @ 0x005765b0. On failure the exe logs GetLastError's text.
bool create(Archive& a, const char* path) {
  a.file = zutl::open(path, "w+b");  // into the write dir
  if (!a.file) return false;
  a.path = strdup(path);
  return true;
}

namespace {
Entry* new_entry(Archive& a, const char* name, uint32_t* start) {
  reserve(a, a.count + 1);
  std::fseek(a.file, 0, SEEK_END);
  *start = static_cast<uint32_t>(std::ftell(a.file));
  Entry* e = &a.entries[a.count];
  std::memset(e, 0, sizeof *e);
  e->start = *start;
  std::strncpy(e->name, name, sizeof e->name);
  return e;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00576990 (the comment and file time arguments are not used here)
void add(Archive& a, const char* name, const void* data, uint32_t size) {
  uint32_t start;
  Entry* e = new_entry(a, name, &start);
  e->length = size;
  std::fwrite(data, 1, size, a.file);
  ++a.count;
  a.dirty = true;
}

// Mech3.exe 1.2 @ 0x00576890
void add_written(Archive& a, const char* name, uint32_t (*write)(std::FILE* f, void* user), void* user) {
  uint32_t start;
  new_entry(a, name, &start);
  const uint32_t length = write(a.file, user);
  a.entries[a.count].length = length;
  ++a.count;
  a.dirty = true;
}

// Mech3.exe 1.2 @ 0x00576720
bool write_table(Archive& a) {
  std::fseek(a.file, 0, SEEK_END);
  std::fwrite(a.entries, sizeof(Entry), static_cast<size_t>(a.count), a.file);
  const uint32_t tail[2] = {1, static_cast<uint32_t>(a.count)};
  std::fwrite(tail, 4, 2, a.file);
  a.dirty = false;
  return true;
}

// Mech3.exe 1.2 @ 0x005763f0. A null string is written as length 0.
uint32_t write_string(std::FILE* file, const char* s) {
  const uint32_t n = s ? static_cast<uint32_t>(std::strlen(s)) : 0;
  uint32_t total = static_cast<uint32_t>(std::fwrite(&n, 1, 4, file));
  if (n) total += static_cast<uint32_t>(std::fwrite(s, 1, n, file));
  return total;
}

}  // namespace zutl_zar

