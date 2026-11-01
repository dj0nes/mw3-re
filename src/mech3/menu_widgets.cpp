#include "mech3/menu_widgets.h"
#include "mech3/menu_ripple.h"

#include "mech3/mech_config.h"
#include "mech3/mech_salvage.h"
#include "mech3/pilot.h"

#include <cstdlib>
#include <map>
#include <cstring>

#include "gamez/zimage/zimg_library.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

using ztwod::Rect;

namespace {
int32_t width_of(const zimg::Texture* t) { return t ? t->width : 0; }
int32_t height_of(const zimg::Texture* t) { return t ? t->height : 0; }

// FIX (B): the menus move by a step each frame, so they ran faster the faster
// the machine drew. With zfix's per_second the step is taken once for every
// 30 fps frame that has passed (owed: what is left over); off, once a frame.
int32_t frames_due(float dt, float* owed) {
  if (!zfix::options().per_second) return 1;
  *owed += zfix::frame_steps(dt);
  const int32_t n = static_cast<int32_t>(*owed);
  *owed -= static_cast<float>(n);
  return n;
}
}  // namespace

// ---- the background ----

ScrollImage::ScrollImage(Engine& e) : Image(0), e_(e) {
  const int32_t h = e_.display_height();
  const int32_t w = e_.display_width();
  view = Rect{0, 0, w, h};
  moving = e_.fast_menu() ? 0 : 1;
}

// Mech3.exe 1.2 @ 0x0040a490
void ScrollImage::draw() {
  if (!moving) {
    Image::draw();
    return;
  }
  for (int i = 0; i < 4; ++i) {
    if (!piece[i]) continue;
    set_pos(piece_pos[i][0], piece_pos[i][1]);
    src = &piece_src[i];
    Image::draw();
    piece[i] = 0;
  }
}

// Mech3.exe 1.2 @ 0x0040a210: the view moves and wraps at the image's size;
// piece 0 is the view cut at the image's right and bottom edges, pieces 1 and 2
// fill the screen past them from the image's left and top, piece 3 the corner.
void ScrollImage::update(float dt) {
  if (moving) {
    const int32_t n = frames_due(dt, &steps_);
    view.left += step_x * n;
    view.right += step_x * n;
    view.bottom += step_y * n;
    view.top += step_y * n;
    if (view.left >= width_of(image)) {
      view.left -= width_of(image);
      view.right = e_.display_width() + view.left;
    }
    if (view.top >= height_of(image)) {
      view.top -= height_of(image);
      view.bottom = e_.display_height() + view.top;
    }
    piece[0] = 1;
    Rect& p0 = piece_src[0];
    p0 = view;
    // (a second test, right < right, follows each of these: never true)
    piece[1] = 0;
    if (p0.right > width_of(image)) {
      p0.right = width_of(image);
      piece[1] = 1;
    }
    piece[2] = 0;
    if (p0.bottom > height_of(image)) {
      p0.bottom = height_of(image);
      piece[2] = 1;
    }
    piece[3] = piece[1] && piece[2] ? 1 : 0;
    const int32_t w = p0.right - p0.left;
    const int32_t h = p0.bottom - p0.top;
    if (piece[1]) {
      piece_src[1] = Rect{0, p0.top, e_.display_width() - w, p0.bottom};
      piece_pos[1][0] = w;
      piece_pos[1][1] = 0;
    }
    if (piece[2]) {
      piece_src[2] = Rect{p0.left, 0, p0.right, e_.display_height() - h};
      piece_pos[2][0] = 0;
      piece_pos[2][1] = h;
    }
    if (piece[3]) {
      piece_src[3] = Rect{piece_src[1].left, piece_src[2].top, piece_src[1].right, piece_src[2].bottom};
      piece_pos[3][0] = w;
      piece_pos[3][1] = h;
    }
  }
  Object2D::update(dt);
}

// ---- the title ----

Marquee::Marquee(Engine& e) : Image(0), e_(e) {}

// Mech3.exe 1.2 @ 0x0042fb30
void Marquee::update(float dt) {
  if (!e_.fast_menu()) {
    int32_t step = static_cast<int32_t>(speed);
    if (zfix::options().per_second) {
      steps_ += speed * zfix::frame_steps(dt);
      step = static_cast<int32_t>(steps_);
      steps_ -= static_cast<float>(step);
    }
    const int32_t py = get_y();
    set_pos(get_x() - step, py);
    if (get_x() + width_of(image) <= 0) {
      const int32_t y2 = get_y();
      set_pos(e_.display_width(), y2);
    }
  }
  Object2D::update(dt);
}

// ---- the screen ----

// Mech3.exe 1.2 @ 0x0040a110
MenuScreen::MenuScreen(Engine& e, Handle s) : e_(e), state(s), background(e) {
  origin_x = ztwod::dialog_origin().x;  // FIX (B): 0 but on a canvas (resolution.md step 5)
  origin_y = ztwod::dialog_origin().y;
}

// Mech3.exe 1.2 @ 0x0040a520
uint8_t MenuScreen::set_busy(uint8_t on) {
  const uint8_t old = busy_;
  busy_ = on;
  return old;
}

// Mech3.exe 1.2 @ 0x00486ed0 (called on the main menu, the option screens and the credits)
void MenuScreen::bake() {
  set_active(1);
  cursor_obj.hide();
  update(0.0f);
  cursor_obj.show_for(-1.0f);
  zimg::Texture* grab = ztwod::host().screen_grab(1);
  set_active(0);
  restore_behind();
  set_static_bg(grab);
  add(&still);
  still.set_image(grab);
}

void MenuScreen::borrow_background(bool option_screen) {
  zimg::Texture* img = nullptr;
  uint8_t moving = 0;
  const bool have = e_.main_menu_background(&img, &moving);
  if (option_screen && !have) {
    bind_primitive(&background, "BACKGROUND");
  } else {
    // FIX (bug): the credits read the main menu's fields without checking it is
    // there (unreachable: they open only from it); here no image, still.
    add(&background);
    background.set_image(img);
    background.moving = moving;
    if (!moving) {
      background.set_pos(0, 0);
      background.src = &background.view;
    }
  }
  if (option_screen && e_.mission_running()) {
    background.moving = 0;
    background.set_pos(0, 0);
    background.src = &background.view;
  }
}

