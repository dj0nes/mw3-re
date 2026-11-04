// W58 (docs/lancemate-pathing.md): the walkability grid and its paths on a
// made-up world of flat ground, boxes for walls, a cliff and water.
#include <cmath>
#include <cstdio>
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
  int grounds = 0, walls = 0;
  static bool in(const float r[4], float x, float z) { return x >= r[0] && x < r[1] && z >= r[2] && z < r[3]; }
  bool ground(float x, float z, float, float* y, float* water) override {
    ++grounds;
    *water = std::nanf("");
    if (in(hole, x, z)) return false;
    *y = (x >= cliff_x && x < cliff_x + 20 && z >= cliff_z0 && z < cliff_z1) ? 10.0f : 0.0f;
    if (in(pool, x, z)) *water = 8.0f;
    return true;
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

void build_all(nav::Grid& g, int tx0, int tz0, int tx1, int tz1) {
  for (int tz = tz0; tz <= tz1; ++tz)
    for (int tx = tx0; tx <= tx1; ++tx) {
      double budget = 1e12;
      g.build(tx, tz, &budget);
    }
}

// Each leg of a path (from `from`) clear of the world's walls at a mech's body height.
bool path_clear(FakeWorld& w, const float from[3], const std::vector<std::array<float, 2>>& p) {
  float a[3] = {from[0], 4.0f, from[2]};
  for (const auto& q : p) {
    const float b[3] = {q[0], 4.0f, q[1]};
    if (w.wall(a, b)) return false;
    a[0] = b[0], a[2] = b[2];
  }
  return true;
}
float path_len(const float from[3], const std::vector<std::array<float, 2>>& p) {
  float n = 0, x = from[0], z = from[2];
  for (const auto& q : p) n += std::hypot(q[0] - x, q[1] - z), x = q[0], z = q[1];
  return n;
}
}  // namespace

TEST(nav_open_ground_is_one_straight_leg) {
  FakeWorld w;
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  const float a[3] = {60, 0, 60}, b[3] = {200, 0, 180};
  EXPECT(nav::line_clear(g, a[0], a[2], b[0], b[2]));
  std::vector<std::array<float, 2>> p;
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  EXPECT(p.size() == 1 && p[0][0] == b[0] && p[0][1] == b[2]);  // the brain's point itself
}

TEST(nav_walks_round_a_wall_and_through_a_wide_gap_only) {
  FakeWorld w;
  // A wall 120 m long across the straight way (x 120..124, z 60..180).
  w.boxes.push_back({120, 60, 124, 180, 20});
  nav::Grid g;
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  const float a[3] = {80, 0, 120}, b[3] = {170, 0, 120};
  EXPECT(!nav::line_clear(g, a[0], a[2], b[0], b[2]));
  std::vector<std::array<float, 2>> p;
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
  EXPECT(!nav::line_clear(g, a[0], a[2], b[0], b[2]));
  std::vector<std::array<float, 2>> p;
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kOk);
  bool round = false;  // the way passes beyond the cliff's ends
  for (const auto& q : p) round = round || q[1] < 64 || q[1] > 176;
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
  for (const auto& q : p) round = round || q[1] < 44 || q[1] > 196;
  EXPECT(round);
  // A deep pool right across: crossed (costed, not blocked), its depth kept.
  std::fill(w.hole, w.hole + 4, 0.0f);
  const float pool[4] = {110, 130, 0, 512};
  std::copy(pool, pool + 4, w.pool);
  g.reset(&w);
  build_all(g, -1, -1, 2, 2);
  EXPECT(g.cell(nav::cell_of(120), nav::cell_of(120))->depth == 8.0f);
  EXPECT(nav::find_path(g, a, b2, &p) == nav::Find::kOk && path_len(a, p) < 100.0f);
}

TEST(nav_tiles_build_within_their_budget_and_paths_wait_for_them) {
  FakeWorld w;
  nav::Grid g;
  g.reset(&w);
  const float a[3] = {60, 0, 60}, b[3] = {100, 0, 70};
  std::vector<std::array<float, 2>> p;
  EXPECT(nav::find_path(g, a, b, &p) == nav::Find::kBuilding && !g.queue.empty());
  EXPECT(!g.built(nav::cell_of(60), nav::cell_of(60)));
  double budget = 1.0;  // 1 us: a few cells at most
  EXPECT(!g.build(0, 0, &budget) && budget == 0.0);
  EXPECT(!g.built(nav::cell_of(60), nav::cell_of(60)));  // a part-built tile reads as unknown
  int rounds = 1;
  for (; rounds < 100000; ++rounds) {
    budget = 50.0;
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
  EXPECT(f.step(pos, to, 1.0f, 1 / 30.0f, out));
  EXPECT(f.paths == 1 && !(out[0] == to[0] && out[2] == to[2]));
  const float wp[3] = {out[0], 4.0f, out[2]}, here[3] = {pos[0], 4.0f, pos[2]};
  EXPECT(!w.wall(here, wp));
  // Standing still: held cells and new paths, a step back, then the exe's way.
  float now = 1.0f;
  bool gave_up = false;
  for (int k = 0; k < 30 * 30 && !gave_up; ++k) {
    now += 1 / 30.0f;
    gave_up = !f.step(pos, to, now, 1 / 30.0f, out);
  }
  std::fprintf(stderr, "nav stuck: gave up %d at %.2f, stuck %d paths %d fallback %.2f\n", gave_up, now, f.stuck, f.paths, f.fallback_until);
  EXPECT(gave_up && now > 10.0f && now < 20.0f);
  EXPECT(!f.step(pos, to, now + 5.0f, 1 / 30.0f, out));  // the fallback's 10 s
  g.release(now + 11.0f);
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
