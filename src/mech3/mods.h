#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
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
  std::vector<std::string> worlds;  // world=<folder>: a new world (M2), its files in zbd\<folder>\, its <folder>.gs
  std::vector<std::pair<std::string, std::string>> ia_worlds;  // ia_world=<folder> <name>: on Instant Action's list
};
// An Instant Action world a mod adds: its folder (any world's) and the name shown.
struct ModIaWorld {
  std::string folder, name;
};
std::vector<ModIaWorld>& mod_ia_worlds();

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

// Each mounted mod's strings.txt (M2) over the message table `text` (the
// first mod's wins): `id=text` lines, the id decimal, 0x hex or a message's
// name (`ids`); the text UTF-8, in the game's code page, `\n` a new line, `\\`
// a backslash; # a comment. The log's lines: each file's count and the
// lines it could not use.
std::vector<std::string> mods_apply_strings(std::map<int32_t, std::string>& text,
                                            const std::map<std::string, int32_t>& ids);

// mw3 --check-mod DIR (M3): the mod at `dir` read as the game would read
// it, nothing mounted into play: mod.txt, its worlds and Instant Action
// entries, every file (what it replaces in `install`, when given, or adds;
// the player's own kind ignored), every archive's table, every reader
// archive's entries and loose .zrd through the bounded reader, strings.txt.
// The report's lines; `errors` counts what the game would refuse.
std::vector<std::string> mod_check(const std::string& dir, const std::string& install, int* errors);

// For the multiplayer lobby (a game needs every player's gameplay data the
// same): whether a mod's file is gameplay data, which is anything but
// strings (strings.txt), images (the texture and image archives, .png),
// sounds (sounds*.zbd, .wav), music and movies; and one number for the
// enabled mods' gameplay files, in their order (0 with none). A translation
// or a texture pack does not change it.
bool mod_file_is_gameplay(const std::string& rel);
uint64_t mods_gameplay_hash(const std::string& write_dir, const std::vector<std::string>& enabled);

// This program's mods as the lobby gives them to the other players: the
// gameplay hash, and the names (mod.txt's, else the folder's) of the
// enabled mods that change gameplay (each one's own hash not 0). Set once
// the mods are mounted (mods_identity_set); none: 0 and no names.
struct ModsIdentity {
  uint64_t hash = 0;
  std::vector<std::string> names;
};
ModsIdentity& mods_identity();
void mods_identity_set(const std::string& write_dir, const std::vector<std::string>& enabled);

}  // namespace mech3
