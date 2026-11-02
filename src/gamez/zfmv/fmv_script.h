#pragma once
#include <cstdint>
#include <string>

#include "gamez/zgame/unported.h"
#include "gamez/zfmv/fmv_stream.h"

namespace zrdr { struct Node; }

// C:\zipper\GameZMech3\zFMV\fmv_script.cpp: the movie scripts of fmv.zrd
// (docs/fmv.md). A script is a named list of actions run one after another:
// images shown or loaded, waits, fades, blurs, sounds and the AVI movies.
// PLAYAVI is a stub until the movie player (fmv_stream.cpp) is ported: it ends
// at once, as the original's does when the file is not found.
namespace zfmv {

using zgame::Handle;

struct Rect {                      // a Win32 RECT
  int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// What zFMV calls outside itself, one virtual per original function (its
// address the key, as zgame::Unported); a rect argument reports as 0 when
// null, else 1 and its four words.
class Host : public zgame::Unported {
 public:
  // zVideo's buffers (zvid_buff.c).
  virtual Handle target_base();     // 0x00576d80  the 2D target's pixels
  virtual Handle target_width();    // 0x00576d90
  virtual Handle target_height();   // 0x00576da0
  virtual Handle target_pitch();    // 0x00576db0
  virtual Handle render_base();     // 0x00576ca0  the software renderer's buffer
  virtual Handle render_pitch();    // 0x00576cd0
  virtual Handle screen_width();    // 0x00576cb0
  virtual Handle screen_height();   // 0x00576cc0
  virtual int32_t video_mode();     // [0x0072f348]  0 software, else Direct3D (a read, not a call)
  virtual void video_lock();        // 0x00576de0
  virtual void video_unlock();      // 0x00576e70
  virtual void lock_3d();           // 0x00576d00
  virtual void unlock_3d();         // 0x00576d60
  virtual void video_flip(Handle a, Handle b, int32_t c, int32_t d);  // 0x00576ea0
  virtual Handle video_grab(int32_t buffer);                         // 0x00579500: a new image
  virtual void copy_to_second(const Rect* src, const Rect* dst);     // [0x0072f358]
  virtual void copy_from_second(const Rect* src, const Rect* dst);   // [0x0072f35c]
  // The whole image stretched over `dst` ([0x0072f360] = 0x005773e0).
  virtual void image_stretch(Handle image, int32_t key, const Rect* src, const Rect* dst);
  virtual void scene_begin();       // 0x00579af0
  virtual void draw_tint_quad();    // [0x0072f3ec]
  virtual void scene_end();         // 0x00579b10
  // zRender.
  virtual void set_finish_target(Handle base, Handle width, Handle height, Handle pitch);  // 0x00558290
  virtual void render_target(int32_t* w, int32_t* h, int32_t* depth, int32_t* pitch);   // 0x00558080
  virtual void image_draw(Handle image, int32_t x, int32_t y, int32_t key, const Rect* src);  // 0x0055b1f0
  virtual void tint(uint16_t colour, const Rect* rect, double alpha);  // 0x00558540
  virtual void finish();                                               // 0x00558610
  virtual void blur(const Rect* rect, int32_t kind);                   // 0x0055a540
  // zImage.
  virtual Handle image_get(const char* name);   // 0x005179d0
  virtual void image_release(Handle image);     // 0x00517b10
  virtual void image_path(const char* path);    // 0x00516f50
  // zSound.
  virtual void sound_load_set(const char* name);       // 0x0056f520
  virtual Handle sound_snapshot();                     // 0x0056eb10
  virtual void sound_stop_snapshot(Handle snapshot);   // 0x0056f0a0
  virtual Handle sound_find(const char* name);         // 0x0056f6f0
  virtual Handle sound_play(Handle sound, float volume);  // 0x0056e3d0: the voice
  virtual void sound_stop(Handle voice);               // 0x0056e9f0
  // zInput.
  virtual void input_clear();                   // 0x0051a300
  virtual uint32_t key_state(int32_t dik);      // 0x0051a9b0
  // The C runtime and Win32.
  virtual int32_t file_stat(const char* path);  // _stat: 0 found, -1 not
  virtual double now();                         // timeGetTime() x 0.001f, seconds

