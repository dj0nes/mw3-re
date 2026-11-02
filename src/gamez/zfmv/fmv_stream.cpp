#include "gamez/zfmv/fmv_stream.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gamez/zfmv/fmv_script.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zvideo/zvid_ddd3d.h"

namespace zfmv {

namespace {

constexpr uint32_t kVids = 0x73646976;   // 'vids'
constexpr uint32_t kAuds = 0x73647561;   // 'auds'
constexpr uint32_t kVidc = 0x63646976;   // 'vidc'
constexpr uint32_t kDecompressBegin = 0x400c, kDecompressEnd = 0x400e;
constexpr uint32_t kFindNextKey = 0x11, kFindPrevKey = 0x14;
constexpr uint32_t kHurryUp = 0x80000000u, kNotKeyFrame = 0x08000000u;

uint32_t rd32(const std::vector<uint8_t>& b, size_t at) {
  uint32_t v = 0;
  if (at + 4 <= b.size()) std::memcpy(&v, &b[at], 4);
  return v;
}
uint16_t rd16(const std::vector<uint8_t>& b, size_t at) {
  uint16_t v = 0;
  if (at + 2 <= b.size()) std::memcpy(&v, &b[at], 2);
  return v;
}
void wr32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  if (at + 4 <= b.size()) std::memcpy(&b[at], &v, 4);
}
void wr16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
  if (at + 2 <= b.size()) std::memcpy(&b[at], &v, 2);
}

}  // namespace

// Mech3.exe 1.2 @ 0x0059a4a0: the sound first, then the picture.
Stream::Stream(const char* p, bool streamed) : path(p ? p : ""), streamed(streamed) {
  host().avi_file_init();
  open_audio();
  open_video();
}

// Mech3.exe 1.2 @ 0x0059a530
Stream::~Stream() {
  Host& h = host();
  if (audio_ok) {
    buffer.clear();
    if (sound) h.sound_stream_free(sound);
    wave.clear();
    h.avi_release(audio);
  }
  if (video_ok) {
    if (decompressor) {
      h.ic_send(decompressor, kDecompressEnd, nullptr, nullptr);
      h.ic_close(decompressor);
    }
    // The image never gets a DirectDraw surface here (the program's stretch
    // makes none), so [0x0072f384] is not called.
    h.avi_release(video);
    h.avi_file_exit();
  }
}

// Mech3.exe 1.2 @ 0x0059a650. The decompressor writes 16-bit pixels (the 2D
// surfaces' depth) with the display's masks, rows top down.
void Stream::open_video() {
  Host& h = host();
  next = 0;
  int32_t size = 0;
  // The engine's report (0x0049bfd0) is empty in the release build; each
  // failure leaves Video for Windows (AVIFileExit).
  if (h.avi_open(&video, path.c_str(), kVids) != 0) return h.avi_file_exit();  // "Cannot Open AVI File"
  if (h.avi_read_format(video, nullptr, &size) != 0) return h.avi_file_exit();  // "Cannot Read AVI Format Size"
  in_format.assign(static_cast<size_t>(size > 0 ? size : 0), 0);
  const uint32_t out_size = static_cast<uint32_t>(size) > 0x6c ? static_cast<uint32_t>(size) : 0x6c;
  out_format.assign(out_size, 0);
  if (h.avi_read_format(video, in_format.data(), &size) != 0) return h.avi_file_exit();  // "Cannot Read AVI Format"
  length = h.avi_length(video);
  if (h.avi_info(video, &video_info) != 0) return h.avi_file_exit();  // "Cannot Read AVI Stream Info"
  std::copy(in_format.begin(), in_format.begin() + std::min<size_t>(in_format.size(), out_format.size()),
            out_format.begin());
  wr32(out_format, 0, out_size);
  wr16(out_format, 0xe, static_cast<uint16_t>(h.display_bits()));
  wr32(out_format, 0x10, rd16(out_format, 0xe) == 24 ? 0 : 3);  // BI_RGB, else BI_BITFIELDS
  wr32(out_format, 0x20, 0);
  const zvid::ChannelMasks& m = zvid::channel_masks();       // 0x00579280
  wr32(out_format, 0x28, m.r), wr32(out_format, 0x2c, m.g), wr32(out_format, 0x30, m.b);
  wr32(out_format, 0x34, 0);
  const uint32_t ow = rd32(out_format, 4), oh = rd32(out_format, 8), obits = rd16(out_format, 0xe);
  wr32(out_format, 0x14, oh * ((ow + 3) & ~3u) * (obits >> 3));
  read_size = (rd16(in_format, 0xe) >> 3) * rd32(in_format, 4) * rd32(in_format, 8);
  if (video_info.suggested_buffer) read_size = video_info.suggested_buffer;
  decompressor = h.ic_locate(kVidc, video_info.handler, in_format.data(), out_format.data(), 2);
  chunk.assign(read_size, 0);
  row_bytes = (obits >> 3) * ow;
  h.ic_send(decompressor, kDecompressBegin, in_format.data(), out_format.data());
  fps = video_info.scale ? video_info.rate / video_info.scale : 0;
  frame_ms = video_info.rate ? (video_info.rate / 2 + 1000 * video_info.scale) / video_info.rate : 0;
  out_width = ow, out_height = oh;
  const uint32_t image_bytes = rd32(out_format, 0x14);
  pixels.assign(image_bytes / 2 + 1, 0);
  image = zimg::Texture{};
  image.pixel_count = static_cast<int32_t>(image_bytes);
  image.width = static_cast<int16_t>(ow);
  image.height = static_cast<int16_t>(oh);
  image.pixels = pixels.data();
  image.width_again = static_cast<int32_t>(static_cast<int16_t>(ow));
  wr32(out_format, 8, static_cast<uint32_t>(-static_cast<int32_t>(oh)));  // top down
  video_ok = true;
}

