// W58 (docs/lancemate-pathing.md): the walkability grid and its paths on a
// made-up world of flat ground, boxes for walls, a cliff and water.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "gamez/zutil/zfix.h"
#include "mech3/options.h"
#include "mech3/zai_nav.h"

namespace {
namespace nav = mech3::ai::nav;

struct Box {
  float x0, z0, x1, z1, top;
};
struct FakeWorld : nav::World {
  std::vector<Box> boxes;
  float cliff_x = 1e9f;          // a ridge 10 m up over x cliff_x..+20 (z in cliff_z0..cliff_z1)
  float cliff_z0 = 0, cliff_z1 = 0;
  float pool[4] = {0, 0, 0, 0};  // water 8 m deep over x0..x1, z0..z1
  float hole[4] = {0, 0, 0, 0};  // no ground
  // A walkway: 10 m up over x walk_x0..walk_x1, z walk_z0..walk_z1, with a
  // ramp down at its west end (from walk_x0 - 20).
  float walk_x0 = 1e9f, walk_x1 = 0, walk_z0 = 0, walk_z1 = 0;
  // A river: a channel 8 m deep over x river_x0..+40, its banks sheer, a ford
  // (ramps on both banks) at z ford_z..+20.
  float river_x0 = 1e9f, ford_z = 0;
  int grounds = 0, walls = 0;
  static bool in(const float r[4], float x, float z) { return x >= r[0] && x < r[1] && z >= r[2] && z < r[3]; }
  // Extra floors: a slab of ground 10 m thick... any place may have others.
  std::vector<std::array<float, 5>> decks;  // x0, z0, x1, z1, y: a floor over the ground
  float mine[5] = {1e9f, 0, 0, 0, 0};       // x0, z0, x1, z1, y: a floor under the ground
  bool mine_ramp = false;                   // its way up: x 500..680, z 60..100, from its floor to the ground
  std::vector<std::array<float, 5>> parked_now;
  float keep_out[4] = {0, 0, 0, 0};
  void parked(std::vector<std::array<float, 5>>* out) override { *out = parked_now; }
  bool avoid(float x, float, float z) override { return in(keep_out, x, z); }
  int ground(float x, float z, nav::Floor* out, int max) override {
    ++grounds;
    if (in(hole, x, z)) return 0;
    float y = (x >= cliff_x && x < cliff_x + 20 && z >= cliff_z0 && z < cliff_z1) ? 10.0f : 0.0f;
    float water = std::nanf("");
    if (in(pool, x, z)) water = 8.0f;
    if (z >= walk_z0 && z < walk_z1 && x >= walk_x0 - 20 && x < walk_x1)
      y = x >= walk_x0 ? 10.0f : (x - (walk_x0 - 20)) * 0.5f;
    if (x >= river_x0 && x < river_x0 + 40) {
      y = -8.0f, water = 0.0f;
      if (z >= ford_z && z < ford_z + 20) {
        const float in_x = std::min(x - river_x0, river_x0 + 40 - x);  // down 0.5 a metre from each bank
        y = std::max(-8.0f, -in_x * 0.5f);
      }
    }
    if (mine_ramp && x >= 500 && x < 680 && z >= 60 && z < 100) y = -90.0f + (x - 500) * 0.5f;
    int n = 0;
    for (const auto& d : decks)
      if (x >= d[0] && x < d[2] && z >= d[1] && z < d[3] && n < max) out[n].y = d[4], out[n].water = std::nanf(""), ++n;
    if (n < max) out[n].y = y, out[n].water = water, ++n;
    if (x >= mine[0] && x < mine[2] && z >= mine[1] && z < mine[3] && n < max) out[n].y = mine[4], out[n].water = std::nanf(""), ++n;
    return n;
  }
  // A segment against each box's sides (2D, below its top).
  bool wall(const float a[3], const float b[3]) override {
    ++walls;
    for (const Box& k : boxes) {
      if (a[1] > k.top && b[1] > k.top) continue;
      float t0 = 0, t1 = 1;
      const float d[2] = {b[0] - a[0], b[2] - a[2]}, o[2] = {a[0], a[2]};
      const float lo[2] = {k.x0, k.z0}, hi[2] = {k.x1, k.z1};
      bool hit = true;
      for (int i = 0; i < 2 && hit; ++i) {
        if (std::fabs(d[i]) < 1e-6f) {
          if (o[i] < lo[i] || o[i] > hi[i]) hit = false;
          continue;
        }
        float u = (lo[i] - o[i]) / d[i], v = (hi[i] - o[i]) / d[i];
        if (u > v) std::swap(u, v);
        t0 = std::max(t0, u), t1 = std::min(t1, v);
        if (t0 > t1) hit = false;
      }
      if (hit) return true;
    }
    return false;
  }
};

// A path, its tiles built as it asks.
nav::Find find_pumped(nav::Grid& g, const float a[3], const float b[3], nav::Way* p) {
  nav::Find f = nav::Find::kBuilding;
  for (int i = 0; i < 40 && f == nav::Find::kBuilding; ++i) {
    f = nav::find_path(g, a, b, p);
    while (!g.queue.empty()) {
      int budget = 1 << 30;
      const auto [tx, tz] = g.queue.front();
      g.build(tx, tz, &budget);
      g.queue.erase(g.queue.begin());
    }
  }
  return f;
}

void build_all(nav::Grid& g, int tx0, int tz0, int tx1, int tz1) {
  for (int tz = tz0; tz <= tz1; ++tz)
    for (int tx = tx0; tx <= tx1; ++tx) {
      int budget = 1 << 30;
      g.build(tx, tz, &budget);
    }
}

// Each leg of a path (from `from`) clear of the world's walls at a mech's body height.
bool path_clear(FakeWorld& w, const float from[3], const nav::Way& p) {
  float a[3] = {from[0], 4.0f, from[2]};
  for (const auto& q : p) {
    const float b[3] = {q[0], 4.0f, q[2]};
    if (w.wall(a, b)) return false;
    a[0] = b[0], a[2] = b[2];
  }
  return true;
}
float path_len(const float from[3], const nav::Way& p) {
  float n = 0, x = from[0], z = from[2];
  for (const auto& q : p) n += std::hypot(q[0] - x, q[2] - z), x = q[0], z = q[2];
  return n;
}
}  // namespace

