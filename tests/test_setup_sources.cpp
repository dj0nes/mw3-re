// SETUP's sources (docs/setup-sources.md): the archive layer read against
// archives this test writes with libarchive's own writer (nothing committed):
// zip, 7z, a zip inside a zip, an ISO 9660 image, the same as raw 2352-byte
// sectors, an ISO inside a 7z, and a damaged archive.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "mech3/source_archive.h"
#include "mech3/source_map.h"
#include "mech3/disc_install.h"
#include "mech3/game_setup.h"
#include "mech3/patch_import.h"
#include "mech3/source_install.h"
#include <filesystem>

#if MW3_HAVE_LIBARCHIVE
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX  // archive.h brings <windows.h>, whose min/max macros break std::min
#endif
#include <archive.h>
#include <archive_entry.h>

namespace {

using Files = std::map<std::string, std::string>;  // path -> bytes

// `files` written in `format` ("zip", "7zip", "iso9660") into memory.
std::string write_archive(const char* format, const Files& files) {
  archive* a = archive_write_new();
  archive_write_set_format_by_name(a, format);
  if (std::strcmp(format, "7zip") == 0) archive_write_set_format_option(a, "7zip", "compression", "lzma2");
  std::vector<char> buf(64 << 20);
  size_t used = 0;
  archive_write_open_memory(a, buf.data(), buf.size(), &used);
  for (const auto& [path, data] : files) {
    archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, path.c_str());
    archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    archive_write_data(a, data.data(), data.size());
    archive_entry_free(e);
  }
  archive_write_close(a);
  archive_write_free(a);
  return std::string(buf.data(), used);
}

// A ReadFn over bytes in memory.
mech3::source::ArchiveReader::ReadFn memory_stream(const std::string& bytes) {
  auto at = std::make_shared<size_t>(0);
  return [bytes, at](uint8_t* buf, size_t n) -> int64_t {
    const size_t k = std::min(n, bytes.size() - *at);
    std::memcpy(buf, bytes.data() + *at, k);
    *at += k;
    return static_cast<int64_t>(k);
  };
}

// Every file of a reader, read whole.
Files read_all(mech3::source::ArchiveReader& r) {
  Files out;
  mech3::source::ArchiveReader::Entry e;
  while (r.next(&e)) {
    if (e.dir) continue;
    std::string data;
    uint8_t buf[4096];
    for (int64_t n; (n = r.read(buf, sizeof buf)) > 0;) data.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    out[e.path] = data;
  }
  return out;
}

// Test files: a big one (several compressed blocks), a small one, one in a folder.
Files sample() {
  std::string big;
  for (int i = 0; i < 300000; ++i) big += static_cast<char>("MW3 rebuild "[i % 12] + (i / 997) % 7);
  return {{"zbd/reader.zbd", big}, {"Mech3.exe", "MZ fake exe"}, {"video/intro.avi", std::string(70000, 'v')}};
}

std::string temp_file(const char* name, const std::string& bytes) {
  const std::string p = std::string(MW3_TEST_TMP) + "/" + name;
  std::FILE* f = std::fopen(p.c_str(), "wb");
  std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  return p;
}

// An ISO image's 2048-byte sectors as a raw MODE1/2352 track: sync, header,
// the data at 16, the EDC/ECC area left zero (the reader skips it).
std::string raw_track(const std::string& iso) {
  std::string out;
  for (size_t at = 0; at < iso.size(); at += 2048) {
    std::string s(2352, '\0');
    s[0] = 0;
    for (int i = 1; i < 11; ++i) s[static_cast<size_t>(i)] = static_cast<char>(0xff);
    s[15] = 1;  // MODE1
    std::memcpy(&s[16], iso.data() + at, std::min<size_t>(2048, iso.size() - at));
    out += s;
  }
  return out;
}

}  // namespace

TEST(setup_sources_read_zip_and_7z_from_files) {
  const Files want = sample();
  for (const char* fmt : {"zip", "7zip"}) {
    const std::string path = temp_file(std::strcmp(fmt, "zip") == 0 ? "src.zip" : "src.7z", write_archive(fmt, want));
    mech3::source::ArchiveReader r;
    EXPECT(r.open_file(path));
    const Files got = read_all(r);
    EXPECT(got == want);
    std::printf("  %s: %zu files, format %s\n", fmt, got.size(), r.format().c_str());
  }
}

