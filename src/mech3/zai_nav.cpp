#include "mech3/zai_nav.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <queue>

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zutil/zfix.h"
#include "mech3/app_mech.h"
#include "mech3/app_vehicle.h"
#include "mech3/mission_world.h"
#include "mech3/options.h"
#include "mech3/zainet_inout.h"

namespace mech3::ai::nav {

namespace {
constexpr float kGiveWay = 26.0f;  // m round a parked vehicle

// The world's own queries, every zone's (the walks skip those not in view).
struct ZclsWorld : World {
  int ground(float x, float z, Floor* out, int max) override {
    zcls::Node* w = world_hooks().world;
    if (!w) return 0;
    float probe[3] = {x, 2000.0f, z};
    static zcls::GroundHits hits;
    const int32_t mode = zmod::zones().mode;
    zmod::zones().mode = 0;
    zcls::ground_query(w, probe, 1, 2000.0f, &hits);
    zmod::zones().mode = mode;
    // Sorted high to low, one floor per height; water (soil 1) goes to the
    // ground under it.
    float heights[32], waters[32];
    int n = 0, nw = 0;
    for (int i = 0; i < hits.count && i < 32; ++i) {
      const float h = hits.hit[i].height;
      if (hits.hit[i].material && hits.hit[i].material->soil == 1) waters[nw++] = h;
      else heights[n++] = h;
    }
    std::sort(heights, heights + n, std::greater<float>());
    int floors = 0;
    for (int i = 0; i < n && floors < max; ++i) {
      if (floors && out[floors - 1].y - heights[i] < 1.0f) continue;
      out[floors].y = heights[i], out[floors].water = std::nanf("");
      ++floors;
    }
    for (int k = 0; k < nw; ++k)
      for (int f = 0; f < floors; ++f)
        if (out[f].y < waters[k] && !(out[f].water >= waters[k])) {
          out[f].water = waters[k];
          break;
        }
    return floors;
  }
  bool wall(const float a[3], const float b[3]) override {
    zcls::Node* w = world_hooks().world;
    if (!w) return false;
    float seg[6] = {a[0], a[1], a[2], b[0], b[1], b[2]};
    static zcls::SegmentHits hits[1];
    const int32_t mode = zmod::zones().mode;
    zmod::zones().mode = 0;
    zcls::segment_query(w, seg, 2, hits);
    zmod::zones().mode = mode;
    for (int i = 0; i < hits[0].count; ++i) {
      if (!(std::fabs(hits[0].hit[i].normal[1]) < 0.5f)) continue;
      // What stays only: a mech or a vehicle (the leader himself, one
      // standing on a tile as it is built) is no wall; a building is.
      const void* owner = node_entity(hits[0].hit[i].node);
      if (owner && (entity_is_mech(owner) || is_program_vehicle(owner))) continue;
      return true;
    }
    return false;
  }
  bool avoid(float x, float y, float z) override {
    const float p[3] = {x, y, z};
    float out[3];
    return inout_pull(p, p, out);  // from inside a shape, the point out of it
  }
  // The vehicles at rest (a parked mobile HQ, a tank waiting): the grid's
  // edges leave them out, for they may drive off.
  void parked(std::vector<std::array<float, 5>>* out) override {
    for (void* v : program_vehicles()) {
      if (!program_vehicle_active(v)) continue;
      float vel[3];
      program_vehicle_velocity(v, vel);
      if (std::hypot(vel[0], vel[2]) > 0.5f) continue;
      float e[6];
      auto* node = static_cast<zcls::Node*>(program_vehicle_node(v));
      if (!node || zcls::node_box_extent_xz(node, e) != 0) continue;
      float at[3] = {};
      zcls::node_world_position(node, at);
      out->push_back({e[0], e[2], e[3], e[5], at[1]});
    }
  }
};

constexpr float kProgress = 6.0f;  // m in 3 s: under this he is not getting on
constexpr float kWallLow = 2.5f, kWallHigh = 6.5f;  // the wall probes over the higher ground (a mech's legs and body)
constexpr int kWindows[] = {64, 128, 256};  // the search's margins round from and to, in cells (256 m .. 1 km)
constexpr int kMaxExpand = 250000;

}  // namespace

World& zcls_world() {
  static ZclsWorld w;
  return w;
}

Grid& grid() {
  static Grid g;
  return g;
}

void Grid::reset(World* w) {
  world_ = w;
  ++resets;
  tiles_.clear();
  fresh_.clear();
  refreshed_.clear();
  queue.clear();
  held_.clear();
  parked_t = -1e30f;
  ++version;
}

void Grid::refresh(float x0, float z0, float x1, float z1, float now) {
  // A cell on either side of the change: the edges into it are the neighbour's.
  for (int tz = tile_of(cell_of(z0 - kCell)); tz <= tile_of(cell_of(z1 + kCell)); ++tz)
    for (int tx = tile_of(cell_of(x0 - kCell)); tx <= tile_of(cell_of(x1 + kCell)); ++tx) {
      const uint64_t k = key(tx, tz);
      if (!tiles_.count(k) || fresh_.count(k)) continue;
      auto seen = refreshed_.find(k);
      if (seen != refreshed_.end() && now - seen->second < 10.0f) continue;  // something moving all the time
      refreshed_[k] = now;
      fresh_[k] = std::make_unique<Tile>();
      const std::pair<int, int> at{tx, tz};
      if (std::find(queue.begin(), queue.end(), at) == queue.end()) queue.push_back(at);
    }
}

Tile* Grid::tile(int tx, int tz, bool make) {
  auto it = tiles_.find(key(tx, tz));
  if (it != tiles_.end()) return it->second.get();
  if (!make) return nullptr;
  auto t = std::make_unique<Tile>();
  Tile* p = t.get();
  tiles_[key(tx, tz)] = std::move(t);
  return p;
}

int Grid::tiles_done() const {
  int n = 0;
  for (const auto& kv : tiles_) n += kv.second->done();
  return n;
}

void Grid::build_ground(Tile& t, int tx, int tz, int i) {
  const int cx = tx * kTile + i % kTile, cz = tz * kTile + i / kTile;
  Floor f[kLevels];
  const int n = world_->ground(centre_of(cx), centre_of(cz), f, kLevels);
  for (int l = 0; l < n; ++l) {
    Cell& c = t.cells[i * kLevels + l];
    c.y = f[l].y, c.flags = 0;
    c.depth = std::isfinite(f[l].water) && f[l].water > f[l].y ? f[l].water - f[l].y : 0.0f;
    if (l > 0 && f[l - 1].y - f[l].y < kHead) c.flags |= kLow;
  }
}

void Grid::build_edges(Tile& t, int tx, int tz, int i) {
  const int lx = i % kTile, lz = i / kTile;
  const int cx = tx * kTile + lx, cz = tz * kTile + lz;
  for (int l = 0; l < kLevels; ++l) {
    Cell& c = t.cells[i * kLevels + l];
    if (c.flags & (kNoGround | kLow)) continue;
    for (int k = 0; k < 2; ++k) {
      const int nx = cx + (k == 0), nz = cz + (k == 1);
      Floor f[kLevels];
      int n = 0;
      if (k == 0 ? lx + 1 < kTile : lz + 1 < kTile) {
        const Cell* other = &t.cells[(k == 0 ? i + 1 : i + kTile) * kLevels];
        for (int o = 0; o < kLevels; ++o) {
          f[o].y = (other[o].flags & (kNoGround | kLow)) ? 1e30f : other[o].y;
          if (!(other[o].flags & kNoGround)) n = o + 1;
        }
      } else {
        n = world_->ground(centre_of(nx), centre_of(nz), f, kLevels);
        for (int o = n - 1; o > 0; --o)
          if (f[o - 1].y - f[o].y < kHead) f[o].y = 1e30f;
      }
      // The floor there a step away, the nearest.
      int to = -1;
      float best = kClimb * kCell;
      for (int o = 0; o < n; ++o) {
        const float d = std::fabs(f[o].y - c.y);
        if (d <= best) best = d, to = o;
      }
      if (to < 0) continue;  // too steep or nothing there: no way, no wall
      const float base = std::max(c.y, f[to].y);
      bool walled = false;
      for (float up : {kWallLow, kWallHigh}) {
        const float a[3] = {centre_of(cx), base + up, centre_of(cz)}, b[3] = {centre_of(nx), base + up, centre_of(nz)};
        if (world_->wall(a, b)) walled = true;
      }
      if (walled) c.flags |= k == 0 ? kWallE : kWallS;
      else (k == 0 ? c.east : c.south) = static_cast<int8_t>(to);
    }
  }
}

bool Grid::build(int tx, int tz, int* steps) {
  auto again = fresh_.find(key(tx, tz));
  Tile* t = again != fresh_.end() ? again->second.get() : tile(tx, tz, true);
  if (t->done()) return true;
  const int n = kTile * kTile;
  while (!t->done()) {
    if (*steps <= 0) return false;
    if (t->built < n) build_ground(*t, tx, tz, t->built);
    else build_edges(*t, tx, tz, t->built - n);
    ++t->built;
    --*steps;
  }
  if (again != fresh_.end()) {  // in place of the old, its holds put back at the next release
    tiles_[key(tx, tz)] = std::move(again->second);
    fresh_.erase(again);
  }
  ++version;
  return true;
}

const Cell* Grid::cell(int cx, int cz, int level) const {
  const int tx = tile_of(cx), tz = tile_of(cz);
  auto it = tiles_.find(key(tx, tz));
  if (it == tiles_.end() || !it->second->done()) return nullptr;
  return &it->second->cells[((cz - tz * kTile) * kTile + (cx - tx * kTile)) * kLevels + level];
}

int Grid::level_at(int cx, int cz, float y) const {
  int best = -1;
  float near = 1e30f;
  for (int l = 0; l < kLevels; ++l) {
    const Cell* c = cell(cx, cz, l);
    if (!c) return -1;
    if (c->flags & kNoGround) continue;
    const float d = std::fabs(c->y - y);
    if (d < near) near = d, best = l;
  }
  return best;
}

bool Grid::walkable(int cx, int cz, int level) const {
  const Cell* me = cell(cx, cz, level);
  if (!me || (me->flags & (kNoGround | kHeld | kLow))) return false;
  const float y = me->y;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx) {
      const float run = kCell * (dx && dz ? 1.41421356f : 1.0f);
      for (int l = 0; l < kLevels; ++l) {
        const Cell* c = cell(cx + dx, cz + dz, l);
        if (!c) return false;
        if (c->flags & kNoGround) continue;
        const float up = c->y - y;
        if ((c->flags & kHeld) && std::fabs(up) < kHead) return false;
        // Something over a step above this floor within the body's height.
        if ((dx || dz) && up > kClimb * run && up < kHead) return false;
        // A wall at this height among the 3 x 3's edges.
        if (std::fabs(up) < kHead) {
          if (dx <= 0 && (c->flags & kWallE)) return false;
          if (dz <= 0 && (c->flags & kWallS)) return false;
        }
      }
    }
  return true;
}

