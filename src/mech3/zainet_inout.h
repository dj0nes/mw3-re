#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace zcls { struct Node; }
namespace zmod { struct Polygon; }

// mech3\ZAiNetMechInOut.cpp (0x004c11f0..0x004c3ed0): the mission boundary
// (docs/ai.md section 11). At mission load the world's `inout` polygons
// become areas, one per mission of the operation (or one for Instant Action
// and training), and the `inout` subtree is deleted; in play the AI's
// destinations are kept out of the current area's tested polygons (0x004c3000).
namespace mech3::ai {

struct InOutArea;
struct InOutQuery;

// A polygon's corner (0x1c bytes): its place, the place pushed out by the
// polygon's width along the corner's bisector (y 0), and the length of the
// side to the next corner.
struct InOutVertex {
  float pos[3] = {};     // +0x00
  float offset[3] = {};  // +0x0c
  float length = 0;      // +0x18
};

// 0x004c1ab0 (0x40 bytes, vtable 0x005cac6c): one boundary polygon, classed
// by its material's colour. Red (kind 0) and blue (2) are tested by the query,
// yellow (1) and cyan (3) are not; blue and cyan are wound the other way and
// count crossings the other way round.
struct InOutPolygon {
  // ECX this; name, the model polygon, its vertices transformed, the width.
  InOutPolygon(const char* name, const zmod::Polygon& poly, const float* verts, float width);
  ~InOutPolygon();  // 0x004c1ff0
  void cross(InOutArea& area) const;  // 0x004c2080: when tested, its sides against the area's query
  bool blocks(const float* from, const float* p) const;  // 0x004c25f0: a side crosses from -> p
  const InOutVertex* way_round(const InOutQuery& q) const;  // 0x004c2f20: near_a, or near_b the shorter way
  // 0x004c2470 over 0x004c22c0: the point of the pushed-out outline nearest
  // `from` into `out`, and its side's corners.
  void nearest(const float* from, float out[3], const InOutVertex** a, const InOutVertex** b) const;