TEST(setup_sources_read_a_zip_inside_a_zip_without_unpacking) {
  const Files want = sample();
  const std::string inner = write_archive("zip", want);
  const std::string outer =
      write_archive("zip", {{"MechWarrior 3 (USA).zip", inner}, {"scans/box front.jpg", std::string(5000, 'j')}});
  mech3::source::ArchiveReader r;
  EXPECT(r.open_stream(memory_stream(outer)));  // a stream: no seeking at all
  mech3::source::ArchiveReader::Entry e;
  bool found = false;
  while (r.next(&e)) {
    if (!mech3::source::archive_name(e.path)) continue;
    mech3::source::ArchiveReader inner_r;
    EXPECT(inner_r.open_stream(r.entry_stream()));
    found = read_all(inner_r) == want;
  }
  EXPECT(found);
}

TEST(setup_sources_read_iso_images_cooked_raw_and_inside_7z) {
  const Files want = sample();
  // ISO 9660 keeps names in upper case 8.3 unless told otherwise; Rock Ridge
  // (libarchive's default) keeps them as written.
  const std::string iso = write_archive("iso9660", want);
  EXPECT(iso.size() % 2048 == 0 && iso.size() > 300000);
  {
    mech3::source::ArchiveReader r;
    EXPECT(r.open_iso_stream(memory_stream(iso)));
    EXPECT(read_all(r) == want);
  }
  {  // the same image as a .bin's raw sectors
    mech3::source::ArchiveReader r;
    EXPECT(r.open_iso_stream(mech3::source::cooked_sectors(memory_stream(raw_track(iso)))));
    EXPECT(read_all(r) == want);
  }
  {  // an .iso inside a 7z, read through the 7z's entry
    const std::string path = temp_file("iso.7z", write_archive("7zip", {{"MW3.iso", iso}}));
    mech3::source::ArchiveReader outer;
    EXPECT(outer.open_file(path));
    mech3::source::ArchiveReader::Entry e;
    EXPECT(outer.next(&e) && e.path == "MW3.iso");
    mech3::source::ArchiveReader r;
    EXPECT(r.open_iso_stream(outer.entry_stream()));
    EXPECT(read_all(r) == want);
  }
}

TEST(setup_sources_refuse_damaged_and_unknown_files) {
  std::string zip = write_archive("zip", sample());
  zip.resize(zip.size() / 2);  // cut short: no central directory
  mech3::source::ArchiveReader r;
  const bool opened = r.open_file(temp_file("cut.zip", zip));
  Files got;
  if (opened) got = read_all(r);
  EXPECT(!opened || got != sample());
  EXPECT(!r.error().empty() || !opened || got.size() < 3);
  mech3::source::ArchiveReader t;
  EXPECT(!t.open_file(temp_file("plain.txt", "not an archive at all, just text\n")));
  EXPECT(mech3::source::archive_name("x.ZIP") && mech3::source::archive_name("a.tar.gz") && !mech3::source::archive_name("track.bin"));
}


namespace {

std::string slurp(const std::string& path) {
  std::string d;
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    char buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.append(buf, n);
    std::fclose(f);
  }
  return d;
}
const std::string kGame = std::string(MW3_SOURCE_DIR) + "/game";
bool have_games() {
  return std::filesystem::exists(kGame + "/mw3-1.0/zbd/reader.zbd") && std::filesystem::exists(kGame + "/mw3-1.2/zbd/reader.zbd") &&
         std::filesystem::exists(kGame + "/pm/zbd/reader.zbd") && std::filesystem::exists(kGame + "/pm-disc/pm.iso") &&
         std::filesystem::exists(kGame + "/patch-1.2/MechWarrior-3_Patch_Win_EN_Patch-12-US/data1.hdr");
}
// Pirate's Moon's disc header, from its image.
std::string pm_hdr() {
  mech3::Disc d;
  std::string err;
  if (!d.open(kGame + "/pm-disc/pm.iso", &err)) return "";
  const mech3::Disc::File* f = d.find("DATA1.HDR");
  if (!f) return "";
  std::string out(static_cast<size_t>(f->size), '\0');
  d.read(*f, 0, out.size(), reinterpret_cast<uint8_t*>(&out[0]));
  return out;
}
// A raw MODE1/2352 track of an image, with an audio track and a cue as
// Redump zips them (the .bins before the .cue).
Files redump(const std::string& iso, const std::string& name) {
  Files f;
  f[name + " (Track 1).bin"] = raw_track(iso);
  f[name + " (Track 2).bin"] = std::string(2352 * 200, '\x11');  // audio: no sync pattern
  f[name + ".cue"] = "FILE \"" + name + " (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\nFILE \"" +
                     name + " (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n";
  return f;
}

}  // namespace

