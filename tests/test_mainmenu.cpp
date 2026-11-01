// The main menu's pieces (mech3/menu_widgets, docs/mainmenu.md) where the
// differential test cannot look: the frame-rate fix (FIX (B), fixes.md).
#include <cmath>

#include "check.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zutil/zfix.h"
#include "mech3/briefing.h"
#include "mech3/menu_ripple.h"
#include "mech3/menu_widgets.h"

namespace {
struct MenuEngine : mech3::Engine {
  int32_t fast_menu() override { return 0; }
  int32_t display_width() override { return 640; }
  int32_t display_height() override { return 480; }
};
struct MenuMotion {
  int32_t scrolled, slid;
  float ripple_seconds;
};
// One second of the background, the title and a ripple at `fps`, hidden.
MenuMotion menu_motion(float fps, bool per_second) {
  zfix::options().per_second = per_second;
  MenuEngine e;
  zimg::Texture big{};
  big.width = 2000, big.height = 2000;
  mech3::ScrollImage bg(e);
  bg.image = &big;
  bg.flags |= ztwod::kHidden;
  mech3::Marquee title(e);
  title.image = &big;
  title.x = 1000;
  title.flags |= ztwod::kHidden;
  mech3::SlideButton b(e, -1);
  b.flags |= ztwod::kHidden;
  b.init_ripple();
  b.max_radius = 1000;
  b.restart_ripple();
  const float dt = 1.0f / fps;
  float rippled = 0.0f;
  for (int i = 0; i < static_cast<int>(fps); ++i) {
    bg.update(dt);
    title.update(dt);
    if (b.rippling) rippled += dt;
    b.update(dt);
  }
  bg.image = title.image = nullptr;
  zfix::options().per_second = true;
  return {bg.view.left, 1000 - title.x, rippled};
}
}  // namespace

// The background crawls 30 pixels a second, MECH3TEXT 60 and a ripple lasts
// about 0.6 s (18 frames at 30 fps) whatever the frame rate; the exe's steps
// go by frames.
TEST(main_menu_moves_per_second) {
  for (float fps : {30.0f, 60.0f, 144.0f}) {
    const MenuMotion m = menu_motion(fps, true);
    EXPECT(m.scrolled >= 29 && m.scrolled <= 30);
    EXPECT(m.slid >= 58 && m.slid <= 60);
    EXPECT(m.ripple_seconds > 0.55f && m.ripple_seconds < 0.62f);  // 7 to 2 at 9 a second, the last frame drawn
  }
  const MenuMotion raw = menu_motion(144.0f, false);
  EXPECT(raw.scrolled == 144 && raw.slid == 288);
  EXPECT(raw.ripple_seconds > 17.5f / 144.0f && raw.ripple_seconds < 18.5f / 144.0f);
}

// The launcher's ripple is the main menu's (Owen: "much slower than the
// original ripple"): both run mech3::menu_ripple_step from the same start,
// so a SlideButton and the launcher's ring have the same radius at every
// frame, and the ring spreads 120 pixels a second at any frame rate. The
// exe truncates each frame's growth, so above 120 fps its ring never
// grew; the fraction is carried (FIX (B), per_second).
TEST(launcher_ripple_matches_the_main_menus) {
  for (float fps : {30.0f, 60.0f, 144.0f}) {
    MenuEngine e;
    mech3::SlideButton b(e, -1);
    b.flags |= ztwod::kHidden;
    b.init_ripple();
    b.max_radius = mech3::MenuRipple::kMaxRadius;  // read_ripple's default (the main menu's dialog has no MAXRADIUS)
    b.restart_ripple();
    int32_t radius = mech3::MenuRipple::kStartRadius;  // as the launcher starts it on a new button
    float carry = 0.0f, amp = mech3::MenuRipple::kStartAmplitude, phase = 0.0f;
    const float dt = 1.0f / fps;
    bool same = true;
    const int half = static_cast<int>(fps / 2);
    for (int i = 0; i < half; ++i) {
      b.update(dt);
      mech3::menu_ripple_step(radius, carry, amp, phase, mech3::MenuRipple::kSpeed, mech3::MenuRipple::kWavelength,
                              mech3::MenuRipple::kMaxRadius, dt);
      same = same && radius == b.radius && amp == b.amplitude;
    }
    std::printf("ripple at %.0f fps: after half a second the menu's %d, the launcher's %d\n", fps, b.radius, radius);
    EXPECT(same);
    EXPECT(radius >= 79 && radius <= 80);  // 20 + 120 x 0.5
  }
}

// The briefing's transport bar (0x00406440): 0.005 a frame towards the load;
// fixed, 0.15 a second.
TEST(fix_briefing_bar_per_second) {
  struct Quiet : mech3::Engine {
    uint32_t unported(const char*, std::initializer_list<mech3::Arg>) override { return 0; }
    const char* message(int32_t) override { return ""; }
    int32_t game_kind() override { return mech3::kCampaign; }
    float frame_dt() override { return 0.0f; }
  } e;
  float value[2];
  for (int fix = 0; fix < 2; ++fix) {
    zfix::options().per_second = fix != 0;
    mech3::BriefingScreen s(e, "brief_c11");
    s.progress(1.0f);
    for (int i = 0; i < 120; ++i) s.update(1.0f / 120.0f);  // a second at 120 fps
    value[fix] = s.bar.value;
  }
  zfix::options().per_second = true;
  EXPECT(std::fabs(value[1] - 0.15f) < 1e-3f);
  EXPECT(std::fabs(value[0] - 0.6f) < 1e-3f);  // the exe's: 120 frames of 0.005
}