TEST(nav_open_ground_is_one_straight_leg) {
  FakeWorld w;
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  const float a[3] = {60, 0, 60}, b[3] = {200, 0, 180};
  EXPECT(nav::line_clear(g, a, b));
  nav::Way p;
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  EXPECT(p.size() == 1 && p[0][0] == b[0] && p[0][2] == b[2]);  // the brain's point itself
}

TEST(nav_walks_round_a_wall_and_through_a_wide_gap_only) {
  FakeWorld w;
  // A wall 120 m long across the straight way (x 120..124, z 60..180).
  w.boxes.push_back({120, 60, 124, 180, 20});
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  const float a[3] = {80, 0, 120}, b[3] = {170, 0, 120};
  EXPECT(!nav::line_clear(g, a, b));
  nav::Way p;
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  EXPECT(p.size() >= 2 && path_clear(w, a, p));
  EXPECT(path_len(a, p) < 2.5f * 90.0f);  // round an end, about 60 m out and back
  // A 16 m gap at its middle (z 112..128) takes the way through it.
  w.boxes = {{120, 60, 124, 112, 20}, {120, 128, 124, 180, 20}};
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  EXPECT(path_clear(w, a, p) && path_len(a, p) < 100.0f);
  // A 6 m gap is narrower than a mech with the cell's margin: round the end again.
  w.boxes = {{120, 60, 124, 117, 20}, {120, 123, 124, 180, 20}};
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  EXPECT(path_clear(w, a, p) && path_len(a, p) > 130.0f);
}

TEST(nav_a_cliff_and_a_hole_are_walked_round_water_is_waded) {
  FakeWorld w;
  w.cliff_x = 120, w.cliff_z0 = 60, w.cliff_z1 = 180;  // 10 m up in one 4 m cell: past 35 degrees
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  const float a[3] = {80, 0, 120}, b[3] = {170, 0, 120};
  EXPECT(!nav::line_clear(g, a, b));
  nav::Way p;
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  bool round = false;  // the way passes beyond the cliff's ends
  for (const auto& q : p) round = round || q[2] < 64 || q[2] > 176;
  EXPECT(round);
  // No ground at all: the same.
  w.cliff_x = 1e9f;
  const float hole[4] = {110, 130, 40, 200};
  std::copy(hole, hole + 4, w.hole);
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  const float b2[3] = {170, 0, 120};
  EXPECT(nav::find_path(g, a, b2, &p) == nav::Find::kOk);
  round = false;
  for (const auto& q : p) round = round || q[2] < 44 || q[2] > 196;
  EXPECT(round);
  // A deep pool right across: crossed (costed, not blocked), its depth kept.
  std::fill(w.hole, w.hole + 4, 0.0f);
  const float pool[4] = {110, 130, 0, 512};
  std::copy(pool, pool + 4, w.pool);
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  EXPECT(g.cell(nav::cell_of(120), nav::cell_of(120), 0)->depth == 8.0f);
  EXPECT(nav::find_path(g, a, b2, &p) == nav::Find::kOk && path_len(a, p) < 100.0f);
}