namespace {
// The floor an orthogonal step reaches, whichever cell holds the edge.
int orth_step(const Grid& g, int cx, int cz, int level, int dx, int dz) {
  if (dx > 0 || dz > 0) {
    const Cell* c = g.cell(cx, cz, level);
    return c ? (dx > 0 ? c->east : c->south) : -1;
  }
  for (int l = 0; l < kLevels; ++l) {
    const Cell* n = g.cell(cx + dx, cz + dz, l);
    if (n && (dx < 0 ? n->east : n->south) == level) return l;
  }
  return -1;
}
}  // namespace

int Grid::step(int cx, int cz, int level, int dx, int dz) const {
  const Cell* a = cell(cx, cz, level);
  if (!a) return -1;
  int to = -1;
  if (dx == 0 || dz == 0) {
    to = orth_step(*this, cx, cz, level, dx, dz);
  } else {
    // A diagonal: its rise allowed, and neither corner a wall or above the
    // way (past a drop, a walkway laid at an angle, it may be nothing).
    to = level_at(cx + dx, cz + dz, a->y);
    if (to < 0) return -1;
    const Cell* b = cell(cx + dx, cz + dz, to);
    if ((b->flags & kNoGround) || std::fabs(b->y - a->y) > kClimb * kCell * 1.41421356f) return -1;
    const float top = std::max(a->y, b->y);
    for (const auto& corner : {std::array<int, 2>{cx + dx, cz}, std::array<int, 2>{cx, cz + dz}})
      for (int l = 0; l < kLevels; ++l) {
        const Cell* c = cell(corner[0], corner[1], l);
        if (!c) return -1;
        if (c->flags & kNoGround) continue;
        const float up = c->y - top;
        if (up > kClimb * kCell && up < kHead) return -1;
        if ((c->flags & kHeld) && std::fabs(up) < kHead) return -1;
      }
    // The walls on the four edges round it, at this height.
    const auto walled = [&](int x, int z, uint16_t bit) {
      for (int l = 0; l < kLevels; ++l) {
        const Cell* c = cell(x, z, l);
        if (c && !(c->flags & kNoGround) && (c->flags & bit) && std::fabs(c->y - top) < kHead) return true;
      }
      return false;
    };
    const int ex = dx < 0 ? cx - 1 : cx, ez = dz < 0 ? cz - 1 : cz;
    if (walled(ex, cz, kWallE) || walled(ex, cz + dz, kWallE) || walled(cx, ez, kWallS) || walled(cx + dx, ez, kWallS)) return -1;
  }
  if (to < 0 || !walkable(cx + dx, cz + dz, to)) return -1;
  return to;
}