  std::vector<InOutVertex> verts;  // +0x04
  float perimeter = 0;             // +0x14
  float width = 0;                 // +0x18  3.0 (0x005e8a18)
  float min[3], max[3];            // +0x1c, +0x28  FLT_MAX, -FLT_MAX before the corners
  uint8_t inverted = 0;            // +0x34  blue, cyan
  uint8_t tested = 0;              // +0x35  red, blue
  int32_t kind = 0;                // +0x38  0 red, 1 yellow, 2 blue, 3 cyan (0 for any other colour)
  char* name = nullptr;            // +0x3c  strdup'd: the node's name past "inoutXX"
};

// A query's state (0x4c bytes; its fresh copy is the template at 0x0060b348,
// set at start-up by 0x004c1a00): the segment from -> to against the
// polygons' sides. A polygon's own pass (0x004c2080) fills one, the area
// keeps the nearest crossing's.
struct InOutQuery {
  const float* from = nullptr;                       // +0x00
  const float* to = nullptr;                         // +0x04
  InOutPolygon* poly = nullptr;                      // +0x08  the nearest crossing's polygon
  const InOutVertex *near_a = nullptr, *near_b = nullptr;  // +0x0c, +0x10  the side crossed first
  const InOutVertex *far_a = nullptr, *far_b = nullptr;    // +0x14, +0x18  the side crossed last
  float length = 0;                                  // +0x1c  from -> to in x and z
  float t_near = 3.4028234663852886e38f;             // +0x20  FLT_MAX
  float t_far = 0;                                   // +0x24
  float near_point[3] = {};                          // +0x28  short of the first crossing by the width
  float far_point[3] = {};                           // +0x34  past the last one by the width
  int32_t crossings = 0;                             // +0x40
  int32_t from_rays = 0;                             // +0x44  sides the ray from `from` through `to` meets
  int32_t to_rays = 0;                               // +0x48  sides the ray from `to` back through `from` meets
};

// One mission's boundary (0x88 bytes, vtable 0x005cac70, ctor 0x004c26b0):
// its polygons, the query's state, a box (FLT_MAX / -FLT_MAX, never set) and
// the last answer.
struct InOutArea {
  InOutArea();
  std::vector<std::unique_ptr<InOutPolygon>> polygons;  // +0x04
  InOutQuery q;                                         // +0x14
  float min[3], max[3];                                 // +0x60, +0x6c
  uint8_t from_in = 0, to_in = 0;  // +0x78, +0x79  inside the nearest polygon (outside, for blue and cyan)
  float normal[3] = {};            // +0x7c  of the side nearest `from` when it is inside
  // Mech3.exe 1.2 @ 0x004c2b00 (ECX area): where the way from -> to may go:
  // `to` itself, or a point in this area (or a polygon's pushed-out corner).
  const float* query(const float* from, const float* to);
  bool clear(const float* from, const float* p) const;  // 0x004c2e30: no polygon's side crosses from -> p
};

// The variant of the `inout` node a game reads (0x004c36c0, 0x20 bytes on
// the stack): campaign "inoutm" + the mission number, one area per mission of
// the operation; Instant Action "inoutia" and training "inoutt", one area.
// Multiplayer has none.
struct InOutVariant {
  int32_t operation = 0;  // +0x00  vt+0x48
  int32_t index = 0;      // +0x04  the mission number - 1 (campaign), else 0
  int32_t count = 0;      // +0x08  the operation's missions (0x00484840), else 1
  int32_t game = -1;      // +0x0c  vt+0x2c
  int32_t prefix = 0;     // +0x10  the name's length
  uint8_t numbered = 0;   // +0x14
  uint8_t valid = 0;      // +0x15
  char name[9] = {};      // +0x16
  int32_t match(const char* child) const;  // 0x004c3850: its area, -1 for none
};
InOutVariant inout_variant();  // 0x004c36c0

// What the boundary needs from modules not ported here; the program sets them.
struct InOutHooks {
  bool (*have_game)() = nullptr;                 // [0x0060861c] set
  int32_t (*operation)() = nullptr;              // [0x0060861c]+0x6c vt+0x48
  int32_t (*game_type)() = nullptr;              // vt+0x2c
  int32_t (*mission_number)() = nullptr;         // vt+0x4c
  // [0x00608c84]: with a world, each corner is put on the ground under it
  // (0x004c4240: 0x0041eb90 from 100 above, 0x004abd90 within 20). None: kept.
  void (*ground)(float pos[3]) = nullptr;
};
InOutHooks& inout_hooks();

// The areas read at the mission load ([0x0060b338]+4 = [0x0060b33c], a
// vector of 0x88-byte areas) and the current mission's ([0x0060b344]).
struct InOut {
  std::unique_ptr<std::vector<InOutArea>> areas;  // [0x0060b33c]
  InOutArea* current = nullptr;                   // [0x0060b344]
};
InOut& inout();

// 0x004c3da0 (the listener's slot +0x08) -> 0x004c3930: the world's `inout`
// read into the areas (none without the node or a variant), then the node
// taken off its parents and deleted with its subtree.
void inout_mission_load();
void inout_select();       // 0x004c3b80 (0x00490bde): the current mission's area
void inout_mission_end();  // 0x004c3dd0 (the listener's slot +0x10): the areas freed

// Mech3.exe 1.2 @ 0x004c3000 (ECX the vehicle, EDX `in`, the stack `out`):
// with a current area, `in` as the vehicle at `pos` may reach it; true with
// `out` the place it is pulled to (at `in`'s height). The exe also keeps a
// record of each query on the vehicle (+0xd8, 0x004c0ed0 / 0x004c0f70) that
// nothing reads; not kept.
bool inout_pull(const float pos[3], const float in[3], float out[3]);
// Mech3.exe 1.2 @ 0x004c3bd0 (ECX the animation, EDX the AI message; from
// 0x004d4a20): messages 0x39..0x3c test, of the current area's polygons named
// as the animation's node, only the yellow, red, cyan or blue ones.
void inout_set_state(const char* name, int32_t message);
int32_t inout_changes();  // ours (W58): counts the messages that switch shapes, and new areas

// 0x004c3e50 with 0x004c3600 -> 0x004c3380: a node's subtree walked through
// the matrix stack (0x004c3ed0), each model's polygons into the area.
void inout_read_tree(zcls::Node* node, InOutArea& area);

}  // namespace mech3::ai