TEST(nav_a_thin_walkway_is_reached_by_its_ramp) {
  FakeWorld w;
  // 8 m wide, 10 m up, 100 m long, its ramp at the west end; a drop both sides.
  w.walk_x0 = 120, w.walk_x1 = 220, w.walk_z0 = 116, w.walk_z1 = 124;
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  EXPECT(g.walkable(nav::cell_of(170), nav::cell_of(118), 0));  // on it: the drops need no margin
  EXPECT(!g.walkable(nav::cell_of(170), nav::cell_of(114), 0));  // beside it on the ground: it rises over a mech
  const float a[3] = {170, 0, 90}, b[3] = {200, 10, 118};  // from the ground beside it, to its far end
  nav::Way p;
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  bool by_the_ramp = false;
  for (const auto& q : p) by_the_ramp = by_the_ramp || q[0] < 120;
  EXPECT(by_the_ramp && p.back()[0] == b[0] && p.back()[2] == b[2]);
}

TEST(nav_a_river_is_crossed_at_its_ford_however_far) {
  FakeWorld w;
  w.river_x0 = 100, w.ford_z = 700;  // 580 m along from the straight way
  nav::Grid g;
  g.reset(&w);
  const float a[3] = {70, 0, 120}, b[3] = {170, 0, 120};
  nav::Way p;
  const nav::Find f = find_pumped(g, a, b, &p);
  EXPECT(f == nav::Find::kOk);
  bool ford = false;
  for (const auto& q : p) ford = ford || (q[2] > 690 && q[2] < 730);
  EXPECT(ford);
}

TEST(nav_floors_a_head_height_bridge_is_no_way_under_a_high_one_is) {
  FakeWorld w;
  nav::Grid g;
  // A deck 8 m up (a mech's head): the ground under it and beside it blocked.
  w.decks.push_back({120, 60, 140, 180, 8.0f});
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  EXPECT(!g.walkable(nav::cell_of(130), nav::cell_of(120), 1));  // under it
  EXPECT(!g.walkable(nav::cell_of(118), nav::cell_of(120), 0));  // against it
  const float a[3] = {90, 0, 120}, b[3] = {170, 0, 120};
  nav::Way p;
  EXPECT(!nav::line_clear(g, a, b));
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  bool round = false;
  for (const auto& q : p) round = round || q[2] < 64 || q[2] > 176;
  EXPECT(round);
  // One cell, nothing round it to rise: only the room under it says no.
  w.decks = {{120, 120, 124, 124, 8.0f}};
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  EXPECT(g.level_at(nav::cell_of(122), nav::cell_of(122), 0.0f) == 1);
  EXPECT(!g.walkable(nav::cell_of(122), nav::cell_of(122), 1));
  // The same deck 30 m up: walked under, straight.
  w.decks = {{120, 60, 140, 180, 30.0f}};
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  EXPECT(g.walkable(nav::cell_of(130), nav::cell_of(120), 1));
  EXPECT(nav::line_clear(g, a, b));
}

TEST(nav_floors_a_mine_under_the_ground_is_reached_by_its_ramp) {
  FakeWorld w;
  // A mine floor 90 m under the ground over x 60..500, z 60..200; its way in
  // a ramp at the far east end: the ground over z 60..80 falling to it.
  const float a[3] = {100, -90, 120}, b[3] = {100, 0, 120};  // under and over each other
  w.mine[0] = 60, w.mine[1] = 60, w.mine[2] = 500, w.mine[3] = 200, w.mine[4] = -90;
  w.decks.clear();
  nav::Grid g;
  g.reset(&w);
  build_all(g, -2, -2, 4, 4);
  const int cx = nav::cell_of(100), cz = nav::cell_of(120);
  EXPECT(g.level_at(cx, cz, -90) == 1 && g.level_at(cx, cz, 0) == 0);
  EXPECT(g.walkable(cx, cz, 1) && g.walkable(cx, cz, 0));
  nav::Way p;
  // No way between them yet: the floors do not join.
  EXPECT(find_pumped(g, a, b, &p) == nav::Find::kNone);
  // Over the mine's floor the walk stays on it, and over the ground on it.
  const float a2[3] = {300, -90, 120}, b2[3] = {300, 0, 120};
  EXPECT(nav::line_clear(g, a, a2) && nav::line_clear(g, b, b2));
  EXPECT(!nav::line_clear(g, a, b2));  // ends on another floor
  // With its ramp the way from the mine up to the place right over it runs
  // out to the ramp and back on top.
  w.mine_ramp = true;
  g.reset(&w);
  EXPECT(find_pumped(g, a, b, &p) == nav::Find::kOk);
  float far_east = 0;
  for (const auto& q : p) far_east = std::max(far_east, q[0]);
  EXPECT(far_east > 600 && p.back()[1] == 0.0f);
}

