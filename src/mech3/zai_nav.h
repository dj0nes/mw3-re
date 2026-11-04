#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
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
constexpr int kLevels = 3;      // floors a place may hold (the ground over a mine, a bridge)
constexpr float kHead = 12.0f;  // the room a mech needs under what is above it

// A floor at a place: the height of its ground, and of the water over it (NaN none).
struct Floor {
  float y = 0, water = 0;
};
// What the grid reads of the world.
struct World {
  virtual ~World() = default;
  // The grounds under (x, z), highest first, water left out; how many (at most max).
  virtual int ground(float x, float z, Floor* out, int max) = 0;
  virtual bool wall(const float a[3], const float b[3]) = 0;
  // What stands in the way for now and may leave: its hull's x0, z0, x1, z1 and its height.
  virtual void parked(std::vector<std::array<float, 5>>* out) { (void)out; }
  // Whether the designers told the AI to keep out of here (the mission's
  // boundary shapes, docs/ai.md section 11).
  virtual bool avoid(float x, float y, float z) { (void)x, (void)y, (void)z; return false; }
};
World& zcls_world();  // the mission's world through zClass's queries

enum : uint16_t {
  kNoGround = 1,   // no such floor here
  kWallE = 2,      // a wall on the step east (+x)
  kWallS = 4,      // a wall on the step south (+z)
  kHeld = 8,       // blocked for a while (a parked vehicle; the unstuck rule)
  kLow = 16,       // too little room under the floor above (a bridge at head height)
  kNear = 32,      // round a parked vehicle, for a while: walked only if there is no other way
  kAvoidKnown = 64,  // World::avoid asked here
  kAvoid = 128,      // a shape the AI keeps out of: as kNear
  kRoomKnown = 256,  // tight asked here
  kTight = 512,      // a wall or a rise two cells off: a mech's corners and probes reach it
};
struct Cell {
  float y = 0;       // ground height
  float depth = 0;   // water over it, 0 none
  uint16_t flags = kNoGround;
  int8_t east = -1;  // the floor a step east (+x) reaches, -1 none (too steep, walled, no ground)
  int8_t south = -1; // the same south (+z)
};
struct Tile {
  std::array<Cell, kTile * kTile * kLevels> cells;
  int built = 0;   // places built: grounds 0..n, then edges n..2n; 2n done
  bool done() const { return built >= 2 * kTile * kTile; }
};

class Grid {
 public:
  void reset(World* w);  // a new world: every tile gone
  World* world() const { return world_; }
  // The tile (tx, tz) built for at most `*steps` places' work, taken off it
  // (true when it is done). By work, not time: the same mission plays the same.
  bool build(int tx, int tz, int* steps);
  const Cell* cell(int cx, int cz, int level) const;  // null: its tile not built yet
  bool built(int cx, int cz) const { return cell(cx, cz, 0) != nullptr; }
  // The floor here nearest height y, -1 none.
  int level_at(int cx, int cz, float y) const;
  // The floor's own walkability, with the 4 m buffer against what would hit
  // a mech: no wall among it and its eight neighbours at its height, none of
  // them rising past a step into its body. A drop beside it needs no margin
  // (a thin walkway, a ledge). Unknown (unbuilt): false.
  bool walkable(int cx, int cz, int level) const;
  // The floor the move to the neighbouring place reaches, -1 none.
  int step(int cx, int cz, int level, int dx, int dz) const;
  bool avoided(int cx, int cz, int level);             // World::avoid here, asked once
  bool tight(int cx, int cz, int level);               // kTight here, worked out once
  void forget_avoid();                                 // the shapes changed
  void hold(int cx, int cz, float y, float until, uint16_t what = kHeld);  // blocked (or kNear) for a while
  void release(float now);                             // holds past their time lifted
  // The tiles under that rectangle built again behind the old ones, as what
  // stands there has changed (a door opened, a ramp fallen, a building down).
  void refresh(float x0, float z0, float x1, float z1, float now);
  int version = 0;  // counts the changes a path may depend on
  int resets = 0;   // counts reset: a search begun before one is over
  size_t tiles() const { return tiles_.size(); }
  int tiles_done() const;
  std::vector<std::pair<int, int>> queue;              // tiles asked for, oldest first
  float parked_t = -1e30f;                             // the last look at the parked vehicles
  int search_budget = 0;                               // the paths' work left this frame (cells), all pilots'
  // For the dump: every tile.
  const std::unordered_map<uint64_t, std::unique_ptr<Tile>>& all() const { return tiles_; }
  static uint64_t key(int tx, int tz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(tx)) << 32) | static_cast<uint32_t>(tz);
  }

 private:
  struct Held {
    int cx, cz, level;
    float until;
    uint16_t what;
  };
  Tile* tile(int tx, int tz, bool make);
  void build_ground(Tile& t, int tx, int tz, int i);
  void build_edges(Tile& t, int tx, int tz, int i);
  Cell* held_cell(const Held& h);
  World* world_ = nullptr;
  std::unordered_map<uint64_t, std::unique_ptr<Tile>> tiles_;
  std::unordered_map<uint64_t, std::unique_ptr<Tile>> fresh_;  // being built again
  std::unordered_map<uint64_t, float> refreshed_;              // when each tile was last asked again
  std::vector<Held> held_;
};
Grid& grid();

