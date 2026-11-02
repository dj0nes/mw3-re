#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Mods (ours, docs/modding.md; Owen, 2026-11-01): folders of game data in
// the write dir's mods\, each laid out like the install with a mod.txt,
// mounted in front of the 1.2 patch and the install in the order mw3.cfg's
// mod= lines give (the first wins), behind the write dir (the player's own
// files win). A mod never serves the player's own files (zutl::mod_may_serve)
// and nothing is written into it or the install. With no mods enabled
// nothing is mounted and nothing changes.
namespace mech3 {

struct ModInfo {
  std::string folder;       // its folder's name under mods\ (what mod= names)
  std::string path;         // the folder
  std::string name, version, author, game, description;  // mod.txt's
  std::string error;        // why it cannot be used, "" when it can
};

// mod.txt: key=value lines (name, version, author, for, description), # a
// comment; `name` required, `for` mw3 when given. False with `out->error`.
bool mod_parse(const std::string& text, ModInfo* out);
// Every folder under <write dir>/mods, by folder name; each one's mod.txt read.
std::vector<ModInfo> mods_found(const std::string& write_dir);

// The enabled mods (folder names, in order) mounted (zutl::mount_mod) after
// whatever is mounted now and before `later` (the patch and the install,
// mounted after this by the caller). The log's lines: each mod and every
// file it replaces or adds, the player's own files it holds ignored, and
// names that are no usable mod.
std::vector<std::string> mods_mount(const std::string& write_dir, const std::vector<std::string>& enabled,
                                    const std::vector<std::string>& later);

// For the multiplayer lobby (a game needs every player's gameplay data the
// same): whether a mod's file is gameplay data, which is anything but
// strings (strings.txt), images (the texture and image archives, .png),
// sounds (sounds*.zbd, .wav), music and movies; and one number for the
// enabled mods' gameplay files, in their order (0 with none). A translation
// or a texture pack does not change it.
bool mod_file_is_gameplay(const std::string& rel);
uint64_t mods_gameplay_hash(const std::string& write_dir, const std::vector<std::string>& enabled);

}  // namespace mech3