TEST(nav_tiles_build_within_their_budget_and_paths_wait_for_them) {
  FakeWorld w;
  nav::Grid g;
  g.reset(&w);
  const float a[3] = {60, 0, 60}, b[3] = {100, 0, 70};
  nav::Way p;
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kBuilding && !g.queue.empty());
  EXPECT(!g.built(nav::cell_of(60), nav::cell_of(60)));
  int budget = 100;  // a hundred places: no tile yet
  EXPECT(!g.build(0, 0, &budget) && budget == 0 && w.grounds == 100);
  EXPECT(!g.built(nav::cell_of(60), nav::cell_of(60)));  // a part-built tile reads as unknown
  int rounds = 1;
  for (; rounds < 100000; ++rounds) {
    budget = 100;
    if (g.build(0, 0, &budget)) break;
  }
  EXPECT(rounds > 1 && g.built(nav::cell_of(60), nav::cell_of(60)));
  EXPECT(w.grounds >= nav::kTile * nav::kTile);
}

TEST(nav_follower_takes_waypoints_round_the_wall_and_gives_up_when_stuck) {
  FakeWorld w;
  w.boxes.push_back({120, 60, 124, 180, 20});
  nav::Grid& g = nav::grid();
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  nav::Follower f;
  const float pos[3] = {80, 0, 120}, to[3] = {170, 0, 120};
  float out[3];
  g.search_budget = 1 << 30;  // service gives it each frame
  EXPECT(f.step(pos, to, 1.0f, 1 / 30.0f, out));
  EXPECT(f.paths == 1 && !(out[0] == to[0] && out[2] == to[2]));
  const float wp[3] = {out[0], 4.0f, out[2]}, here[3] = {pos[0], 4.0f, pos[2]};
  EXPECT(!w.wall(here, wp));
  // Standing still: held cells and new paths, a step back, then the exe's way.
  float now = 1.0f;
  bool gave_up = false;
  for (int k = 0; k < 30 * 30 && !gave_up; ++k) {
    now += 1 / 30.0f;
    g.search_budget = 1 << 30;
    gave_up = !f.step(pos, to, now, 1 / 30.0f, out);
  }
  EXPECT(gave_up && now > 10.0f && now < 20.0f);
  EXPECT(!f.step(pos, to, now + 5.0f, 1 / 30.0f, out));  // the fallback's 10 s
  g.release(now + 11.0f);
  g.search_budget = 1 << 30;
  EXPECT(f.step(pos, to, now + 11.0f, 1 / 30.0f, out));  // and the grid again after
  g.reset(nullptr);
}

TEST(nav_is_on_for_the_players_side_with_bugs_and_off_in_network_games) {
  zfix::Options& o = zfix::options();
  const zfix::Options keep = o;
  const bool mp = mech3::options().multiplayer;
  o.bugs = true, o.nav_allies = true, o.nav_enemies = false;
  mech3::options().multiplayer = 0;
  EXPECT(nav::on_for(true) && !nav::on_for(false));
  o.nav_enemies = true;
  EXPECT(nav::on_for(false));
  mech3::options().multiplayer = 1;
  EXPECT(!nav::on_for(true) && !nav::on_for(false));
  mech3::options().multiplayer = 0;
  o.bugs = false;
  EXPECT(!nav::on_for(true));
  o = keep;
  mech3::options().multiplayer = mp;
}

TEST(nav_a_door_opening_is_seen_behind_the_old_tiles) {
  FakeWorld w;
  w.boxes.push_back({120, 0, 124, 240, 20});  // a wall right across
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  const float a[3] = {80, 0, 120}, b[3] = {170, 0, 120};
  EXPECT(!nav::line_clear(g, a, b));
  // The door in it opens.
  w.boxes = {{120, 0, 124, 100, 20}, {120, 140, 124, 240, 20}};
  EXPECT(!nav::line_clear(g, a, b));  // the grid has not looked yet
  const int was = g.version;
  g.refresh(120, 100, 124, 140, 50.0f);
  EXPECT(g.built(nav::cell_of(122), nav::cell_of(120)));  // the old tile serves meanwhile
  EXPECT(!g.queue.empty());
  while (!g.queue.empty()) {
    int budget = 1 << 30;
    const auto [tx, tz] = g.queue.front();
    g.build(tx, tz, &budget);
    g.queue.erase(g.queue.begin());
  }
  EXPECT(g.version != was && nav::line_clear(g, a, b));
  // Something moving all the time is not built again each look.
  g.refresh(120, 100, 124, 140, 51.0f);
  EXPECT(g.queue.empty());
}