// Mech3.exe 1.2 @ 0x0059a900. Streamed: a buffer of two halves (each the
// stream's suggested buffer size), the first read now; else the whole sound.
void Stream::open_audio() {
  Host& h = host();
  audio = 0;
  if (h.avi_open(&audio, path.c_str(), kAuds) != 0) return;
  int32_t size = 0;
  if (h.avi_read_format(audio, nullptr, &size) != 0) return;  // "Cannot Read AVI Sound Format Size"
  wave.assign(static_cast<size_t>(size > 0 ? size : 0), 0);
  if (h.avi_read_format(audio, wave.data(), &size) != 0) return;  // "Cannot Read AVI Sound Format"
  if (h.avi_info(audio, &audio_info) != 0) return;               // "Cannot Read AVI Sound Stream Info"
  const uint32_t block = audio_info.sample_size;
  if (streamed) {
    half = audio_info.suggested_buffer;
    buffer.assign(static_cast<size_t>(half) * 2, 0);
    if (h.avi_read(audio, 0, static_cast<int32_t>(block ? half / block : 0), buffer.data(),
                   static_cast<int32_t>(half)) != 0)
      return;  // "Cannot Read AVI Sound Stream"
    sound = h.sound_stream_create(wave.data(), buffer.data(), half * 2);
    fill_second = true;
    audio_ok = true;
    read = block ? half / block : 0;
    if (std::getenv("MW3_MOVIE_TRACE"))  // ours, for diagnosis
      std::printf("mw3: movie trace: sound buffer 2 x %u bytes, %u Hz, block %u\n", half, rd32(wave, 4), block);
    return;
  }
  half = static_cast<uint32_t>(h.avi_length(audio)) * block;
  buffer.assign(half, 0);
  if (h.avi_read(audio, 0, static_cast<int32_t>(audio_info.length), buffer.data(), static_cast<int32_t>(half)) != 0)
    return;
  sound = h.sound_stream_create(wave.data(), buffer.data(), half);
  audio_ok = true;
}

int32_t Stream::decompress(uint32_t flags) {
  return host().ic_decompress(decompressor, flags, in_format.data(), chunk.data(), out_format.data(),
                              pixels.data());
}

// Mech3.exe 1.2 @ 0x0059aaf0. With sound the frame is the play cursor's
// (0x0059ad70), whatever is asked.
int32_t Stream::frame(int32_t want) {
  Host& h = host();
  if (audio_ok) next = audio_frame();
  else if (want != -1) next = want;
  const int32_t f = next;
  if (f < length) {
    const int32_t got = catch_up_to(f);
    shown = got;
    if (got < f) {
      if (h.avi_read(video, f, 1, chunk.data(), static_cast<int32_t>(read_size)) != 0) return 0;
      const uint32_t flags = h.avi_find_sample(video, f, kFindPrevKey) != f ? kNotKeyFrame : 0;
      if (decompress(flags) != 0) return 0;  // "Cannot Decompress AVI Video Stream"
      shown = f;
    }
  }
  next = f + 1;
  if (next >= length) next = 0;
  return next;
}

