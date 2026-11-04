#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// Ours, not the exe's (W58, docs/lancemate-pathing.md): a walkability grid
// over the mission's world and paths on it, so an AI pilot whose straight
// way is blocked walks round. Behind zfix bugs (nav_allies, nav_enemies);
// the brain's decisions stay the exe's, only the point it walks toward is
// a path's waypoint when the straight line is blocked.
namespace mech3::ai::nav {

constexpr float kCell = 4.0f;   // m
constexpr int kTile = 64;       // cells a side: 256 m
constexpr float kClimb = 0.7f;  // rise over run a step may take: tan 35 degrees (the skis' clamp)

// What the grid reads of the world: the ground (water left out) and the
// water's surface at a place, and whether a wall stands between two points.
struct World {
  virtual ~World() = default;
  // The highest ground under (x, z) from `top` down; false when none. `water`
  // the water's surface there, NaN when none.
  virtual bool ground(float x, float z, float top, float* y, float* water) = 0;
  virtual bool wall(const float a[3], const float b[3]) = 0;
  // What stands in the way for now and may leave: x0, z0, x1, z1 each.
  virtual void parked(std::vector<std::array<float, 4>>* out) { (void)out; }
};
World& zcls_world();  // the mission's world through zClass's queries

enum : uint8_t {
  kNoGround = 1,   // no ground: never walked
  kWallE = 2,      // the step east (+x) is a wall or too steep
  kWallS = 4,      // the step south (+z)
  kHeld = 8,       // blocked for a while by the unstuck rule (a vehicle, a gate the grid missed)
};
struct Cell {
  float y = 0;     // ground height
  float depth = 0; // water over it, 0 none
  uint8_t flags = kNoGround;
};
struct Tile {
  std::array<Cell, kTile * kTile> cells;
  int built = 0;   // cells built: grounds 0..n, then edges n..2n; 2n done
  bool done() const { return built >= 2 * kTile * kTile; }
};

class Grid {
 public:
  void reset(World* w);  // a new world: every tile gone
  World* world() const { return world_; }
  // The tile holding cell (cx, cz) built within `budget_us` of work (true
  // when it is done); queued otherwise.
  bool build(int tx, int tz, double* budget_us);
  const Cell* cell(int cx, int cz) const;  // null: its tile not built yet
  bool built(int cx, int cz) const { return cell(cx, cz) != nullptr; }
  // The cell's own walkability, with the 4 m buffer: it and its eight
  // neighbours have ground and no wall between them. Unknown (unbuilt): false.
  bool walkable(int cx, int cz) const;
  bool step_ok(int cx, int cz, int dx, int dz) const;  // the move to the neighbour
  void hold(int cx, int cz, float until);              // the unstuck rule's block
  void release(float now);                             // holds past their time lifted
  size_t tiles() const { return tiles_.size(); }
  int tiles_done() const;
  std::vector<std::pair<int, int>> queue;              // tiles asked for, oldest first
  // For the dump: every tile.
  const std::unordered_map<uint64_t, std::unique_ptr<Tile>>& all() const { return tiles_; }
  static uint64_t key(int tx, int tz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(tx)) << 32) | static_cast<uint32_t>(tz);
  }

 private:
  Tile* tile(int tx, int tz, bool make);
  void build_ground(Tile& t, int tx, int tz, int i);
  void build_edges(Tile& t, int tx, int tz, int i);
  World* world_ = nullptr;
  std::unordered_map<uint64_t, std::unique_ptr<Tile>> tiles_;
  std::vector<std::pair<std::array<int, 2>, float>> held_;
};
Grid& grid();

inline int cell_of(float v) { return static_cast<int>(std::floor(v / kCell)); }
inline float centre_of(int c) { return (static_cast<float>(c) + 0.5f) * kCell; }
inline int tile_of(int c) { return c >= 0 ? c / kTile : -((-c + kTile - 1) / kTile); }

// The straight way from a to b over built cells, each walkable and each step
// allowed; false when blocked or any cell is unbuilt.
bool line_clear(const Grid& g, float ax, float az, float bx, float bz);

enum class Find { kOk, kBuilding, kNone };
// A path from `from` to `to` (or the walkable cell nearest it, within 32 m),
// string-pulled into waypoints (x, z), the last the goal. kBuilding: tiles it
// needs are queued; kNone: no way within the search's window.
Find find_path(Grid& g, const float from[3], const float to[3], std::vector<std::array<float, 2>>* out);

// The once-a-frame work: the queued tiles built within `budget_us`, the
// holds past their time lifted. `now` the game's time (s).
void service(float now, double budget_us);

// The pilot's follower: the path it is on toward the brain's point.
struct Follower {
  std::vector<std::array<float, 2>> path;
  int at = 0;                  // the waypoint walked to
  float goal[2] = {};          // the brain's point the path was found for
  float found_t = -1e9f;       // when
  float watch_t = 0, watch_pos[2] = {};  // progress: where the pilot stood at the window's start
  float last_ask = -1e9f;      // the last find (one each 0.5 s)
  int stuck = 0;               // unstuck rounds in the last 20 s
  float stuck_t = -1e9f;
  float back_until = -1e9f;    // stepping back until then
  float fallback_until = -1e9f;  // the exe's way (or W17/W27) until then
  int32_t paths = 0, repaths = 0, waypoints = 0;  // for the tests
  // Where to walk this frame toward `to` (the brain's point), the pilot at
  // `pos`: true with `out` a waypoint when the straight way is blocked and a
  // path is had; false to go as before (the exe's, or W17/W27).
  bool step(const float pos[3], const float to[3], float now, float dt, float out[3]);
};

// Whether a pilot of that side walks by the grid: zfix bugs and the side's
// switch, and not in a network game.
bool on_for(bool ally);

// Ours (W58, for the tests and the dump): MW3_NAV_DUMP=file.ppm writes the
// built cells (black no ground, dark red walls, grey by height, blue water)
// and the paths (green) each 10 s of game time and at the end.
void dump(const char* path, const std::vector<const Follower*>& followers);

}  // namespace mech3::ai::nav