// The map: installed copies on disk (1.0, 1.2, Pirate's Moon refused).
TEST(setup_map_installed_copies_on_disk) {
  if (!have_games()) return;
  const mech3::source::Found a = mech3::source::map_source(kGame + "/mw3-1.0");
  EXPECT(a.kind == mech3::source::Found::kInstall && a.game == mech3::source::Found::kMw3 && a.version == "1.0" && a.on_disk);
  const mech3::source::Found b = mech3::source::map_source(kGame + "/mw3-1.2/Mech3.exe");  // its exe
  EXPECT(b.kind == mech3::source::Found::kInstall && b.version == "1.2");
  const mech3::source::Found c = mech3::source::map_source(kGame + "/pm");
  EXPECT(c.game == mech3::source::Found::kPiratesMoon && c.summary.find("Pirate's Moon") != std::string::npos);
  const mech3::source::Found d = mech3::source::map_source(kGame + "/pm-disc/pm.iso");
  EXPECT(d.kind == mech3::source::Found::kDisc && d.game == mech3::source::Found::kPiratesMoon);
  std::printf("  %s | %s | %s\n", a.summary.c_str(), b.summary.c_str(), c.summary.c_str());
}

// The map in archives: an installed copy zipped, the same zipped again, a
// disc image (2048 and raw, an unknown label, Redump's cue and audio order)
// in a zip and a 7z, Pirate's Moon's refused, and an archive with no game.
TEST(setup_map_finds_the_game_inside_archives) {
  if (!have_games()) return;
  using mech3::source::Found;
  const Files install = {{"MechWarrior 3/zbd/reader.zbd", slurp(kGame + "/mw3-1.0/zbd/reader.zbd")},
                         {"MechWarrior 3/Mech3.exe", slurp(kGame + "/mw3-1.0/Mech3.exe")},
                         {"MechWarrior 3/readme.txt", "hello"}};
  const std::string zip = write_archive("zip", install);
  Found a = mech3::source::map_source(temp_file("install.zip", zip));
  EXPECT(a.kind == Found::kInstall && a.game == Found::kMw3 && a.version == "1.0" && a.root == "MechWarrior 3/" &&
         a.steps.empty() && !a.on_disk);
  Found b = mech3::source::map_source(temp_file("nested.zip", write_archive("zip", {{"inner/mw3.zip", zip}, {"box.jpg", "j"}})));
  EXPECT(b.kind == Found::kInstall && b.steps.size() == 1 && b.steps[0].entry == "inner/mw3.zip");

  // A disc: the 1.2 patch's installer header stands in for the disc's (it
  // lists zbd/reader.zbd at 1.2's size); libarchive labels the image
  // "CDROM", a label no pressing has: content decides.
  const std::string hdr = slurp(kGame + "/patch-1.2/MechWarrior-3_Patch_Win_EN_Patch-12-US/data1.hdr");
  const std::string iso = write_archive("iso9660", {{"DATA1.HDR", hdr}, {"DATA1.CAB", std::string(300000, 'c')}});
  Found c = mech3::source::map_source(temp_file("disc.7z", write_archive("7zip", {{"MW3.iso", iso}})));
  EXPECT(c.kind == Found::kDisc && c.game == Found::kMw3 && c.version == "1.2" && c.steps.size() == 1 &&
         c.steps[0].kind == mech3::source::Step::kImage && c.label == "CDROM");
  Found d = mech3::source::map_source(temp_file("redump.zip", write_archive("zip", redump(iso, "MechWarrior 3 (USA)"))));
  EXPECT(d.kind == Found::kDisc && d.game == Found::kMw3 && d.raw && d.raw_offset == 16 && d.audio.size() == 1 && d.audio[0] == "MechWarrior 3 (USA) (Track 2).bin" &&
         d.data_track == "MechWarrior 3 (USA) (Track 1).bin" && d.audio_track == std::vector<int>{2} &&
         d.audio_start == std::vector<int64_t>{2 * 75 * 2352});
  std::printf("  redump: %s (%s)\n", d.summary.c_str(), d.detail.c_str());
  // The same on disk: its cue read for the music.
  std::string cue;
  for (const auto& [name, bytes] : redump(iso, "On Disk")) {
    const std::string p = temp_file(name.c_str(), bytes);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".cue") == 0) cue = p;
  }
  Found g = mech3::source::map_source(cue);
  EXPECT(g.kind == Found::kDisc && g.on_disk && g.game == Found::kMw3 && g.audio == std::vector<std::string>{"On Disk (Track 2).bin"} &&
         g.audio_track == std::vector<int>{2} && g.audio_start == std::vector<int64_t>{2 * 75 * 2352});

  // Pirate's Moon's disc in a zip in a zip: named, not installed.
  const std::string pm_iso = write_archive("iso9660", {{"DATA1.HDR", pm_hdr()}});
  Found e = mech3::source::map_source(
      temp_file("pm.zip", write_archive("zip", {{"pm/disc.zip", write_archive("zip", {{"PM.iso", pm_iso}})}})));
  EXPECT(e.game == Found::kPiratesMoon && e.summary.find("comes later") != std::string::npos);
  // The look stopped from its progress (SETUP's CANCEL): nothing found, said so.
  int asked = 0;
  Found s = mech3::source::map_source(temp_file("stop.zip", write_archive("zip", redump(iso, "Stop"))),
                                      [&](double, const std::string&) { return ++asked < 2; });
  EXPECT(s.cancelled && s.kind == Found::kNothing && asked == 2);
  // No game at all.
  Found f = mech3::source::map_source(temp_file("other.zip", write_archive("zip", {{"notes.txt", "x"}, {"data/a.bin", "abc"}})));
  EXPECT(f.kind == Found::kNothing && !f.summary.empty());
}

