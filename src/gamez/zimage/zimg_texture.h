#pragma once
#include <cstdint>
#include <cstdio>

// The global texture table (zImage\zimg_texture.cpp), filled from a world's
// texture directory (docs/gamez.md) and by name lookups, and the loading that
// gives each entry its image and Direct3D texture record (0x00519080..,
// between zimg_texture.cpp and zimg_fonts.cpp; docs/drafts/bake-textures.md).
namespace zimg {

struct Texture;

struct TextureEntry {        // 0x28 bytes in the file
  void* image;               // +0x00  the zimg::Texture, set when the texture is loaded by name
  void* surface;             // +0x04  the zvid::D3DTexture record (0x0057b130), on Direct3D
  char name[20];             // +0x08
  int32_t state;             // +0x1c  0 free, 2 to load (every world entry), 3 to reload, 1 loaded
  int32_t group;             // +0x20
  TextureEntry* mip;         // +0x24  an index in the file, -1 for none; rebuilt by name at load
};

constexpr int kMaxTextures = 0x1000;

struct TextureTable {
  TextureEntry entries[kMaxTextures];  // 0x00610484
  int32_t count = 0;                   // 0x00610480
  int32_t group = 0;                   // 0x006384d4  current texture group
};
TextureTable& textures();

// 0x00518160. Ours (docs/modding.md, M3): a mip index outside the table
// refuses the directory (-1, the table as it was), `where` named in the log.
int read_texture_directory(int count, std::FILE* f, const char* where = nullptr);
TextureEntry* texture_at(int index);                  // 0x00518080

// --- Loading ---

// Calls the loading makes into code not ported, or ported elsewhere. Unset
// ones do nothing (and return null / false).
struct TextureHooks {
  Texture* (*fallback)(const char* name) = nullptr;  // [0x00638520], never set in 1.2
  // 0x00542710: the entry is one of the three surface-mark images at
  // 0x0074a2b4 (zmod::mark_is_map), kept as an image only.
  bool (*projected)(const TextureEntry* entry) = nullptr;
  // [0x0072f374] = 0x0057baa0: copy a new image into an existing record (state 3).
  void (*reload)(void* surface, Texture* image) = nullptr;
  // [0x0072f378] (ECX the record): a Direct3D texture record let go.
  void (*release)(void* surface) = nullptr;
  // [0x0072f37c]: the device's textures let go, at the textures' free.
  void (*release_all)() = nullptr;
};
TextureHooks& texture_hooks();

struct TextureLoad {
  int32_t list = 0;          // 0x0063851c  the library list searched first (the last hit)
  int32_t memory = 0;        // 0x00638524  bytes given to Direct3D (2 per texel)
};
TextureLoad& texture_load();

Texture& default_image();    // 0x005ecbc0: 8x8 red/green checks, 565
// Mech3.exe 1.2 @ 0x00518b70, at the mission unload: every entry let go
// (0x00518b30), the table emptied, the palettes freed, the world's
// libraries freed (0x005186b0) and the image library's files closed.
int32_t textures_free();
TextureEntry* default_entry();  // 0x00518210: the entry naming default_image()
// 0x00518b30: a loaded entry's image and device record freed, a waiting one
// forgotten (state 0); a reloading one left.
void entry_free(TextureEntry& e);

TextureEntry* find_texture(const char* name);  // 0x00518220: case-sensitive, in-use entries
// 0x00518d10 (ECX name, EDX group): a free entry (not a found one) named,
// marked to load in `group` and loaded now; the default entry when the
// table is full.
TextureEntry* load_named(const char* name, int32_t group, int32_t free_pixels);
TextureEntry* free_texture_entry();            // 0x00518cc0: the first free one, else one more (4096 at most)
// A texture by file name: the one loaded under the name without its
// extension, else a new entry to load (state 2, the current group), else
// (a full table) the default.
TextureEntry* add_texture(const char* name);   // 0x00518c60
void set_entry_name(const char* path, char* dst);  // 0x00519210: base name, extension cut (0x00519150)
// ECX name: the image from the current library list, else the other (which
// then becomes current); an alpha texture's transparent texels are zeroed.
Texture* fetch_image(const char* name);        // 0x00518ee0
// zImage's image cache ([0x006384ec], made on the first fetch): images by
// name, counted. 0x005179d0 one more use of the image under `name`, loaded
// (fetch_image) when new; 0x00517b10 one less, freed (0x005185f0) when none
// is left or when it was never cached.
Texture* cache_fetch(const char* name);        // 0x005179d0
void cache_release(Texture* image);            // 0x00517b10
void clear_transparent(Texture& t);            // 0x00518e50
void link_mips(TextureEntry& entry);           // 0x00519270: "<name>_1" -> _2, _3, ... by name
// ECX entry, stack free_pixels (EDX unused): image, mips, then on Direct3D the
// record through zvid::create_texture. Only entries in state 2 or 3.
void load_texture(TextureEntry* entry, int32_t free_pixels);  // 0x00518f70
void load_group(int32_t group);                // 0x00519080: ECX group, -1 for all
int32_t load_group_0();                        // 0x005190d0

// zRender (between zreader.cpp and zrndr_draw.c): the software renderer's
// span constants of an image. Kept here until that file is ported.
void software_prepare(Texture& t);             // 0x00557ed0

}  // namespace zimg
