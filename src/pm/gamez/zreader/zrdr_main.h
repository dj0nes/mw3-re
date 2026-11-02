#pragma once
#include <cstdint>
#include <cstdio>

#include "gamez/zreader/zreader.h"

// C:\zipper\branchep1\gamez\zReader\zrdr_main.cpp: Pirate's Moon's reader,
// MW3's zreader.cpp rewritten (docs/pm-map.md, step 1). The files are the
// same (one tree of nodes, MW3's binary and text forms) and so is what every
// function MW3 also has returns (tests/differential/test_pm_zreader.py), so
// those stay zrdr's. Here are PM's new getters and its text files, which
// take #include (docs/zreader.md, Pirate's Moon).
namespace pm::zrdr {

using ::zrdr::Node;

// An int; a one-item list too. Through fild and _ftol, which give the int back.
bool get_int_value(Node* node, int32_t* out);                          // Pirate's Moon 1.0 @ 0x00578f70
// zrdr::find, then MW3's get_float_value (0x00556fe0) / get_string_value (0x00557030).
bool get_float_value(Node* node, const char* key, float* out);         // Pirate's Moon 1.0 @ 0x00579090
bool get_string_value(Node* node, const char* key, const char** out);  // Pirate's Moon 1.0 @ 0x005790b0

// A loose text file's tree, as MW3's zrdr::parse_text (0x005563d0) with
// #include NAME: the named file is read in its place, back to this one at
// its end, up to 32 files deep. The balance check takes this file only.
Node* parse_text(std::FILE* file);  // Pirate's Moon 1.0 @ 0x005782f0

}  // namespace pm::zrdr