// ---- the push button ----

namespace {
// The owner's +0x14 / +0x1c are the menu screen's (every ShellButton lives in one).
MenuScreen* screen_of(ztwod::Layer* owner) { return dynamic_cast<MenuScreen*>(owner); }
}  // namespace

// Mech3.exe 1.2 @ 0x0040a5d0
ShellButton::ShellButton(Engine& e, int32_t i) : id(i), e_(e) {}

// Mech3.exe 1.2 @ 0x0040a600
ShellButton::~ShellButton() {
  if (borrowed) {
    img_normal = set_image(static_cast<zimg::Texture*>(nullptr));
    img_activate = nullptr;
    img_disabled = nullptr;
    img_rollover = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x0040a6b0
Rect* ShellButton::get_rect() {
  if (reading && e_.fast_menu()) return nullptr;
  return Control::get_rect();
}

// Mech3.exe 1.2 @ 0x0042a3d0
void ShellButton::on_left_up() {
  if (!enabled) return;
  Control::on_left_up();
  notify(1, 0);
}

// Mech3.exe 1.2 @ 0x0040a720: no mouse while the screen is busy.
bool ShellButton::accepts(ztwod::Window*, bool) {
  if (!owner) return true;
  MenuScreen* s = screen_of(owner);
  return s ? !s->busy() : true;
}

// Mech3.exe 1.2 @ 0x0040a680
bool ShellButton::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  reading = 1;
  const bool r = Control::read_control(node, dialog);
  reading = 0;
  return r;
}

// Mech3.exe 1.2 @ 0x0040a6e0
void ShellButton::notify(int32_t a, int32_t b) {
  if (!owner) return;
  if (MenuScreen* s = screen_of(owner)) s->on_button(id, a, b);
}

// ---- the rippling button ----

// Mech3.exe 1.2 @ 0x00438d70
SlideButton::SlideButton(Engine& e, int32_t i) : ShellButton(e, i) {}

// Mech3.exe 1.2 @ 0x0042a450
SlideButton::~SlideButton() {
  if (stashed) ztwod::host().image_release(stashed);
  stashed = nullptr;
  for (ztwod::Object2D*& o : owned) {
    delete o;
    o = nullptr;
  }
  owned.clear();
}

// Mech3.exe 1.2 @ 0x0042a400
void SlideButton::set_pos(int32_t px, int32_t py) {
  Image::set_pos(px, py);
  after_bind();
}

// Mech3.exe 1.2 @ 0x00485650: the ring grows by `speed` a second up to
// MAXRADIUS while the ripple loses 0.3 a frame; it stops below 2.
void SlideButton::update(float dt) {
  if (rippling) dirty();
  Object2D::update(dt);
  if (!rippling) return;
  if (!menu_ripple_step(radius, radius_carry, amplitude, phase, speed, wavelength, max_radius, dt)) rippling = 0;
  ripple.amplitude = static_cast<int32_t>(amplitude);
  ripple.inner = static_cast<int32_t>(static_cast<float>(radius) - wavelength * 3.0f);
  ripple.outer = radius;
  ripple.phase = phase;
  ripple.wavelength = wavelength;
  ripple.clip = nullptr;
  ripple.draw_ripple();
}

// Mech3.exe 1.2 @ 0x00485590: a baked button's bounds are its normal image's.
Rect* SlideButton::get_rect() {
  if (reading) return nullptr;
  if (stashed) set_image(stashed);
  Rect* r = reading && e_.fast_menu() ? nullptr : Control::get_rect();
  if (stashed) set_image(static_cast<zimg::Texture*>(nullptr));
  return r;
}

// Mech3.exe 1.2 @ 0x00485570
void SlideButton::on_enter() {
  restart_ripple();
  Control::on_enter();
}

// Mech3.exe 1.2 @ 0x00485640
void SlideButton::on_leave() {
  rippling = 0;
  Control::on_leave();
}

// Mech3.exe 1.2 @ 0x004855f0: inside the bounds, edges included, hidden or not.
int32_t SlideButton::hit(int32_t px, int32_t py) {
  const Rect* r = get_rect();
  if (!r) return 0;
  return r->left <= px && px <= r->right && r->top <= py && py <= r->bottom ? 1 : 0;
}

void SlideButton::read_ripple(zrdr::Node* node) {
  reach_radius = 20.0f;
  max_radius = 200;
  zrdr::get_float(node, "RADIUS", &reach_radius);
  zrdr::get_int(node, "MAXRADIUS", &max_radius);
  reach_sq = reach_radius * reach_radius;
}

// Mech3.exe 1.2 @ 0x0042a1d0
bool SlideButton::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  reading = 1;
  const bool r = Control::read_control(node, dialog);
  reading = 0;
  read_ripple(node);
  return r;
}

// Mech3.exe 1.2 @ 0x0042a250: the ripple at the image's centre.
void SlideButton::after_bind() {
  const int32_t w = width_of(image), h = height_of(image);
  reach.top = static_cast<int32_t>(static_cast<float>(get_y() + h / 2) - reach_radius);
  reach.left = static_cast<int32_t>(static_cast<float>(get_x() + w / 2) - reach_radius);
  reach.bottom = static_cast<int32_t>(static_cast<float>(get_y() + h / 2) + reach_radius);
  reach.right = static_cast<int32_t>(static_cast<float>(get_x() + w / 2) + reach_radius);
  const float cy = static_cast<float>(get_y() + h / 2);
  const float cx = static_cast<float>(get_x() + w / 2);
  const int32_t iy = static_cast<int32_t>(cy);
  ripple.set_pos(static_cast<int32_t>(cx), iy);
  init_ripple();
}

