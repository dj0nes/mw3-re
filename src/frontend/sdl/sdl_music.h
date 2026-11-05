#pragma once
#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "platform/platform.h"

// platform::MusicOutput on SDL3 audio (CLAUDE.md, Music): the CD drive behind
// MCI "cdaudio" (zsnd_cd). A track file (music/trackNN.ogg, .flac, .wav or
// .mp3, found through the mounts) is decoded as it plays by SDL3_sound when
// the build has it, else only .wav (SDL's own loader); its end is posted to the
// message pump as MCI's MM_MCINOTIFY was. FIX (A): there is no CD drive.
namespace sdlfe {

class SdlMusicOutput : public platform::MusicOutput {
 public:
  SdlMusicOutput();
  ~SdlMusicOutput() override;
  bool play(const std::string& path) override;
  void stop() override;
  void pause() override;
  void resume() override;
  void set_volume(uint16_t left, uint16_t right) override;

  // Ours (tvOS): the device paused while the app is in the background,
  // apart from the game's own pause (MCI's).
  void pause_device(bool on);

  static bool decodes(const std::string& ext);  // what this build can play
  bool playing() const { return stream_ != nullptr && !paused_; }

 private:
  static void SDLCALL feed(void* self, SDL_AudioStream* stream, int additional, int total);
  void close();
  SDL_AudioStream* stream_ = nullptr;
  void* sample_ = nullptr;           // Sound_Sample*, when SDL3_sound decodes
  std::vector<uint8_t> wav_;         // a whole .wav otherwise
  size_t wav_at_ = 0;
  std::atomic<bool> ended_{false}, drained_{false};
  bool paused_ = false;
  bool device_paused_ = false;
  float gain_ = 1.0f;
};

}  // namespace sdlfe
