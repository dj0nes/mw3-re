#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>

// C:\zipper\GameZMech3\zUtil\zutl_zar.cpp: the .zbd archive. A plain
// concatenation of files with the table of contents at the end:
//
//   file data ... | Entry[count] | uint32 version (1) | uint32 count
//
// The writer (0x005765b0 create, 0x00576890 and 0x00576990 add, 0x00576720
// write the table) is used at run time only for the saved loadouts.
namespace zutl_zar {

// One table entry, 0x94 bytes, read straight from the file.
struct Entry {
  uint32_t start;        // +0x00 offset of the data
  uint32_t length;       // +0x04
  char name[64];         // +0x08 matched case-insensitively
  uint32_t flags;        // +0x48 bit 1: comment and filetime are set
  char comment[64];      // +0x4c
  uint32_t filetime[2];  // +0x8c Win32 FILETIME, low then high
};
static_assert(sizeof(Entry) == 0x94, "Entry must match the file layout");

// The original object is 0x18 bytes; the fields keep its order.
struct Archive {
  char* path = nullptr;         // +0x00 _strdup of the name it was opened with
  std::FILE* file = nullptr;    // +0x04 HANDLE, INVALID_HANDLE_VALUE when closed
  bool dirty = false;           // +0x08 table changed, write it on close
  int count = 0;                // +0x0c entries in use
  int capacity = 0;             // +0x10 entries allocated
  Entry* entries = nullptr;     // +0x14
  uint32_t checksum = 0;        // +0x18 Pirate's Moon's only (0x1c bytes): version 2's CRC (src/pm)
};

void init(Archive& a);                         // 0x005764e0
void destroy(Archive& a);                      // 0x00576500
// 0x00576520. It and close() read and write the table through the running
// game's (zgame::game(): MW3's version 1, PM's version 2).
bool open(Archive& a, const char* path);
bool read_table(Archive& a);                   // 0x005767b0, MW3's
void reserve(Archive& a, int count);           // 0x00576690
bool close(Archive& a);                        // 0x00576650, writes the table when dirty
bool create(Archive& a, const char* path);     // 0x005765b0, truncates
// 0x00576990: appends the bytes and a table entry. The exe's entry also holds
// whatever its stack had in the flags, comment and file time; here they are 0.
void add(Archive& a, const char* name, const void* data, uint32_t size);
// 0x00576890: the same, the bytes written by `write` at the file's end; the
// entry's length is what `write` returns.
void add_written(Archive& a, const char* name, uint32_t (*write)(std::FILE* f, void* user), void* user);
bool write_table(Archive& a);                  // 0x00576720, MW3's
// Ours: the bytes entry `index` may take for a bounded reader, by the
// running game (zgame::Table::zar_span); 0 for no such entry.
uint32_t span(const Archive& a, int index);
uint32_t length_span(const Archive& a, int index);  // MW3's: its length
void reset(Archive& a);                        // 0x005766d0
int find(const Archive& a, const char* name);  // 0x00576a80, -1 if absent
// 0x00576ae0: positions the file at entry `index` and returns it (the original
// returns the HANDLE), or nullptr for INVALID_HANDLE_VALUE.
std::FILE* seek(Archive& a, int index, uint32_t* length);
// 0x00576b30: copies entry `index` into `buffer`. *size is the buffer size in and
// the entry length out. 0 on success, 0x10001 no such entry, 0x10002 too small.
uint32_t read(Archive& a, int index, void* buffer, uint32_t* size);

// 0x005763f0: the writing side of read_string; returns the bytes written.
uint32_t write_string(std::FILE* file, const char* s);

// 0x00576460, next to the archive code (its source file is not named): a string
// stored as uint32 length then the bytes, read into a zeroed malloc(length + 1).
// Returns the bytes read.
uint32_t read_string(std::FILE* file, char** out);

// 0x00576c10 (next to the archive code): CRC-32 MSB first, polynomial
// 0x04c11db7, no final xor, its table 0x0072eec0 built on first use by
// 0x00576bc0. The exe keeps the running value at ECX; here it is passed and returned.
uint32_t crc32(uint32_t crc, const void* data, size_t n);

// FIX (A): the original passes Windows paths ("zbd\\reader.zbd") to CreateFileA.
// The rebuild turns '\\' into '/' before opening.
void host_path(const char* path, char* out, size_t size);

}  // namespace zutl_zar