// Mech3.exe 1.2 @ 0x0042a160
void SlideButton::set_baked(bool on) {
  if (on) {
    stashed = img_normal;
    set_image(static_cast<zimg::Texture*>(nullptr));
    img_normal = set_image(static_cast<zimg::Texture*>(nullptr));
    return;
  }
  if (!stashed) return;
  img_normal = set_image(stashed);
  set_image(stashed);
  stashed = nullptr;
}

// Mech3.exe 1.2 @ 0x00485510
void SlideButton::init_ripple() {
  wavelength = MenuRipple::kWavelength;
  speed = MenuRipple::kSpeed;
}

// Mech3.exe 1.2 @ 0x00485530
void SlideButton::restart_ripple() {
  radius = MenuRipple::kStartRadius;
  radius_carry = 0.0f;
  phase = 0.0f;
  amplitude = MenuRipple::kStartAmplitude;
  rippling = e_.fast_menu() ? 0 : 1;
}

// ---- the mech lab's button with help ----

// Mech3.exe 1.2 @ 0x0044aaf0
HelpButton::HelpButton(Engine& e, int32_t id) : ShellButton(e, id) {}

// Mech3.exe 1.2 @ 0x0044ab80
HelpButton::~HelpButton() {
  for (ztwod::Text*& t : help) {
    delete t;
    t = nullptr;
  }
  help.clear();
}

// Mech3.exe 1.2 @ 0x0040a910
void HelpButton::update(float dt) {
  blink(dt);
  ShellButton::update(dt);
}

// Mech3.exe 1.2 @ 0x0044ae80
void HelpButton::on_enter() {
  if (selected || !enabled) return;
  Control::on_enter();
  set_blink(true);
  for (ztwod::Text* t : help) t->show_for(-1.0f);
}

// Mech3.exe 1.2 @ 0x0044aee0
void HelpButton::on_leave() {
  Control::on_leave();
  for (ztwod::Text* t : help) t->hide();
  set_blink(false);
}

// Mech3.exe 1.2 @ 0x0044ac90: HELP's labels join the dialog, hidden.
bool HelpButton::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  reading = 1;
  const bool r = Control::read_control(node, dialog);
  reading = 0;
  zrdr::get_ivec2(node, "GLOW_OFFSET", glow);
  if (zrdr::Node* h = zrdr::find(node, "HELP")) {
    int32_t count = 0;
    ztwod::Text** texts = ztwod::read_labels(dialog, h, &count, &base_x);
    if (count) {
      for (int32_t i = 0; i < count; ++i) {
        dialog->add(texts[i]);
        help.push_back(texts[i]);
      }
      for (ztwod::Text* t : help) t->hide();
    }
  }
  return r;
}

// Mech3.exe 1.2 @ 0x0044ae10
void HelpButton::set_blink(bool on) {
  blinking = on ? 1 : 0;
  dirty();
  if (on) return;
  if (selected) set_image(img_activate);
  else if (enabled) set_image(img_normal);
  else set_image(img_disabled);
}

// Mech3.exe 1.2 @ 0x0044af20: the time between swaps is in seconds already.
void HelpButton::blink(float dt) {
  if (!blinking) return;
  timer -= dt;
  if (timer > 0.0f) return;
  timer = period;
  set_image(image == img_normal ? img_activate : img_normal);
  dirty();
}

// ---- the cycle button ----

// Mech3.exe 1.2 @ 0x0048b280: the ShellButton's id stays -1; `id` is the cycle's.
CycleButton::CycleButton(Engine& e, int32_t id) : ShellButton(e, -1), cycle_id(id) {}

// Mech3.exe 1.2 @ 0x0048b380
CycleButton::~CycleButton() {
  clear_items();
  if (!labels.empty()) labels[0]->set_text_plain("deleted cycle button");
}

// Mech3.exe 1.2 @ 0x0048b9b0
void CycleButton::set_pos(int32_t px, int32_t py) {
  if (!labels.empty()) {
    ztwod::Text* l = labels[0];
    const int32_t lx = l->get_x(), x0 = get_x(), ly = l->get_y(), y0 = get_y();
    l->set_pos(lx - x0 + px, ly - y0 + py);
  }
  Image::set_pos(px, py);
  prev.set_pos(prev_dx + px, prev_dy + py);
  next.set_pos(next_dx + px, next_dy + py);
}

namespace {
// 0x0048b670: the dialog's cursor position.
void cursor_xy(ztwod::Layer* owner, int32_t* x, int32_t* y) {
  auto* w = dynamic_cast<ztwod::Window*>(owner);
  // (the original reads through the owner's cursor unchecked; every dialog
  // the game loads has one)
  if (!w || !w->cursor) {
    *x = *y = 0;
    return;
  }
  *x = w->cursor->get_x();
  *y = w->cursor->get_y();
}
}  // namespace

// Mech3.exe 1.2 @ 0x0048b550: rolling onto the right arrow wins over the left.
void CycleButton::update(float dt) {
  int32_t mx, my;
  cursor_xy(owner, &mx, &my);
  const bool in_next = next.hit(mx, my) == 1;
  const bool in_prev = prev.hit(mx, my) == 1;
  if ((hover == 1) != in_next) {
    if (in_next) {
      next.on_enter();
      hover = 1;
    } else {
      hover = 0;
      next.on_leave();
    }
  } else if ((hover == 2) != in_prev) {
    if (in_prev) {
      prev.on_enter();
      hover = 2;
    } else {
      hover = 0;
      prev.on_leave();
    }
  }
  Object2D::update(dt);
  prev.update(dt);
  next.update(dt);
}

// Mech3.exe 1.2 @ 0x0048b6b0: the left arrow steps back, the right one (or,
// with click_steps, anywhere else) forward; the screen hears of it either way.
void CycleButton::on_left_up() {
  int32_t mx, my;
  cursor_xy(owner, &mx, &my);
  if (prev.enabled && prev.hit(mx, my)) {
    prev.on_left_up();
    step(-1);
    notify(1, 0);
    return;
  }
  if ((next.enabled && next.hit(mx, my)) || click_steps) {
    next.on_left_up();
    step(1);
  }
  notify(1, 0);
}