inline int cell_of(float v) { return static_cast<int>(std::floor(v / kCell)); }
inline float centre_of(int c) { return (static_cast<float>(c) + 0.5f) * kCell; }
inline int tile_of(int c) { return c >= 0 ? c / kTile : -((-c + kTile - 1) / kTile); }

// The straight way from a to b over built cells on one walk of floors, each
// walkable and each step allowed, ending on b's floor; false when blocked or
// any cell is unbuilt.
bool line_clear(const Grid& g, const float a[3], const float b[3]);
// The same, not crossing into a shape the AI keeps out of or a tight cell
// (from inside to inside it may): a way a mech's whole body takes.
bool line_clear_avoiding(Grid& g, const float a[3], const float b[3], bool shapes = true);
// Whether the goal stands in a shape the AI keeps out of: sent there, the shapes
// no longer count on the way (the fewest cells in one could be a long way round).
bool goal_in_shape(Grid& g, const float to[3]);

enum class Find { kOk, kBuilding, kNone, kWorking };
using Way = std::vector<std::array<float, 3>>;
// A path from `from` to `to` (or the walkable cell nearest it, within 32 m, on
// its floor), string-pulled into waypoints (x, y, z), the last the goal. The
// search looks 256 m past both ends, then 512 m, then 1 km (a river's far
// ramp), over the tiles built. kBuilding: none yet, and tiles it needs are
// queued; kNone: no way in the widest window.
Find find_path(Grid& g, const float from[3], const float to[3], Way* out);

// The same over several frames: run() does at most `*budget` cells' work, taken
// off it, and answers kWorking while there is more. A failed search over the
// widest window takes 320 ms in Release; one frame must not pay that.
class PathSearch {
 public:
  bool active() const { return active_; }
  void begin(const float from[3], const float to[3]);
  Find run(Grid& g, int* budget, Way* out);

 private:
  using Item = std::pair<float, int32_t>;
  bool active_ = false, started_ = false, shapes_ = true, open_window_ = false;
  float from_[3] = {}, to_[3] = {};
  int fx_ = 0, fz_ = 0, tx_ = 0, tz_ = 0, sx_ = 0, sz_ = 0, sl_ = 0, gx_ = 0, gz_ = 0, gl_ = 0;
  int window_ = 0, x0_ = 0, x1_ = 0, z0_ = 0, z1_ = 0, w_ = 0, expanded_ = 0, resets_ = 0;
  std::vector<float> cost_;
  std::vector<int32_t> from_of_;
  std::vector<Item> open_;
  float octile(int x, int z) const;
  bool finish(Grid& g, Way* out);
};

// The once-a-frame work: the queued tiles built for `steps` places, the holds
// past their time lifted. `now` the game's time (s).
void service(float now, int steps, int search_steps = 3000);
// A mission's start, replays too (the same world's address, its nodes new): the
// grid and what it watches begun again.
void restart();

// The pilot's follower: the path it is on toward the brain's point.
struct Follower {
  Way path;
  PathSearch search;           // the one under way
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
  // No way found: asked again only when the world or the goal changed, or
  // after a wait that doubles.
  int none_version = -1;
  float none_until = -1e9f, none_wait = 5.0f, none_goal[2] = {};
  int found_version = -1;      // the grid the path was found on
  // The exe's own walk watched: no 2 m in 3 s and the grid takes over for 10 s.
  float line_t = -1e9f, line_pos[2] = {}, grid_until = -1e9f;
  // Where to walk this frame toward `to` (the brain's point), the pilot at
  // `pos`: true with `out` a waypoint when the straight way is blocked and a
  // path is had; false to go as before (the exe's, or W17/W27).
  // `must_find`: the straight way is not to be taken as it is (the exe's pull
  // round a shape the AI keeps out of has brought him to its edge).
  // `reach`: he need only come that near on the same storey, so the straight
  // way is judged that far.
  bool step(const float pos[3], const float to[3], float now, float dt, float out[3], bool must_find = false, float reach = 0.0f);
};

// Whether a pilot of that side walks by the grid: zfix bugs and the side's
// switch, and not in a network game.
bool on_for(bool ally);

// Ours (W58, for the tests): the built cells to a PPM (black no ground, dark red
// not walkable, grey by height, blue water) with the followers' paths in green;
// the top floor, or the floor that many down where there is one.
void dump(const char* path, const std::vector<const Follower*>& followers, int floor_from_top = 0);

}  // namespace mech3::ai::nav