Cell* Grid::held_cell(const Held& h) {
  Tile* t = tile(tile_of(h.cx), tile_of(h.cz), false);
  if (!t) return nullptr;
  return &t->cells[((h.cz - tile_of(h.cz) * kTile) * kTile + (h.cx - tile_of(h.cx) * kTile)) * kLevels + h.level];
}

void Grid::hold(int cx, int cz, float y, float until, uint16_t what) {
  const int level = level_at(cx, cz, y);
  if (level < 0) return;
  const Held h{cx, cz, level, until, what};
  if (Cell* c = held_cell(h)) {
    c->flags |= what;
    held_.push_back(h);
  }
}

void Grid::release(float now) {
  for (auto it = held_.begin(); it != held_.end();) {
    if (it->until > now) {
      ++it;
      continue;
    }
    if (Cell* c = held_cell(*it)) c->flags &= static_cast<uint16_t>(~it->what);
    it = held_.erase(it);
  }
  // A cell lifted above may be under a later hold too.
  for (const Held& h : held_)
    if (Cell* c = held_cell(h)) c->flags |= h.what;
}

bool Grid::avoided(int cx, int cz, int level) {
  auto* c = const_cast<Cell*>(cell(cx, cz, level));
  if (!c) return false;
  if (!(c->flags & kAvoidKnown)) {
    c->flags |= kAvoidKnown;
    if (world_ && world_->avoid(centre_of(cx), c->y, centre_of(cz))) c->flags |= kAvoid;
  }
  return (c->flags & kAvoid) != 0;
}

bool Grid::tight(int cx, int cz, int level) {
  auto* me = const_cast<Cell*>(cell(cx, cz, level));
  if (!me) return false;
  if (me->flags & kRoomKnown) return (me->flags & kTight) != 0;
  me->flags |= kRoomKnown;
  const float y = me->y;
  for (int dz = -2; dz <= 2; ++dz)
    for (int dx = -2; dx <= 2; ++dx) {
      if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
      const float run = kCell * std::hypot(static_cast<float>(dx), static_cast<float>(dz));
      for (int l = 0; l < kLevels; ++l) {
        const Cell* c = cell(cx + dx, cz + dz, l);
        if (!c || (c->flags & kNoGround)) continue;
        const float up = c->y - y;
        if ((up > kClimb * run && up < kHead) || (std::fabs(up) < kHead && (c->flags & (kWallE | kWallS)))) {
          me->flags |= kTight;
          return true;
        }
      }
    }
  return false;
}

