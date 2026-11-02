#include "mech3/options_menu.h"

#include <cmath>
#include <cstdio>

#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "mech3/options.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
// The render target from zVideo's buffers (0x00558290), as the main menu's.
void target_buffers(Engine& e) {
  const Handle a = e.video_back_buffer();
  const Handle b = e.video_buffer_da0();
  const Handle r = e.video_buffer_d90();
  e.render_set_target2(e.video_target(), r, b, a);
}

// ObjectLOD and EffectsLevel run 0 (most) .. 2; the cycles show LOW .. HIGH.
int32_t level_of(int32_t v) { return v == 0 ? 2 : v == 1 ? 1 : 0; }
}  // namespace

// Mech3.exe 1.2 @ 0x00428c60
void StateBackButton::on_left_up() {
  if (auto* s = dynamic_cast<MenuScreen*>(owner)) s->state_escape();
}

// Mech3.exe 1.2 @ 0x00437f90
void CameraCycle::after_bind() {
  CycleButton::after_bind();
  add_item(e_.message(0x1c), 1);
  add_item(e_.message(0x1b), 0);
  add_item(e_.message(0xdbc), 2);
}

// ---- the game options ----

// Mech3.exe 1.2 @ 0x00437f60
void GameOptionsScreen::AcceptButton::on_left_up() {
  auto* s = dynamic_cast<MenuScreen*>(owner);
  if (!s) return;
  s->apply(1);
  e_.settings_write();
  s->state_escape();
}

// Mech3.exe 1.2 @ 0x004380c0, 0x004380e0
void GameOptionsScreen::PushButton::on_left_up() { e_.push_state(e_.peek32(target), 0); }

// Mech3.exe 1.2 @ 0x004379f0
GameOptionsScreen::GameOptionsScreen(Engine& e, Handle s)
    : MenuScreen(e, s), accept(e), back(e), difficulty(e), camera(e), invincible(e, 1), infinite_ammo(e, 1),
      heat(e, 0), torso(e), throttle(e), jump_jets(e), invert_y(e), gfx_audio(e, 0x60986c),
      controls(e, 0x609870) {
  target_buffers(e);
  const bool moving = !e.fast_menu();
  if (load("dlgOptions.zrd", "GAME_OPTION_DIALOG", moving)) {
    borrow_background(true);
    bind_primitive(&lines, "LINES");
    bind_primitive(&lines1, "LINES1");
    bind_button(&accept, "ACCEPT");
    bind_button(&back, "BACK");
    bind_button(&gfx_audio, "GFX_AUDIO_OPTIONS");
    bind_button(&controls, "CONTROLS_CFG");
    if (!moving) {
      bake();
      remove(&background);
      remove(&lines);
      remove(&lines1);
      accept.set_baked(true);
      back.set_baked(true);
      gfx_audio.set_baked(true);
      controls.set_baked(true);
    }
    bind_button(&difficulty, "DIFFICULTY");
    bind_button(&camera, "CAMERA");
    bind_button(&invincible, "INVINCIBLE");
    bind_button(&infinite_ammo, "INFINITE_AMMO");
    bind_button(&heat, "HEAT_MGMT");
    bind_button(&torso, "TORSO_AUTO_RTN");
    bind_button(&throttle, "THROTTLE_CONST");
    bind_button(&jump_jets, "MW2JJETS");
    bind_button(&invert_y, "CMD_INVERT_MOUSE_Y");
    unload();
  }
  set_flags_all(0);
  set_active(1);
}

// Mech3.exe 1.2 @ 0x00438100: inverted mouse Y is MouseAccelY's sign, set to
// -1.3 or 1.3 (any magnitude the registry held is lost).
// 0x00438100's MouseAccelY: -1.3 inverted, else 1.3. FIX (bug): the
// player's sensitivity kept and only its sign set (fixes.md, Decided); a zero
// one takes the original's 1.3.
float mouse_y_accel(float current, bool inverted) {
  if (!zfix::options().bugs) return inverted ? static_cast<float>(-1.3) : static_cast<float>(1.3);
  float mag = std::fabs(current);
  if (mag == 0.0f) mag = 1.3f;
  return inverted ? -mag : mag;
}