// Mech3.exe 1.2 @ 0x0048b950
int32_t CycleButton::hit(int32_t px, int32_t py) {
  if (Control::hit(px, py) || prev.hit(px, py)) return 1;
  return next.hit(px, py) ? 1 : 0;
}

// Mech3.exe 1.2 @ 0x0048b4e0
void CycleButton::show_for(float seconds) {
  Control::show_for(seconds);
  prev.show_for(seconds);
  next.show_for(seconds);
}

// Mech3.exe 1.2 @ 0x0048b520
void CycleButton::hide() {
  Control::hide();
  prev.hide();
  next.hide();
}

// Mech3.exe 1.2 @ 0x0048be70: TOGGLE_LEFT and TOGGLE_RIGHT read as controls,
// their places kept and taken out of the dialog (the button draws them).
bool CycleButton::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  reading = 1;
  const bool r = Control::read_control(node, dialog);
  reading = 0;
  if (zrdr::Node* t = zrdr::find(node, "TOGGLE_LEFT")) {
    prev.read_control(t, dialog);
    prev.after_bind();
    prev_dx = prev.get_x();
    prev_dy = prev.get_y();
    dialog->remove(&prev);
  }
  if (zrdr::Node* t = zrdr::find(node, "TOGGLE_RIGHT")) {
    next.read_control(t, dialog);
    next.after_bind();
    next_dx = next.get_x();
    next_dy = next.get_y();
    dialog->remove(&next);
  }
  const int32_t py = get_y();
  set_pos(get_x(), py);
  return r;
}

// Mech3.exe 1.2 @ 0x0048bf60: no backgrounds, every flag but kHidden cleared
// (drawn every frame).
void CycleButton::after_bind() {
  prev.set_background(nullptr, nullptr);
  next.set_background(nullptr, nullptr);
  flags &= ztwod::kHidden;
  prev.flags &= ztwod::kHidden;
  next.flags &= ztwod::kHidden;
}

// Mech3.exe 1.2 @ 0x0042fbc0
void CycleButton::notify(int32_t a, int32_t b) {
  if (!owner) return;
  if (auto* s = dynamic_cast<MenuScreen*>(owner)) s->on_button(cycle_id, a, b);
}

// Mech3.exe 1.2 @ 0x0048b490
void CycleButton::set_baked(bool on) { set_enabled(on ? 1 : 0); }
void CycleButton::set_enabled(int32_t on) {
  enabled = on;
  refresh();
  prev.enabled = on;
  prev.refresh();
  next.enabled = on;
  next.refresh();
}

// Mech3.exe 1.2 @ 0x0048bd40
void CycleButton::set_arrows(uint8_t prev_on, uint8_t click_on, uint8_t next_on) {
  click_steps = click_on;
  prev.enabled = prev_on;
  prev.refresh();
  next.enabled = next_on;
  next.refresh();
}

// Mech3.exe 1.2 @ 0x0048bcf0
void CycleButton::set_arrows(uint8_t prev_on, uint8_t next_on) {
  prev.enabled = prev_on;
  prev.refresh();
  next.enabled = next_on;
  next.refresh();
}

// Mech3.exe 1.2 @ 0x0048ba80
void CycleButton::set_label_font(Handle font) {
  if (!labels.empty() && font) labels[0]->set_font(font);
}

// Mech3.exe 1.2 @ 0x0048b850
void CycleButton::select(int32_t i) {
  index = i;
  if (i >= 0 && i < count()) {
    if (!labels.empty()) labels[0]->set_text_plain(items[static_cast<size_t>(i)]->text);
    return;
  }
  if (!labels.empty()) labels[0]->set_text_plain("");
}

// Mech3.exe 1.2 @ 0x0048b790
void CycleButton::step(int32_t by) {
  int32_t i = index + by;
  index = i;
  const int32_t n = count();
  if (i < 0) {
    i = wraps ? n + i : 0;
  } else if (i >= n) {
    i = wraps ? i - n : n - 1;
  } else {
    select(index);
    return;
  }
  index = i;
  select(index);
}

// Mech3.exe 1.2 @ 0x0048bac0: the first choice added is selected at once.
int32_t CycleButton::add_item(const char* text, int32_t value) {
  const int32_t old = count();
  auto* it = new Item{text ? ::strdup(text) : nullptr, value};
  items.push_back(it);
  if (index < 0) step(-index);
  return old;
}

// Mech3.exe 1.2 @ 0x0048bda0
void CycleButton::clear_items() {
  for (Item*& it : items) {
    if (it) {
      std::free(it->text);
      delete it;
    }
    it = nullptr;
  }
  items.clear();
  if (!labels.empty()) labels[0]->set_text_plain("");
  index = -1;
}

// Mech3.exe 1.2 @ 0x00438e10
CycleButton::Item* CycleButton::item(int32_t i) {
  if (i < 0 || i >= count()) return nullptr;
  return items[static_cast<size_t>(i)];
}

// Mech3.exe 1.2 @ 0x00438e50
int32_t CycleButton::value_at(int32_t i) {
  Item* it = item(i);
  return it ? it->value : 0;
}

// Mech3.exe 1.2 @ 0x0048b8f0
int32_t CycleButton::select_value(int32_t v) {
  for (int32_t i = 0; i < count(); ++i) {
    if (items[static_cast<size_t>(i)]->value == v) {
      select(i);
      return i;
    }
  }
  return -1;
}

// Mech3.exe 1.2 @ 0x0043a710
void CycleButton::select_each_value(int32_t v) {
  for (int32_t i = 0; i < count(); ++i)
    if (value_at(i) == v) select(i);
}

