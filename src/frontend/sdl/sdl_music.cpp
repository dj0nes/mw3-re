#include "frontend/sdl/sdl_music.h"

#include <cstdio>
#include <cstring>

#include "frontend/sdl/sdl_platform.h"
#include "gamez/zutil/zutl_path.h"

#ifdef MW3_HAVE_SDL_SOUND
#include <SDL3_sound/SDL_sound.h>
#endif

namespace sdlfe {

namespace {
void post_end() {
  SDL_Event e{};
  e.type = SdlPump::music_event();
  SDL_PushEvent(&e);
}
std::string ext_of(const std::string& path) {
  const size_t dot = path.rfind('.');
  std::string e = dot == std::string::npos ? "" : path.substr(dot + 1);
  for (char& c : e) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
  return e;
}
}  // namespace

SdlMusicOutput::SdlMusicOutput() {
#ifdef MW3_HAVE_SDL_SOUND
  if (!Sound_Init()) std::fprintf(stderr, "mw3: SDL_sound: %s\n", Sound_GetError());
#endif
}

SdlMusicOutput::~SdlMusicOutput() {
  close();
#ifdef MW3_HAVE_SDL_SOUND
  Sound_Quit();
#endif
}

bool SdlMusicOutput::decodes(const std::string& ext) {
#ifdef MW3_HAVE_SDL_SOUND
  return ext == "ogg" || ext == "flac" || ext == "wav" || ext == "mp3";
#else
  return ext == "wav";
#endif
}

void SdlMusicOutput::close() {
  if (stream_) SDL_DestroyAudioStream(stream_);  // waits out a feed in progress
  stream_ = nullptr;
#ifdef MW3_HAVE_SDL_SOUND
  if (sample_) Sound_FreeSample(static_cast<Sound_Sample*>(sample_));
#endif
  sample_ = nullptr;
  wav_.clear();
  wav_at_ = 0;
  paused_ = false;
}

// Feeds the device from the decoder; at the end, once, the pump hears of it.
void SDLCALL SdlMusicOutput::feed(void* self, SDL_AudioStream* stream, int additional, int) {
  auto* m = static_cast<SdlMusicOutput*>(self);
  if (m->ended_) return;
  if (m->drained_) {  // all decoded: the end once the device has played it
    if (SDL_GetAudioStreamQueued(stream) <= 0) {
      m->ended_ = true;
      post_end();
    }
    return;
  }
  while (additional > 0) {
#ifdef MW3_HAVE_SDL_SOUND
    if (m->sample_) {
      auto* s = static_cast<Sound_Sample*>(m->sample_);
      const Uint32 n = Sound_Decode(s);
      if (n > 0) SDL_PutAudioStreamData(stream, s->buffer, static_cast<int>(n));
      additional -= static_cast<int>(n);
      if (n == 0 || (s->flags & (SOUND_SAMPLEFLAG_EOF | SOUND_SAMPLEFLAG_ERROR))) {
        m->drained_ = true;
        return;
      }
      continue;
    }
#endif
    const size_t left = m->wav_.size() - m->wav_at_;
    if (left == 0) {
      m->drained_ = true;
      return;
    }
    const size_t n = left < static_cast<size_t>(additional) ? left : static_cast<size_t>(additional);
    SDL_PutAudioStreamData(stream, m->wav_.data() + m->wav_at_, static_cast<int>(n));
    m->wav_at_ += n;
    additional -= static_cast<int>(n);
  }
}

bool SdlMusicOutput::play(const std::string& path) {
  close();
  const std::string host = zutl::find(path.c_str());
  if (host.empty() || !decodes(ext_of(host))) {
    std::fprintf(stderr, "mw3: music: cannot play %s\n", path.c_str());
    return false;
  }
  SDL_AudioSpec spec{};
#ifdef MW3_HAVE_SDL_SOUND
  Sound_Sample* s = Sound_NewSampleFromFile(host.c_str(), nullptr, 64 * 1024);
  if (!s) {
    std::fprintf(stderr, "mw3: music: %s: %s\n", host.c_str(), Sound_GetError());
    return false;
  }
  sample_ = s;
  spec = s->actual;
#else
  Uint8* buf = nullptr;
  Uint32 len = 0;
  if (!SDL_LoadWAV(host.c_str(), &spec, &buf, &len)) {
    std::fprintf(stderr, "mw3: music: %s: %s\n", host.c_str(), SDL_GetError());
    return false;
  }
  wav_.assign(buf, buf + len);
  SDL_free(buf);
#endif
  ended_ = false, drained_ = false;
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, this);
  if (!stream_) {
    std::fprintf(stderr, "mw3: music: no audio device: %s\n", SDL_GetError());
    close();
    return false;
  }
  SDL_SetAudioStreamGain(stream_, gain_);
  if (!device_paused_) SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void SdlMusicOutput::stop() { close(); }

void SdlMusicOutput::pause() {
  if (stream_) SDL_PauseAudioStreamDevice(stream_);
  paused_ = true;
}
void SdlMusicOutput::resume() {
  if (stream_ && !device_paused_) SDL_ResumeAudioStreamDevice(stream_);
  paused_ = false;
}
void SdlMusicOutput::pause_device(bool on) {
  device_paused_ = on;
  if (!stream_) return;
  if (on) SDL_PauseAudioStreamDevice(stream_);
  else if (!paused_) SDL_ResumeAudioStreamDevice(stream_);
}

// auxSetVolume's two channels, 0..0xffff each; the stream has one gain.
void SdlMusicOutput::set_volume(uint16_t left, uint16_t right) {
  gain_ = (static_cast<float>(left) + static_cast<float>(right)) / (2.0f * 65535.0f);
  if (stream_) SDL_SetAudioStreamGain(stream_, gain_);
}

}  // namespace sdlfe