void Grid::forget_avoid() {
  for (auto& kv : tiles_)
    for (Cell& c : kv.second->cells) c.flags &= static_cast<uint16_t>(~(kAvoidKnown | kAvoid));
  ++version;
}

bool line_clear(const Grid& g, const float a[3], const float b[3]) {
  int cx = cell_of(a[0]), cz = cell_of(a[2]);
  const int ex = cell_of(b[0]), ez = cell_of(b[2]);
  int level = g.level_at(cx, cz, a[1]);
  if (level < 0 || !g.walkable(cx, cz, level)) return false;
  // A supercover walk of the cells the segment crosses, each step allowed.
  const float dx = b[0] - a[0], dz = b[2] - a[2];
  const int sx = dx > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
  const float tdx = dx != 0 ? kCell / std::fabs(dx) : 1e30f, tdz = dz != 0 ? kCell / std::fabs(dz) : 1e30f;
  float tmx = dx != 0 ? ((sx > 0 ? (cx + 1) * kCell : cx * kCell) - a[0]) / dx : 1e30f;
  float tmz = dz != 0 ? ((sz > 0 ? (cz + 1) * kCell : cz * kCell) - a[2]) / dz : 1e30f;
  for (int n = 0; (cx != ex || cz != ez) && n < 4096; ++n) {
    int mx = 0, mz = 0;
    if (tmx < tmz) mx = sx, tmx += tdx;
    else mz = sz, tmz += tdz;
    level = g.step(cx, cz, level, mx, mz);
    if (level < 0) return false;
    cx += mx, cz += mz;
  }
  // On b's floor, not one over or under it.
  return level == g.level_at(cx, cz, b[1]);
}

bool goal_in_shape(Grid& g, const float to[3]) {
  const int cx = cell_of(to[0]), cz = cell_of(to[2]), l = g.level_at(cx, cz, to[1]);
  return l >= 0 && g.avoided(cx, cz, l);
}

bool line_clear_avoiding(Grid& g, const float a[3], const float b[3], bool shapes) {
  if (!line_clear(g, a, b)) return false;
  const float len = std::hypot(b[0] - a[0], b[2] - a[2]);
  const int steps = static_cast<int>(len / kCell) + 1;
  const auto in_it = [&](float t) {
    const float x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
    const int cx = cell_of(x), cz = cell_of(z), l = g.level_at(cx, cz, y);
    return l >= 0 && ((shapes && g.avoided(cx, cz, l)) || g.tight(cx, cz, l));
  };
  if (in_it(0.0f) && in_it(1.0f)) return true;
  for (int i = 0; i <= steps; ++i)
    if (in_it(static_cast<float>(i) / static_cast<float>(steps)) && i > 0) return false;
  return true;
}

namespace {
// The gait's slowdown in deep water (0x004181c0, the FIX's line): a wading
// cell costs the time it takes. H a mech's height, about 12 m.
float water_cost(float depth) {
  constexpr float kH = 12.0f;
  if (!(depth > 0.1f * kH)) return 1.0f;
  const float f = depth >= kH ? 0.7f : 1.0f - 0.3f * (depth - 0.1f * kH) / (0.9f * kH);
  return 1.0f / f;
}

// The walkable floor nearest (cx, cz) within r cells, on height y's storey.
bool nearest_walkable(const Grid& g, int cx, int cz, float y, int r, int* ox, int* oz, int* ol) {
  for (int d = 0; d <= r; ++d)
    for (int dz = -d; dz <= d; ++dz)
      for (int dx = -d; dx <= d; ++dx) {
        if (std::max(std::abs(dx), std::abs(dz)) != d) continue;
        const int l = g.level_at(cx + dx, cz + dz, y);
        if (l < 0 || !g.walkable(cx + dx, cz + dz, l)) continue;
        if (std::fabs(g.cell(cx + dx, cz + dz, l)->y - y) >= kHead) continue;
        return *ox = cx + dx, *oz = cz + dz, *ol = l, true;
      }
  return false;
}

void ask_tiles(Grid& g, int x0, int z0, int x1, int z1) {
  for (int tz = tile_of(z0); tz <= tile_of(z1); ++tz)
    for (int tx = tile_of(x0); tx <= tile_of(x1); ++tx) {
      const Tile* t = nullptr;
      auto it = g.all().find(Grid::key(tx, tz));
      if (it != g.all().end()) t = it->second.get();
      if (t && t->done()) continue;
      const std::pair<int, int> k{tx, tz};
      if (std::find(g.queue.begin(), g.queue.end(), k) == g.queue.end()) g.queue.push_back(k);
    }
}
}  // namespace

void PathSearch::begin(const float from[3], const float to[3]) {
  for (int i = 0; i < 3; ++i) from_[i] = from[i], to_[i] = to[i];
  active_ = true, started_ = false, open_window_ = false, window_ = 0;
}