// Mech3.exe 1.2 @ 0x00437fe0: OFF / ON (messages 0x20, 0x21), green or red.
void AlertCycle::after_bind() {
  CycleButton::after_bind();
  add_item(e_.message(0x20), 0);
  add_item(e_.message(0x21), 1);
  font_normal = ztwod::host().font_find("ia_small_green");
  font_alert = ztwod::host().font_find("ia_small_red");
}

// Mech3.exe 1.2 @ 0x00438040
void AlertCycle::select(int32_t i) {
  CycleButton::select(i);
  if (i == alert) {
    if (font_alert) set_label_font(font_alert);
  } else if (font_normal) {
    set_label_font(font_normal);
  }
}

// Mech3.exe 1.2 @ 0x00438ed0
void OnOffCycle::after_bind() {
  CycleButton::after_bind();
  add_item(e_.message(0x20), 0);
  add_item(e_.message(0x21), 1);
}

// Mech3.exe 1.2 @ 0x00438f10
void LevelCycle::after_bind() {
  CycleButton::after_bind();
  add_item(e_.message(0x40), 0);
  add_item(e_.message(0x39), 1);
  add_item(e_.message(0x38), 2);
}

// ---- the file list ----

namespace {
ztwod::Object2D* cursor_of(ztwod::Layer* owner) {
  auto* w = dynamic_cast<ztwod::Window*>(owner);
  return w ? w->cursor : nullptr;
}
bool shown(const ztwod::Object2D* o) { return !(o->flags & ztwod::kHidden); }
}  // namespace

// Mech3.exe 1.2 @ 0x0048bfe0
FileRow::FileRow(FileList* l, Handle font) : list(l) {
  FontText::set_font(font);
  flash_rate = 0.01f;
}

// Mech3.exe 1.2 @ 0x0048c070
void FileRow::update(float dt) {
  if (flags & ztwod::kHidden) return;
  if (attached) attached->update(dt);
  Text::update(dt);
}

// Mech3.exe 1.2 @ 0x0048e520
void FileRow::on_left_up() { list->select(index); }

// Mech3.exe 1.2 @ 0x0048e550
void FileRow::on_enter() {
  if (attached) attached->on_enter();
}

// Mech3.exe 1.2 @ 0x0048e560
void FileRow::on_leave() {
  if (attached) attached->on_leave();
}

// Mech3.exe 1.2 @ 0x0048c0b0
void FileRow::flash(bool on, Handle font) {
  if (!on) {
    stop_flash();
    return;
  }
  if (font) set_flash(ztwod::host().font_fields(font).color_top, 0.0f);
}

// Mech3.exe 1.2 @ 0x0048e4e0 (UP), 0x0048e500 (DOWN)
void FileListArrow::on_left_up() {
  Control::on_left_up();
  list->scroll(by);
}

// Mech3.exe 1.2 @ 0x0048c0e0
FileList::FileList(Engine& e, int32_t i) : ShellButton(e, i) {}

// Mech3.exe 1.2 @ 0x0048d880, with 0x0048def0 (no kinds) and 0x0048df60.
StoreList::StoreList(Engine& e, Handle s, int32_t k) : FileList(e, -1), store(s) {
  if (k == 5) {
    for (uint8_t& b : kinds) b = 1;
    all = 1;
  } else {
    kinds[k] = 1;
  }
}

// Mech3.exe 1.2 @ 0x0048df90
void StoreList::update(float dt) {
  FileList::update(dt);
  if (x20d) title.show_for(-1.0f);
  else title.hide();
  title.update(dt);
}

// Mech3.exe 1.2 @ 0x0048db30
bool StoreList::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  const bool r = FileList::read_control(node, dialog);
  title_font = ztwod::host().font_find(zrdr::get_string(node, "STATIC_FONT"));
  return r;
}

// Mech3.exe 1.2 @ 0x0048db70: filled, its title under the DOWN arrow.
void StoreList::after_bind() {
  FileList::after_bind();
  refill();
  if (font_normal && down) {
    title.set_font(font_normal);
    title.set_pos(get_x(), down->get_y() + 0xc);
  }
  title.hide();
}

// Mech3.exe 1.2 @ 0x0048df00
bool StoreList::can_light(int32_t i) {
  return i >= 0 && i < item_count() && items[static_cast<size_t>(i)]->value != 0;
}

// Mech3.exe 1.2 @ 0x0048ccf0
void StoreList::layout() {
  FileList::layout();
  const size_t n = title_font && !rows.empty() ? rows.size() : 0;
  for (size_t i = 0; i < n; ++i) {
    FileRow* r = rows[i];
    if (r->flags & ztwod::kHidden) continue;
    r->set_font(can_light(r->index) ? font_normal : title_font);
  }
}

// The engine's defaults: stores and items get handles from a table of their
// own (0x5f000000 up), as the exe's pointers would be.
namespace {
std::map<const void*, Handle>& handles() {
  static std::map<const void*, Handle> m;
  return m;
}
std::map<Handle, const void*>& objects() {
  static std::map<Handle, const void*> m;
  return m;
}
Handle handle_for(const void* p) {
  if (!p) return 0;
  auto f = handles().find(p);
  if (f != handles().end()) return f->second;
  const Handle h = 0x5f000000u + static_cast<Handle>(handles().size()) * 0x10u;
  handles()[p] = h;
  objects()[h] = p;
  return h;
}
}  // namespace
Store* Engine::store_of(Handle h) {
  auto f = objects().find(h);
  return f == objects().end() ? nullptr : static_cast<Store*>(const_cast<void*>(f->second));
}
Handle Engine::store_handle(Store* s) { return handle_for(s); }
Handle Engine::item_handle(const ConfigItem* it) { return handle_for(it); }
const ConfigItem* Engine::item_of(Handle h) {
  auto f = objects().find(h);
  return f == objects().end() ? nullptr : static_cast<const ConfigItem*>(f->second);
}

// Mech3.exe 1.2 @ 0x0048d950 (0x0048d960 on the selected index)
Handle StoreList::selected_value() {
  if (selected < 0 || selected >= item_count()) return 0;
  return static_cast<Handle>(items[static_cast<size_t>(selected)]->value);
}

