#include "mech3/patch12.h"

#include <cctype>
#include <map>

#include "gamez/zclass/cls_zbd.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/data_version.h"

namespace mech3::patch12 {
namespace {

std::map<std::string, bool> g_is_10;

// "zbd\\C3\\readerm2.zbd" -> "zbd/c3/readerm2.zbd", data_files()' form.
std::string data_path(const std::string& path) {
  std::string p;
  for (char c : path) p += c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (p.compare(0, 2, "./") == 0) p.erase(0, 2);
  return p == "mech3msg.dll" ? "Mech3Msg.dll" : p;
}

std::string lower(const std::string& s) {
  std::string out;
  for (char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

void hook(const char* archive, const char* entry, zrdr::Node* root) { on_load(archive, entry, root); }

// Entries 1.2 adds to one archive that 1.0 already has in another of the
// same operation (patch12_copies.inc): the source's size and CRC, so only
// 1.0's own entry is taken.
struct Copy {
  const char *archive, *entry, *source;
  uint32_t size, crc;
};
constexpr Copy kCopies[] = {
#include "mech3/patch12_copies.inc"
};

// Entries 1.2 adds that 1.0 has nowhere, whole (patch12_added.inc).
struct Added {
  const char *archive, *entry, *text;
};
constexpr Added kAdded[] = {
#include "mech3/patch12_added.inc"
};

bool archive_open(const std::string& path) {
  for (const zutl_zar::Archive* a : zrdr::state().archives)
    if (a && a->path && data_path(a->path) == path) return true;
  return false;
}

zrdr::Node* missing_hook(const char* entry) { return provide(entry); }

bool wave_hook(const char* name, zsnd::WaveAlias* out) { return wave_alias(name, &out->source, &out->trim_end_seconds); }

}  // namespace

bool is_10(const std::string& path) {
  const std::string p = data_path(path);
  auto it = g_is_10.find(p);
  if (it == g_is_10.end()) it = g_is_10.emplace(p, data_file_version(p.c_str()) == 10).first;
  return it->second;
}

void forget_files() { g_is_10.clear(); }

void install() {
  forget_files();
  zrdr::set_load_hook(hook);
  zrdr::set_missing_hook(missing_hook);
  zsnd::set_wave_alias_hook(wave_hook);
  zcls::set_open_hook(world_hook);
}

void uninstall() {
  zrdr::set_load_hook(nullptr);
  zrdr::set_missing_hook(nullptr);
  zsnd::set_wave_alias_hook(nullptr);
  zcls::set_open_hook(nullptr);
}

zrdr::Node* provide(const char* entry) {
  if (!entry) return nullptr;
  const std::string e = lower(entry);
  for (const Copy& c : kCopies) {
    if (e != c.entry || !archive_open(c.archive) || !is_10(c.archive)) continue;
    return copy_entry(c.archive, c.entry, c.source, c.size, c.crc);
  }
  for (const Added& a : kAdded) {
    if (e != a.entry || !archive_open(a.archive) || !is_10(a.archive)) continue;
    zrdr::Node v{};
    if (!parse_value(a.text, &v)) return nullptr;
    zrdr::Node* root = zrdr::new_nodes(zrdr::List, 1);
    *root = v;
    return root;
  }
  return nullptr;
}

zrdr::Node* copy_entry(const std::string& archive, const std::string& entry, const std::string& source,
                       uint32_t size, uint32_t crc) {
  std::string path = source;
  for (char& ch : path) ch = ch == '/' ? '\\' : ch;
  zutl_zar::Archive a;
  zutl_zar::init(a);
  zrdr::Node* root = nullptr;
  if (zutl_zar::open(a, path.c_str())) {
    const int i = zutl_zar::find(a, entry.c_str());
    std::vector<uint8_t> bytes(size);
    uint32_t got = size;
    if (i >= 0 && zutl_zar::read(a, i, bytes.data(), &got) == 0 && got == size &&
        zutl_zar::crc32(0, bytes.data(), bytes.size()) == crc) {
      if (std::FILE* f = zutl_zar::seek(a, i, nullptr)) {
        root = zrdr::new_nodes(zrdr::List, 1);
        zrdr::read_node(f, root);
        // The edits that make it 1.2's; one that does not match: no copy.
        if (has_edits(archive, entry) && !apply_edits(archive, entry, root)) zrdr::free_node(root), root = nullptr;
      }
    }
  }
  zutl_zar::close(a);
  zutl_zar::destroy(a);
  return root;
}

bool wave_alias(const std::string& name, std::string* source, float* trim_end_seconds) {
  // 1.2's sounds.zrd names these; on 1.0's (the layer's edit) they come from
  // 1.0's own: the same sounds at 8 bits, and the engine loop without the
  // 176 samples (at 22 kHz) that click as it loops.
  if (!is_10("zbd/reader.zbd")) return false;
  const std::string n = lower(name);
  *trim_end_seconds = 0;
  if (n == "mech_engine_np.wav") {
    *source = "mech_engine.wav", *trim_end_seconds = 176.0f / 22050.0f;
    return true;
  }
  const size_t at = n.rfind("_2s.wav");
  if (at == std::string::npos || at + 7 != n.size()) return false;
  const std::string base = n.substr(0, at);
  if (base != "mech_footfall" && base.compare(0, 5, "soil_") != 0) return false;
  *source = base + ".wav";
  return true;
}

bool on_load(const char* archive, const char* entry, zrdr::Node* root) {
  if (!archive || !entry || !root || !is_10(archive)) return false;
  return apply(data_path(archive), lower(entry), root);
}

bool apply(const std::string& archive, const std::string& entry, zrdr::Node* root) {
  if (archive == "zbd/reader.zbd" && entry == "hud.zrd") return hud(root);
  return apply_edits(archive, entry, root);
}

bool messages(zutl::MessageDll& dll) {
  if (!is_10("Mech3Msg.dll")) return false;
  apply_messages(dll);
  return true;
}

}  // namespace mech3::patch12