  // Video for Windows (AVIFIL32, MSVFW32), the movie player's (fmv_stream.h).
  // Results are HRESULTs (0 succeeded) unless named otherwise.
  virtual void avi_file_init();                                      // AVIFileInit
  virtual void avi_file_exit();                                      // AVIFileExit
  virtual int32_t avi_open(Handle* stream, const char* path, uint32_t type);  // AVIStreamOpenFromFileA (OF_READ)
  // AVIStreamReadFormat at position 0: with no buffer only *size, else up to *size bytes.
  virtual int32_t avi_read_format(Handle stream, void* format, int32_t* size);
  virtual int32_t avi_length(Handle stream);                         // AVIStreamLength
  virtual int32_t avi_info(Handle stream, AviStreamInfo* info);      // AVIStreamInfoA (0x8c)
  virtual int32_t avi_read(Handle stream, int32_t start, int32_t samples, void* buffer, int32_t bytes);  // AVIStreamRead
  virtual int32_t avi_find_sample(Handle stream, int32_t pos, uint32_t flags);  // AVIStreamFindSample: the sample, -1
  virtual void avi_release(Handle stream);                           // AVIStreamRelease
  virtual Handle ic_locate(uint32_t type, uint32_t handler, const void* in, const void* out, uint32_t mode);
  virtual int32_t ic_send(Handle hic, uint32_t message, const void* a, const void* b);  // ICSendMessage
  // ICDecompress: the chunk read last into `image` (the 16-bit picture), 0 (ICERR_OK) when it worked.
  virtual int32_t ic_decompress(Handle hic, uint32_t flags, const void* in, const void* chunk, const void* out,
                                void* image);
  virtual void ic_close(Handle hic);                                 // ICClose
  virtual uint32_t display_bits();                                   // 0x00576c70: the 2D surfaces' bits per pixel
  // The stream's picture as the image handle [0x0072f360] takes.
  virtual Handle stream_image(class Stream* stream);
  // zSound's streaming buffer (zsnd_create.cpp).
  virtual Handle sound_stream_create(const void* format, const void* data, uint32_t bytes);  // 0x005758c0
  virtual int32_t sound_stream_lock(Handle sound, uint32_t offset, uint32_t bytes, void** p1, uint32_t* n1,
                                    void** p2, uint32_t* n2);       // 0x00575550: 1 when locked
  virtual void sound_stream_unlock(Handle sound, void* p1, uint32_t n1, void* p2, uint32_t n2);  // 0x00575600
  virtual uint32_t sound_stream_position(Handle sound);              // 0x00575690: the play cursor, bytes
  virtual void sound_stream_free(Handle sound);                      // 0x00575980
};
Host& host();
void set_host(Host* h);  // null: the default (every call unported)

class Script;

// An action: the base class's vtable 0x005c6130. The script calls start when
// the action becomes current, step each poll until it returns false, then end.
class Action {
 public:
  virtual ~Action() = default;
  virtual bool step(double t) { (void)t; return false; }  // 0x00493110
  virtual void start(double t) { (void)t; }               // 0x0040baf0
  virtual void end() {}                                   // 0x0049bfd0
  Action* next = nullptr;                                 // +0x04
};

// SHOWIMAGE, BLITIMAGE, LOADIMAGE (vtable 0x005cf850): a named image drawn,
// at x, y or over the whole target.
class ImageAction : public Action {
 public:
  ImageAction(const char* name, bool flip, int32_t x, int32_t y);  // 0x00599760
  ImageAction(const char* name, bool flip);                        // 0x00599820
  ~ImageAction() override;                                         // 0x005998d0
  bool step(double t) override;                                    // 0x00599950
  void start(double t) override;                                   // 0x00599930
  void end() override;                                             // 0x005999d0
  std::string name;               // +0x08
  Handle image = 0;               // +0x0c
  bool flip;                      // +0x10  shown at once (SHOWIMAGE, BLITIMAGE)
  bool placed;                    // +0x11  drawn at rect's corner, else stretched over it
  Rect rect;                      // +0x14
};

// WAIT (vtable 0x005cf820).
class WaitAction : public Action {
 public:
  explicit WaitAction(float seconds) : seconds(seconds) {}
  bool step(double t) override;   // 0x0059a470
  void start(double t) override;  // 0x0059a460
  void end() override;            // 0x0059a490
  float seconds;                  // +0x08
  float started = 0;              // +0x0c
};

// FADEIN, FADEOUT (vtable 0x005cf868): the screen as it was at the start,
// under a colour going from `alpha` to none (in) or none to `alpha` (out).
class FadeAction : public Action {
 public:
  FadeAction(uint8_t r, uint8_t g, uint8_t b, float seconds, int32_t out, float alpha);  // 0x005999f0
  bool step(double t) override;   // 0x00599ab0
  void start(double t) override;  // 0x00599a40
  void end() override;            // 0x00599be0
  int32_t out;                    // +0x08  0 in, 1 out
  uint16_t colour;                // +0x0c
  float seconds;                  // +0x10
  double started = 0;             // +0x18
  Handle image = 0;               // +0x20  the still
  float alpha;                    // +0x24
};

// PLAYAVI (vtable 0x005cf880): the movie (fmv_stream.h) stretched over the
// screen, frame by frame.
class AviAction : public Action {
 public:
  AviAction(const char* dir, const char* name, bool flag);  // 0x00599c00
  ~AviAction() override;          // 0x00599d80
  bool step(double t) override;   // 0x00599de0
  void start(double t) override;  // 0x00599ea0
  void end() override;            // 0x00599f40
  std::string path;               // +0x08  "<FMV_PATH>\<name>", empty when not found
  bool flag;                      // +0x0c  the sound streamed
  Stream* stream = nullptr;       // +0x10
  double started = 0;             // +0x18  the first step's time
  int32_t frame = -1;             // +0x20  the frame last asked for
  Rect rect;                      // +0x24  0, 0, the render target's size
  int32_t sound_seen = -1;        // ours: the sound's frame last shown (the judder's fix)
  double trace_t0 = 0, trace_last = 0;  // ours: MW3_MOVIE_TRACE's times
  int32_t trace_skipped = 0;
};

// PLAYMCI (vtable 0x005cf8b0): a movie through MCI. No script uses it; it ends
// at once, as PLAYAVI does for now.
class MciAction : public Action {
 public:
  MciAction(uint32_t hwnd, const char* dir, const char* name);  // 0x0059a220
  std::string path;               // +0x08
  uint32_t hwnd;
};

// BLUR, BLURH, BLURV (vtables 0x005cf898, 0x005cf808, 0x005cf7f0): `passes`
// blurs a step, `steps` steps.
class BlurAction : public Action {
 public:
  BlurAction(int32_t steps, int32_t passes, int32_t kind);  // 0x00599f70; kind 3, 1, 2
  bool step(double t) override;   // 0x0059a070, 0x0059a100, 0x0059a190
  void start(double t) override;  // 0x00599f90
  void end() override;            // 0x0059a040
  void run();                     // 0x00599a70: start, step until done, end (the pause screen's)
  int32_t steps;                  // +0x08
  int32_t passes;                 // +0x0c
  int32_t kind;                   // which pass (0x0055a540's EDX): 3 both, 1 along, 2 down
  Rect screen;                    // +0x10  0, 0, the screen's size
  Rect target;                    // +0x20  0, 0, the target's size
};

// PLAYSOUND (vtable 0x005cf7d8).
class SoundAction : public Action {
 public:
  explicit SoundAction(const char* name);
  void start(double t) override;  // 0x00599d10
  Handle sound = 0;               // +0x08
  Handle voice = 0;               // +0x0c
  std::string name;               // +0x10  strncpy'd, 0x32 bytes
};

// The script (new(0x20)).
class Script {
 public:
  Script(const char* file, const char* name, uint32_t hwnd);  // 0x00598cc0
  ~Script();                                                  // 0x00598d00
  // 0x00598d30: every action deleted, or (`all` false) back to the first.
  void clear(bool all);
  // 0x00598d80: the named block of the file's actions; -1 without the file,
  // else the number of entries read (0 when one is not a list).
  int32_t load(const char* file, const char* name);
  int32_t load_tree(zrdr::Node* root, const char* name);  // its body, on a loaded tree
  bool append(Action* a);        // 0x00599570
  bool play_at(double t);        // 0x005995b0
  bool playing_at(double t);     // 0x00599620
  bool play() { return play_at(host().now()); }        // 0x005996d0
  bool playing() { return playing_at(host().now()); }  // 0x00599710
  void stop(bool all) { clear(all); }                  // 0x00599750

  std::string path;              // +0x00  FMV_PATH
  uint32_t hwnd;                 // +0x04
  double started = 0;            // +0x08
  Action* head = nullptr;        // +0x10
  Action* tail = nullptr;        // +0x14
  Action* current = nullptr;     // +0x18
};

}  // namespace zfmv
