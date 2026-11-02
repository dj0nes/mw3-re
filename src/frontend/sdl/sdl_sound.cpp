#include "frontend/sdl/sdl_sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sdlfe {

namespace {
using SO = platform::SoundOutput;
float gain(int32_t hundredths_db) { return hundredths_db <= -10000 ? 0.0f : std::pow(10.0f, hundredths_db / 2000.0f); }
}  // namespace

SdlSoundOutput::~SdlSoundOutput() { release_device(); }

SdlSoundOutput::Buffer* SdlSoundOutput::get(platform::SoundBuffer b) {
  auto it = buffers_.find(b);
  return it == buffers_.end() ? nullptr : &it->second;
}

int32_t SdlSoundOutput::create() {
  if (stream_) return 0;
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return static_cast<int32_t>(0x88780078);
  SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate_};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
  if (!stream_) return static_cast<int32_t>(0x88780078);  // DSERR_NODRIVER
  SDL_ResumeAudioStreamDevice(stream_);
  if (SDL_getenv("MW3_MOVIE_TRACE")) {  // ours, for diagnosis: the device's period
    SDL_AudioSpec dev{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &dev, &frames);
    std::printf("mw3: movie trace: sound device %d Hz, %d channels, %d sample frames a period; mixing at %d Hz\n", dev.freq,
                dev.channels, frames, rate_);
  }
  return 0;
}

void SdlSoundOutput::release_device() {
  if (stream_) SDL_DestroyAudioStream(stream_);  // stops the callback first
  stream_ = nullptr;
  std::lock_guard<std::mutex> l(mutex_);
  buffers_.clear();
}

int32_t SdlSoundOutput::set_cooperative_level(uint32_t, uint32_t) { return 0; }

int32_t SdlSoundOutput::get_caps(uint32_t* flags) {
  *flags = kCapsPrimary16Bit;
  return 0;
}

int32_t SdlSoundOutput::create_buffer(const platform::BufferDesc& d, platform::SoundBuffer* out) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer b;
  b.flags = d.flags;
  if (d.flags & kPrimary) {
    b.format = platform::WaveFormat{1, 2, static_cast<uint32_t>(rate_), static_cast<uint32_t>(rate_) * 4, 4, 16};
  } else {
    if (!d.format || d.format->format_tag != 1 || (d.format->bits_per_sample != 8 && d.format->bits_per_sample != 16) ||
        d.format->channels < 1 || d.format->channels > 2 || d.format->samples_per_sec == 0)
      return kInvalidParam;
    b.format = *d.format;
    b.data = std::make_shared<std::vector<uint8_t>>(d.bytes, d.format->bits_per_sample == 8 ? 0x80 : 0);
  }
  b.freq = b.format.samples_per_sec;
  *out = next_++;
  buffers_[*out] = b;
  return 0;
}

int32_t SdlSoundOutput::duplicate_buffer(platform::SoundBuffer original, platform::SoundBuffer* out) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* o = get(original);
  if (!o || !o->data) return kInvalidParam;
  Buffer b = *o;
  b.playing = false, b.frame = 0.0;
  *out = next_++;
  buffers_[*out] = b;
  return 0;
}

void SdlSoundOutput::release(platform::SoundBuffer b) {
  std::lock_guard<std::mutex> l(mutex_);
  buffers_.erase(b);
}

int32_t SdlSoundOutput::get_format(platform::SoundBuffer b, platform::WaveFormat* out) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  *out = x->format;
  return 0;
}

int32_t SdlSoundOutput::set_format(platform::SoundBuffer b, const platform::WaveFormat& f) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x || !(x->flags & kPrimary)) return kInvalidParam;
  x->format = f;  // kept for get_format; the device mixes as it is (header)
  return 0;
}

int32_t SdlSoundOutput::get_status(platform::SoundBuffer b, uint32_t* status) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  *status = (x->playing ? kStatusPlaying : 0u) | (x->playing && x->looping ? kStatusLooping : 0u);
  return 0;
}

int32_t SdlSoundOutput::restore(platform::SoundBuffer) { return 0; }

int32_t SdlSoundOutput::lock(platform::SoundBuffer b, uint32_t offset, uint32_t bytes, void** p1, uint32_t* n1,
                             void** p2, uint32_t* n2) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x || !x->data) return kInvalidParam;
  const uint32_t size = static_cast<uint32_t>(x->data->size());
  if (offset > size || bytes > size) return kInvalidParam;
  const uint32_t first = std::min(bytes, size - offset);
  *p1 = x->data->data() + offset, *n1 = first;
  *p2 = bytes > first ? x->data->data() : nullptr, *n2 = bytes - first;
  return 0;
}

int32_t SdlSoundOutput::unlock(platform::SoundBuffer, void*, uint32_t, void*, uint32_t) { return 0; }

int32_t SdlSoundOutput::play(platform::SoundBuffer b, uint32_t flags) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  x->playing = true, x->looping = flags & kPlayLooping;
  return 0;
}

int32_t SdlSoundOutput::stop(platform::SoundBuffer b) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  x->playing = false;
  return 0;
}

int32_t SdlSoundOutput::set_current_position(platform::SoundBuffer b, uint32_t position) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x || !x->data) return kInvalidParam;
  x->frame = static_cast<double>(position / std::max<uint32_t>(x->format.block_align, 1));
  return 0;
}