float PathSearch::octile(int x, int z) const {
  const float ax = static_cast<float>(std::abs(x - gx_)), az = static_cast<float>(std::abs(z - gz_));
  return (std::max(ax, az) + 0.41421356f * std::min(ax, az)) * kCell;
}

Find PathSearch::run(Grid& g, int* budget, Way* out) {
  out->clear();
  // The grid begun again under a search (a new mission): its cells are gone.
  if (started_ && resets_ != g.resets) return active_ = false, open_window_ = false, Find::kBuilding;
  if (!started_) {
    resets_ = g.resets;
    fx_ = cell_of(from_[0]), fz_ = cell_of(from_[2]), tx_ = cell_of(to_[0]), tz_ = cell_of(to_[2]);
    // Round both ends first: nothing can be said without them.
    auto ask_ends = [&] {
      ask_tiles(g, fx_ - 8, fz_ - 8, fx_ + 8, fz_ + 8);
      ask_tiles(g, tx_ - 8, tz_ - 8, tx_ + 8, tz_ + 8);
    };
    if (!g.built(fx_, fz_) || !g.built(tx_, tz_)) return ask_ends(), active_ = false, Find::kBuilding;
    if (!nearest_walkable(g, fx_, fz_, from_[1], 8, &sx_, &sz_, &sl_) || !nearest_walkable(g, tx_, tz_, to_[1], 8, &gx_, &gz_, &gl_)) {
      ask_ends();
      active_ = false;
      return g.queue.empty() ? Find::kNone : Find::kBuilding;
    }
    shapes_ = !goal_in_shape(g, to_);
    started_ = true;
  }
  for (;;) {
    if (!open_window_) {
      if (window_ >= static_cast<int>(sizeof kWindows / sizeof kWindows[0])) return active_ = false, Find::kNone;
      const int margin = kWindows[window_];
      x0_ = std::min(fx_, tx_) - margin, x1_ = std::max(fx_, tx_) + margin;
      z0_ = std::min(fz_, tz_) - margin, z1_ = std::max(fz_, tz_) + margin;
      // No answer from part of a window: ground not built yet reads as a wall,
      // and a path round that is a wrong one.
      bool missing = false;
      for (int tz2 = tile_of(z0_); tz2 <= tile_of(z1_) && !missing; ++tz2)
        for (int tx2 = tile_of(x0_); tx2 <= tile_of(x1_) && !missing; ++tx2) {
          auto it = g.all().find(Grid::key(tx2, tz2));
          missing = it == g.all().end() || !it->second->done();
        }
      if (missing) {
        ask_tiles(g, x0_, z0_, x1_, z1_);
        active_ = false;
        return Find::kBuilding;
      }
      w_ = x1_ - x0_ + 1;
      const size_t size = static_cast<size_t>(w_) * (z1_ - z0_ + 1) * kLevels;
      cost_.assign(size, 1e30f);
      from_of_.assign(size, -1);
      open_.clear();
      const int first = ((sz_ - z0_) * w_ + (sx_ - x0_)) * kLevels + sl_;
      cost_[first] = 0;
      open_.push_back({octile(sx_, sz_), first});
      expanded_ = 0;
      open_window_ = true;
    }
    const auto later = std::greater<Item>();
    while (!open_.empty() && expanded_ < kMaxExpand) {
      if (*budget <= 0) return Find::kWorking;
      std::pop_heap(open_.begin(), open_.end(), later);
      const auto [f, i] = open_.back();
      open_.pop_back();
      const int place = i / kLevels, l = i % kLevels;
      const int x = x0_ + place % w_, z = z0_ + place / w_;
      if (f - octile(x, z) > cost_[i] + 1e-3f) continue;  // stale
      if (x == gx_ && z == gz_ && l == gl_) {
        if (finish(g, out)) return Find::kOk;
        return Find::kBuilding;  // ask again
      }
      ++expanded_, --*budget;
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          if (!dx && !dz) continue;
          const int nx = x + dx, nz = z + dz;
          if (nx < x0_ || nx > x1_ || nz < z0_ || nz > z1_) continue;
          const int nl = g.step(x, z, l, dx, dz);
          if (nl < 0) continue;
          const Cell* there = g.cell(nx, nz, nl);
          const float step = (dx && dz ? 1.41421356f : 1.0f) * kCell * water_cost(there->depth) * (there->flags & kNear ? 8.0f : 1.0f) *
                             (shapes_ && g.avoided(nx, nz, nl) ? 20.0f : 1.0f) * (g.tight(nx, nz, nl) ? 3.0f : 1.0f);
          const int j = ((nz - z0_) * w_ + (nx - x0_)) * kLevels + nl;
          if (cost_[i] + step < cost_[j]) {
            cost_[j] = cost_[i] + step, from_of_[j] = i;
            open_.push_back({cost_[j] + octile(nx, nz), j});
            std::push_heap(open_.begin(), open_.end(), later);
          }
        }
    }
    open_window_ = false;
    ++window_;
  }
}