// Mech3.exe 1.2 @ 0x0048d9a0: an item of `kind` the list takes (its kind
// listed, the filter's yes, a weapon's or equipment's tech in +0x210) as a
// row: its label, ammo "name (n rounds)" or "(unlimited)" for the store's
// own. The row of the item selected before the refill is kept in `found`.
void StoreList::add_row(const ConfigItem* item, int32_t kind, Handle previous, int32_t* found) {
  Engine& e = e_;
  const int32_t k = item->kind();
  const Handle h = e.item_handle(item);
  if (k < 0 || k > 4 || !kinds_or_all(k)) return;
  if (x200 && !e.store_list_filter(x200, h)) return;
  if (kind != 5 && k != kind) return;
  if (k == 1 && !(static_cast<const WeaponItem*>(item)->tech & x210)) return;
  if (k == 3 && !(static_cast<const EquipmentItem*>(item)->tech & x210)) return;
  auto msg = [&e](int32_t id) { return e.message(id); };
  std::string text;
  if (k == 2) {
    const std::string name = item_name(*item, msg);
    text = item->b10 ? e.message_format_sd(0x1c3, name.c_str(), 0, false)
                     : e.message_format_sd(0x1c4, name.c_str(), item->count, true);
  } else {
    text = item_label(*item, msg);
  }
  const int32_t i = add_item(text.c_str(), static_cast<int32_t>(h));
  if (h == previous) *found = i;
}

// Mech3.exe 1.2 @ 0x0048dbe0: the rows again, the selection kept on its item
// (else on its index).
void StoreList::refill() {
  Engine& e = e_;
  const Handle previous = selected_value();
  const int32_t old = selected;
  clear_items();
  layout();
  Store* s = store ? e.store_of(store) : nullptr;
  if (!s) return;
  int32_t found = -1;
  for (int32_t k = 0; k < 4; ++k) {
    if (!kinds[k]) continue;
    if (x209) {
      const char* h = e.message(0x294 + k);
      add_item(h ? h : "", 0);
    }
    for (const ConfigItem* it : s->items) add_row(it, k, previous, &found);
  }
  if (found == -1) found = old;
  if (x20d) {
    // The tonnage, summed in a float as the x87 stores it each time.
    float tons = 0.0f;
    for (const ConfigItem* it : s->items) tons = static_cast<float>(it->weight(-1) + tons);
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", static_cast<double>(tons));
    title.set_text_id(0x299, b);
  }
  select(found);
}

// Mech3.exe 1.2 @ 0x0048c250
FileList::~FileList() {
  clear_items();
  layout();
  delete selected_row;
  for (FileRow* r : rows) delete r;
  rows.clear();
  delete up;
  delete down;
  delete bar_image;
  delete roll_image;
}

void FileList::clear_items() {
  for (Item* it : items) {
    if (!it) continue;
    if (it->text) std::free(it->text);
    delete it;
  }
  items.clear();
}

// Mech3.exe 1.2 @ 0x0042fbf0
int32_t FileList::add_item(const char* text, int32_t value) {
  const int32_t old = item_count();
  items.push_back(new Item{strdup(text), value});
  layout();
  return old;
}

// Mech3.exe 1.2 @ 0x0048c410
bool FileList::read_control(zrdr::Node* node, ztwod::Dialog* dialog) {
  ztwod::Host& h = ztwod::host();
  reading = 1;
  Control::read_control(node, dialog);
  reading = 0;
  font_selected = h.font_find(zrdr::get_string(node, "SELECTED_FONT"));
  font_normal = h.font_find(zrdr::get_string(node, "NORMAL_FONT"));
  if (!font_selected) font_selected = font_normal;
  int32_t v[2];
  if (zrdr::get_ivec2(node, "SPACING", v)) spacing_x = v[0], spacing_y = v[1];
  if (zrdr::get_ivec2(node, "OFFSET", v)) offset_x = v[0], offset_y = v[1];
  zrdr::get_int(node, "SIZE", &size);
  if (const char* sn = zrdr::get_string(node, "ROLLOVER_SOUND")) sounds[0] = h.sound_find(sn);
  if (const char* sn = zrdr::get_string(node, "SELECT_SOUND")) sounds[1] = h.sound_find(sn);
  if (zrdr::Node* n = zrdr::find(node, "BAR")) {
    bar_image = new ztwod::Image(0);
    const char* name = zrdr::get_string(n, "BITMAP");
    if (zrdr::get_ivec2(n, "OFFSET", v)) bar_dx = v[0], bar_dy = v[1];
    bar_image->set_image(name);
  }
  if (zrdr::Node* n = zrdr::find(node, "ITEMROLLOVER")) {
    roll_image = new ztwod::Image(0);
    const char* name = zrdr::get_string(n, "BITMAP");
    if (zrdr::get_ivec2(n, "OFFSET", v)) roll_dx = v[0], roll_dy = v[1];
    roll_image->set_image(name);
    font_rollover = h.font_find(zrdr::get_string(node, "FONT"));  // looked up from the list's entry
  }
  // The arrows are placed from the list and taken out of the dialog: the list updates them.
  for (int k = 0; k < 2; ++k) {
    zrdr::Node* n = zrdr::find(node, k ? "DOWN" : "UP");
    if (!n) continue;
    FileListArrow*& a = k ? down : up;
    a = new FileListArrow(e_, this, k ? 1 : -1);
    a->read_control(n, dialog);
    a->after_bind();
    const int32_t ay = a->get_y() + get_y();
    a->set_pos(a->get_x() + get_x(), ay);
    dialog->remove(a);
  }
  place_at(this);
  return true;
}

// Mech3.exe 1.2 @ 0x0048c7f0
void FileList::after_bind() {
  set_rows(size);
  selected_row = new FileRow(this, font_selected);
  set_pos(x, y);
}

