# Lancemates that walk round things (W58, design, 2026-11-03)

Owen, for 1.0.2: "the pathing is a ridiculous joke in the original. they
should be able to walk around walls and attack targets and such." An
improvement behind `zfix::options().bugs` (`--original` and the probes keep
the exe's); this file is the design, for the lead before any code.

## The port is not making it worse

The two pieces ai.md lists as left on the AI's path are not on the
lancemates' way:
- **The nets' ground snap** (node positions put on the ground at load): the
  node state measures flat (0x4cee80 flattens the carrot, 0x51e210), so an
  unsnapped height changes no "reached".
- **The built-in net** (0x604008 over 0x4c0090): only a pilot without a net
  of its own gets it, and the mission start (player.cpp 0x49e810) makes none
  such; the lance orders use the target's VehicleNet (0x4d70f0, ported) and
  the formation net (0x603fb8, ported).
The follow, attack and formation paths are differential-tested
(`test_ai_attack.py`, `test_ai_node.py`, `test_ai_orders.py`); W17 and W27
traced the walls to the exe's own logic.

## (a) What the original does

A lancemate's movement is a carrot on a segment (0x4cea30): the brain's
behaviour gives a point and the steering walks straight at it.
- **Follow / attack my target / form on me** (0x4ea450, 0x4ea4b0): the
  target's VehicleNet follow (0x4d1980) aims the carrot straight at the
  leader (or the formation slot from 0x603fb8), within a range band
  (+0x28, +0x2c). No net, no region, no look at the world.
- **The AI nets** (`net_NN.zrd`) are scripted unit routes, 9 to 62 nodes a
  map with one-way links, not a navigation graph; the way finder 0x4c7f70
  found no way between Dominic's and the player's nearest nodes (W17).
- **Fights** (0x4d1d10 approach, the manoeuvre): toward the target, or an
  orbit point round it; again straight.
- **Walls**: the only response is the controls' on_path 1 (0x4a5270): the
  throttle reversed and a yaw kick of speed x 0.01. The contacts' push
  (0x4ce780) then holds the carrot inside the slack and the throttle stays
  0: the lancemate stands at the wall, or bumps along it for ever.
So the point picked is fine; the way to it is a straight line.

What the port already adds behind `bugs` (W17, W27, `FollowState`):
a wall detour (25 m probes in 15-degree steps) and breadcrumbs of the
leader's way. Both are local: a building between two open areas, a canyon
mouth or a town street grid still beats them.

## (b) Obstacle-aware movement

Keep the brain: whom to attack, the slot, the orders, the range band, the
gait and turning. Change only the carrot: instead of the point itself, the
next waypoint of a path to it.

**The grid.** A walkability grid over the world, 4 m cells (a mech is 8 to
14 m wide; 4 m keeps 2-cell streets), heights from the world's ground query
(0x4ee2f0, the zones off, water left out), built **in tiles on demand**:
64 x 64 cells (256 m square) when a path first needs one, kept for the
mission, freed at its end. A cell is blocked when:
- its ground is missing (holes, off the map) or outside the mission
  boundary (the `inout` polygons, section 11; the cell is in the polygon);
- a wall stands in it: the segment query (0x4efcd0, a hit with
  |normal.y| < 0.5, as `wall_between`) at 2.5 and 6.5 m above its ground,
  on its east and south edges (each edge shared, so 2 segments a cell);
- the step to a neighbour climbs more than the skis allow: rise over run
  past tan 35 degrees (the skis' clamp) or n.y below 0.85 (the slide);
- deep water, optional: water deeper than 0.7 of the mech's height (where
  the gait's factor is at its least), costed rather than blocked (x3).
Then a clearance pass (distance to the nearest blocked cell, in cells), and
a mech may use a cell only with clearance of half its width.

**The path.** A* on the 8-connected grid, cost the step length times the
water factor, the heuristic the octile distance; then string-pulled (a
line-of-sight walk over the grid's clearance) into a few waypoints. The
goal is the brain's point; if it is blocked, the nearest free cell to it.

**Following.** The carrot walks the waypoints: the original's steering and
gait take the next waypoint as their point (the carrot's segment set from
the last waypoint to the next), so turning, speed, the range band and
stopping stay the exe's. A waypoint is reached flat within 6 m (as the
breadcrumbs).

**When.** Only when the straight way is blocked: the original's line first
(the grid's clearance along it, or one `wall_between` at 4 m when no tile
is built yet). On open ground nothing changes from the exe's walk. Repath
when the brain's point moves more than 15 m from the path's goal, or every
2 s while following.

## (c) Attacking

When the brain's behaviour is an attack (the follow of an enemy, 0x4d1d10's
approach, the manoeuvre's orbit), the goal becomes a firing spot:
- candidates on rings round the target at 0.6 and 0.8 of the lancemate's
  longest weapon's range (from its weapon manager), 16 directions;
- each: free in the grid, line of sight from its weapons' height to the
  target's centroid (`wall_between`), path cost from here;
- cheap extra: off the player's line of fire (the candidate not within 15
  degrees of the line from the player to the target, nearer than the
  target);
- the best by path cost, kept until the target moves 25 m or the line of
  sight breaks; then pick again (at most once a second).
The brain's firing state (0x4b5b80) and aim are unchanged.

## (d) Getting unstuck

No 2 m of progress along the path in 3 s (the W27 standstill), or a wall
contact (on_path 1) twice in 2 s: mark the cells in front blocked for 10 s
(a dynamic obstacle the grid missed: a vehicle, a wreck, a gate), repath;
on the third in 20 s, step back 10 m along the path, then repath; after
that, the exe's follow for 10 s (as W17's give-up).