// The install: entry paths that would leave the folder are refused.
TEST(setup_install_refuses_paths_that_leave_the_folder) {
  using mech3::source::safe_relative;
  EXPECT(safe_relative("MechWarrior 3/zbd/reader.zbd") == "MechWarrior 3/zbd/reader.zbd");
  EXPECT(safe_relative("a\\b\\c.txt") == "a/b/c.txt" && safe_relative("./a//b") == "a/b");
  EXPECT(safe_relative("/etc/passwd") == "" && safe_relative("\\\\server\\share\\x") == "");  // absolute, a share
  EXPECT(safe_relative("C:/Windows/x") == "" && safe_relative("c:x") == "");                // drives
  EXPECT(safe_relative("../x") == "" && safe_relative("..\\x") == "" && safe_relative("a/../../x") == "");
  EXPECT(safe_relative("a/..") == "" && safe_relative("") == "" && safe_relative("./") == "");
  if (!have_games()) return;
  // In an installed copy's archive: the whole copy refused, nothing left.
  const std::string reader = slurp(kGame + "/mw3-1.0/zbd/reader.zbd");
  const std::string tmp = std::string(MW3_TEST_TMP) + "/unsafe";
  std::error_code ec;
  // The copy at the archive's top, so every entry is the copy's (an entry
  // outside the copy's folder is passed over, never written).
  for (const char* bad : {"../evil.txt", "zbd/..\\..\\evil.txt", "/tmp/mw3-evil.txt", "C:/evil.txt", "C:evil.txt"}) {
    std::filesystem::remove_all(tmp, ec);
    const std::string zip = temp_file("unsafe.zip", write_archive("zip", {{"zbd/reader.zbd", reader}, {bad, "evil"}}));
    const mech3::source::Found f = mech3::source::map_source(zip);
    const mech3::setup::Result r = mech3::setup::install_from_source(f, tmp + "/game", "", "", tmp + "/write", nullptr);
    std::printf("  %s: %s\n", bad, r.summary.c_str());
    EXPECT(f.kind == mech3::source::Found::kInstall && !r.ok && !std::filesystem::exists(tmp + "/game", ec) &&
           !std::filesystem::exists(tmp + "/game.new", ec) && !std::filesystem::exists(tmp + "/evil.txt", ec) &&
           !std::filesystem::exists("/tmp/mw3-evil.txt", ec));
  }
  // A path that resolves outside through a link already in the folder.
  std::filesystem::remove_all(tmp, ec);
  std::filesystem::create_directories(tmp + "/into", ec);
  std::filesystem::create_directories(tmp + "/outside", ec);
  std::filesystem::create_directory_symlink(tmp + "/outside", tmp + "/into/link", ec);
  if (!ec) {
    const std::string zip = temp_file("link.zip", write_archive("zip", {{"MechWarrior 3/link/evil.txt", "evil"},
                                                                        {"MechWarrior 3/zbd/reader.zbd", reader}}));
    const mech3::source::Staged st = mech3::source::stage_source(mech3::source::map_source(zip), tmp + "/stage", tmp + "/into", nullptr);
    EXPECT(!st.ok && !std::filesystem::exists(tmp + "/outside/evil.txt", ec));
  }
}

