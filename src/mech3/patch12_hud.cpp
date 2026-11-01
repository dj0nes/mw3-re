// The 1.2 data layer's hud.zrd (docs/patch12.md): 1.2 adds blocks for
// 1152x864, 1280x1024 and 1600x1200. Each of their 34 values is the
// 1024x768 block's moved with the screen edge it hangs from, so they are made
// from 1.0's own block.
#include "mech3/patch12.h"
#include "mech3/patch12_tree.h"

#include <vector>

namespace mech3::patch12 {
namespace {

using zrdr::Node;

// What moves with the screen: x by the extra width (1) or half of it (2,
// the centred compass), y by the extra height (1). Everything else stays.
struct Anchor {
  const char *group, *key;
  int x, y;
};
constexpr Anchor kAnchors[] = {
    {"MFD", "position", 1, 1},      {"TARGET", "position", 0, 1},  {"WEAPONS", "position", 1, 0},
    {"EQUIPMENT", "position", 1, 0}, {"COMPASS", "tape", 2, 0},     {"COMPASS", "frame", 2, 0},
    {"TIMER", "position", 1, 0},    {"GAUGES", "speed_frame", 1, 0}, {"GAUGES", "fuel_frame", 1, 0},
};

struct Mode {
  const char* name;
  int32_t w, h;
};
constexpr Mode kModes[] = {{"V1152x864", 1152, 864}, {"V1280x1024", 1280, 1024}, {"V1600x1200", 1600, 1200}};

void shift(Node* pair, int32_t dx, int32_t dy) {
  if (!pair || pair->type != zrdr::List || pair->list[0].i < 3) return;
  pair->list[1].i += dx, pair->list[2].i += dy;
}

}  // namespace

bool hud(Node* root) {
  if (!root || root->type != zrdr::List || root->list[0].i < 2) return false;
  Node* top = &root->list[1];
  Node* base = tree::value(top, "V1024x768");
  if (!base || tree::value(top, kModes[0].name)) return false;
  std::vector<Node> add;  // made before the list grows: `base` points into it
  for (const Mode& m : kModes) {
    Node block = tree::clone(*base);
    const int32_t dw = m.w - 1024, dh = m.h - 768;
    for (const Anchor& a : kAnchors)
      shift(tree::path(&block, {a.group, a.key}), a.x == 1 ? dw : a.x == 2 ? dw / 2 : 0, a.y ? dh : 0);
    add.push_back(tree::str(m.name)), add.push_back(block);
  }
  tree::insert(top, top->list[0].i, std::move(add));
  return true;
}

}  // namespace mech3::patch12
