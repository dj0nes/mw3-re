#pragma once
#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>

#include "platform/sound_output.h"

// platform::SoundOutput on SDL3's audio (docs/sound.md section 7): a small
// software mixer standing in for DirectSound's. Buffers hold the WAV bytes as
// the game copied them in (8- or 16-bit PCM, mono or stereo), are resampled
// to the device rate at their frequency and mixed with DirectSound's volume
// (hundredths of a dB) and pan law. Controls a buffer was not created with
// fail as in DirectSound. FIX (A): the primary buffer's format is kept but the
// device mixes stereo at its own rate, as DirectSound does on Windows since
// Vista (the game asks for mono, docs/sound.md, playback format).
namespace sdlfe {

class SdlSoundOutput : public platform::SoundOutput {
 public:
  SdlSoundOutput() = default;
  ~SdlSoundOutput() override;

  int32_t create() override;
  void release_device() override;
  int32_t set_cooperative_level(uint32_t window, uint32_t level) override;
  int32_t get_caps(uint32_t* flags) override;
  int32_t create_buffer(const platform::BufferDesc& desc, platform::SoundBuffer* out) override;
  int32_t duplicate_buffer(platform::SoundBuffer original, platform::SoundBuffer* out) override;
  void release(platform::SoundBuffer b) override;
  int32_t get_format(platform::SoundBuffer b, platform::WaveFormat* out) override;
  int32_t set_format(platform::SoundBuffer b, const platform::WaveFormat& format) override;
  int32_t get_status(platform::SoundBuffer b, uint32_t* status) override;
  int32_t restore(platform::SoundBuffer b) override;
  int32_t lock(platform::SoundBuffer b, uint32_t offset, uint32_t bytes, void** p1, uint32_t* n1, void** p2,
               uint32_t* n2) override;
  int32_t unlock(platform::SoundBuffer b, void* p1, uint32_t n1, void* p2, uint32_t n2) override;
  int32_t play(platform::SoundBuffer b, uint32_t flags) override;
  int32_t stop(platform::SoundBuffer b) override;
  int32_t set_current_position(platform::SoundBuffer b, uint32_t position) override;
  int32_t get_current_position(platform::SoundBuffer b, uint32_t* play, uint32_t* write) override;
  int32_t get_volume(platform::SoundBuffer b, int32_t* volume) override;
  int32_t set_volume(platform::SoundBuffer b, int32_t volume) override;
  int32_t set_pan(platform::SoundBuffer b, int32_t pan) override;
  int32_t get_frequency(platform::SoundBuffer b, uint32_t* hz) override;
  int32_t set_frequency(platform::SoundBuffer b, uint32_t hz) override;

  int playing_count();  // buffers sounding now (for tools)
  // The window has the focus. Without it a buffer made without
  // DSBCAPS_GLOBALFOCUS (0x8000; the game asks it of none) plays on silent, as
  // DirectSound's did (FIX (A): the pump tells it).
  void set_focused(bool on) { focused_ = on; }
  // Ours (tvOS): the device itself paused while the app is in the background.
  void pause_device(bool on);
  bool device_paused() const;
  // The device's callback mixes through this (the tests call it): every
  // playing buffer into `frames` stereo floats, added to what is there.
  void mix(float* out, int frames);

 private:
  struct Buffer {
    uint32_t flags = 0;
    std::shared_ptr<std::vector<uint8_t>> data;  // shared by duplicates
    platform::WaveFormat format;
    bool playing = false, looping = false;
    double frame = 0.0;  // play position in source frames
    int32_t volume = 0, pan = 0;
    uint32_t freq = 0;
  };
  static void SDLCALL callback(void* self, SDL_AudioStream* stream, int additional, int total);
  Buffer* get(platform::SoundBuffer b);

  std::mutex mutex_;
  std::map<platform::SoundBuffer, Buffer> buffers_;
  platform::SoundBuffer next_ = 1;
  SDL_AudioStream* stream_ = nullptr;
  int rate_ = 44100;
  std::vector<float> scratch_;
  std::atomic<bool> focused_{true};
};

}  // namespace sdlfe