namespace {
// A disc image like the MW3 disc: the 1.2 patch's InstallShield pair stands
// in for DATA1.HDR/CAB (31 files, zbd/reader.zbd among them), two movies.
struct TestDisc {
  std::string hdr, cab, movie, iso;
  std::vector<mech3::PatchEntry> want;
  bool make() {
    const std::string folder = kGame + "/patch-1.2/MechWarrior-3_Patch_Win_EN_Patch-12-US";
    if (!std::filesystem::exists(folder + "/data1.cab")) return false;
    hdr = slurp(folder + "/data1.hdr"), cab = slurp(folder + "/data1.cab");
    for (int i = 0; i < 70000; ++i) movie += static_cast<char>(i * 31);
    iso = write_archive("iso9660", {{"DATA1.HDR", hdr}, {"DATA1.CAB", cab}, {"SETUP.EXE", std::string(3000, 'x')},
                                    {"VIDEO/C1.AVI", movie}, {"VIDEO/INTRO.AVI", std::string(6000, 'i')}});
    // The MW3 disc's mastering marks in its volume descriptor's reserved fields.
    iso.replace(16 * 2048 + 88, 3, "%/@");
    iso.replace(16 * 2048 + 1395, 2, "2)");
    std::string err;
    return mech3::read_installshield(std::vector<uint8_t>(hdr.begin(), hdr.end()), std::vector<uint8_t>(cab.begin(), cab.end()),
                                     &want, &err);
  }
  bool installed(const std::string& out) const {
    int same = 0;
    for (const mech3::PatchEntry& e : want) same += slurp(out + "/" + e.path) == std::string(e.data.begin(), e.data.end());
    return same == static_cast<int>(want.size()) && slurp(out + "/video/C1.AVI") == movie &&
           slurp(out + "/video/INTRO.AVI").size() == 6000 && std::filesystem::exists(out + "/mw3-install.txt") &&
           !std::filesystem::exists(out + "/SETUP.EXE") && !std::filesystem::exists(out + ".new");
  }
};
}  // namespace