void GameOptionsScreen::apply(int32_t to_options) {
  Engine& e = engine();
  if (!to_options) {
    difficulty.select_each_value(e.game_intensity());
    camera.select_each_value(e.camera_mode());
    invincible.select(e.invincible() != 0);
    infinite_ammo.select(e.infinite_ammo() != 0);
    heat.select(e.heat() != 0);
    torso.select(e.torso_auto_return() != 0);
    throttle.select(e.throttle_const() != 0);
    jump_jets.select(e.mw2_jump_jets() != 0);
    invert_y.select(e.mouse_accel_y() < 0.0f);
    return;
  }
  e.set_game_intensity(difficulty.selected_value());
  e.set_camera_mode(camera.selected_value());
  e.set_invincible(invincible.selected_value());
  e.set_infinite_ammo(infinite_ammo.selected_value());
  e.set_heat(heat.selected_value());
  e.set_torso_auto_return(torso.selected_value());
  e.set_throttle_const(throttle.selected_value());
  e.set_mw2_jump_jets(jump_jets.selected_value());
  e.set_mouse_accel_y(mouse_y_accel(e.mouse_accel_y(), invert_y.selected_value() != 0));
}

// ---- the graphics and audio options ----

// Mech3.exe 1.2 @ 0x0043a6f0
ztwod::Rect* VolumeSlider::get_rect() { return reading ? nullptr : Control::get_rect(); }

// Mech3.exe 1.2 @ 0x00439470 (music), 0x00439490 (effects)
void VolumeSlider::on_left_up() {
  set_from_mouse();
  if (music_) e_.set_cd_volume(value);
  else e_.set_sound_frequency(value);
}

// Mech3.exe 1.2 @ 0x0043a6c0
bool VolumeSlider::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  reading = e_.fast_menu() ? 0 : 1;
  const bool r = Slider::read_control(node, dialog);
  reading = 0;
  return r;
}

// Mech3.exe 1.2 @ 0x004394b0
void ResolutionCycle::after_bind() {
  CycleButton::after_bind();
  rebuild(e_.hw_card_dev());
}

// Mech3.exe 1.2 @ 0x0043a470: 320x240 .. 1600x1200 (messages 0x41.. 0xfdc)
// within the device's range; the old choice kept, pulled into the range.
void ResolutionCycle::rebuild(int32_t dev) {
  const int32_t cur = selected_value();
  int32_t lo = dev, hi = 0;
  e_.video_mode_range(dev, &lo, &hi);
  clear_items();
  static const struct {
    int32_t mode, message;
  } kModes[] = {{3, 0x41}, {5, 0x42}, {6, 0x43}, {7, 0x44}, {8, 0xfda}, {9, 0xfdb}, {10, 0xfdc}};
  for (const auto& m : kModes)
    if (lo <= m.mode && hi >= m.mode) add_item(e_.message(m.message), m.mode);
  // FIX (B) (resolution.md): the new sizes, by their size (no message names them).
  for (int32_t mode = 11; zfix::options().any_resolution && mode <= kLastVMode; ++mode) {
    int32_t w = 0, h = 0;
    if (lo > mode || hi < mode || !vmode_size(mode, &w, &h)) continue;
    char label[24];
    std::snprintf(label, sizeof label, "%dx%d", w, h);
    add_item(label, mode);
  }
  if (cur < lo) select_value(lo);
  else if (cur > hi) select_value(hi);
  else select_value(cur);
}

// Mech3.exe 1.2 @ 0x00439550
void TextureCycle::after_bind() {
  CycleButton::after_bind();
  rebuild(e_.hw_card_dev());
}