## (e) Cost (measured on c1 and c3, 2026-11-03)

- Ground query 2.7 to 2.8 us, a 25 m segment query 4.5 to 6.8 us (this
  Mac, 20000 each).
- A tile: 4096 cells x (1 ground probe + 4 segments) = about 120 ms. Built
  across frames: at most 2 ms of building a frame (about 70 cells), so a
  tile in about 60 frames (2 s at 30 fps); the lancemate keeps the exe's
  walk (and W17/W27) meanwhile. c1m2's area (2.1 x 1.4 km) is about 45
  tiles, 5 s of building in all if every one is needed, in practice the
  few along the lance's way.
- Memory: a cell is 4 bytes (height as a 16-bit value, flags, clearance):
  16 KB a tile, under 1 MB for a whole map.
- A*: at most 4096 x 4 cells (four tiles) expanded, under 1 ms; at most one
  path a frame across all mechs (a queue), each mech at most every 0.5 s.
- Per tick following: a waypoint check, nothing else.
- The grid is built from the static world only; moving vehicles are left
  to (d) and the contacts' push.

## (f) Scope

Lancemates first: the player's lance (the brain's team the player's).
A switch per side in the options (`zfix::Options`: `nav_allies` with
`bugs`, `nav_enemies` off until Owen decides; enemies pathing round walls
makes the game harder). Network games: the same code on every machine;
the grid is built from the same world, so it agrees, but it stays off in
network games until checked.

## (g) Testing