// Mech3.exe 1.2 @ 0x0059ac40: one part of the buffer read from the stream.
// Quirk kept: the second piece (a lock across the buffer's end) advances the
// count by the first piece's samples; the halves never wrap, so it never
// shows.
void Stream::refill(uint32_t offset, uint32_t bytes) {
  Host& h = host();
  void* p1 = nullptr;
  void* p2 = nullptr;
  uint32_t n1 = 0, n2 = 0;
  if (!h.sound_stream_lock(sound, offset, bytes, &p1, &n1, &p2, &n2)) return;
  const uint32_t block = audio_info.sample_size;
  if (n1) {
    h.avi_read(audio, static_cast<int32_t>(read), static_cast<int32_t>(block ? n1 / block : 0), p1,
               static_cast<int32_t>(n1));
    read += block ? n1 / block : 0;
  }
  if (n2) {
    h.avi_read(audio, static_cast<int32_t>(read), static_cast<int32_t>(block ? n2 / block : 0), p2,
               static_cast<int32_t>(n2));
    read += block ? n1 / block : 0;
  }
  h.sound_stream_unlock(sound, p1, n1, p2, n2);
}

// Mech3.exe 1.2 @ 0x0059ad70: the first call starts the sound (frame 0). Then
// the half the cursor left is refilled, and the frame is the samples played,
// counted from what was read, x the frame rate / the sample rate. A whole
// (not streamed) sound always answers 0. Unsigned, as the original: with the
// cursor still at 0 after the first refill the frame is huge (the end).
int32_t Stream::audio_frame() {
  Host& h = host();
  if (first) {
    first = false;
    h.sound_play(sound, 1.0f);
    return 0;
  }
  if (!streamed) return 0;
  const uint32_t block = audio_info.sample_size ? audio_info.sample_size : 1;
  const uint32_t half_samples = half / block;
  const uint32_t cursor = h.sound_stream_position(sound);
  const uint32_t played = cursor / block;
  uint32_t base;
  if (fill_second) {
    // FIX (bug): a cursor at exactly 0 has wrapped into the first half, but
    // the exe read it as still in the second and gave a frame a whole buffer
    // back. Video for Windows gives INTRO's sound an exact second a half, so
    // a device's 10 ms steps land on 0 at each wrap: a frame from 2 s before
    // flashed every 2 s (docs/fmv.md section 5). The start (only the first
    // half read) keeps the exe's reading.
    const bool wrapped = cursor == 0 && zfix::options().bugs && read > half_samples;
    if ((cursor > 0 || wrapped) && cursor < half) {
      refill(half, half);
      fill_second = false;
      base = read - 2 * half_samples;
    } else {
      base = read - 3 * half_samples;
    }
  } else if (cursor > half) {
    refill(0, half);
    fill_second = true;
    base = read - 3 * half_samples;
  } else {
    base = read - 2 * half_samples;
  }
  const uint32_t rate = rd32(wave, 4);
  return static_cast<int32_t>(rate ? (base + played) * fps / rate : 0);
}

// Mech3.exe 1.2 @ 0x0059ae50: before frame `want`, with the next key frame
// one ahead (or at it) that key frame is decoded instead (shown a frame early);
// past skipped frames, they are decoded from the key frame before (or the
// last shown), hurried. The frame decoded last.
int32_t Stream::catch_up_to(int32_t want) {
  Host& h = host();
  int32_t last = shown;
  if (!catch_up) return last;
  int32_t key = h.avi_find_sample(video, want + 1, kFindNextKey);
  if (key < 0) key = want;
  if (key - want < 2) {
    if (h.avi_read(video, key, 1, chunk.data(), static_cast<int32_t>(read_size)) != 0) return 0;
    if (decompress(0) != 0) return 0;
    return key;
  }
  if (want <= shown + 1) return last;
  int32_t at = h.avi_find_sample(video, want - 1, kFindPrevKey);
  uint32_t flags = kHurryUp;
  if (at < shown) at = shown, flags = kHurryUp | kNotKeyFrame;
  for (;;) {
    if (h.avi_read(video, at, 1, chunk.data(), static_cast<int32_t>(read_size)) != 0) return 0;
    if (decompress(flags) != 0) return 0;
    last = at;
    if (++at >= want) break;
  }
  return last;
}

}  // namespace zfmv
