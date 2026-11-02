#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

// Texture libraries: rimage.zbd, rmechtex*.zbd (docs/textures.md). Original
// file unknown: these sit between zimg_library.cpp and zimg_texture.cpp.
namespace zimg {

enum TexFlags : uint8_t {
  kTwoBytes = 0x01,
  kAlpha = 0x02,
  kNoAlpha = 0x04,
  kFullAlpha = 0x08,
  kGlobalPalette = 0x10,
  kPixelsOwned = 0x20,
  kAlphaOwned = 0x40,
  kPaletteOwned = 0x80,
};

struct Texture {             // 0x38 bytes, calloc'd
  int32_t pixel_count;       // +0x00
  int16_t width;             // +0x04
  int16_t height;            // +0x06
  uint8_t unk_08;            // +0x08  byte 8 of the file's info
  uint8_t flags;             // +0x09  low byte of the file's flags
  uint8_t log2_width;        // +0x0a  set by software_prepare (0x00557ed0)
  uint8_t log2_height;       // +0x0b
  int16_t stretch;           // +0x0c  bit 0 clamp u, bit 1 clamp v
  int16_t palette_count;     // +0x0e
  void* pixels;              // +0x10  16-bit colours, or 8-bit indices when palette_count
  uint8_t* alpha;            // +0x14
  uint16_t* palette;         // +0x18
  float mip_scale;           // +0x1c  1, or the base texture's width over this mip's (0x00519270)
  int32_t span_shift;        // +0x24  20 - log2 width (the software spans)
  int32_t span_mask_u;       // +0x28  width - 1
  int32_t span_mask_v;       // +0x2c  (height << 20) - 0x100000
  int32_t width_again;       // +0x34
};

struct Header {              // 0x18 bytes at the start of the file
  uint32_t zero00;
  uint32_t has_entries;      // must be 1
  int32_t palette_count;     // global palettes after the entries
  int32_t texture_count;
  uint32_t zero10, zero14;
};

struct Entry {               // 0x28 bytes
  char name[32];
  uint32_t offset;
  int32_t palette;           // global palette index, -1 for none
};
static_assert(sizeof(Entry) == 0x28);

struct Library {             // 0xac bytes
  char name[0x80];           // +0x00  base name, then the resolved file name
  std::FILE* file;           // +0x80
  Header header;             // +0x84
  Entry* entries;            // +0x9c
  int32_t palette_base;      // +0xa0  first global palette in State::palettes
  int32_t variant;           // +0xa4  highest numbered file to try, 0 for none
  bool needs_resolve;        // +0xa8
  bool r_prefix;             // +0xa9
  bool missing;              // +0xaa
  long size = -1;            // ours (docs/modding.md, M3): the file's, bounding the table and textures
};

struct State {
  bool enabled = true;                        // 0x005ecc20
  std::vector<uint16_t*> palettes;            // 0x00638510, count 0x0063850c
  int32_t display_format = 6;                 // 0x00802728  5 = RGB555
  bool (*file_exists)(const char*) = nullptr; // 0x005762b0 search path; FIX (A)
  const char* dir = "";                       // FIX (A): where the zbd files are
};
State& state();

// Two lists of libraries, searched in turn by fetch_image (zimg_texture):
// 0 the world and mech textures (0x006384f0), 1 the image library (0x00638500).
std::vector<Library*>& library_list(int list);
Library* new_library(const char* name, int32_t variant, bool r_prefix);  // 0x005182a0, list 0
Library* new_image_library(const char* name, int32_t variant, bool r_prefix);  // 0x005183b0, list 1
int32_t register_image_library(int32_t on);  // 0x005184c0: "image" (rimage.zbd) when on; returns 1
void open_all(int list);                     // 0x00518670 (0x005187c0 on each)
void close_all(int list);                    // 0x00518630 (0x00518600 on each)
void close(Library& lib);                    // 0x00518600: the file only; the entries stay
void free_all(int list);                     // 0x005186b0: closed, their entries (0x00518760) and themselves freed
Texture* find_in_list(int list, const char* name);  // 0x00518d60: the first library that has it
void resolve_name(Library& lib);                                        // 0x00518a30
bool open(Library& lib);                                                // 0x005187c0
Texture* load(Library& lib, const char* name);                          // 0x00518db0

// 0x00517f80. Ours (docs/modding.md, M3): with `end`, a texture whose sizes
// or buffers the bytes up to it cannot hold is refused (null), not read.
Texture* read_texture(std::FILE* f, long end = -1);
int read_info(std::FILE* f, Texture& t);                // 0x00517d80
int read_data(std::FILE* f, Texture& t, int bytes);     // 0x00517df0
int bytes_per_pixel(const Texture& t);                  // 0x00517c20
void free_texture(Texture* t);                          // 0x00517cc0
void release_buffers(Texture& t);                       // 0x00517d00: the owned pixels, alpha, palette

}  // namespace zimg
