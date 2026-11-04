#include "mech3/zai_nav.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <queue>

#include "gamez/zclass/cls_di.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zutil/zfix.h"
#include "mech3/app_mech.h"
#include "mech3/app_vehicle.h"
#include "mech3/mission_world.h"
#include "mech3/options.h"

namespace mech3::ai::nav {

namespace {
// The world's own queries (as mech3's wall_between and the tests' ground):
// every zone's ground, water a material of soil 1.
struct ZclsWorld : World {
  bool ground(float x, float z, float top, float* y, float* water) override {
    zcls::Node* w = world_hooks().world;
    *water = std::nanf("");
    if (!w) return false;
    float probe[3] = {x, top, z};
    static zcls::GroundHits hits;
    const int32_t mode = zmod::zones().mode;
    zmod::zones().mode = 0;
    zcls::ground_query(w, probe, 1, top, &hits);
    zmod::zones().mode = mode;
    bool any = false;
    for (int i = 0; i < hits.count; ++i) {
      const float h = hits.hit[i].height;
      if (hits.hit[i].material && hits.hit[i].material->soil == 1) {
        if (!(h <= *water)) *water = h;
        continue;
      }
      if (!any || h > *y) *y = h, any = true;
    }
    return any;
  }
  bool wall(const float a[3], const float b[3]) override {
    zcls::Node* w = world_hooks().world;
    if (!w) return false;
    float seg[6] = {a[0], a[1], a[2], b[0], b[1], b[2]};
    static zcls::SegmentHits hits[1];
    zcls::segment_query(w, seg, 2, hits);
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
};

constexpr float kTop = 2000.0f;          // the ground probes' start
constexpr float kWallLow = 2.5f, kWallHigh = 6.5f;  // the wall probes over the higher ground (a mech's legs and body)
constexpr int kWindow = 64;              // the search's margin round from and to, in cells (256 m)
constexpr int kMaxExpand = 60000;

// The dump's copies of the paths walked lately (a follower may be gone by
// the dump), by follower.
std::unordered_map<const void*, std::vector<std::array<float, 2>>>& dump_paths() {
  static std::unordered_map<const void*, std::vector<std::array<float, 2>>> m;
  return m;
}
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
  tiles_.clear();
  queue.clear();
  held_.clear();
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
  Cell& c = t.cells[i];
  const int cx = tx * kTile + i % kTile, cz = tz * kTile + i / kTile;
  float y = 0, water = 0;
  if (world_->ground(centre_of(cx), centre_of(cz), kTop, &y, &water)) {
    c.y = y, c.flags = 0;
    c.depth = std::isfinite(water) && water > y ? water - y : 0.0f;
  } else {
    c.flags = kNoGround;
  }
}

void Grid::build_edges(Tile& t, int tx, int tz, int i) {
  Cell& c = t.cells[i];
  if (c.flags & kNoGround) return;
  const int lx = i % kTile, lz = i / kTile;
  const int cx = tx * kTile + lx, cz = tz * kTile + lz;
  for (int k = 0; k < 2; ++k) {
    const int nx = cx + (k == 0), nz = cz + (k == 1);
    float ny = 0, water = 0;
    bool has = false;
    if (k == 0 ? lx + 1 < kTile : lz + 1 < kTile) {
      const Cell& n = t.cells[(k == 0 ? i + 1 : i + kTile)];
      has = !(n.flags & kNoGround), ny = n.y;
    } else {
      has = world_->ground(centre_of(nx), centre_of(nz), kTop, &ny, &water);
    }
    const uint8_t bit = k == 0 ? kWallE : kWallS;
    if (!has || std::fabs(ny - c.y) > kClimb * kCell) {
      c.flags |= bit;
      continue;
    }
    const float base = std::max(c.y, ny);
    for (float up : {kWallLow, kWallHigh}) {
      const float a[3] = {centre_of(cx), base + up, centre_of(cz)}, b[3] = {centre_of(nx), base + up, centre_of(nz)};
      if (world_->wall(a, b)) {
        c.flags |= bit;
        break;
      }
    }
  }
}

bool Grid::build(int tx, int tz, double* budget_us) {
  Tile* t = tile(tx, tz, true);
  if (t->done()) return true;
  const auto t0 = std::chrono::steady_clock::now();
  const int n = kTile * kTile;
  while (!t->done()) {
    if (t->built < n) build_ground(*t, tx, tz, t->built);
    else build_edges(*t, tx, tz, t->built - n);
    ++t->built;
    if ((t->built & 15) == 0) {
      const double spent = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
      if (spent >= *budget_us) {
        *budget_us = 0;
        return false;
      }
    }
  }
  *budget_us -= std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
  return true;
}

const Cell* Grid::cell(int cx, int cz) const {
  const int tx = tile_of(cx), tz = tile_of(cz);
  auto it = tiles_.find(key(tx, tz));
  if (it == tiles_.end() || !it->second->done()) return nullptr;
  return &it->second->cells[(cz - tz * kTile) * kTile + (cx - tx * kTile)];
}

namespace {
// The edge between a cell and its east / south neighbour: walkable both ends
// and not walled. From either side.
bool edge_free(const Grid& g, int cx, int cz, int dx, int dz) {
  if (dx == 0 && dz == 0) return true;
  const int ax = dx < 0 ? cx - 1 : cx, az = dz < 0 ? cz - 1 : cz;
  const Cell* a = g.cell(ax, az);
  if (!a || (a->flags & (kNoGround | kHeld))) return false;
  return !(a->flags & (dx != 0 ? kWallE : kWallS));
}
}  // namespace

bool Grid::step_ok(int cx, int cz, int dx, int dz) const {
  if (!walkable(cx + dx, cz + dz)) return false;
  if (dx != 0 && dz != 0)  // a diagonal: both orthogonal ways free too
    return edge_free(*this, cx, cz, dx, 0) && edge_free(*this, cx + dx, cz, 0, dz) && edge_free(*this, cx, cz, 0, dz) &&
           edge_free(*this, cx, cz + dz, dx, 0);
  return edge_free(*this, cx, cz, dx, dz);
}

bool Grid::walkable(int cx, int cz) const {
  for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx) {
      const Cell* c = cell(cx + dx, cz + dz);
      if (!c || (c->flags & (kNoGround | kHeld))) return false;
    }
  // No wall inside the 3 x 3: the edges between its cells.
  for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 0; ++dx)
      if (cell(cx + dx, cz + dz)->flags & kWallE) return false;
  for (int dz = -1; dz <= 0; ++dz)
    for (int dx = -1; dx <= 1; ++dx)
      if (cell(cx + dx, cz + dz)->flags & kWallS) return false;
  return true;
}