bool PathSearch::finish(Grid& g, Way* out) {
  Way cells;
  active_ = false, open_window_ = false;
  const int goal = ((gz_ - z0_) * w_ + (gx_ - x0_)) * kLevels + gl_;
  for (int i = goal; i >= 0; i = from_of_[i]) {
    const int place = i / kLevels;
    const int x = x0_ + place % w_, z = z0_ + place / w_;
    // A tile built again while the search ran may hold other floors: the height
    // only picks a floor for the line checks below, which then fail safe, and the
    // walk asks again if the way on is shut.
    const Cell* c = g.cell(x, z, i % kLevels);
    if (!c) return false;
    cells.push_back({centre_of(x), c->y, centre_of(z)});
  }
  std::reverse(cells.begin(), cells.end());
  // String-pulled: from each point the farthest one in a clear line, looked for
  // at most 512 m on (a leg is checked cell by cell: long paths would pay twice).
  constexpr size_t kLook = 128;
  std::array<float, 3> here = {from_[0], from_[1], from_[2]};
  size_t k = 0;
  while (k + 1 < cells.size()) {
    size_t far = k + 1;
    for (size_t j = std::min(cells.size() - 1, k + kLook); j > k + 1; --j)
      if (line_clear_avoiding(g, here.data(), cells[j].data(), shapes_)) {
        far = j;
        break;
      }
    out->push_back(cells[far]);
    here = cells[far];
    k = far;
  }
  if (out->empty()) out->push_back(cells.back());
  // The last waypoint the brain's point itself when it is walkable.
  const int tl = g.level_at(tx_, tz_, to_[1]);
  if (tl >= 0 && g.walkable(tx_, tz_, tl)) out->back() = {to_[0], to_[1], to_[2]};
  cost_ = {}, from_of_ = {}, open_ = {};
  return true;
}

Find find_path(Grid& g, const float from[3], const float to[3], Way* out) {
  PathSearch s;
  s.begin(from, to);
  int budget = 1 << 30;
  return s.run(g, &budget, out);
}

namespace {
// What stands still and may change: the world's object3ds that are no mech's
// or vehicle's, by their world matrix and whether they are there at all. A
// node's slot outlives it: `data` tells a freed one (a building brought down).
struct Watched {
  zcls::Node* node;
  void* data;
  float m[12];
  uint32_t on;
  float box[4];  // where it was: x0, z0, x1, z1
};
constexpr uint32_t kThere = 0x4 | 0x10;  // node flags: active, intersect
std::vector<Watched>& watched() {
  static std::vector<Watched> w;
  return w;
}
bool watch_box(zcls::Node* n, float box[4]) {
  float x[6];
  if (zcls::node_box_extent_xz(n, x) != 0) return false;
  box[0] = x[0], box[1] = x[2], box[2] = x[3], box[3] = x[5];
  return true;
}
void watch_begin() {
  std::vector<Watched>& w = watched();
  w.clear();
  zcls::NodePool& pool = zcls::nodes();
  for (auto& block : pool.blocks)
    for (int i = 0; i < pool.block_size; ++i) {
      zcls::Node& n = block[i];
      if (!n.data || n.cls() != 5 || !(n.flags() & 0x80000) || !(n.flags() & 0x100)) continue;
      const void* owner = node_entity(&n);
      if (owner && (entity_is_mech(owner) || is_program_vehicle(owner))) continue;
      Watched e{&n, n.data, {}, n.flags() & kThere, {}};
      if (!watch_box(&n, e.box)) continue;
      std::memcpy(e.m, static_cast<zcls::Object3d*>(n.data)->floats(0x60), sizeof e.m);
      w.push_back(e);
    }
}
void watch_look(Grid& g, float now) {
  std::vector<Watched>& w = watched();
  for (size_t i = 0; i < w.size();) {
    Watched& e = w[i];
    if (e.node->data != e.data || e.node->cls() != 5) {  // gone: the way may be open
      g.refresh(e.box[0], e.box[1], e.box[2], e.box[3], now);
      e = w.back();
      w.pop_back();
      continue;
    }
    ++i;
    const float* m = static_cast<zcls::Object3d*>(e.node->data)->floats(0x60);
    const uint32_t on = e.node->flags() & kThere;
    if (on == e.on && std::memcmp(m, e.m, sizeof e.m) == 0) continue;
    std::memcpy(e.m, m, sizeof e.m);
    e.on = on;
    // Where it was and where it is now.
    g.refresh(e.box[0], e.box[1], e.box[2], e.box[3], now);
    if (watch_box(e.node, e.box)) g.refresh(e.box[0], e.box[1], e.box[2], e.box[3], now);
  }
}
float g_watch_t = -1e30f;
}  // namespace

void restart() {
  Grid& g = grid();
  g.reset(&zcls_world());
  watched().clear();
  if (world_hooks().world) watch_begin();
  g_watch_t = -1e30f;
}