// The install from archives, in one pass: an installed copy zipped, a disc's
// files in a zip's folder, an .iso in a 7z, Redump's .bins and cue in a zip
// (the audio track as the music, its pregap left out).
TEST(setup_install_streams_from_archives) {
  if (!have_games()) return;
  namespace su = mech3::setup;
  const std::string tmp = std::string(MW3_TEST_TMP) + "/stream";
  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);
  const std::string reader = slurp(kGame + "/mw3-1.0/zbd/reader.zbd"), exe = slurp(kGame + "/mw3-1.0/Mech3.exe");
  const std::string zip = temp_file("copy.zip", write_archive("zip", {{"A box.jpg", "j"},
                                                                     {"MechWarrior 3/zbd/reader.zbd", reader},
                                                                     {"MechWarrior 3/Mech3.exe", exe},
                                                                     {"MechWarrior 3/video/C1.AVI", "m"}}));
  double last = 0;
  bool forward = true;
  const su::Result a = su::install_from_source(mech3::source::map_source(zip), tmp + "/copy", "", "", tmp + "/write",
                                               [&](const su::Progress& p) {
                                                 forward = forward && p.fraction >= last, last = p.fraction;
                                                 return true;
                                               });
  std::printf("  copy: %s\n", a.summary.c_str());
  EXPECT(a.ok && a.files == 3 && slurp(tmp + "/copy/zbd/reader.zbd") == reader && slurp(tmp + "/copy/video/C1.AVI") == "m" &&
         std::filesystem::exists(tmp + "/copy/mw3-install.txt") && !std::filesystem::exists(tmp + "/copy/A box.jpg") &&
         !std::filesystem::exists(tmp + "/copy.new") && forward && last == 1);
  // Again over it (an earlier install is replaced); not over someone's folder.
  EXPECT(su::install_from_source(mech3::source::map_source(zip), tmp + "/copy", "", "", tmp + "/write", nullptr).ok);
  std::filesystem::create_directories(tmp + "/mine", ec);
  temp_file("stream/mine/notes.txt", "mine");
  EXPECT(!su::install_from_source(mech3::source::map_source(zip), tmp + "/mine", "", "", tmp + "/write", nullptr).ok &&
         slurp(tmp + "/mine/notes.txt") == "mine");

  TestDisc disc;
  if (!disc.make()) return;
  const std::string folder = temp_file("folder.zip", write_archive("zip", {{"MW3CD/DATA1.HDR", disc.hdr}, {"MW3CD/DATA1.CAB", disc.cab},
                                                                          {"MW3CD/VIDEO/C1.AVI", disc.movie},
                                                                          {"MW3CD/VIDEO/INTRO.AVI", std::string(6000, 'i')}}));
  const su::Result b = su::install_from_source(mech3::source::map_source(folder), tmp + "/folder", "", "", tmp + "/write", nullptr);
  std::printf("  folder: %s\n", b.summary.c_str());
  EXPECT(b.ok && b.movies == 2 && disc.installed(tmp + "/folder"));
  const std::string sevenz = temp_file("iso.7z", write_archive("7zip", {{"MW3.iso", disc.iso}}));
  const su::Result c = su::install_from_source(mech3::source::map_source(sevenz), tmp + "/iso", "", "", tmp + "/write", nullptr);
  EXPECT(c.ok && disc.installed(tmp + "/iso"));
  const Files bins = redump(disc.iso, "MechWarrior 3 (USA)");
  const std::string redump_zip = temp_file("redump-install.zip", write_archive("zip", bins));
  const su::Result d = su::install_from_source(mech3::source::map_source(redump_zip), tmp + "/redump", "", tmp + "/music",
                                               tmp + "/write", nullptr);
  std::printf("  redump: %s\n", d.summary.c_str());
  const std::string wav = slurp(tmp + "/music/track02.wav");
  EXPECT(d.ok && d.music_tracks == 1 && disc.installed(tmp + "/redump") && wav.size() == 44 + 50 * 2352 &&
         wav.compare(0, 4, "RIFF") == 0 && wav.substr(44) == std::string(50 * 2352, '\x11'));
  EXPECT(!std::filesystem::exists(tmp + "/write/setup-staging"));
}

// A cancel, a damaged archive and too little space: nothing is left behind.
TEST(setup_install_leaves_nothing_when_it_stops) {
  if (!have_games()) return;
  namespace su = mech3::setup;
  TestDisc disc;
  if (!disc.make()) return;
  const std::string tmp = std::string(MW3_TEST_TMP) + "/stops";
  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);
  auto nothing = [&](const std::string& out) {
    return !std::filesystem::exists(out, ec) && !std::filesystem::exists(out + ".new", ec) &&
           !std::filesystem::exists(tmp + "/write/setup-staging", ec) && !std::filesystem::exists(tmp + "/music", ec);
  };
  const std::string zip = temp_file("stops.zip", write_archive("zip", redump(disc.iso, "MW3")));
  const mech3::source::Found f = mech3::source::map_source(zip);
  int calls = 0;
  const su::Result a = su::install_from_source(f, tmp + "/a", "", tmp + "/music", tmp + "/write",
                                               [&](const su::Progress&) { return ++calls < 3; });
  EXPECT(a.cancelled && !a.ok && nothing(tmp + "/a"));
  // The zip cut short, inside its data track.
  const std::string cut = temp_file("cut.zip", slurp(zip).substr(0, slurp(zip).size() * 2 / 3));
  const mech3::source::Found g = mech3::source::map_source(cut);
  if (g.kind != mech3::source::Found::kNothing) {
    const su::Result b = su::install_from_source(g, tmp + "/b", "", tmp + "/music", tmp + "/write", nullptr);
    std::printf("  cut: %s\n", b.summary.c_str());
    EXPECT(!b.ok && nothing(tmp + "/b"));
  }
  // No room: refused before anything is read or written.
  const uint64_t keep = mech3::source::space_spare();
  mech3::source::space_spare() = uint64_t{1} << 60;
  calls = 0;
  Files padded = redump(disc.iso, "MW3");
  padded["A readme.txt"] = "passed over, before the disc";  // a progress report before any file is written
  const mech3::source::Found h = mech3::source::map_source(temp_file("padded.zip", write_archive("zip", padded)));
  const su::Result c = su::install_from_source(h, tmp + "/c", "", tmp + "/music", tmp + "/write",
                                               [&](const su::Progress&) { return ++calls, true; });
  mech3::source::space_spare() = keep;
  std::printf("  no room: %s\n", c.summary.c_str());
  EXPECT(!c.ok && c.summary.find("free space") != std::string::npos && calls == 0 && nothing(tmp + "/c"));
}

