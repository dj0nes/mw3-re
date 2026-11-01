# The player's view and its cameras

The view ([0x005f4c8c], made by the mission load at 0x0048ff99) owns the
scene's camera node, camera1. Each frame its camera mode, one of the target
mech's six cameras, puts that node in the cockpit, behind, above or beside
the mech. Ported in `mech3/player_view` (all of it, emulator-checked) and wired
into the program in `mech3/app_view`.

## 1. The view (0x0040aca0, vtable 0x005bc2e8; base zView 0x004b5060)

| Offset | Field |
|---|---|
| +0x04 | the camera node (camera1) |
| +0x08 / +0x0c | zView's window and the object whose +4 is the world node the ground query asks (0x004b4fe0) |
| +0x14 | [0x0072f348] when made: the render's path (0x004b52c0: 0x004f7030 in Direct3D, else 0x004f6dc0) |
| +0x18 | the target's speed, smoothed |
| +0x1c / +0x2c / +0x3c | the offset's rotation, the look, the look times the mode's delta (quaternions w, x, y, z) |
| +0x4c | where the camera is put |
| +0x58 / +0x5c | the mode, the last mode's id (1 at first) |
| +0x60 | the distance used |
| +0x64 | the target: a mech, or the stealth object (the game editor's, editor.md) |
| +0x68 / +0x6a | handled (nothing in the family sets it), under water |
| +0x78 | the last frame time |

The constructor sets the camera's horizon to the `horizon` node (list 6).
Slots: +0x00 0x0040bab0 adds a node under the camera, +0x04 0x004b52c0
renders, +0x08 0x0040ada0 updates.

- **Update** (0x0040ada0, from the player's tick 0x004a1160 each frame, and
  once from the mission start): the frame time kept, the mode's update; then
  the ground and water check 0x0040af30 (with a mode and a target) and the
  weather check 0x0040b9a0.
- **Attach** (0x0040adf0): the old mode's switch off and the old target's
  vt+0x104, the new target's vt+0x100 and the mode's activation, then one
  update with the last frame time. The mech's attach (0x0041ce30) passes its
  data's camera list's current camera (+0x10, 0 at first: tether), then sets
  up the HUD (0x00430aa0, 0x0043bb80). The stealth's (0x004acd90) passes
  the vehicles' current camera (0x004acde0's list): the base camera mode's
  update (0x0040b8d0) reads only what every vehicle has (+0x30 the velocity,
  +0x48 the orientation, +0x14's centroid node), so the port's target is a
  `Vehicle` and the mech's own modes take it as a mech (they sit only in a
  mech's list). The program asks `program_view_mech()` for a mech target:
  none while the view is on the stealth, so no mech HUD draws then
  (2026-11-01, editor.md S4).
- **Camera switch** (0x0040ae70): the target's camera `id` (mech vt+0x10c)
  when it is usable: the last mode's id kept, its switch off, the new one on.
  The launch (0x00491150) switches to the options' camera (GameOptions bit 0
  or 8, 0x0040ce70), the cockpit by default; the camera key (0x0040aed0)
  goes attach, zippercam (when the pilot's game allows, +0x8e), tether.
- **Placing** (0x0040b7e0): the position (0x0040b600) at the mech's point,
  plus the offset turned by the rotation and times the distance; with a
  distance the rotation follows at dt x the mode's turn rate (+0x38) by a
  scaled relative quaternion (0x00525690, 0x00525be0), and tether, zippercam
  and area are pulled in before walls (0x0040b310, section 5). Then the look (0x0040b750) follows at dt x the look rate (+0x3c),
  times the mode's delta, as the camera's Euler angles (0x00525f80). The
  reset form (0x0040b210) sets both at once.
- **Ground and water** (0x0040af30): the ground under the camera (from 50 m
  above, 0x004edab0) and the water there (0x004abd90): under the water (its
  level above the camera and above the ground) the world's fog becomes
  world.zrd's water fog (0x0040b0e0), else the air's. An outside camera below
  the ground is lifted to half a metre above it. The camera's zone comes from
  the ground's hit (0x0040b080).
- **Weather** (0x0040b9a0): a segment 100 m up from the camera; something
  over it hides the weather (vt+0x64), the open shows it again (vt+0x60).

## 2. The camera modes

Base (0x0040a950, vtable 0x005bc2c0), 0x78 bytes as the mech cameras are:

| Offset | Field (the camera block's key) |
|---|---|
| +0x04 | id: 0 tether, 1 attach (cockpit), 2 zippercam, 3 godseye, 4 mcbtether, 5 area |
| +0x08 | the delta rotation (`delta_rot`, degrees as Euler angles) |
| +0x18 | the offset (`desired_pos`) |
| +0x24 / +0x28 / +0x2c / +0x30 / +0x34 | `dist_vary_factor`, `dist`, `dist_factor`, `dist_vary`, `dist_catch_up` |
| +0x38 / +0x3c | the turn and look rates (2, 3; the cockpit 2, 4) |
| +0x40 | the free look (0 left, 1 right, 2 ahead, 3 none, 4 up, 5 down) |
| +0x44 | the look's rotation (mech cameras) |
| +0x54 | look along the torso |
| +0x5c | the speed, smoothed |
| +0x60 | the view |
| +0x64 | the cockpit's fixed state: its camera node a child of the torso node |
| +0x68..+0x74 | `look_limits` left, right, down, up (degrees; 89, -89, -15, 45 by default) |

- **The mech cameras** (0x00421530, vtable 0x005bd2f8): tether, zippercam,
  godseye, mcbtether. Update (0x00421760): the speed's absolute forward value
  (local velocity z) drops slowly (0.15 x the gap per second) and rises at
  once; the distance `dist + dist_factor x speed + dist_vary x (speed - the
  view's)`, never under 0.9 x `dist`; the mech's rotation (times the torso's
  aim when looking along it), times the free look.
- **attach** (vtable 0x005be9f0, update 0x00421e40): at the torso node plus
  the offset turned by the mech (or by the AI's held view, torso +0xa4 when
  +0xa2), the head's bob (the mech's +0x58 vector less the torso's twist and
  pitch inputs x -0.2, times 0.15, [0x005e244c], when [0x005e2448]).
  Activating it hands the torso the look (0x0041ce10, 0x00426780) and, with
  the mech switched off or powering down, fixes the camera: its node becomes
  the torso node's child (0x00421d50).
