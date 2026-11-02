// C:\zipper\GameZMech3\zSound\zsnd_create.cpp: the WAV reader, sound buffers
// and the DirectSound error names (docs/sound.md section 3). The error
// reporters (0x00574660, 0x005746b0, 0x00574af0) and the WAV reader
// (0x00574d40..0x00574f50) sit just before the file's string-referencing
// functions; the release and stream functions (0x00575690..0x00575980) after.
#include <cstdio>
#include <cstring>

#include "gamez/zerror/zerr.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"

namespace zsnd {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zSound\\zsnd_create.cpp";

uint32_t u32(const std::vector<uint8_t>& b, size_t at) {
  uint32_t v = 0;
  if (at + 4 <= b.size()) std::memcpy(&v, b.data() + at, 4);
  return v;
}

platform::SoundOutput* out() { return hooks().output; }
}  // namespace

// Mech3.exe 1.2 @ 0x00574af0 (ECX hr, EDX file, line). The A3D twin 0x005746b0
// and the MCI one 0x00574660 (fprintf to stderr) are not needed.
int32_t ds_error(int32_t hr, const char* file, int line) {
  const char* name = "UNKNOWN";
  switch (static_cast<uint32_t>(hr)) {
    case 0: return 1;
    case 0x80004005: name = "DSERR_GENERIC"; break;
    case 0x80004001: name = "DSERR_UNSUPPORTED"; break;
    case 0x8007000e: name = "DSERR_OUTOFMEMORY"; break;
    case 0x80040110: name = "DSERR_NOAGGREGATION"; break;
    case 0x8878000a: name = "DSERR_ALLOCATED"; break;
    case 0x80070057: name = "DSERR_INVALIDPARAM"; break;
    case 0x88780032: name = "DSERR_INVALIDCALL"; break;
    case 0x8878001e: name = "DSERR_CONTROLUNAVAIL"; break;
    case 0x88780064: name = "DSERR_BADFORMAT"; break;
    case 0x88780046: name = "DSERR_PRIOLEVELNEEDED"; break;
    case 0x88780082: name = "DSERR_ALREADYINITIALIZED"; break;
    case 0x88780078: name = "DSERR_NODRIVER"; break;
    case 0x88780096: name = "DSERR_BUFFERLOST"; break;
    case 0x887800a0: name = "DSERR_OTHERAPPHASPRIO"; break;
    default: break;
  }
  char msg[64];
  std::snprintf(msg, sizeof msg, "DirectSound Error [%s]", name);
  zerr::report(0x400, file, line, msg);
  return 0;
}

// Mech3.exe 1.2 @ 0x00574db0. The chunk walk of a RIFF WAVE: the first "fmt "
// (at least 0xe bytes, else the walk stops), the first "data" and "cue ".
// A file that is not RIFF WAVE still counts as read, with no format. Reads
// past the buffer give 0 here (the exe reads on into the heap).
int32_t wave_parse(Wave& w) {
  if (w.bytes.empty()) return 0;
  w.has_format = false;
  w.data = 0;
  w.data_size = 0;
  const std::vector<uint8_t>& b = w.bytes;
  const uint32_t riff = u32(b, 0), size = u32(b, 4), wave = u32(b, 8);
  if (riff != 0x46464952 || wave != 0x45564157) return 1;
  const size_t end = static_cast<size_t>(size) + 8;
  size_t p = 12;
  bool bad = false;
  while (p < end && !bad) {
    const uint32_t id = u32(b, p), n = u32(b, p + 4);
    p += 8;
    if (id == 0x20657563) {  // "cue "
      if (w.cues == 0 || w.cue_count == 0) w.cue_count = u32(b, p), w.cues = p + 4;
    } else if (id == 0x20746d66) {  // "fmt "
      if (!w.has_format) {
        if (n < 0xe) {
          bad = true;
        } else {
          w.has_format = true;
          w.format = platform::WaveFormat{};
          if (p + sizeof(platform::WaveFormat) <= b.size()) std::memcpy(&w.format, b.data() + p, sizeof w.format);
        }
      }
    } else if (id == 0x61746164) {  // "data"
      if (w.data == 0 || w.data_size == 0) w.data = p, w.data_size = n;
    }
    if (!bad) p += (static_cast<size_t>(n) + 1) & ~static_cast<size_t>(1);
  }
  return 1;
}

// Ours (docs/modding.md, M3): what makes a parsed WAV unplayable, or null.
// The exe copies the samples and reads the cues as the chunks say.
const char* wave_broken(const Wave& w) {
  const uint64_t n = w.bytes.size();
  if (w.data && w.data + static_cast<uint64_t>(w.data_size) > n) return "its samples past its end";
  if (w.cues && w.cues + static_cast<uint64_t>(w.cue_count) * 0x18 > n) return "its cue points past its end";
  if (w.has_format) {
    const platform::WaveFormat& f = w.format;
    if (f.channels == 0 || f.channels > 8 || f.bits_per_sample == 0 || f.bits_per_sample > 32 || f.samples_per_sec == 0 ||
        f.samples_per_sec > 384000 || f.block_align == 0 || f.avg_bytes_per_sec == 0)
      return "its format";
  }
  return nullptr;
}