TEST(nav_a_parked_vehicle_is_solid_and_costly_round_it_but_no_wall) {
  FakeWorld w;
  // A pass 60 m wide between two walls, a vehicle parked in its middle.
  w.boxes = {{120, 0, 124, 90, 30}, {120, 150, 124, 256, 30}};
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  for (float x = 118; x < 126; x += 4)
    for (float z = 116; z < 124; z += 4) g.hold(nav::cell_of(x), nav::cell_of(z), 0.0f, 100.0f);
  for (float x = 100; x <= 144; x += 4)
    for (float z = 96; z <= 144; z += 4) g.hold(nav::cell_of(x), nav::cell_of(z), 0.0f, 100.0f, nav::kNear);
  EXPECT(!g.walkable(nav::cell_of(120), nav::cell_of(120), 0));
  const float a[3] = {80, 0, 120}, b[3] = {170, 0, 120};
  nav::Way p;
  // The only way is through the costly ring: taken, round the hull.
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk && !p.empty());
  g.release(200.0f);
  EXPECT(g.walkable(nav::cell_of(120), nav::cell_of(120), 0));  // it has driven off
  EXPECT(nav::line_clear(g, a, b));
}

TEST(nav_a_keep_out_shape_is_walked_round_unless_the_goal_is_in_it) {
  FakeWorld w;
  const float shape[4] = {110, 140, 90, 150};  // x0, x1, z0, z1
  std::copy(shape, shape + 4, w.keep_out);
  nav::Grid g;
  g.reset(&w);
  build_all(g, -2, -2, 3, 3);
  const float a[3] = {70, 0, 120}, b[3] = {180, 0, 120};
  nav::Way p;
  EXPECT(find_pumped(g, a, b, &p) == nav::Find::kOk);
  bool round = false;
  for (const auto& q : p) round = round || q[2] < 94 || q[2] > 146;
  EXPECT(round);
  const float inside[3] = {125, 0, 120};
  EXPECT(find_pumped(g, a, inside, &p) == nav::Find::kOk);
  EXPECT(p.back()[0] == inside[0] && p.back()[2] == inside[2]);
  EXPECT(path_len(a, p) < 70.0f);  // straight in: sent there, the shape does not count
}

TEST(nav_dump_writes_the_grid) {
  FakeWorld w;
  nav::Grid& g = nav::grid();
  g.reset(&w);
  build_all(g, 0, 0, 0, 0);
  const std::string file = std::string(MW3_TEST_TMP) + "/nav_dump.ppm";
  nav::dump(file.c_str(), {});
  std::FILE* f = std::fopen(file.c_str(), "rb");
  EXPECT(f != nullptr);
  if (f) {
    char head[3] = {};
    EXPECT(std::fread(head, 1, 2, f) == 2 && head[0] == 'P' && head[1] == '6');
    std::fclose(f);
  }
  g.reset(nullptr);
}

TEST(nav_a_search_spreads_over_frames_to_the_same_way) {
  FakeWorld w;
  w.boxes.push_back({120, 0, 124, 240, 20});  // a long wall: more than a frame's search
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  const float a[3] = {80, 0, 120}, b[3] = {170, 0, 120};
  nav::Way whole, part;
  EXPECT(nav::find_path(g, a, b, &whole) == nav::Find::kOk && !whole.empty());
  nav::PathSearch s;
  s.begin(a, b);
  int frames = 0;
  nav::Find f = nav::Find::kWorking;
  while (f == nav::Find::kWorking && frames < 100000) {
    int budget = 50;
    f = s.run(g, &budget, &part);
    EXPECT(budget <= 0 || f != nav::Find::kWorking);  // no more than it was given
    ++frames;
  }
  EXPECT(f == nav::Find::kOk && frames > 3 && part == whole && !s.active());
  // The grid begun again under a search (a replay): over at once, no cell read.
  s.begin(a, b);
  int some = 50;
  EXPECT(s.run(g, &some, &part) == nav::Find::kWorking && s.active());
  g.reset(&w);
  some = 1 << 30;
  EXPECT(s.run(g, &some, &part) == nav::Find::kBuilding && !s.active() && part.empty());
  EXPECT(g.queue.empty());  // and asks nothing of the new grid for the old search
}
