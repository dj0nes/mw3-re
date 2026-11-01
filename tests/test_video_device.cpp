// The graphics screen's video devices (mech3/options_menu, app.cpp's
// restore_video): FIX (A), no software renderer.
#include <vector>

#include "check.h"
#include "mech3/app.h"
#include "mech3/options.h"
#include "mech3/options_menu.h"

// FIX (A): the software renderer is not ported, so the graphics screen's
// VIDEO DEVICE cycle offers only the GPU, and a saved software choice
// (HWCardFlag 0, HWCardDev -1) starts on the GPU's device. The original's
// cycle and start (an engine that says software_renderer) are unchanged.
TEST(software_renderer_is_not_offered_and_a_saved_one_starts_on_the_gpu) {
  struct E : mech3::Engine {
    bool sw = false;
    uint32_t unported(const char*, std::initializer_list<zgame::Arg>) override { return 0; }
    bool software_renderer() override { return sw; }
    uint32_t install_options(uint32_t mask) override { return mask & 0x070707; }  // the port's full install
    uint32_t video_device_count() override { return 1; }
    const char* video_device_name(uint32_t) override { return "Metal"; }
    const char* message(int32_t) override { return "SOFTWARE"; }
    int32_t pick_video_device(int32_t, int32_t) override { return -1; }
  };
  auto offered = [](E& e) {
    mech3::DeviceCycle d(e);
    d.after_bind();
    std::vector<int32_t> v;
    for (int32_t i = 0; i < d.count(); ++i) v.push_back(d.value_at(i));
    return v;
  };
  E gpu, original;
  original.sw = true;
  EXPECT(offered(gpu) == std::vector<int32_t>({0}));
  EXPECT(offered(original) == std::vector<int32_t>({-1, 0}));
  const mech3::Options keep = mech3::options();
  for (E* e : {&gpu, &original}) {
    for (int32_t saved : {-1, 999}) {  // software saved; a first start's "pick one" that picks software
      mech3::set_hw_card_dev(saved);
      mech3::Mech3App app(e);
      app.restore_video(saved >= 0, false);
      if (e == &gpu) EXPECT(mech3::hw_card_dev() == 0 && mech3::hw_card_flag() == 1);
      else EXPECT(mech3::hw_card_dev() == -1 && mech3::hw_card_flag() == 0);
    }
  }
  // The settings as loaded, before either start (windowed too).
  for (E* e : {&gpu, &original}) {
    mech3::select_hw(0), mech3::options().hw_card_dev = -1;
    mech3::Mech3App app(e);
    app.gpu_only();
    if (e == &gpu) EXPECT(mech3::hw_card_flag() == 1 && mech3::hw_card_dev() == 999);
    else EXPECT(mech3::hw_card_flag() == 0 && mech3::hw_card_dev() == -1);
  }
  mech3::options() = keep;
}