// Mech3.exe 1.2 @ 0x0043a310: 2 MB (6), 4 MB (5), 8 MB (3) or MAX (0, software
// only) by InstallOptions and the card's memory; TextureMemory if set, else
// the last one added (or the card's megabytes when 2 MB is not offered).
void TextureCycle::rebuild(int32_t dev) {
  clear_items();
  int32_t sel = dev;
  if (dev == -1) {
    if (e_.install_options(0x100)) {
      sel = 6;
      add_item(e_.message(0x18), 6);
    }
    if (e_.install_options(0x200)) {
      sel = 5;
      add_item(e_.message(0x17), 5);
    }
    if (e_.install_options(0x400)) {
      sel = 0;
      add_item(e_.message(0x14), 0);
    }
  } else {
    uint32_t total = 0;
    uint32_t mb = 2;
    if (e_.video_card_memory(dev, &total)) mb = total >> 20;
    if (e_.install_options(1) && mb >= 2) {
      sel = 6;
      add_item(e_.message(0x18), 6);
    } else {
      sel = static_cast<int32_t>(mb);
    }
    if (e_.install_options(2) && mb >= 4) {
      sel = 5;
      add_item(e_.message(0x17), 5);
    }
    if (e_.install_options(4) && mb >= 8) {
      sel = 3;
      add_item(e_.message(0x15), 3);
    }
  }
  if (e_.texture_memory()) sel = e_.texture_memory();
  select_value(sel);
}

// Mech3.exe 1.2 @ 0x004394d0: InstallOptions 0xff00 offers the software
// renderer, 0xff the cards. FIX (A): the software renderer is not ported (the
// GPU draws everything, docs/render.md), so it is not offered.
void DeviceCycle::after_bind() {
  CycleButton::after_bind();
  if (e_.install_options(0xff00) && e_.software_renderer()) add_item(e_.message(0x4d), -1);
  if (!e_.install_options(0xff)) return;
  const uint32_t n = e_.video_device_count();
  for (uint32_t i = 0; i < n; ++i) {
    char name[0x40];
    // sprintf(name, the device's name): the name is the format (a '%' in it
    // reads stack); copied as text here.
    std::snprintf(name, sizeof name, "%s", e_.video_device_name(i));
    add_item(name, static_cast<int32_t>(i));
  }
}

// Mech3.exe 1.2 @ 0x0043a0d0
void DeviceCycle::on_left_up() {
  ztwod::Dialog* d = dialog;
  CycleButton::on_left_up();
  if (!d) return;
  if (auto* s = dynamic_cast<GfxAudioScreen*>(d)) s->device_changed(selected_value());
}

// Mech3.exe 1.2 @ 0x00439570
void SoundCycle::after_bind() {
  CycleButton::after_bind();
  if (e_.install_options(0x40000)) add_item(e_.message(0x38), 0);
  if (e_.install_options(0x10000)) add_item(e_.message(0x40), 2);
}

// Mech3.exe 1.2 @ 0x00439440
void GfxAudioScreen::AcceptButton::on_left_up() {
  e_.pop_state(1);
  if (auto* s = dynamic_cast<MenuScreen*>(owner)) e_.vcall(e_.state_screen(s->state), 0x10, {1});
}

// Mech3.exe 1.2 @ 0x00438f60
GfxAudioScreen::GfxAudioScreen(Engine& e, Handle s)
    : MenuScreen(e, s), music(e, true), sfx(e, false), shadows(e), self_shadows(e), lighting(e), resolution(e),
      device(e), textures(e), sound(e), detail(e), effects(e), accept(e), back(e) {
  target_buffers(e);
  const bool moving = !e.fast_menu();
  if (load("dlgOptions.zrd", "GFX_AUDIO_OPTIONDIALOG", moving)) {
    borrow_background(true);
    bind_primitive(&lines, "LINES");
    bind_primitive(&lines1, "LINES1");
    bind_button(&accept, "ACCEPT");
    bind_button(&back, "BACK");
    if (!moving) {
      bake();
      remove(&background);
      remove(&lines);
      remove(&lines1);
      accept.set_baked(true);
      back.set_baked(true);
    }
    bind_button(&shadows, "SHADOWS");
    bind_button(&music, "MUSICSLIDER");
    bind_button(&sfx, "SFXSLIDER");
    bind_button(&self_shadows, "SELF_SHADOWS");
    bind_button(&lighting, "LIGHTING");
    bind_button(&device, "VIDEO_DEVICE");
    bind_button(&resolution, "RESOLUTION");
    bind_button(&textures, "TEXTURES");
    bind_button(&sound, "SOUND_QUALITY");
    bind_button(&detail, "OBJECT_DETAIL");
    bind_button(&effects, "SPECIAL_EFFECTS");
    unload();
  }
  set_flags_all(0);
  set_active(1);
}

