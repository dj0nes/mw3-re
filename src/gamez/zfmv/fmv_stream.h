#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gamez/zgame/unported.h"
#include "gamez/zimage/zimg_library.h"

// C:\zipper\GameZMech3\zFMV\fmv_stream.cpp (0x0059a4a0..0x0059b040): the
// PLAYAVI movie player (docs/fmv.md, The movie player). The picture through
// Video for Windows (AVIStreamRead and the Indeo 5 codec, ICDecompress) into
// a 16-bit image that zFMV stretches over the screen; the sound into a
// zSound streaming buffer, two halves refilled as it plays, whose play cursor
// says which frame to show. FIX (A): the Video for Windows calls go to the
// zfmv::Host, which in the program reads the importer's files (pl_mpeg for
// the picture, the AVI's own PCM for the sound).
namespace zfmv {

using zgame::Handle;

// AVIStreamInfoA's AVISTREAMINFOA (0x8c bytes).
struct AviStreamInfo {
  uint32_t type = 0, handler = 0, flags = 0, caps = 0;  // +0x00
  uint16_t priority = 0, language = 0;                  // +0x10
  uint32_t scale = 0, rate = 0, start = 0, length = 0;  // +0x14
  uint32_t initial_frames = 0, suggested_buffer = 0;    // +0x24
  uint32_t quality = 0, sample_size = 0;                // +0x2c
  int32_t frame[4] = {};                                // +0x34
  uint32_t edit_count = 0, format_change_count = 0;     // +0x44
  char name[64] = {};                                   // +0x4c
};
static_assert(sizeof(AviStreamInfo) == 0x8c, "AVISTREAMINFOA");

// The stream (new(0x1e4)). It starts with an image header the 2D blits take
// (+0x00..+0x37), here the zimg::Texture `image`.
class Stream {
 public:
  Stream(const char* path, bool streamed);  // 0x0059a4a0
  ~Stream();                                // 0x0059a530
  // 0x0059aaf0: shows the frame for `frame` (or, with sound, for the play
  // cursor); the next frame's number, 0 once past the last.
  int32_t frame(int32_t frame);
  // Ours (the movies' judder, fmv.md section 5): the frame the playing
  // sound has reached, refilling as the cursor moves; -1 when no streamed
  // sound drives the picture or it is not started.
  int32_t sound_frame() { return audio_ok && streamed && !first ? audio_frame() : -1; }

  zimg::Texture image{};          // +0x00  the decoded picture
  std::vector<uint16_t> pixels;   // +0x10  its 16-bit pixels
  std::string path;               // +0x38
  bool video_ok = false;          // +0x3c
  Handle video = 0;               // +0x40  PAVISTREAM
  std::vector<uint8_t> in_format;   // +0x44  the stream's BITMAPINFOHEADER
  std::vector<uint8_t> out_format;  // +0x48  the decompressor's output, at least 0x6c
  int32_t length = 0;             // +0x4c  frames
  AviStreamInfo video_info;       // +0x50
  uint32_t read_size = 0;         // +0xdc
  Handle decompressor = 0;        // +0xe0  HIC
  std::vector<uint8_t> chunk;     // +0xe4  the frame read
  uint32_t row_bytes = 0;         // +0xe8
  uint32_t fps = 0;               // +0xec  rate / scale
  uint32_t frame_ms = 0;          // +0xf0
  uint32_t out_width = 0, out_height = 0;  // +0xfc, +0x100
  int32_t next = 0;               // +0x104
  int32_t shown = -1;             // +0x108  the frame last decoded
  bool catch_up = true;           // +0x134  skipped frames decoded from a key frame
  bool audio_ok = false;          // +0x135
  Handle audio = 0;               // +0x138
  AviStreamInfo audio_info;       // +0x13c
  std::vector<uint8_t> wave;      // +0x1c8  WAVEFORMAT
  uint32_t half = 0;              // +0x1cc  bytes in each half of the buffer
  std::vector<uint8_t> buffer;    // +0x1d0
  Handle sound = 0;               // +0x1d4  the zSound stream
  bool first = true;              // +0x1d8  the sound not yet played
  uint32_t read = 0;              // +0x1dc  samples read into the buffer
  bool fill_second = false;       // +0x1e0  the second half is the next to fill
  bool streamed;                  // +0x1e1  the sound refilled as it plays (PLAYAVI's flag)

 private:
  void open_video();               // 0x0059a650
  void open_audio();               // 0x0059a900
  void refill(uint32_t offset, uint32_t bytes);  // 0x0059ac40
  int32_t audio_frame();           // 0x0059ad70
  int32_t catch_up_to(int32_t frame);  // 0x0059ae50
  int32_t decompress(uint32_t flags);  // ICDecompress into the image; 0 when it worked
};

}  // namespace zfmv
