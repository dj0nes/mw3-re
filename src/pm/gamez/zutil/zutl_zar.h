#pragma once
#include <cstdint>

#include "gamez/zutil/zutl_zar.h"

// C:\zipper\branchep1\gamez\zUtil\zutl_zar.cpp: Pirate's Moon's zArchive,
// version 2 (docs/pm-map.md, The data formats). The table ends
//
//   Entry[count] | uint32 version (2) | uint32 count | uint32 checksum
//
// The checksum is zutl_zar::crc32 over every entry's data in order; the
// readers' archives carry it, mechlib, motion and the sounds hold 0. Only
// what changed from MW3's is here; the rest is zutl_zar's.
namespace pm::zar {

bool read_table(zutl_zar::Archive& a);   // Pirate's Moon 1.0 @ 0x00597610
bool write_table(zutl_zar::Archive& a);  // Pirate's Moon 1.0 @ 0x00597570
uint32_t archive_crc(zutl_zar::Archive& a);    // 0x00597a30: every entry's data
bool checksum_ok(zutl_zar::Archive& a);        // 0x00597a10: that against +0x18
bool file_checksum_ok(const char* path);       // 0x00597af0: open, check, close
// Ours: up to the next entry's start (else the table's), since motion.zbd's
// entries all store a length of 1 (the exe reads a motion to its end).
uint32_t span(const zutl_zar::Archive& a, int index);

}  // namespace pm::zar