// Mech3.exe 1.2 @ 0x004395c0: a new video device asks to restart the game
// (message 0xf2f); the rest is written whatever the answer. Lighting and the
// shadows need a 3D card that InstallOptions allows; else they are written off.
void GfxAudioScreen::apply(int32_t to_options) {
  Engine& e = engine();
  if (!to_options) {
    music.set_raw(e.cd_volume());
    sfx.set_raw(e.sound_frequency());
    resolution.select_each_value(e.in_game_vmode());
    device.select_each_value(e.hw_card_dev());
    textures.select_each_value(e.texture_memory());
    lighting.select((e.gfx_flags() & 1) != 0);
    sound.select_value(e.sound_lod());
    const int32_t lod = e.object_lod();
    if (lod >= 0 && lod <= 2) detail.select_each_value(level_of(lod));
    const int32_t fx = e.effects_level();
    if (fx >= 0 && fx <= 2) effects.select_each_value(level_of(fx));
    self_shadows.select(e.shadow_parts() != 0);
    shadows.select(e.shadow() != 0);
    device_on_entry = device.selected_value();
    device_changed(device_on_entry);
    return;
  }
  if (device_on_entry != device.value_at(device.index)) {
    if (e.message_box(1, 0xf2e, 0xf2f) == 1) {
      e.set_hw_card_dev(device.value_at(device.index));
      e.replace_with_exit();
      if (Handle st = e.peek32(0x609868)) e.vcall(e.state_screen(st), 0x10, {1});
    } else {
      e.set_hw_card_dev(device_on_entry);
    }
  }
  int32_t flags = e.gfx_flags();
  e.set_cd_volume(music.value);
  e.set_texture_memory(textures.selected_value());
  e.set_sound_frequency(sfx.value);
  e.set_sound_lod(sound.selected_value());
  e.set_object_lod(level_of(detail.selected_value()));
  e.set_effects_level(level_of(effects.value_at(effects.index)));
  e.set_in_game_vmode(resolution.selected_value());
  if (e.hw_card_flag() && e.install_options(0xff)) {
    flags = lighting.selected_value() ? flags | 1 : flags & ~1;
    e.set_gfx_flags(flags);
    e.set_shadow_parts(self_shadows.selected_value());
    e.set_shadow(shadows.selected_value());
  } else {
    e.set_gfx_flags(flags & ~1);
    e.set_shadow_parts(0);
    e.set_shadow(0);
  }
}

// Mech3.exe 1.2 @ 0x00439a90: the lists and switches of the device: lighting
// and self-shadows for a 3D card, shadows also by its capability 3; the
// device's own settings shown (it is selected only meanwhile), and zModel's
// and zRender's GfxFlags taken from its _HW or _SW entry.
void GfxAudioScreen::device_changed(int32_t dev) {
  Engine& e = engine();
  const bool hw = dev >= 0;
  resolution.rebuild(dev);
  textures.rebuild(dev);
  const bool cap = e.video_has_cap(3) != 0;
  shadows.set_enabled(hw && cap ? 1 : 0);
  lighting.set_enabled(hw ? 1 : 0);
  self_shadows.set_enabled(hw ? 1 : 0);
  const int32_t old = e.hw_card_dev();
  e.set_hw_card_dev(dev);
  lighting.select((e.gfx_flags() & 1) != 0);
  const int32_t fx = e.effects_level();
  if (fx >= 0 && fx <= 2) effects.select_each_value(level_of(fx));
  self_shadows.select(e.shadow_parts() != 0);
  shadows.select(e.shadow() != 0);
  e.set_hw_card_dev(old);
  const char* entry = hw ? "GfxFlags_HW" : "GfxFlags_SW";
  e.model_use_gfx_flags(entry, e.gfx_flags());
  e.render_use_gfx_flags(entry, e.gfx_flags());
}

}  // namespace mech3