void Grid::hold(int cx, int cz, float until) {
  Tile* t = tile(tile_of(cx), tile_of(cz), false);
  if (!t || !t->done()) return;
  t->cells[(cz - tile_of(cz) * kTile) * kTile + (cx - tile_of(cx) * kTile)].flags |= kHeld;
  held_.push_back({{cx, cz}, until});
}

void Grid::release(float now) {
  for (auto it = held_.begin(); it != held_.end();) {
    if (it->second > now) {
      ++it;
      continue;
    }
    if (Tile* t = tile(tile_of(it->first[0]), tile_of(it->first[1]), false))
      t->cells[(it->first[1] - tile_of(it->first[1]) * kTile) * kTile + (it->first[0] - tile_of(it->first[0]) * kTile)].flags &=
          static_cast<uint8_t>(~kHeld);
    it = held_.erase(it);
  }
}

bool line_clear(const Grid& g, float ax, float az, float bx, float bz) {
  int cx = cell_of(ax), cz = cell_of(az);
  const int ex = cell_of(bx), ez = cell_of(bz);
  if (!g.walkable(cx, cz)) return false;
  // A supercover walk of the cells the segment crosses, each step allowed.
  const float dx = bx - ax, dz = bz - az;
  const int sx = dx > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
  const float tdx = dx != 0 ? kCell / std::fabs(dx) : 1e30f, tdz = dz != 0 ? kCell / std::fabs(dz) : 1e30f;
  float tmx = dx != 0 ? ((sx > 0 ? (cx + 1) * kCell : cx * kCell) - ax) / dx : 1e30f;
  float tmz = dz != 0 ? ((sz > 0 ? (cz + 1) * kCell : cz * kCell) - az) / dz : 1e30f;
  for (int n = 0; (cx != ex || cz != ez) && n < 4096; ++n) {
    int mx = 0, mz = 0;
    if (tmx < tmz) mx = sx, tmx += tdx;
    else mz = sz, tmz += tdz;
    if (!g.step_ok(cx, cz, mx, mz)) return false;
    cx += mx, cz += mz;
  }
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

bool nearest_walkable(const Grid& g, int cx, int cz, int r, int* ox, int* oz) {
  if (g.walkable(cx, cz)) return *ox = cx, *oz = cz, true;
  for (int d = 1; d <= r; ++d)
    for (int dz = -d; dz <= d; ++dz)
      for (int dx = -d; dx <= d; ++dx) {
        if (std::max(std::abs(dx), std::abs(dz)) != d) continue;
        if (g.walkable(cx + dx, cz + dz)) return *ox = cx + dx, *oz = cz + dz, true;
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

Find find_path(Grid& g, const float from[3], const float to[3], std::vector<std::array<float, 2>>* out) {
  out->clear();
  const int fx = cell_of(from[0]), fz = cell_of(from[2]), tx = cell_of(to[0]), tz = cell_of(to[2]);
  const int x0 = std::min(fx, tx) - kWindow, x1 = std::max(fx, tx) + kWindow;
  const int z0 = std::min(fz, tz) - kWindow, z1 = std::max(fz, tz) + kWindow;
  // The tiles under the straight way and round both ends first.
  bool missing = false;
  for (int tz2 = tile_of(z0); tz2 <= tile_of(z1) && !missing; ++tz2)
    for (int tx2 = tile_of(x0); tx2 <= tile_of(x1) && !missing; ++tx2) {
      auto it = g.all().find(Grid::key(tx2, tz2));
      missing = it == g.all().end() || !it->second->done();
    }
  if (missing) {
    ask_tiles(g, fx - 8, fz - 8, fx + 8, fz + 8);
    ask_tiles(g, tx - 8, tz - 8, tx + 8, tz + 8);
    ask_tiles(g, x0, z0, x1, z1);
    return Find::kBuilding;
  }
  int sx, sz, gx, gz;
  if (!nearest_walkable(g, fx, fz, 3, &sx, &sz) || !nearest_walkable(g, tx, tz, 8, &gx, &gz)) return Find::kNone;
  const int w = x1 - x0 + 1, h = z1 - z0 + 1;
  auto idx = [&](int x, int z) { return (z - z0) * w + (x - x0); };
  std::vector<float> cost(static_cast<size_t>(w) * h, 1e30f);
  std::vector<int32_t> from_of(static_cast<size_t>(w) * h, -1);
  using Item = std::pair<float, int32_t>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
  auto octile = [&](int x, int z) {
    const float ax = static_cast<float>(std::abs(x - gx)), az = static_cast<float>(std::abs(z - gz));
    return (std::max(ax, az) + 0.41421356f * std::min(ax, az)) * kCell;
  };
  cost[idx(sx, sz)] = 0;
  open.push({octile(sx, sz), idx(sx, sz)});
  int expanded = 0;
  bool found = false;
  while (!open.empty() && expanded < kMaxExpand) {
    const auto [f, i] = open.top();
    open.pop();
    const int x = x0 + i % w, z = z0 + i / w;
    if (f - octile(x, z) > cost[i] + 1e-3f) continue;  // stale
    if (x == gx && z == gz) {
      found = true;
      break;
    }
    ++expanded;
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dz) continue;
        const int nx = x + dx, nz = z + dz;
        if (nx < x0 || nx > x1 || nz < z0 || nz > z1 || !g.step_ok(x, z, dx, dz)) continue;
        const Cell* c = g.cell(nx, nz);
        const float step = (dx && dz ? 1.41421356f : 1.0f) * kCell * water_cost(c->depth);
        const int j = idx(nx, nz);
        if (cost[i] + step < cost[j]) {
          cost[j] = cost[i] + step, from_of[j] = i;
          open.push({cost[j] + octile(nx, nz), j});
        }
      }
  }
  if (!found) return Find::kNone;
  std::vector<std::array<float, 2>> cells;
  for (int i = idx(gx, gz); i >= 0; i = from_of[i]) cells.push_back({centre_of(x0 + i % w), centre_of(z0 + i / w)});
  std::reverse(cells.begin(), cells.end());
  // String-pulled: from each point the farthest one in a clear line.
  std::array<float, 2> here = {from[0], from[2]};
  size_t k = 0;
  while (k + 1 < cells.size()) {
    size_t far = k + 1;
    for (size_t j = cells.size() - 1; j > k + 1; --j)
      if (line_clear(g, here[0], here[1], cells[j][0], cells[j][1])) {
        far = j;
        break;
      }
    out->push_back(cells[far]);
    here = cells[far];
    k = far;
  }
  if (out->empty()) out->push_back(cells.back());
  // The last waypoint the brain's point itself when it is walkable.
  if (g.walkable(tx, tz)) out->back() = {to[0], to[2]};
  return Find::kOk;
}

void service(float now, double budget_us) {
  Grid& g = grid();
  if (g.world() != &zcls_world() || (world_hooks().world == nullptr && g.tiles())) g.reset(&zcls_world());
  static const zcls::Node* world_seen = nullptr;
  if (world_hooks().world != world_seen) world_seen = world_hooks().world, g.reset(&zcls_world());
  g.release(now);
  static float dumped = -1e30f;
  if (const char* file = std::getenv("MW3_NAV_DUMP"); file && now - dumped >= 10.0f && g.tiles()) {
    dumped = now;
    dump(file, {});
    dump_paths().clear();
  }
  while (!g.queue.empty() && budget_us > 0) {
    const auto [tx, tz] = g.queue.front();
    if (g.build(tx, tz, &budget_us)) g.queue.erase(g.queue.begin());
  }
}

bool on_for(bool ally) {
  const zfix::Options& o = zfix::options();
  if (!o.bugs || options().multiplayer) return false;
  return ally ? o.nav_allies : o.nav_enemies;
}

bool Follower::step(const float pos[3], const float to[3], float now, float dt, float out[3]) {
  Grid& g = grid();
  if (now < fallback_until) return false;
  const float flat = std::hypot(to[0] - pos[0], to[2] - pos[2]);
  if (flat < 8.0f) return path.clear(), false;
  // The straight way first: on open ground the exe's walk, untouched.
  const bool straight = line_clear(g, pos[0], pos[2], to[0], to[2]);
  if (straight && path.empty()) return false;
  if (straight && g.built(cell_of(pos[0]), cell_of(pos[2]))) {
    path.clear();
    return false;
  }
  if (!straight && !g.built(cell_of(pos[0]), cell_of(pos[2]))) {
    // Not built here yet: the tiles asked for; the fallback walks meanwhile.
    std::vector<std::array<float, 2>> none;
    find_path(g, pos, to, &none);
    return false;
  }
  // A path, new or again: the goal moved 15 m, every 2 s, or none yet.
  const bool moved = std::hypot(to[0] - goal[0], to[2] - goal[1]) > 15.0f;
  if ((path.empty() || moved || now - found_t > 2.0f) && now - last_ask >= 0.5f) {
    last_ask = now;
    std::vector<std::array<float, 2>> p;
    const Find f = find_path(g, pos, to, &p);
    if (f == Find::kOk) {
      if (!path.empty()) ++repaths;
      ++paths;
      if (path.empty()) watch_t = now, watch_pos[0] = pos[0], watch_pos[1] = pos[2];
      path = std::move(p), at = 0, goal[0] = to[0], goal[1] = to[2], found_t = now;
    } else if (f == Find::kNone) {
      path.clear();
      fallback_until = now + 5.0f;  // W17/W27 and the exe's walk
      return false;
    } else if (path.empty()) {
      return false;
    }
  }
  if (path.empty()) return false;
  // The waypoint: reached within 6 m, or passed by when the next is in a clear line.
  while (at + 1 < static_cast<int>(path.size()) &&
         (std::hypot(path[at][0] - pos[0], path[at][1] - pos[2]) < 6.0f ||
          line_clear(g, pos[0], pos[2], path[at + 1][0], path[at + 1][1]))) {
    ++at, ++waypoints;
  }
  const float d = std::hypot(path[at][0] - pos[0], path[at][1] - pos[2]);
  // Unstuck: not 2 m from where it stood 3 s ago: the cells ahead held for
  // 10 s, a new path; the third time in 20 s, a step back; then the fallback.
  if (std::hypot(pos[0] - watch_pos[0], pos[2] - watch_pos[1]) > 2.0f) {
    watch_t = now, watch_pos[0] = pos[0], watch_pos[1] = pos[2];
  } else if (now - watch_t > 3.0f) {
    if (now - stuck_t > 20.0f) stuck = 0;
    ++stuck, stuck_t = now;
    watch_t = now;
    if (stuck >= 4) {
      path.clear(), stuck = 0;
      fallback_until = now + 10.0f;
      return false;
    }
    const float ux = (path[at][0] - pos[0]) / std::max(d, 0.01f), uz = (path[at][1] - pos[2]) / std::max(d, 0.01f);
    for (float s = 4.0f; s <= 12.0f; s += 4.0f) g.hold(cell_of(pos[0] + ux * s), cell_of(pos[2] + uz * s), now + 10.0f);
    if (stuck == 3) back_until = now + 2.0f;
    found_t = -1e9f, last_ask = -1e9f;  // ask again
  }
  if (now < back_until && at > 0) {
    out[0] = path[at - 1][0], out[2] = path[at - 1][1];
  } else {
    out[0] = path[at][0], out[2] = path[at][1];
  }
  out[1] = pos[1];
  if (std::getenv("MW3_NAV_DUMP")) dump_paths()[this] = path;
  static const bool log = std::getenv("MW3_NAV_LOG") != nullptr;
  if (log && static_cast<int>(now * 2) != static_cast<int>((now - dt) * 2))
    std::fprintf(stderr, "nav %.1f: at %.0f %.0f to %.0f %.0f, wp %d/%zu %.0f %.0f (%.0f m), stuck %d\n", now, pos[0], pos[2], to[0],
                 to[2], at, path.size(), out[0], out[2], d, stuck);
  return true;
}

void dump(const char* file, const std::vector<const Follower*>& followers) {
  const Grid& g = grid();
  if (!file || g.all().empty()) return;
  int x0 = 1 << 30, z0 = 1 << 30, x1 = -(1 << 30), z1 = -(1 << 30);
  float ylo = 1e30f, yhi = -1e30f;
  for (const auto& kv : g.all()) {
    const int tx = static_cast<int32_t>(kv.first >> 32), tz = static_cast<int32_t>(kv.first & 0xffffffffu);
    x0 = std::min(x0, tx * kTile), z0 = std::min(z0, tz * kTile);
    x1 = std::max(x1, tx * kTile + kTile - 1), z1 = std::max(z1, tz * kTile + kTile - 1);
    if (kv.second->done())
      for (const Cell& c : kv.second->cells)
        if (!(c.flags & kNoGround)) ylo = std::min(ylo, c.y), yhi = std::max(yhi, c.y);
  }
  const int w = x1 - x0 + 1, h = z1 - z0 + 1;
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3, 40);
  for (int z = z0; z <= z1; ++z)
    for (int x = x0; x <= x1; ++x) {
      uint8_t* p = &px[(static_cast<size_t>(z - z0) * w + (x - x0)) * 3];
      const Cell* c = g.cell(x, z);
      if (!c) continue;
      if (c->flags & kNoGround) {
        p[0] = p[1] = p[2] = 0;
      } else if (c->flags & (kWallE | kWallS | kHeld)) {
        p[0] = 150, p[1] = 20, p[2] = 20;
      } else {
        const uint8_t v = static_cast<uint8_t>(80 + 150 * (c->y - ylo) / std::max(1.0f, yhi - ylo));
        p[0] = p[1] = p[2] = v;
        if (c->depth > 0) p[0] = static_cast<uint8_t>(v / 3), p[1] = static_cast<uint8_t>(v / 2), p[2] = 220;
      }
    }
  std::vector<const std::vector<std::array<float, 2>>*> paths;
  for (const Follower* f : followers)
    if (f) paths.push_back(&f->path);
  for (const auto& kv : dump_paths()) paths.push_back(&kv.second);
  for (const auto* p : paths)
    for (size_t k = 0; k < p->size(); ++k) {
      const int x = cell_of((*p)[k][0]), z = cell_of((*p)[k][1]);
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
