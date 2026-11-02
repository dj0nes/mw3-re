// mech3's controls setup on the real zInput map, and the fixes the
// differential test cannot reach (the original crashes or hangs there).
#include <cstdio>
#include <cstring>
#include <string>

#include "check.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/controls_cfg.h"

namespace {
const std::string kDir = std::string(MW3_TEST_TMP) + "/test_controls";

void fresh() {
  zin::init(0xa7, "DefaultCmdMap");
  mech3::controls_setup() = mech3::ControlsSetup{};
  mech3::controls_setup_calls() = mech3::ControlsSetupCalls{};
  mech3::axis_table() = mech3::AxisTable{};
  zutl::mounts().read.clear();
  zutl::set_write_dir(kDir);
}

void write(const char* path, const std::string& text) {
  std::FILE* f = zutl::open(path, "wb");
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
}

std::string parse(const std::string& text) {
  write("parse.zrd", text);
  zrdr::Node* root = zrdr::load("parse.zrd");
  if (!root) return "null";
  std::string out;
  for (int32_t i = 1; i < root->list[0].i; ++i) {
    const zrdr::Node& n = root->list[i];
    out += n.type == zrdr::String ? n.s : n.type == zrdr::List ? "(list)" : "#";
    out += ' ';
  }
  zrdr::free_node(root);
  return out;
}
}  // namespace

TEST(controls_defaults_save_and_load_round_trip) {
  fresh();
  EXPECT(mech3::controls_set_defaults() == 1);
  EXPECT(zin::key1_of(0x3d) == 0x39 && zin::joy_of(0x3d) == 1 && zin::mouse_of(0x3d) == 1);  // fire: Space
  EXPECT(zin::key1_of(1) == 0x13e);       // Quick Exit: Alt-F4
  EXPECT(zin::key1_of(0x71) == 0x210);    // Exit Mission: Ctrl-Q
  EXPECT(zin::key2_of(2) == 0xd2 && zin::key2_of(3) == 0xc9);  // Insert, Page Up: also the jets' rotations
  EXPECT(zin::key1_of(0x11) == 0xd2 && zin::key1_of(0x12) == 0xc9);
  EXPECT(mech3::controls_group_count() == 7);
  EXPECT(mech3::controls_command("CMD_TRIGGER1") == 0x3d);
  EXPECT(mech3::controls_setup().commands[0x18].axis_key == 1);
  mech3::axis_table().set(1, 9, 1.0f, 0.0f, 0.0f);  // twist on the mouse
  mech3::controls_save("keys\\mine.zrd");
  zin::bind(0x3d, nullptr, 0x1c, 0, 0, 0);
  mech3::axis_table() = mech3::AxisTable{};
  mech3::controls_load("keys\\mine.zrd");
  EXPECT(zin::key1_of(0x3d) == 0x39 && zin::joy_of(0x3d) == 1);
  EXPECT(zin::key1_of(1) == 0);  // not in the file: unbound, as in the original
  EXPECT(mech3::axis_table().axes[1].source == 9 && mech3::axis_table().axes[0].source == 3);
  EXPECT(mech3::axis_table().ready == 0 && mech3::axis_table().stock == 0);
}

// Playtest W40: a map saved with the quit box up (mouse button 1 on nothing,
// TRIGGER1 without a mouse button) gets the button back on TRIGGER1; maps the
// player rebound on purpose stay as they are.
TEST(controls_repair_mouse_fire_only_mends_the_quit_boxs_damage) {
  fresh();
  mech3::controls_set_defaults();
  EXPECT(!mech3::controls_repair_mouse_fire());  // the defaults: nothing to mend
  zin::bind_mouse(0, 0x3d);                       // as the quit box (0x0048e2d0) unbinds it
  EXPECT(zin::mouse_of(0x3d) == 0 && zin::mouse_command(1) == 0);
  EXPECT(mech3::controls_repair_mouse_fire());
  EXPECT(zin::mouse_of(0x3d) == 1 && zin::mouse_command(1) == 0x3d);
  EXPECT(zin::mouse_of(0x17) == 2 && zin::key1_of(0x3d) == 0x39);  // the rest untouched
  zin::bind_mouse(2, 0x3d);  // fire moved to the right button: button 1 on nothing
  EXPECT(!mech3::controls_repair_mouse_fire());
  EXPECT(zin::mouse_of(0x3d) == 2 && zin::mouse_command(1) == 0);
  mech3::controls_set_defaults();
  zin::bind_mouse(1, 0x17);  // button 1 on free look: fire has none
  EXPECT(!mech3::controls_repair_mouse_fire());
  EXPECT(zin::mouse_of(0x3d) == 0 && zin::mouse_command(1) == 0x17);
}

TEST(controls_stock_axes_and_missing_files) {
  fresh();
  mech3::controls_set_defaults();
  mech3::controls_save("keys\\stock.zrd");  // the table as it starts: the joystick's defaults
  mech3::controls_load("keys\\stock.zrd");
  EXPECT(mech3::axis_table().stock == 1);
  mech3::controls_load("keys\\nothing.zrd");  // no file: nothing changes
  EXPECT(mech3::axis_table().stock == 1 && zin::key1_of(0x3d) == 0x39);
}

TEST(controls_file_fixes_skip_what_crashed_the_original) {
  fresh();
  mech3::controls_set_defaults();
  write("keys\\bad.zrd",
        "( KEYS ( 5 (7 keya(0x10)) () (CMD_JJ_UP keya(0x24)) )\n"
        "  AXES ( (Yaw joystick(Y) slope(2.0) deadzone(0.2)) (LR mouse(X)) x ) )\n");
  mech3::controls_load("keys\\bad.zrd");
  EXPECT(zin::key1_of(0xc) == 0x24);
  EXPECT(mech3::controls_group_count() == 7);  // the original wrote the Yaw entry over the groups' list
  EXPECT(mech3::axis_table().axes[3].source == 9);
  EXPECT(mech3::axis_table().axes[2].source == 0);
}

TEST(zreader_text_files) {
  fresh();
  EXPECT(parse("(a ; comment (\n b \"c d\" 5 e\"f\"g)") == "(list) ");
  EXPECT(parse("a (b") == "null");
  EXPECT(parse("x\n#ifdef DEBUG\ny\n#else\nz\n#endif\nw") == "x z ");  // w: the end of file drops it
  // FIX (bug): a token of 256 characters or more is dropped (the original
  // overwrote its return address), an open quote or #ifdef ends at the end.
  EXPECT(parse(std::string(300, 'q') + " k ") == "");  // the list ends there
  EXPECT(parse("a \"open") == "a ");
  EXPECT(parse("a\n#ifdef X\nb c ") == "a ");
  zrdr::state().if_depth = 0;
}