void service(float now, int steps, int search_steps) {
  Grid& g = grid();
  g.search_budget = search_steps;
  if (g.world() != &zcls_world()) restart();
  if (now - g_watch_t >= 1.0f || now < g_watch_t) g_watch_t = now, watch_look(g, now);
  static int32_t shapes = 0;
  if (inout_changes() != shapes) shapes = inout_changes(), g.forget_avoid();
  g.release(now);
  if (g.world() && now - g.parked_t >= 1.0f) {  // held a little past the next look
    g.parked_t = now;
    std::vector<std::array<float, 5>> rects;
    g.world()->parked(&rects);
    for (const auto& r : rects) {
      for (int cz = cell_of(r[1]); cz <= cell_of(r[3]); ++cz)
        for (int cx = cell_of(r[0]); cx <= cell_of(r[2]); ++cx) g.hold(cx, cz, r[4], now + 1.5f);
      // Round it, as far as the giving way reaches (0x004cc3e0: too close
      // inside +0x78 x 0.9, shipped mostly 25): a path through there is pushed
      // back, so one is taken only with no other way (the HQs park in a pass).
      const float mx = (r[0] + r[2]) * 0.5f, mz = (r[1] + r[3]) * 0.5f;
      for (int cz = cell_of(mz - kGiveWay); cz <= cell_of(mz + kGiveWay); ++cz)
        for (int cx = cell_of(mx - kGiveWay); cx <= cell_of(mx + kGiveWay); ++cx)
          if (std::hypot(centre_of(cx) - mx, centre_of(cz) - mz) <= kGiveWay) g.hold(cx, cz, r[4], now + 1.5f, kNear);
    }
  }
  while (!g.queue.empty() && steps > 0) {
    const auto [tx, tz] = g.queue.front();
    if (g.build(tx, tz, &steps)) g.queue.erase(g.queue.begin());
  }
}

bool on_for(bool ally) {
  const zfix::Options& o = zfix::options();
  if (!o.bugs || options().multiplayer) return false;
  return ally ? o.nav_allies : o.nav_enemies;
}

bool Follower::step(const float pos[3], const float to[3], float now, float dt, float out[3], bool must_find, float reach) {
  Grid& g = grid();
  if (now < fallback_until) return false;
  const float flat = std::hypot(to[0] - pos[0], to[2] - pos[2]);
  if (flat < 8.0f && std::fabs(to[1] - pos[1]) < kHead) return path.clear(), false;
  // The straight way first: on open ground the exe's walk, untouched.
  // Watched while it walks (a corner, a slope), the grid's from then on.
  const bool should_walk = !(reach > 1.0f && flat <= reach * 1.25f);  // not while he waits in his band
  if (!should_walk || now - line_t > 6.0f) {
    line_t = now, line_pos[0] = pos[0], line_pos[1] = pos[2];
  } else if (now - line_t >= 3.0f) {  // net progress: bouncing on the spot is none
    if (path.empty() && std::hypot(pos[0] - line_pos[0], pos[2] - line_pos[1]) < kProgress) grid_until = now + 10.0f;
    line_t = now, line_pos[0] = pos[0], line_pos[1] = pos[2];
  }
  const bool grids_turn = must_find || now < grid_until;
  float near_it[3] = {to[0], to[1], to[2]};
  if (reach > 1.0f && flat > reach && std::fabs(to[1] - pos[1]) < kHead) {
    const float f = 1.0f - reach / flat;
    for (int i = 0; i < 3; ++i) near_it[i] = pos[i] + (to[i] - pos[i]) * f;
  }
  const bool straight = !grids_turn && line_clear(g, pos, near_it);
  if (straight && path.empty()) return false;
  if (straight && g.built(cell_of(pos[0]), cell_of(pos[2]))) {
    path.clear();
    return false;
  }
  if (!straight && !g.built(cell_of(pos[0]), cell_of(pos[2]))) {
    // Not built here yet: the tiles asked for; the fallback walks meanwhile.
    Way none;
    find_path(g, pos, to, &none);
    return false;
  }
  // A path, new or again: the goal moved 15 m, every 2 s, or none yet.
  const bool moved = std::hypot(to[0] - goal[0], to[2] - goal[1]) > 15.0f;
  if (none_version == g.version && now < none_until && std::hypot(to[0] - none_goal[0], to[2] - none_goal[1]) < 50.0f) return false;
  // The world changed under the path: a new one only if the way on is shut.
  const bool changed = found_version != g.version && now - found_t > 2.0f && !path.empty() && !line_clear(g, pos, path[at].data());
  if (found_version != g.version && !changed) found_version = g.version;
  if (search.active() || ((path.empty() || moved || changed || found_t < -1e8f) && now - last_ask >= 0.5f)) {
    if (!search.active()) last_ask = now, search.begin(pos, to);
    Way p;
    const Find f = search.run(g, &g.search_budget, &p);
    if (f == Find::kWorking) {
      if (path.empty()) return false;  // the walk as before until there is an answer
    } else if (f == Find::kOk) {
      if (!path.empty()) ++repaths;
      ++paths;
      if (path.empty()) watch_t = now, watch_pos[0] = pos[0], watch_pos[1] = pos[2];
      path = std::move(p), at = 0, goal[0] = to[0], goal[1] = to[2], found_t = now, found_version = g.version;
    } else if (f == Find::kNone) {
      path.clear();
      // W17/W27 and the exe's walk, until the world or the goal changes.
      none_wait = none_version == g.version ? std::min(none_wait * 2.0f, 60.0f) : 5.0f;
      none_version = g.version, none_until = now + none_wait, none_goal[0] = to[0], none_goal[1] = to[2];
      return false;
    } else {  // tiles building: not again for a while, the search is the dear part
      last_ask = now + 1.5f;
      if (path.empty()) return false;
    }
  }
  if (path.empty()) return false;
  // The waypoint: reached within 4 m (a walkway is narrow), or passed by when
  // the next is in a clear line.
  while (at + 1 < static_cast<int>(path.size()) &&
         (std::hypot(path[at][0] - pos[0], path[at][2] - pos[2]) < 4.0f || line_clear_avoiding(g, pos, path[at + 1].data(), !goal_in_shape(g, to)))) {
    ++at, ++waypoints;
  }
  const float d = std::hypot(path[at][0] - pos[0], path[at][2] - pos[2]);
  // Unstuck: not kProgress from where he stood 3 s ago (net: bouncing on the
  // spot is no progress): the cells ahead held for 10 s, a new path; the third
  // time in 20 s, a step back; then the fallback.
  if (now - watch_t >= 3.0f) {
    const bool moved_on = std::hypot(pos[0] - watch_pos[0], pos[2] - watch_pos[1]) >= kProgress;
    watch_t = now, watch_pos[0] = pos[0], watch_pos[1] = pos[2];
    if (!moved_on) {
      if (now - stuck_t > 20.0f) stuck = 0;
      ++stuck, stuck_t = now;
      if (stuck >= 4) {
        path.clear(), stuck = 0;
        fallback_until = now + 10.0f;
        return false;
      }
      const float ux = (path[at][0] - pos[0]) / std::max(d, 0.01f), uz = (path[at][2] - pos[2]) / std::max(d, 0.01f);
      for (float s = 4.0f; s <= 12.0f; s += 4.0f) g.hold(cell_of(pos[0] + ux * s), cell_of(pos[2] + uz * s), pos[1], now + 10.0f);
      if (stuck == 3) back_until = now + 2.0f;
      found_t = -1e9f, last_ask = -1e9f;  // ask again
    }
  }
  if (now < back_until && at > 0) {
    out[0] = path[at - 1][0], out[2] = path[at - 1][2];
  } else {
    out[0] = path[at][0], out[2] = path[at][2];
  }
  out[1] = pos[1];
  return true;
}