namespace {
// A broken WAV said and refused; `where` the file or the archive it came from.
int32_t refuse_broken(Wave& w, const std::string& where) {
  const char* why = w.valid ? wave_broken(w) : nullptr;
  if (!why) return w.valid;
  std::printf("mw3: %s: sound %s is broken (%s), not read\n", where.c_str(), w.path.c_str(), why);
  w.valid = 0;
  w.bytes.clear();
  w.has_format = false, w.data = w.data_size = 0, w.cues = 0, w.cue_count = 0;
  return 0;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00574e90. FIX (A): fopen on the found path becomes the
// mounts (zutl_path.h).
int32_t wave_read_file(Wave& w) {
  if (w.valid) return 1;
  if (w.path.empty()) return 0;
  std::FILE* f = zutl::open(w.path.c_str(), "rb");
  if (!f) return 0;
  std::fseek(f, 0, SEEK_END);  // 0x005760b0
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  w.bytes.assign(size > 0 ? static_cast<size_t>(size) : 0, 0);
  if (!w.bytes.empty() && std::fread(w.bytes.data(), w.bytes.size(), 1, f) != 1) {
    // fread's count is not checked by the original; a short read leaves zeros.
  }
  std::fclose(f);
  w.valid = wave_parse(w);
  const std::string host = zutl::find(w.path.c_str());
  return refuse_broken(w, host.empty() ? w.path : host);
}

// Mech3.exe 1.2 @ 0x00574f50: the entry's length, then its bytes.
int32_t wave_read_archive(Wave& w, zutl_zar::Archive& a) {
  if (w.valid) return 1;
  uint32_t size = 0;
  zutl_zar::read(a, zutl_zar::find(a, w.path.c_str()), nullptr, &size);
  if (size != 0) {
    w.bytes.assign(size, 0);
    zutl_zar::read(a, zutl_zar::find(a, w.path.c_str()), w.bytes.data(), &size);
    w.valid = wave_parse(w);
    const std::string host = a.path ? zutl::find(a.path) : std::string();
    return refuse_broken(w, !host.empty() ? host : a.path ? a.path : "a sounds archive");
  }
  return w.valid;
}

// Mech3.exe 1.2 @ 0x005751e0 (ECX the sound + 4). The cue points as times
// (seconds) and byte offsets, and the sound's length as a last cue.
int32_t make_cues(Sound* s, const Wave& w) {
  s->cue_times.clear();
  s->cue_at.clear();
  s->cue_bytes.clear();
  s->cue_count = 0;
  s->cue_fn = nullptr;
  const uint32_t n = w.cue_count;
  s->cue_count = n;
  if (n == 0) return 0;
  s->cue_times.assign(n + 1, 0.0f);
  s->cue_at.assign(n + 1, 0.0f);
  s->cue_bytes.assign(n + 1, 0);
  s->cue_count = n + 1;
  const platform::WaveFormat& f = w.format;
  uint32_t i = 0;
  for (; i < n; ++i) {
    const uint32_t pos = u32(w.bytes, w.cues + 0x18 * i + 4);  // dwPosition, in samples
    s->cue_bytes[i] = static_cast<uint32_t>(f.bits_per_sample >> 3) * f.channels * pos;
    s->cue_times[i] = static_cast<float>(static_cast<double>(pos) / static_cast<int32_t>(f.samples_per_sec));
  }
  s->cue_times[i] = static_cast<float>(static_cast<double>(w.data_size) / static_cast<int32_t>(f.avg_bytes_per_sec));
  return 1;
}

// Mech3.exe 1.2 @ 0x005752e0 (0x00574fd0's DirectSound branch). Volume always,
// pan for 3D sounds, frequency when asked; purgeable sounds in software,
// hardware-flagged ones static; the whole WAV copied in through Lock.
int32_t create_buffer(Sound* s, const Wave& w) {
  if (s->kind != 0) return 0;
  platform::BufferDesc desc;
  desc.bytes = w.data_size;
  desc.format = w.has_format ? &w.format : nullptr;
  const uint32_t f = s->flags;
  desc.flags = (f & k3D) ? 0xc0 : 0x80;
  if (f & kFrequency) desc.flags |= 0x20;
  if (f & kPurgeable) desc.flags |= 0x8;
  else if (f & kHardware) desc.flags |= 0x2;
  if (f & kStream) desc.flags |= 0x10000;
  int32_t hr = out()->create_buffer(desc, &s->voice.buffer);
  if (hr != 0) {
    char msg[300];
    std::snprintf(msg, sizeof msg, "Error creating sound buffer ( %s )", w.path.c_str());
    zerr::report(0x200, kFile, 0x119, msg);
    ds_error(hr, kFile, 0x11a);
    return 0;
  }
  s->voice.kind = 0;
  const SoundBuffer b = s->voice.buffer;
  uint32_t st = 0;
  hr = out()->get_status(b, &st);
  if (hr != 0) return ds_error(hr, kFile, 0x132);
  if (st & platform::SoundOutput::kStatusLost) {
    hr = out()->restore(b);
    if (hr != 0) return ds_error(hr, kFile, 0x137);
  }
  void *p1 = nullptr, *p2 = nullptr;
  uint32_t n1 = 0, n2 = 0;
  hr = out()->lock(b, 0, w.data_size, &p1, &n1, &p2, &n2);
  if (hr != 0) return ds_error(hr, kFile, 0x141);
  const uint8_t* src = w.data ? w.bytes.data() + w.data : nullptr;
  if (src && p1) std::memcpy(p1, src, n1);
  if (n2) {
    if (src && p2) std::memcpy(p2, src + n1, n2);
    n1 += n2;
  }
  hr = out()->unlock(b, p1, n1, p2, n2);
  if (hr != 0) return ds_error(hr, kFile, 0x14e);
  hr = out()->set_current_position(b, 0);
  if (hr != 0) return ds_error(hr, kFile, 0x154);
  make_cues(s, w);
  s->flags &= ~kCreated;
  return 1;
}

// Mech3.exe 1.2 @ 0x00575700. Frees the cues and format names, releases the
// duplicates and the sound's own buffer.
int32_t release_sound(Sound* s) {
  if (!s || s->kind != 0) return 0;
  s->cue_times.clear();
  s->cue_at.clear();
  s->cue_bytes.clear();
  for (Format& f : s->formats) f.file.clear(), f.has_file = false;
  for (Voice* v : s->dups) {
    if (v->buffer) out()->release(v->buffer), v->buffer = 0;
    delete v;
  }
  s->dups.clear();
  if (!s->voice.buffer) return 1;
  out()->release(s->voice.buffer);
  s->voice.buffer = 0;
  s->flags &= ~kLoaded;
  return 1;
}

// Mech3.exe 1.2 @ 0x005758c0 (ECX format, EDX samples, the size): a looping
// sound for the movie player's soundtrack, filled through stream_lock.
Sound* stream_create(const platform::WaveFormat* format, const void* data, uint32_t bytes) {
  auto* s = new Sound{};
  s->volume = 0.0f, s->queue_life = 0.0f, s->range_min = 0.0f, s->range_max = 0.0f;  // calloc
  s->freq_max = 0.0f, s->freq_min = 0.0f, s->a3d_dist = 0.0f;
  Wave w;  // the path is "" (0x005f4c90), nothing read
  w.has_format = format != nullptr;
  if (format) w.format = *format;
  w.data_size = bytes;
  if (data && bytes) {
    w.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
    w.bytes.insert(w.bytes.begin(), 0);  // data at offset 1: 0 means none
    w.data = 1;
  }
  s->flags |= kLooped | kStream;
  const int32_t ok = create_buffer(s, w);
  s->volume = 1.0f;
  s->flags = (s->flags & ~kLoaded) | ((ok & 1) << 3);
  if (!(s->flags & kLoaded)) {
    delete s;
    return nullptr;
  }
  return s;
}

// Mech3.exe 1.2 @ 0x00575550 (ECX sound, EDX offset): 1 when locked.
int32_t stream_lock(Sound* s, uint32_t offset, uint32_t bytes, void** p1, uint32_t* n1, void** p2, uint32_t* n2) {
  if (s->kind != 0) return 0;
  const int32_t hr = out()->lock(s->voice.buffer, offset, bytes, p1, n1, p2, n2);
  if (hr != 0) return ds_error(hr, kFile, 0x1c4);
  return 1;
}

// Mech3.exe 1.2 @ 0x00575600 (ECX sound, EDX first part).
int32_t stream_unlock(Sound* s, void* p1, uint32_t n1, void* p2, uint32_t n2) {
  if (s->kind != 0) return 0;
  const int32_t hr = out()->unlock(s->voice.buffer, p1, n1, p2, n2);
  if (hr != 0) return ds_error(hr, kFile, 0x1f5);
  return 1;
}

// Mech3.exe 1.2 @ 0x00575690: the play cursor, 0 on failure.
uint32_t stream_position(Sound* s) {
  if (s->kind != 0) return 0;
  uint32_t play = 0, write = 0;
  const int32_t hr = out()->get_current_position(s->voice.buffer, &play, &write);
  return hr == 0 ? play : 0;
}

// Mech3.exe 1.2 @ 0x00575980
void stream_free(Sound* s) {
  release_sound(s);
  delete s;
}

}  // namespace zsnd