- **Before and after** on three missions with lance travel: c1m2 (the
  town), c2m4 (W27's canyon), c3m5 (the fight). The lance ordered to
  follow, form on me, attack an enemy, and hold, the player walking a set
  route (W27's A* walk). Measured: time not moving while ordered to move,
  wall contacts (on_path 1), time to reach the order's range or spot,
  distance behind the player at the route's end. Before is `bugs` off (the
  exe) and the current port (W17/W27); after is the grid.
- **Breaks**: the grid with walls ignored (the lancemate into the wall
  again), the clearance off (stuck at a doorway), the firing spot's line
  of sight off.
- **Unit tests**: the grid on a made-up world (a box, a corridor, a slope
  past 35 degrees, water), A* and the string-pulling, the tile budget.
- **Unchanged**: every differential test (the probes turn `bugs` off) and
  W17/W27's tests with `bugs` off; the campaign runs.

## Open for the lead / Owen

- 4 m cells (accurate streets, 4x the build) or 8 m (cheaper, may close
  narrow streets).
- Water: costed or avoided?
- Enemies: `nav_enemies` (Owen's call).
- Whether the W17 detour and W27 breadcrumbs stay as the fallback while a
  tile builds (proposed: yes).

## As built (W58, 2026-11-03)

The design above was for a one-storey grid. Owen's cases (Epona's first
mission, c2m4: a ramp, a thin walkway, a river; every order, not only
follow) and the runs changed it. Code: `mech3/zai_nav` (the grid, A*, the
follower), `zai_attack`'s `NavWay` (the follow's, the fight's and the repair
walk's use, the firing spot), `GameEngine::nav_prebuild`.

**Floors.** c2m4 has two grounds at the same place: the land (y about 0 to
40) and the works 90 m under it. A grid of the highest ground cannot see the
works at all. Each place holds up to three floors, from every ground hit;
a step joins a floor to the floor beside it within tan 35 degrees (the skis'
clamp). A floor with under 12 m of room below the one above is not walked
(a bridge at head height, which kills in the original). A ramp is then only
a walk of floors, and the land and a mine join where they really do.

**What needs a margin.** A wall, or ground rising into a mech's body, keeps a
cell's distance (the 3 x 3 buffer). A drop beside a cell does not: a thin
walkway is walkable. Too steep or nothing there is no way but no wall.

**The search looks further.** Over the tiles built, 256 m past both ends, then
512 m, then 1 km (a river's far ramp); tiles missing from a window it found no
way in are asked for before a wider one. No way at all: W17/W27 and the exe's
walk, and asked again only when the grid changed, the goal moved 50 m, or
after a wait that doubles (5 to 60 s).

**The world changes.** c2m4's factory door (`factdoor`, x -860, destroy_anim
`open_fdoors`), falling ramps and moving slabs (`fly_slab1..4`, `xramp_fall`,
`zramp_fall`) change the way in play. Once a second every still object3d (no
mech's or vehicle's) is compared with its matrix and active flags; a change
builds the tiles under it again behind the old ones, each at most every 10 s. A
node freed (a building down) does the same from its last box. All of it starts
again at each mission's start (`nav::restart`): a replay has the same world
address and new nodes, and the first version crashed there.

**Mechs and vehicles.** Left out of the walls by their owner (0x0040e920), or
the leader's own body reads as a wall and a mech standing on a tile as it is
built is baked in. But the mobile HQs stand still at speed 0 and pilots bump
along them: **a vehicle at rest** holds its hull's cells solid and, for 1.5 s
at a time, looked at once a second, costs 8x within 26 m of its centre, as the
exe's own giving way (0x4cc3e0, too close inside +0x78 x 0.9, shipped mostly
25) pushes a pilot back from inside that. A cost, not a wall: the HQs park in
a pass in c2m4, and a wall there cuts the lance off from the whole map.

**A mech is wider than its centre line.** Its probes reach about 7 m ahead and
3 m aside. The first ring of cells is a hard margin; the second costs 3x and
string pulling keeps out of it, so a path takes a corridor's middle and a
corner wide when it can (`Grid::tight`).

**The boundary's shapes.** The red patches the AI keeps out of (ai.md section
11) are the designers' path hints, and their way-round pull does real work:
a lancemate walks c2m4's hill by them. For a lancemate the grid speaks first
when the way crosses one, and a cell in one costs 20x: round it when there is
a way, through only when there is none (the player standing in one).

**A mech's own feet** (the biggest find, and no part of the grid): the skis'
and the collision's queries honour the zones in the camera's view. A lancemate
in a zone the player is not in loses the floor under him; the skis fall back
on the land overhead, read a 45 m wall ahead and kick him back every frame.
FIX (bug) behind `bugs`, for AI mechs (`Skis::update`, `Mech::collide_query`);
c2m4: from never arriving, 158 s of 180 stuck, to there in 70 s.