// Mech3.exe 1.2 @ 0x0048d770
void FileList::set_rows(int32_t n) {
  size = n;
  while (static_cast<int32_t>(rows.size()) < size) rows.push_back(new FileRow(this, font_normal));
  while (static_cast<int32_t>(rows.size()) > size) {
    delete rows.back();
    rows.pop_back();
  }
}

// Mech3.exe 1.2 @ 0x0048d2c0
void FileList::set_pos(int32_t px, int32_t py) {
  Image::set_pos(px, py);
  layout();
}

// Mech3.exe 1.2 @ 0x0048d2f0
void FileList::place_at(ztwod::Image* o) {
  if (!o) return;
  const int32_t oy = o->get_y();
  set_pos(o->get_x(), oy);
  rect.left = o->get_x();
  rect.top = o->get_y();
  rect.right = rect.left + (o->image ? o->image->width : 0);
  rect.bottom = rect.top + (o->image ? o->image->height : 0);
}

// Mech3.exe 1.2 @ 0x0048cda0: the rows from `top` down from OFFSET, SPACING
// apart, the selected item in its own row over the BAR; the rectangle round
// the rows when the list has no image of its own.
void FileList::layout() {
  if (flags & ztwod::kHidden) return;
  const int32_t x0 = get_x() + offset_x;
  const int32_t y0 = get_y() + offset_y;
  int32_t n = item_count() - top;
  const int32_t nrows = static_cast<int32_t>(rows.size());
  if (n > nrows) n = nrows;
  else if (n < 0) n = 0;
  int32_t max_h = 0, max_w = 0;
  // FIX (bug): with no row shown the original's bottom is a stack slot never
  // written (the shipped popup's list has no image, so its rectangle is
  // garbage until it has items); here it is the top (docs/fixes.md).
  int32_t row_y = y0;
  if (selected_row) selected_row->hide();
  if (bar_image) bar_image->hide();
  for (int32_t i = 0; i < n; ++i) {
    FileRow* r = rows[static_cast<size_t>(i)];
    r->show_for(-1.0f);
    r->index = i + top;
    // FIX (bug): the original passes the name as the printf format, so a "%"
    // in a file name read arguments that are not there; shown as it is here.
    r->set_text("%s", items[static_cast<size_t>(i + top)]->text);
    row_y = i * spacing_y + y0;
    if (r->needs_render) r->render();
    if (max_w <= r->width) {
      if (r->needs_render) r->render();
      max_w = r->width;
    }
    if (max_h <= r->text_height()) max_h = r->text_height();
    r->set_pos(x0 + spacing_x, row_y);
    if (i + top == selected && selected_row) {
      const int32_t sx = r->get_x();
      const int32_t sy = r->get_y();
      selected_row->show_for(-1.0f);
      selected_row->set_pos(sx, sy);
      selected_row->index = i + top;
      selected_row->set_text("%s", items[static_cast<size_t>(i + top)]->text);
      r->hide();
      if (bar_image) {
        bar_image->set_pos(bar_dx + sx, bar_dy + sy);
        bar_image->show_for(-1.0f);
      }
    }
  }
  for (int32_t i = n; i < nrows; ++i) rows[static_cast<size_t>(i)]->hide();
  if (!image) rect = ztwod::Rect{x0, y0, max_w + x0, row_y + max_h};
  dirty();
}

// Mech3.exe 1.2 @ 0x0048d0d0
void FileList::select(int32_t index) {
  if (index != -1 && (index < 0 || index >= item_count())) return;
  selected = index;
  if (index != -1) {
    if (index < top) {
      top = index;
    } else if (index > size - 1 + top) {
      const int32_t t = index - size + 1;
      top = t < 0 ? 0 : t;
    }
  }
  notify(10, index);
  layout();
}

// Mech3.exe 1.2 @ 0x0048d160
void FileList::scroll(int32_t by) {
  top += by;
  if (top > item_count()) top = item_count();
  else if (top < 0) top = 0;
  layout();
}

// Mech3.exe 1.2 @ 0x0048d1c0. FIX (bug): the original indexes the rows by the
// item's index, past the rows for an item below them; such a row is skipped.
void FileList::set_row_font(int32_t index, Handle font) {
  if (index < 0 || index >= item_count()) return;
  if (index < static_cast<int32_t>(rows.size())) rows[static_cast<size_t>(index)]->set_font(font);
  layout();
}

// Mech3.exe 1.2 @ 0x0048d210
int32_t FileList::hit(int32_t px, int32_t py) {
  const bool in = shown(this) && px >= rect.left && px <= rect.right && py >= rect.top && py <= rect.bottom;
  const int32_t u = up ? up->hit(px, py) : 0;
  const int32_t d = down ? down->hit(px, py) : 0;
  return in || u || d;
}

