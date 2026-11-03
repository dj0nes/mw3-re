#pragma once
#include <cstdint>

#include "mech3/menu_widgets.h"

// The option screens of the main menu and the pause screen (docs/menus.md):
// dlgOptions.zrd's GAME_OPTION_DIALOG and GFX_AUDIO_OPTIONDIALOG. Each reads
// the options into its buttons on entering (+0x10 with 0) and writes them back
// on ACCEPT (+0x10 with 1). Original file unknown: 0x00437f60..0x0043a710,
// between mech3\mech_data.cpp and mech3\mech_map.cpp.
namespace mech3 {

// BACK on the option screens (vtables 0x005bf5a8, 0x005bf7f8; +0x30 0x00428c60):
// the state's +0x28 only, no press sound.
class StateBackButton : public SlideButton {
 public:
  explicit StateBackButton(Engine& e) : SlideButton(e, -1) {}
  void on_left_up() override;        // 0x00428c60
};

// The CAMERA cycle (vtable 0x005bf458, +0x88 0x00437f90): 1st person (1),
// 3rd person high (0), 3rd person low (2).
class CameraCycle : public CycleButton {
 public:
  explicit CameraCycle(Engine& e) : CycleButton(e, -1) {}
  void after_bind() override;        // 0x00437f90
};

// The game options (vtable 0x005bee60), 0x1407c bytes.
// The game options' MouseAccelY for the invert box (0x00438100's ACCEPT).
float mouse_y_accel(float current, bool inverted);

// Ours (Owen, 2026-11-02): MOUSE SENSITIVITY on the graphics and audio
// screen, MouseAccelX and Y's magnitude (Y keeping its sign, the game
// options' invert box), 0.5 to 3.0 in tenths, the original's 1.3 by default;
// its slider (our record edit, patch12::apply_ours) bound only where the
// program turns it on (the probes run the original's screens).
constexpr float kSensitivityLow = 0.5f, kSensitivityHigh = 3.0f;
float sensitivity_of_slider(float v);   // the slider's 0..1 to a magnitude, in tenths
float slider_of_sensitivity(float s);   // and back
bool& options_sensitivity_slider();     // on in the program (GameEngine)

class SensitivitySlider : public ztwod::Slider {
 public:
  void on_left_up() override { set_from_mouse(); }
};

class GameOptionsScreen : public MenuScreen {
 public:
  GameOptionsScreen(Engine& e, Handle state);  // 0x004379f0
  ~GameOptionsScreen() override = default;  // 0x00438490 (deleting 0x00438470)
  void apply(int32_t to_options) override;  // +0x10  0x00438100

  // ACCEPT (vtable 0x005bf648, +0x30 0x00437f60): applied, the settings
  // written to the registry, then the state's +0x28.
  class AcceptButton : public SlideButton {
   public:
    explicit AcceptButton(Engine& e) : SlideButton(e, -1) {}
    void on_left_up() override;      // 0x00437f60
  };
  // GFX_AUDIO_OPTIONS and CONTROLS_CFG (vtables 0x005bef20, 0x005bee80; +0x30
  // 0x004380c0, 0x004380e0): push their menu object, no press sound.
  class PushButton : public SlideButton {
   public:
    PushButton(Engine& e, uint32_t global) : SlideButton(e, -1), target(global) {}
    void on_left_up() override;      // 0x004380c0 / 0x004380e0
    uint32_t target;                 // [0x0060986c] or [0x00609870]
  };

  AcceptButton accept;               // +0x111ec  ACCEPT
  StateBackButton back;              // +0x113d8  BACK
  LevelCycle difficulty;             // +0x115c4  DIFFICULTY: GameIntensity
  CameraCycle camera;                // +0x119e8  CAMERA
  AlertCycle invincible;             // +0x11e0c  INVINCIBLE (ON in red)
  AlertCycle infinite_ammo;          // +0x1223c  INFINITE_AMMO (ON in red)
  AlertCycle heat;                   // +0x1266c  HEAT_MGMT (OFF in red)
  OnOffCycle torso;                  // +0x12a9c  TORSO_AUTO_RTN
  OnOffCycle throttle;               // +0x12ec0  THROTTLE_CONST
  OnOffCycle jump_jets;              // +0x132e4  MW2JJETS
  OnOffCycle invert_y;               // +0x13708  CMD_INVERT_MOUSE_Y: MouseAccelY's sign
  PushButton gfx_audio;              // +0x13b2c  GFX_AUDIO_OPTIONS
  PushButton controls;               // +0x13d18  CONTROLS_CFG
  ztwod::Image lines{0};             // +0x13f04  LINES
  ztwod::Image lines1{0};            // +0x13fc0  LINES1
};

// MUSICSLIDER and SFXSLIDER (vtables 0x005bffb0, 0x005bff20), 0x18c bytes: a
// press sets the volume (+0x30 0x00439470: CDVolume, 0x00439490: SoundVolume).
class VolumeSlider : public ztwod::Slider {
 public:
  VolumeSlider(Engine& e, bool music) : e_(e), music_(music) {}
  ztwod::Rect* get_rect() override;  // +0x2c  0x0043a6f0
  void on_left_up() override;        // +0x30
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0043a6c0
  void set_raw(float v) override { set_value(v); }  // +0x8c  0x0059e350