- **area** (vtable 0x005be9c8, update 0x004221d0): 40 m off at the height the
  view camera's field of view gives (tan of half of it), looking along the
  free look's quarter turns (0x004220c0).
- **Usable** (0x00421a30): the cockpit always; the others unless the pilot's
  game bars outside cameras (+0x8e).
- **The set-up** (0x00430e70, mech_data.cpp): the six with their defaults
  (tether's height by the mech: 0.42 to 0.7 of a unit up), then each one's
  block under the definition's `camera` node (0x004215d0). Every shipped
  `attach` block has `dist (0.0)`, its own `desired_pos` and `look_limits`.

FIX (bug): the cockpit's update leaves its offset unset when its distance is
not 0 (the original reads the stack there); the port uses 0. No shipped
mech reaches it.

## 3. Testing

- `test_view.py` (`tests/probe_view.cpp`): 300 runs of a view, a mech and up
  to four modes of the four classes with random fields, through random calls
  (update over frames, attach, the camera switch, the free look, reset, the
  camera key, the set-up on stubbed reader calls); everything outside stubbed
  and answered from one script. Compared: the call traces (the camera's
  position and angles each frame, the angles within one unit in the last
  place: asin and atan2), the view's and every mode's fields. Breaking the
  port: 21 of 24 caught; the misses cannot change a result (the distance
  floor's NaN rule, the handled flag nothing sets, the ground check's
  default height that a target always replaces).
- mw3tests `mission_c1m1_camera_follows_the_cockpit_headless`: c1m1 from the
  launch, the Mad Cat driven by the keyboard: the view is on it in the
  cockpit, camera1 within 4 m of the torso node every frame for 359 frames,
  walking 20 m and more, the offset turning with the mech.

## 4. In the program (app_view.cpp)

The mission load's 0x0040aca0 makes the port's view on camera1; the mech
data's 0x00430e70 makes each type's cameras; the launch's 0x0041ce30 and
camera switch, the options' camera choice, the player's tick and the Core
state's reset reach it. The app code still reads the view by address: after
each change its mode (a block whose +4 is the id) and target go into the
view's block. The look keys reach the mode's free look (vt+0x10) and its
calls on the torso, the controls and the mech are answered (look.md,
playtest #14). The ground and water check (0x0040af30) has its queries: the
ground under the camera (0x004edab0, `zcls::ground_point`) and the water in
it (0x004abd90, `ground_pick`), so an outside camera is lifted out of the
ground and under water the world takes the water's fog (0x0040b0e0;
`mission_c1m1_view_ground_and_water_headless`). Not wired yet (logged): the
camera's zone 0x004f6c80, the weather query, the HUD's 0x0043bb80.

## 5. The wall pull-in (0x0040b310, 0x004a3290)

The tether, zippercam and area cameras, once placed, are pulled in before
walls (`pull_in`, player_view.cpp):

1. **Behind.** Two segments go through the world's query 0x004efcd0, with
   the view's target left out of it (0x004eb6d0 off, then on again): from
   one unit ([0x005e06f4]) behind the camera to the look point, and the
   same reversed. Their nearest crossing to the look point (0x004a3290 over
   both records) puts the camera one unit in front of it, along the look.
2. **Beside.** Two units across the look (0x00522460: (-z, 0, x), flattened
   by 0x0051e210), a segment each way from the camera. The right one's
   nearest crossing to the camera moves the camera across by what that
   side lacks (x and z only); without one, the left's.

0x004a3290 (ECX the records, EDX how many, then from and out) first looks
for any crossing with a node, record by record, and clears the node of
each water crossing (material soil 1) it passes. It stops at the first
one, so a water crossing after it keeps its node and can still be the
nearest: the camera is then pulled in before the water's surface (kept).
The nearest is by squared distance (0x0051dfa0), the first of equals.

Checked by `test_pull_in.py`: 3000 pull-ins against the original, the
queries answered from a script alike on both sides (the query itself is
test_di's). Breaks caught: the sides measured from the look point, water
kept, the camera behind the crossing, the left side first, the target not
left out, one record behind. Not caught: taking the last of equally near
crossings, which would only show for two different points at exactly the
same distance.

## 6. Left

- The world queries the checks use (0x004edab0, 0x004abd90, 0x004eed90).
  The zone set is ported (section 7).
- The HUD's camera calls. (The torso's cockpit switch 0x0041ce10 acts now
  that the cockpit block is made: playtest #4, 2026-10-21.)

## 7. The camera's zone (0x40b080, 0x4f6c80; ported 2026-10-25, item 12)

Worlds are cut into zones: every node has a zone byte (+0x30, 0xff "all")
and every ground polygon a zone word (count, then up to three ids, the
polygon's +0x20, kept in a ground hit's +0x18). The draw skips a node
whose zone is not in the render's list [0x74a328] (0x53cdc0). In the
shipped worlds the extra zones are interiors and their props: c1 has zone
71 (72 nodes), c2 zones 2, 4, 5, 62 (the pump), 63 (the boiler's elevator,
doors and floor) and 73 (the waterfalls and their triggers), c3 zone 10
(85 nodes), c4b zones 22 and 44 (m6's tunnel, its sound and trigger); c4
and t1 have only zone 1. Counted by loading every campaign and training
mission (the node pool's bytes).

**0x40b080** (the view's ground check, section 1, after the camera is lifted
out of the ground; ECX the view, the stack the hits and the index the
water check picked): the camera's zone set from that polygon's zone word
(0x4f6c80), then the render's list from the camera's (+0x1e4 into
[0x74a328]), the view's target's zones (+0x64: zEntity's 0x5a80d0, which
also gives its node the first id, 0x5a80c0 -> 0x4eb810) and the
animations' (0x502d70: their +0x38 list, +0x28 "known"). **0x4f6c80** (ECX
the camera node, EDX the list): "Null node pointer" / "Null class data
pointer" / "Bad Class Found" (0x1838..0x183a, 5, 5, 3); a list with a count
and no 0xff among the count's ids (as many as the count says, past the
four bytes when over three) sets +0x1e0 to 1 and +0x1e4 to it; else the
camera keeps what it had.

The render's frame (0x4f6f6f, ported in camera.cpp) uses the camera's list
when +0x1e0 is set and the world's zone mode [0x5ea1d0] is 1; otherwise it
asks 0x4eda10 (the ground below the camera kept to one hit) for its zone
word, the mode off around the query. So 0x40b080's copy into [0x74a328] is
the same the next frame makes.

**Before the port** the program never culled by zone: 0x40b080's calls were
logged, the camera's +0x1e0 stayed 0 and the render's probe was unwired, so
every frame's list was "all" and every zone drew (the tunnel's insides
from outside it, and the like). **The port**: `zcls::camera_set_zone`
(camera.cpp); the program's `camera_zone` (app_view.cpp) does 0x40b080's
steps; the render's `zone_probe` is `ground_below` (app_mission.cpp).
zWeapon reads and writes the same list (`zwep::query_zone` and
`set_query_zone`, the exe's word over `zmod::zones().list`): one copy, as
the exe's one global. Its readers and writers: the tick 0x5823c0 keeps the
list in `tick_zone` (0x9a17a8) and puts it back at the end; each shot's
update 0x585710 sets the shot's zones (+0x08, from the shooter's +0x64 at
the spawn; low byte 4: the tick's) and each beam 0x5814b0 its own (+0x10,
else the tick's), so a shot's casts (0x5861f0) and the guided
shots' ground (0x4eda10) run over its shooter's zones; the spawn's
straight-shot cast 0x585010 reads whatever the list is then (the camera's).

**The queries follow it.** The segment and ground queries skip a node
whose zone is not in [0x74a328] (cls_di.cpp, as the exe), so the AI's line
of sight (0x4d5980) and the vehicles' ground probes run over the zones the
camera's ground gives. c4b mission 5 starts over ground that is zone 22
alone ({22}): until the camera reaches zone-1 ground, the queries skip
every zone-1 node, the terrain included. Two things came of turning the
culling on: the tests' `ground_under` helper asked the ground through the
zones and gave NaN off them (it now turns the mode off, as the render's
probe does), and a segment toward a NaN place hung its cell walk 0x4eefe0
(both ends' parameters NaN: the exe's unordered compares never end it and
step it both ways; FIX (bug), fixes.md: the walk stops).

**Checked.** `test_camera_zone.py` (probe `tests/probe_camera_zone.cpp`):
5000 cases (no node, no data, another class; counts 0..5 with ids and 0xff
at random, the bytes past four): the answer and the camera's two words.
`mw3tests` `mission_c4b_m6_camera_zone_follows_the_ground_headless`: at the
start the camera, the render's list, the player's mech (+0x64 and its
node's byte), the animations and the render's own probe read {1}, zone 44
and 22 not drawn; put in the tunnel they read {1, 44}, zone 44 drawn.
A segment toward NaN ends. In the tunnel the weapons' segment query finds
g56, a zone-44 wall 16 m from the player, under {1, 44} and not under {1};
a PPC shot with the player's zones stops on it, one with {1} (and the list
{1} at its spawn) flies through. Breaking the port showed the tests catch it, 9 of 10 (zWeapon
with a private copy of the list: both shots hit the wall): an 0xff not seen, a
count of 0 setting, the ids read only to three, +0x1e0 not set, the
target's zones or the animations' left, the probe's second word; the
render's list left is not caught (the frame copies it anyway, above); and
the NaN walk's stop (the test hangs without it).

## 8. The fall camera (0x413af0 / 0x413b70; ported 2026-10-29)

The mech's fall actions (1, 2, 7, 8, mech_move) call 0x413af0, and getting
up (5, 6) calls 0x413b70, both with ECX the mech. Only the view's target
([0x5f4c8c] +0x64) counts.
- The fall: the torso centred (0x465a00(1) on +0x39c), then the current
  mode's vt+0x20(view, 1). The cockpit camera's (0x421d50) detaches from the
  mech's rotation (+0x64 set), so the view stays put while the mech goes
  over. The other modes' slot is the base's no-op (0x40baf0).
- Getting up: the mode's vt+0x20(view, 0), back with the mech.
- A branch on the byte [0x605624] would instead switch the view's mode: to
  2 (zippercam) from the cockpit, back to the saved mode (+0x5c) on getting
  up. Nothing writes that byte (a zeroed .bss byte, read only here), so the
  branch is dead in 1.2. Ported as a false constant.
- Port: `PlayerView::mech_falls` / `mech_stands` through
  `MoveCalls::fall_camera` / `restore_camera`. Until now a census stand-in,
  so the cockpit camera turned with the falling mech.
- `mission_c1m1_a_fall_fixes_the_cockpit_camera_until_up_headless`: the
  player knocked down at core frame 60, the cockpit camera fixed at 61,
  turning with the mech again at 441 when it is up.