// Mech3.exe 1.2 @ 0x0048d540: a shown row (or the thing it carries) under the
// point, else the selected row.
FileRow* FileList::row_at(int32_t px, int32_t py) {
  for (FileRow* r : rows) {
    if (!shown(r)) continue;
    if (r->hit(px, py)) return r;
    if (r->attached && r->attached->hit(px, py)) return r;
  }
  if (selected_row && shown(selected_row) && selected_row->hit(px, py)) return selected_row;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0048d500
FileRow* FileList::row_under_mouse() {
  ztwod::Object2D* c = cursor_of(owner);
  const int32_t cx = c->get_x();
  const int32_t cy = c->get_y();
  return row_at(cx, cy);
}

// Mech3.exe 1.2 @ 0x0048d600
bool FileList::over_up(int32_t px, int32_t py) { return up && shown(up) && up->enabled && up->hit(px, py); }
// Mech3.exe 1.2 @ 0x0048d650
bool FileList::over_down(int32_t px, int32_t py) {
  return down && shown(down) && down->enabled && down->hit(px, py);
}

// Mech3.exe 1.2 @ 0x0048d380
void FileList::stop_sound(int32_t i) {
  ztwod::host().sound_stop(playing[i]);
  playing[i] = 0;
}

// Mech3.exe 1.2 @ 0x0048d3b0
void FileList::play_sound(int32_t i, float volume) { playing[i] = ztwod::host().sound_play(sounds[i], volume); }

// Mech3.exe 1.2 @ 0x0048d3e0: the row under the mouse lights in FONT's colour
// with the rollover sound; leaving it stops the sound (not on leaving the list).
void FileList::on_drag(int32_t px, int32_t py) {
  if (!inside) return;
  FileRow* r = row_at(px, py);
  if (r == lit) return;
  if (lit) {
    stop_sound(0);
    lit->on_leave();
    lit->flash(false, 0);
  }
  if (r && can_light(r->index)) {
    play_sound(0, 1.0f);
    r->on_enter();
    lit = r;
    r->flash(true, font_rollover);
    dirty();
    return;
  }
  lit = nullptr;
  dirty();
}

// Mech3.exe 1.2 @ 0x0048d4a0
void FileList::on_enter() {
  Control::on_enter();
  inside = 1;
  dirty();
}

// Mech3.exe 1.2 @ 0x0048d4c0
void FileList::on_leave() {
  Control::on_leave();
  inside = 0;
  if (lit) lit->flash(false, 0);
  lit = nullptr;
  dirty();
}

// Mech3.exe 1.2 @ 0x0048d6a0: a row selects itself (the press shown, the
// screen told); off the rows, a press on an arrow, unless repeating.
void FileList::on_left_up() {
  if (FileRow* r = row_under_mouse()) {
    r->on_left_up();
    if (enabled) {
      Control::on_left_up();
      notify(1, 0);
    }
    return;
  }
  if (repeating) return;
  ztwod::Object2D* c = cursor_of(owner);
  const int32_t cx = c->get_x();
  const int32_t cy = c->get_y();
  if (over_up(cx, cy)) {
    up->on_left_up();
    return;
  }
  if (over_down(cx, cy)) down->on_left_up();
}

// Mech3.exe 1.2 @ 0x0048d750
void FileList::on_right_up() {
  if (FileRow* r = row_under_mouse()) r->on_right_up();
}

// Mech3.exe 1.2 @ 0x0048c880
void FileList::show_for(float seconds) {
  Control::show_for(seconds);
  layout();
  if (up) up->show_for(seconds);
  if (down) down->show_for(seconds);
}

// Mech3.exe 1.2 @ 0x0048c8c0
void FileList::hide() {
  Control::hide();
  if (up) up->hide();
  if (down) down->hide();
  if (bar_image) bar_image->hide();
  if (roll_image) roll_image->hide();
}

// Mech3.exe 1.2 @ 0x0048c910: the arrows light under the cursor and repeat
// while the left button is held on them (after repeat_delay, then every
// repeat_rate seconds of the frame time); the rollover image follows the lit
// row; UP is enabled past the first item, DOWN while items are below.
void FileList::update(float dt) {
  if (flags & ztwod::kHidden) return;
  Object2D::update(dt);
  if (!inside) {
    repeating = 0;
    repeat_timer = repeat_delay;
  } else {
    ztwod::Object2D* c = cursor_of(owner);
    const int32_t cx = c->get_x();
    const int32_t cy = c->get_y();
    on_drag(cx, cy);
    const bool u = over_up(cx, cy);
    const bool d = over_down(cx, cy);
    if ((arrow_lit == 1) != u) {
      if (u) {
        up->on_enter();
        arrow_lit = 1;
      } else {
        arrow_lit = 0;
        up->on_leave();
      }
    } else if ((arrow_lit == 2) != d) {
      if (d) {
        down->on_enter();
        arrow_lit = 2;
      } else {
        arrow_lit = 0;
        down->on_leave();
      }
    }
    const ztwod::MouseState m = ztwod::host().mouse();
    if (!no_repeat && m.left == 2) {
      repeat_timer -= dt;
      if (!repeating) {
        repeating = repeat_timer < 0.0f ? 1 : 0;
      } else if (repeat_timer < 0.0f) {
        repeat_timer = repeat_rate;
        if (u) up->on_left_up();
        else if (d) down->on_left_up();
        else repeating = 0;
      }
    } else {
      repeating = 0;
      repeat_timer = repeat_delay;
    }
  }
  if (bar_image) bar_image->update(dt);
  if (roll_image) {
    if (lit && can_roll(lit->index)) {
      const int32_t ly = lit->get_y() + roll_dy;
      roll_image->set_pos(lit->get_x() + roll_dx, ly);
      roll_image->show_for(-1.0f);
    } else {
      roll_image->hide();
    }
    roll_image->update(dt);
  }
  for (size_t i = 0; i < rows.size(); ++i) rows[i]->update(dt);
  if (selected_row) selected_row->update(dt);
  if (up) {
    up->enabled = top != 0;
    up->refresh();
    up->update(dt);
  }
  if (down) {
    const int32_t nrows = static_cast<int32_t>(rows.size());
    const int32_t below = top + nrows < item_count() ? top + nrows : item_count();
    down->enabled = below < item_count();
    down->refresh();
    down->update(dt);
  }
}

// ---- the game's edit bar ----

// Mech3.exe 1.2 @ 0x0048b220
void ShellEditBar::set_pos(int32_t px, int32_t py) {
  Image::set_pos(px, py);
  if (!labels.empty()) labels[0]->set_pos(label_x + px, label_y + py);
}

// Mech3.exe 1.2 @ 0x0042f890
ztwod::Rect* ShellEditBar::get_rect() { return reading ? nullptr : Control::get_rect(); }

// Mech3.exe 1.2 @ 0x0042f860
bool ShellEditBar::read_control(zrdr::Node* n, ztwod::Dialog* d) {
  reading = e_.fast_menu() ? 0 : 1;
  const bool r = Control::read_control(n, d);
  reading = 0;
  return r;
}

// Mech3.exe 1.2 @ 0x0048b1d0
void ShellEditBar::after_bind() {
  if (labels.empty()) return;
  label_x = labels[0]->get_x();
  label_y = labels[0]->get_y();
}

}  // namespace mech3
