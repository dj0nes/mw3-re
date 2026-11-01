#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "gamez/zreader/zreader.h"

namespace zutl { struct MessageDll; }

// The 1.2 data layer (ours, not original code; docs/patch12.md): what the
// official 1.2 patch changed in the game's data, redone while 1.0's data
// loads. Each change applies only to a file that is 1.0's own (by size and
// CRC, data_version.h), so 1.2's files, a mix's patched files and unknown
// data (a mod, another version) are left as they are. Not behind zfix bugs:
// it makes 1.0's data the 1.2 the port was ported from. The changes are our
// own values, edits and wording, not the patch's bytes.
namespace mech3::patch12 {

// Whether the install's file at `path` is 1.0's (a data_files() path, or an
// archive path as the game opens it: "zbd\\c3\\readerm2.zbd"). Measured once
// per file found.
bool is_10(const std::string& path);
void forget_files();  // after the mounts change

// The reader's load hook (zrdr::set_load_hook) on or off.
void install();
void uninstall();

// The hook: entry `entry` of archive `archive` (as opened) just read into
// `root`. Returns whether it changed anything.
bool on_load(const char* archive, const char* entry, zrdr::Node* root);
// The same without the 1.0 check: the edits for `data_path`'s `entry`.
bool apply(const std::string& data_path, const std::string& entry, zrdr::Node* root);

// Mech3Msg.dll's table: 1.2's messages added and edited when the file is
// 1.0's. Returns whether it changed anything.
bool messages(zutl::MessageDll& dll);
// The same without the check (the test).
void apply_messages(zutl::MessageDll& dll);
// The ids whose text is our own wording rather than 1.2's.
bool message_is_ours(int32_t id);

// The reader's missing-entry hook: an entry 1.2 adds to an open archive of
// 1.0's that 1.0 has in another archive of the same operation (c4b mission
// 5's paths and regions, from missions 6 and 4), copied from the player's own
// file and edited to 1.2's. Null when none.
zrdr::Node* provide(const char* entry);
// The same for one entry, without the checks on the open archives.
zrdr::Node* copy_entry(const std::string& archive, const std::string& entry, const std::string& source,
                       uint32_t size, uint32_t crc);

// A value in the tables' text form ((lists), "strings", ints, floats with a
// '.'); false when it does not parse.
bool parse_value(const char* text, zrdr::Node* out);

// The record edits of patch12_edits.inc for one entry, all or none (each
// checks the old value). Returns whether it applied them.
bool has_edits(const std::string& archive, const std::string& entry);
bool apply_edits(const std::string& archive, const std::string& entry, zrdr::Node* root);

// force_eff.ifr (Immersion's project file, read as data): 1.2's effects
// when the file is 1.0's. `data` is the file; returns whether it changed it.
bool force_effects(std::vector<uint8_t>& data);
bool apply_force(std::vector<uint8_t>& data);  // without the check
struct IfrRecord {
  std::string name, type;
  uint8_t head[5] = {};  // between the type and the values
  std::vector<std::pair<std::string, std::string>> values;  // in file order
};
bool read_ifr(const std::vector<uint8_t>& data, std::vector<IfrRecord>* out);
std::vector<uint8_t> write_ifr(const std::vector<IfrRecord>& records);

// The sounds 1.2's sounds.zrd names that 1.0 lacks (the 16-bit soil and
// footstep sounds, the engine loop without its click): which 1.0 sound in the
// same archive stands in, and how much of its end to cut. Only on 1.0's data.
bool wave_alias(const std::string& name, std::string* source, float* trim_end_seconds);

// c3's gamez.zbd as the world loader reads it (zcls::set_open_hook): 1.0's
// file rebuilt with 1.2's changes (patch12_world.cpp). The file's layout:
struct ParsedWorld {
  struct Poly {
    size_t record, data, end;  // the 0x24 record; its data up to `end`
  };
  struct Model {
    size_t header, start, end;  // the 0x60 header; the payload
    std::vector<Poly> polys;
  };
  struct NodeAt {
    size_t record, start, end;  // the 0xd4 record; its data
  };
  size_t materials = 0, models_at = 0, nodes_at = 0;
  std::vector<Model> models;
  std::vector<NodeAt> nodes;
};
bool parse_world(const std::vector<uint8_t>& data, ParsedWorld* out);
bool rebuild_c3_world(std::vector<uint8_t>& data);  // false: left as it was
std::FILE* world_hook(const char* path, std::FILE* f);

// hud.zrd: the 1152x864, 1280x1024 and 1600x1200 blocks, made from the
// 1024x768 block (patch12_hud.cpp).
bool hud(zrdr::Node* root);

}  // namespace mech3::patch12