// The real thing, when the backups are here (a long read: MW3_SETUP_LONG=1):
// Redump's MechWarrior 3 zip, its data track 626 MB and two audio tracks.
TEST(setup_map_the_real_redump_zip) {
  const char* zip = "/Volumes/iO/backups/win98/MechWarrior 3 (USA).zip";
  if (!std::getenv("MW3_SETUP_LONG") || !std::filesystem::exists(zip)) return;
  const mech3::source::Found f = mech3::source::map_source(zip);
  std::printf("  %s\n%s", f.summary.c_str(), f.detail.c_str());
  EXPECT(f.kind == mech3::source::Found::kDisc && f.game == mech3::source::Found::kMw3 && f.version == "1.0" &&
         f.label == "MW3" && f.audio.size() == 2);
}

// And installed from it in one pass (MW3_SETUP_LONG=1): the game as the
// 1.0 disc's, its movies, the two audio tracks as the music.
TEST(setup_install_the_real_redump_zip) {
  const char* zip = "/Volumes/iO/backups/win98/MechWarrior 3 (USA).zip";
  if (!std::getenv("MW3_SETUP_LONG") || !std::filesystem::exists(zip)) return;
  const std::string tmp = std::string(MW3_TEST_TMP) + "/real";
  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);
  const mech3::setup::Result r = mech3::setup::install_from_source(mech3::source::map_source(zip), tmp + "/game", "",
                                                                   tmp + "/music", tmp + "/write", nullptr);
  std::printf("  %s\n%s", r.summary.c_str(), r.detail.c_str());
  EXPECT(r.ok && r.version == "1.0" && r.movies > 0 && r.music_tracks == 2 &&
         std::filesystem::exists(tmp + "/music/track02.wav") && std::filesystem::exists(tmp + "/music/track03.wav") &&
         !std::filesystem::exists(tmp + "/write/setup-staging"));
  std::filesystem::remove_all(tmp, ec);
}

#else

TEST(setup_sources_no_libarchive_in_this_build) { EXPECT(!mech3::source::archives_supported()); }

#endif

// The swap replaces only our own earlier install (its mw3-install.txt) or an
// empty folder: --install-to may name any folder, and the old one is deleted.
TEST(setup_swap_replaces_only_our_own_install) {
  namespace fs = std::filesystem;
  const fs::path root = fs::path(MW3_TEST_TMP) / "swap";
  std::error_code ec;
  fs::remove_all(root, ec);
  const auto put = [](const fs::path& p, const char* text) {
    fs::create_directories(p.parent_path());
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    std::fputs(text, f);
    std::fclose(f);
  };
  put(root / "new1" / "Mech3.exe", "new");
  put(root / "theirs" / "letter.txt", "keep me");
  EXPECT(!mech3::swap_in_folder((root / "new1").string(), (root / "theirs").string(), ec));
  EXPECT(fs::exists(root / "theirs" / "letter.txt") && fs::exists(root / "new1" / "Mech3.exe"));
  put(root / "ours" / "mw3-install.txt", "ours");
  put(root / "ours" / "old.zbd", "old");
  EXPECT(mech3::swap_in_folder((root / "new1").string(), (root / "ours").string(), ec));
  EXPECT(fs::exists(root / "ours" / "Mech3.exe") && !fs::exists(root / "ours" / "old.zbd"));
  EXPECT(!fs::exists(root / "ours.old"));
  fs::create_directories(root / "empty");
  put(root / "new2" / "Mech3.exe", "new");
  EXPECT(mech3::swap_in_folder((root / "new2").string(), (root / "empty").string(), ec));
  EXPECT(fs::exists(root / "empty" / "Mech3.exe"));
  fs::remove_all(root, ec);
}