void dump(const char* file, const std::vector<const Follower*>& followers, int floor_from_top) {
  const Grid& g = grid();
  if (!file || g.all().empty()) return;
  int x0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), z1 = -(1 << 30);
  float ylo = 1e30f, yhi = -1e30f;
  // The floor shown at a place: that many down, or the lowest there is.
  const auto shown = [&](int x, int z) -> const Cell* {
    const Cell* c = nullptr;
    for (int l = 0; l <= floor_from_top && l < kLevels; ++l) {
      const Cell* n = g.cell(x, z, l);
      if (!n) return nullptr;
      if (!(n->flags & kNoGround)) c = n;
    }
    return c ? c : g.cell(x, z, 0);
  };
  for (const auto& kv : g.all()) {
    const int tx = static_cast<int32_t>(kv.first >> 32), tz = static_cast<int32_t>(kv.first & 0xffffffffu);
    x0 = std::min(x0, tx * kTile), z0 = std::min(z0, tz * kTile);
    x1 = std::max(x1, tx * kTile + kTile - 1), z1 = std::max(z1, tz * kTile + kTile - 1);
    if (!kv.second->done()) continue;
    for (int z = tz * kTile; z < (tz + 1) * kTile; ++z)
      for (int x = tx * kTile; x < (tx + 1) * kTile; ++x)
        if (const Cell* c = shown(x, z); c && !(c->flags & kNoGround)) ylo = std::min(ylo, c->y), yhi = std::max(yhi, c->y);
  }
  const int w = x1 - x0 + 1, h = z1 - z0 + 1;
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3, 40);
  for (int z = z0; z <= z1; ++z)
    for (int x = x0; x <= x1; ++x) {
      uint8_t* p = &px[(static_cast<size_t>(z - z0) * w + (x - x0)) * 3];
      const Cell* c = shown(x, z);
      if (!c) continue;
      if (c->flags & kNoGround) {
        p[0] = p[1] = p[2] = 0;
        continue;
      }
      const int l = static_cast<int>(c - g.cell(x, z, 0));
      const uint8_t v = static_cast<uint8_t>(80 + 150 * (c->y - ylo) / std::max(1.0f, yhi - ylo));
      p[0] = p[1] = p[2] = v;
      if (c->depth > 0) p[0] = static_cast<uint8_t>(v / 3), p[1] = static_cast<uint8_t>(v / 2), p[2] = 220;
      if (!g.walkable(x, z, l)) p[0] = 150, p[1] = 20, p[2] = 20;
    }
  std::vector<const Way*> paths;
  for (const Follower* f : followers)
    if (f) paths.push_back(&f->path);
  for (const Way* way : paths)
    for (const auto& q : *way) {
      const int x = cell_of(q[0]), z = cell_of(q[2]);
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          if (x + dx < x0 || x + dx > x1 || z + dz < z0 || z + dz > z1) continue;
          uint8_t* p = &px[(static_cast<size_t>(z + dz - z0) * w + (x + dx - x0)) * 3];
          p[0] = 20, p[1] = 220, p[2] = 20;
        }
    }
  if (std::FILE* fp = std::fopen(file, "wb")) {
    std::fprintf(fp, "P6\n# origin %d %d cell %g\n%d %d\n255\n", x0, z0, static_cast<double>(kCell), w, h);
    std::fwrite(px.data(), 1, px.size(), fp);
    std::fclose(fp);
  }
}

}  // namespace mech3::ai::nav