int32_t SdlSoundOutput::get_current_position(platform::SoundBuffer b, uint32_t* play, uint32_t* write) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  const uint32_t p = static_cast<uint32_t>(x->frame) * std::max<uint32_t>(x->format.block_align, 1);
  if (play) *play = p;
  if (write) *write = p;
  return 0;
}

int32_t SdlSoundOutput::get_volume(platform::SoundBuffer b, int32_t* volume) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  if (!(x->flags & kCtrlVolume)) return kControlUnavailable;
  *volume = x->volume;
  return 0;
}

int32_t SdlSoundOutput::set_volume(platform::SoundBuffer b, int32_t volume) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  if (!(x->flags & kCtrlVolume)) return kControlUnavailable;
  if (volume > 0 || volume < -10000) return kInvalidParam;
  x->volume = volume;
  return 0;
}

int32_t SdlSoundOutput::set_pan(platform::SoundBuffer b, int32_t pan) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  if (!(x->flags & kCtrlPan)) return kControlUnavailable;
  if (pan > 10000 || pan < -10000) return kInvalidParam;
  x->pan = pan;
  return 0;
}

int32_t SdlSoundOutput::get_frequency(platform::SoundBuffer b, uint32_t* hz) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  if (!(x->flags & kCtrlFrequency)) return kControlUnavailable;
  *hz = x->freq;
  return 0;
}

int32_t SdlSoundOutput::set_frequency(platform::SoundBuffer b, uint32_t hz) {
  std::lock_guard<std::mutex> l(mutex_);
  Buffer* x = get(b);
  if (!x) return kInvalidParam;
  if (!(x->flags & kCtrlFrequency)) return kControlUnavailable;
  if (hz != 0 && (hz < 100 || hz > 100000)) return kInvalidParam;  // DSBFREQUENCY_MIN/MAX; 0 = original
  x->freq = hz ? hz : x->format.samples_per_sec;
  return 0;
}

int SdlSoundOutput::playing_count() {
  std::lock_guard<std::mutex> l(mutex_);
  int n = 0;
  for (auto& [k, b] : buffers_) n += b.playing && !(b.flags & kPrimary);
  return n;
}

void SDLCALL SdlSoundOutput::callback(void* self, SDL_AudioStream* stream, int additional, int) {
  auto* o = static_cast<SdlSoundOutput*>(self);
  const int frames = additional / static_cast<int>(sizeof(float) * 2);
  if (frames <= 0) return;
  static const bool trace = SDL_getenv("MW3_MOVIE_TRACE") != nullptr;
  static int traced = 0;
  if (trace && traced < 60) {  // ours: the first callbacks' sizes and spacing
    static Uint64 last = 0;
    const Uint64 now = SDL_GetTicksNS();
    std::printf("mw3: movie trace: sound callback %d frames (%.1f ms of sound), %.1f ms after the last\n", frames,
                1000.0 * frames / o->rate_, last ? (now - last) / 1e6 : 0.0);
    last = now, ++traced;
  }
  o->scratch_.assign(static_cast<size_t>(frames) * 2, 0.0f);
  o->mix(o->scratch_.data(), frames);
  SDL_PutAudioStreamData(stream, o->scratch_.data(), frames * static_cast<int>(sizeof(float) * 2));
}

// Every playing buffer resampled (nearest frame) into the stereo output, with
// DirectSound's pan law: the far side loses |pan| hundredths of a dB.
void SdlSoundOutput::mix(float* out, int frames) {
  std::lock_guard<std::mutex> l(mutex_);
  for (auto& [handle, b] : buffers_) {
    if (!b.playing || !b.data || (b.flags & kPrimary)) continue;
    const platform::WaveFormat& f = b.format;
    const size_t align = std::max<uint16_t>(f.block_align, 1);
    const size_t count = b.data->size() / align;
    if (count == 0) {
      b.playing = false;
      continue;
    }
    const bool heard = focused_ || (b.flags & 0x8000);  // DSBCAPS_GLOBALFOCUS
    const float g = heard ? gain(b.volume) : 0.0f;
    const float gl = g * (b.pan > 0 ? gain(-b.pan) : 1.0f), gr = g * (b.pan < 0 ? gain(b.pan) : 1.0f);
    const double step = static_cast<double>(b.freq) / rate_;
    const uint8_t* d = b.data->data();
    for (int i = 0; i < frames; ++i) {
      size_t at = static_cast<size_t>(b.frame);
      if (at >= count) {
        if (!b.looping) {
          b.playing = false, b.frame = 0.0;
          break;
        }
        b.frame -= static_cast<double>(count);
        at = static_cast<size_t>(b.frame);
      }
      const uint8_t* s = d + at * align;
      float left, right;
      if (f.bits_per_sample == 8) {
        left = (s[0] - 128) / 128.0f;
        right = f.channels == 2 ? (s[1] - 128) / 128.0f : left;
      } else {
        int16_t a, c;
        std::memcpy(&a, s, 2);
        left = a / 32768.0f;
        if (f.channels == 2) std::memcpy(&c, s + 2, 2), right = c / 32768.0f;
        else right = left;
      }
      out[2 * i] += left * gl;
      out[2 * i + 1] += right * gr;
      b.frame += step;
    }
  }
  for (int i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

}  // namespace sdlfe