**When it steers: walk first.** The rule that took the longest to find, and it
fixed the worst results. The grid leaves the exe's walk only when that walk is
failing: the straight way to the brain's range (+0x28) is blocked by the
buffered grid, or the pilot made no 6 m net in 3 s while he should be walking (the
boundary's pull included: it holds him at a shape's edge when the leader stands
in one). Then it takes over for at least 10 s and keeps to a path it took. On
open ground the exe's walk to the centimetre. It hands back to the exe once the
point is within range, in sight **and on the same storey**: the exe's follow
measures flat, so 31 m off and 90 m over the player's head counted as arrived.

**Paths hold still.** No answer from part of a window (ground not built yet
reads as a wall, and a path round that is wrong; he walks the exe's way
meanwhile). A path stays unless the goal moved 15 m, he got stuck, or the world
changed and the way on is shut: re-planning on every new tile had him in
circles.

**Firing spots** only in range without sight, on the enemy's storey; out of
range the goal is the enemy itself (a spot on a ring round a moving enemy was a
detour).

**Unstuck** by the pilot's own net progress (6 m in 3 s), not the distance to a
waypoint (re-paths reset that) nor "moved 2 m from a mark" (bouncing on the spot
resets that: 86 s stuck in c2m4's tunnel).

**Which orders.** Follow, form up, attack and the pocket commander's spot go
through the follow and fight states; the refit's walk to the dock's approach
point (`RepairState`) has its own. The AI nets' scripted routes (`NodeState`)
stay the exe's: those are the mission's script, not an order to a lancemate.

Debugging: `nav::dump` writes the grid and given paths to a PPM from a test.

## Results (2026-11-04)

Each in mw3tests, three ways: the exe's logic (bugs off), the port before (bugs
on, the grid off: W17, W27 and the zone fix) and the grid. "There": in range and
on the storey of the leader, seconds of game time.

| Case | Exe | Before | Grid |
|---|---|---|---|
| c2m4, Epona's first mission: from the start to the works, 618 m, the floor 100 m down | never (158 s of 180 stuck) | 70 s | **58 s**, 0 stuck |
| c2m4: under the conveyor, 522 m | never (154 s stuck) | 91 s | **69 s** (8 s stuck against 2) |
| c2m4: over the river, bank to bank, 210 m | never (90 s stuck, in the river) | never (94 s stuck) | **29 s**, 0 stuck |
| c3m5: a blocked line, 200 m | never (74 s stuck) | 53 s (14 s stuck) | **23 s** (4 s stuck) |
| c2m3: a blocked line, 200 m | never | never | **11 s** |
| c2m1: a blocked line, 150 m | 15 s | 15 s | **8.8 s**, 0 stuck |
| c1m4: a map spot up a cliff | stops at the cliff's foot, 58 m off | the same | **there**, the way round, about 60 s |
| c2m1: an enemy 25 m behind a wall: in sight | no | never | **6 s** (a firing spot) |
| c2m1: open ground, 250 m | 42.9 m at the end | 42.9 m | 42.9 m: the exe's walk exactly |
| c2m1: an enemy 80 m behind a wall | never | 25.8 s | 28 s: **slower** |
| c2m1: the player round the HQs (W27's route): in range, frames | never | 954 | 1642: **slower**, round the parked HQs |
| W27's five other routes | | | the same, or a little better |

**Not shown by a run: the top of c2m4's conveyor.** The strip at x -1206..-942,
z -1390..-1354, y -88.5 joins nothing at mission start (333 cells), and its
pieces move in play by triggers these tests do not fire. The grid sees the strip
and the world watch rebuilds on a move (unit tested on a door), but a lancemate
following the player up onto it is for a playtest. Cost in a Release build: the
worst search in these runs 16.8 ms, the median under 3 ms; building 768 places a
frame about 2 ms (a tile in 11 frames; by work, not time, so a mission plays the
same each run); the 9 tiles at the start 0.2 s. A failed search over the
widest window would cost 344 ms at once in Release, so a search runs over frames
(`PathSearch`, 3000 cells a frame shared by all pilots): the same one has a worst
frame of 4.0 ms over 99 frames, and backs off 5 to 60 s when it finds none.