  uint8_t reading = 0;               // +0x188  !FastMenu inside read_control (get_rect: none)

 private:
  Engine& e_;
  bool music_;                       // which subclass (its vtable)
};

// RESOLUTION (vtable 0x005bfc80, +0x88 0x004394b0): the modes the device can
// run, by 0x004ab860's range.
class ResolutionCycle : public CycleButton {
 public:
  explicit ResolutionCycle(Engine& e) : CycleButton(e, -1) {}
  void after_bind() override;        // 0x004394b0
  void rebuild(int32_t device);      // 0x0043a470
};

// TEXTURES (vtable 0x005bfb30, +0x88 0x00439550): the texture memory sizes.
class TextureCycle : public CycleButton {
 public:
  explicit TextureCycle(Engine& e) : CycleButton(e, -1) {}
  void after_bind() override;        // 0x00439550
  void rebuild(int32_t device);      // 0x0043a310
};

// VIDEO_DEVICE (vtable 0x005bfbd8): Software Render (-1) and the 3D cards.
class DeviceCycle : public CycleButton {
 public:
  explicit DeviceCycle(Engine& e) : CycleButton(e, -1) {}
  void on_left_up() override;        // +0x30  0x0043a0d0: then the screen follows the device
  void after_bind() override;        // +0x88  0x004394d0
};

// SOUND_QUALITY (vtable 0x005bfa88, +0x88 0x00439570): HIGH (0), LOW (2).
class SoundCycle : public CycleButton {
 public:
  explicit SoundCycle(Engine& e) : CycleButton(e, -1) {}
  void after_bind() override;        // 0x00439570
};

// The graphics and audio options (vtable 0x005bf7d8), 0x13f9c bytes.
class GfxAudioScreen : public MenuScreen {
 public:
  GfxAudioScreen(Engine& e, Handle state);  // 0x00438f60
  ~GfxAudioScreen() override = default;  // 0x00439ce0 (deleting 0x00439cc0)
  void apply(int32_t to_options) override;  // +0x10  0x004395c0
  void device_changed(int32_t device);  // 0x00439a90 (and inline in 0x0043a0d0)

  // ACCEPT (vtable 0x005bf898, +0x30 0x00439440): the pop queued, then the
  // state's screen applied.
  class AcceptButton : public SlideButton {
   public:
    explicit AcceptButton(Engine& e) : SlideButton(e, -1) {}
    void on_left_up() override;      // 0x00439440
  };

  VolumeSlider music;                // +0x111ec  MUSICSLIDER: CDVolume
  VolumeSlider sfx;                  // +0x11378  SFXSLIDER: SoundVolume
  SensitivitySlider sensitivity;     // ours: MOUSE_SENSITIVITY (options_sensitivity_slider)
  bool has_sensitivity = false;      // bound
  OnOffCycle shadows;                // +0x11504  SHADOWS: Shadow
  OnOffCycle self_shadows;           // +0x11928  SELF_SHADOWS: ShadowParts
  OnOffCycle lighting;               // +0x11d4c  LIGHTING: GfxFlags bit 0
  ResolutionCycle resolution;        // +0x12170  RESOLUTION: InGameVMode
  DeviceCycle device;                // +0x12594  VIDEO_DEVICE: HWCardDev
  TextureCycle textures;             // +0x129b8  TEXTURES: TextureMemory
  SoundCycle sound;                  // +0x12ddc  SOUND_QUALITY: SoundLOD
  LevelCycle detail;                 // +0x13200  OBJECT_DETAIL: ObjectLOD (2 - level)
  LevelCycle effects;                // +0x13624  SPECIAL_EFFECTS: EffectsLevel (2 - level)
  AcceptButton accept;               // +0x13a48  ACCEPT
  StateBackButton back;              // +0x13c34  BACK
  ztwod::Image lines{0};             // +0x13e20  LINES
  ztwod::Image lines1{0};            // +0x13edc  LINES1
  int32_t device_on_entry = 0;       // +0x13f98  VIDEO_DEVICE's when the screen was read
};

}  // namespace mech3
