# Vehicles and turrets (scoping, 2026-10-21)

What c1m1 and c1m2 need, and what porting it means. Being ported (the
steps: docs/worklogs/ai.md, item 4).

## In the first missions

c1m1's `aiv.zrd` (readerm1.zbd) places, besides the player and the Owens:
three mobile field bases (`mobilehq_01..03`, kind 2, `MSG_AIV_MOBILE_FIELD_BASE`),
two trucks (`ttruck_01`, `_02`, kind 1, `MSG_VEHICLE_TRUCK`) and two APCs
(`apc_01` kind 3, `apc_02` kind 5, `MSG_VEHICLE_APC`), with regions for the
APCs' moves (reg_apcadvance, reg_apcattack, ...). The launch reaches the
mobile base's constructor 0x4589d0 three times and the basic vehicle's
0x4ac320 four times (`mission_start_reads_c1m1_vehicles_headless`); both are
logged, so these vehicles do not exist in the port's missions. c1's reader
also holds `bossturr.zrd` and `triturret.zrd` (the turrets' animations;
their definitions are aiTurrets.zrd: The turrets).

## The basic vehicle (veh_basic.cpp region, 0x4ac320..)

- 0x4ac320 (ECX the controls, EDX the type, the name on the stack): the
  node by name on list 6, else a copy of the type's model (0x4f9af0(model,
  0, 0)) named and put under the world ([0x608c84] vt+0); then a 0xfc-byte
  object, 0x4ac530(controls, node): the vehicle base 0x4aeb00 (AiTarget,
  ZEntity3d, ZEntity), the engine sound at +0xe8 (0x4adaf0, vtable
  0x5c8b90; the setup puts the definition's sounds/engine there from the
  vehicles' shared cache 0x60b110, step 1b) and the motion animation at
  +0xf0 (0x4aec00 zeroes it; the linker shares that body with IControls'
  hit record; the setup's motion_anim),
  vtable 0x5c88e8, the controls (+0xe4) either given (their SetOwner,
  IControls vt+0x08) or made: 0x4ac860(0xa70) and 0x4ae270, the vehicle's
  own basic controls over 0x4ac750 (vtable 0x5c8c50, step 2a; the AI's are
  the ones given), the vehicles' camera modes once (0x4ace00), the node
  attached (0x4d6c90) and zEntity's list (0x5a7f50).
- The destructor 0x4ac650 (0x4ac630 deletes): the engine sound dropped, the
  controls deleted (IControls vt+0x00), the node detached (0x583400), taken
  from the world (0x4ebb40), freed (0x4f9050) and unlisted; the motion
  animation stopped (0x4addb0 -> 0x4ade70 -> 0x506ca0); then the base 0x4aeb50 (the
  weapons 0x4b01d0, the network's record), AiTarget's 0x4d58a0 (the
  contacts, the shadow) and ZEntity3d's 0x5a7f90, which unlists the entity
  again whether it is listed or not, and ZEntity's 0x5a8370.
- The HUD markers 0x4ac3d0: three zTwoD images, `hud_veh_ally_sm`,
  `hud_veh_enemy_sm`, `hud_veh_other_sm`, at [0x60b120] / [0x60b124] /
  [0x60b128]. The basic vehicles' constructor does not make them: the
  stealth object's does (0x4a4de0 from the mission load's "Creating
  Stealth"), and its destructor deletes them (0x4a4fc0 -> 0x4ac4e0) when the
  world unloads, so every mission has them from the load. The radar shows a
  vehicle with them (vt+0x3c, 0x45b970: [0x60b120 + 4 kind]). In the
  program (2026-10-25) the stealth object is not made (back burner): its
  step makes the markers (0x4ac3d0, deleting the last mission's first,
  0x4ac4e0), so the trucks, the APCs and every basic vehicle show on the
  radar as blips of their own; before, they drew nothing (ai.md section 15,
  Radar contacts).
- Ported (steps 1a and 1b, 2026-10-23): all of the above and below to the
  motion animation, `mech3/veh_basic_object`
  (`BasicVehicle` over the shared `Vehicle` base, which `Mech` now derives
  from too), checked by `test_veh_basic.py`. The camera modes (0x4ace00)
  followed with the cameras (below); the engine sounds' clear runs at each
  mission's end. Not in play yet: the program makes no vehicles until their
  placement (step 1c).
- vt+0xdc 0x4a3630(pos, yaw), the placement (ported in step 1c1): the
  ground under the point from 10 above it (0x4edab0), the vehicle 0.1 over
  the first polygon found (not the highest, as the mechs' pick does), else
  where given; the vehicle's own vt+0x18 (zEntity3d's 0x5a8550: the node's
  translation and 0x5a85c0); the controls' y angle (+0x330) the yaw, their
  commands cleared (IControls vt+0x14 0x4ad640: throttle +0x08 and turn
  +0x0c to 0; the jump jets' setters are empty for vehicles), the owner
  refreshed, +0x70 zeroed, the +0x764 block's words at +0x8b0 (count
  +0x7e8) zeroed, +0x20 and +0x24 zeroed, and the points at +0x160 (count
  +0x214) set to the place, but only on controls with a serial: the
  placement runs before the setup's init gives them one, so on a vehicle's
  first placement they stay as they are (0).
- vt+0x158 0x49ec00(def, name, kind): the definition's `sounds` list's
  `engine` (0x4adb20 -> 0x4ad930, the engine-sound cache; not weapons, as
  first read) and its `motion_anim` (0x4ade40), then the controls'
  vt+0x0c (the AI's setup).
- The engine sounds (CZEngineSoundList, the global at 0x60b110, ported in
  step 1b): made at start-up (static initializer 0x4ada90 -> 0x4ad840, room
  for 64 entries of 0xc bytes, never grown; cleared at exit 0x4ade90) and
  emptied at each mission's end (0x491780 -> 0x4d71f0 -> 0x4add10, each
  entry's voice unlocked and stopped, 0x4adbf0). An entry (0x4adb90 /
  0x4adbb0) is the sound by name (0x56f6f0) and the voice playing it (+8,
  set by the frame, step 2): every vehicle with the same engine shares one
  entry. A lookup (0x4ad930) compares names (0x56f740); a sound zSound does
  not have gives none; a 65th distinct sound gives none and the report
  "CZEngineSoundList is too small" (0x49bfd0, empty in this build).
- The motion animation (0x4ade40): the running one (+0xf4) stopped, then
  the definition's by name (0x50b450) at +0xf0, or none.
- vt+0x34 0x4a1040: the frame, from the controls (their throttle and turn
  through vt+0x1c and the IControls base): without the AI nothing moves.
- vt+0x38 0x5a80f0: the ground under it (0x4edab0) and the move.
- vt+0x160 0x4ad3f0: damage (its health, the controls told), vt+0x70
  0x4ad2d0: the death (effects 0x508c10, 0x581400, the weapons 0x4b48c0,
  the AI 0x402510).
- The killer's kill credit already reads a vehicle (on_kill's +0x40 branch,
  75 points, salvage 0x472ec0, not ported).
- About 40 functions in the vtable (most under 30 lines; 0x4d6de0 142 and
  0x4a2ef0 144 are AiTarget / AI), roughly 2,000 lines of pseudocode.

### The heading from the record (2026-11-01)

The mission start places each vehicle (0x0049f8a0 -> vt+0xdc, 0x4a3630)
with its aiv.zrd record's heading, the dword at +0x14 pushed as it is (a
float in radians). The port converted those bits as an integer (1.919862
became about 1.07e9 radians) until 2026-11-01 (`PlayerHooks::rec_heading`,
`vehicle_heading_from_its_record_is_the_float`). It made no difference in
play, in the exe either: the setup that follows (0x49ec00 -> the controls'
vt+0x0c 0x4acbc0 -> vt+0x10 0x4ac8a0) reads the node's own rotation into
the same angles (+0x330), so a vehicle faces its node, not its record.
Records with a heading: c1m1 apc_01, apc_02 (110 degrees); c1m2 bulldog_01..04,
apc_01, apc_02; c1m3 mobilehq_01..03; c3m6 striker_01..05, mobilehq_01..03;
t1m4 apc_01, bulldog_01 (180); every other mission's are 0. None of them
faces it.

## The vehicles' controls base (0x4ac750, ported in step 2a1)

The AI's vehicle controls (0x4e71c0 over 0x4e7090) and the vehicle's own
basic ones (0x4ae270) sit on one base, `mech3::VehicleControls`
(`mech3/veh_controls`; `ai::AiVehicleControls` derives from it). Its
constructor sets only the turret limits; its users fill it:

- The init (vt+0x10, 0x4ac8a0), with the owner and its node: a serial
  number (+0x14, from the counter [0x60b148]), the node's rotation (+0x32c,
  0x4fbe90; a copy at +0x320) turned into the owner's matrix (0x4ab990,
  zMath's euler rotation) and its second row kept (+0x308), the owner
  refreshed (0x5a85c0), the nodes `body`, `turret`, `shadow`, `gun` found
  in the tree (+0x700, +0x704, +0x70c, +0x708), the gun's world position
  (+0x6d8), then the gun's firepoints: `fpnt_c` (its marker model dropped),
  `fpnt_l`, `fpnt_r` (removed and freed once read); the health 100 (+0x740,
  +0x744).
- The setup (vt+0x0c, 0x4acbc0, the end of the vehicle's 0x49ec00): the init
  (through the vtable, so the basic controls' 0x4ae3c0 runs there), each
  `start_anims` animation started on the owner's node (0x508c10), the
  definition's `health`, the owner's 0x4af2a0 (step 2a1b) and its weapons
  (0x4b4660, the port's `build_vehicle_weapons`), the owner's node at +0x3b0,
  the block at +0x398 cleared (0x5869d0).
- vt+0x18 0x4accd0: +0x1c the gravity ([0x609a14]), +0x30, +0x3c, +0x350,
  +0x5e8 zero.

- The weapon slots (the owner's 0x4af2a0, ported in step 2a1b): ten of
  0x38 bytes from +0x3b8, emptied, then one per `weapons` item (name n n
  ammo ...; the same list `build_vehicle_weapons` reads): zWeapon's
  definition by the name (0x581ed0), bit 1 paired (the item's third value),
  bit 2 counted (its second value, when the definition exists) with the
  fourth value as a float at +0x18, the fifth to seventh values kept as
  they are, bit 0 from the eighth. Under the gun, its firepoint node is
  named after the weapon, or `<name>_L` and `<name>_R` when paired; each is
  hidden (0x4eb170), the first slot's shown again at the end (+0x3b4 points
  at it). A beam weapon gets its beam (0x584a70) with the muzzle +0x608 and
  direction +0x5f0, which the beam reads live, and 8 segments on the
  controls whose serial is 1, 1 on every other: the serial counts every
  vehicle controls set up since the program started ([0x60b148] is never
  reset). No shipped vehicle carries a beam (their weapons are AC2, MGUN,
  SRM2), so it never shows. FIX (bug): a weapon zWeapon lacks crashes the
  original; here the slot stays empty (fixes.md).

Open: a firepoint the gun has lands in one set of fields (fpnt_c +0x62c with
the inverse of its length at +0x724, fpnt_r +0x638, fpnt_l +0x644), the
starting value for one it lacks in another (+0x608 (0, 0, -1), +0x614
(1.51, 0.396, -2.46), +0x620 (-1.51, 0.396, -2.46)). +0x608 is the muzzle a
beam reads live, so the found positions are likely the local offsets the
aiming turns into it each frame; settled with the frame (step 2c).

## The motion block (the controls' +0x764, ported in step 1c2a)

`mech3::VehicleMotion`, 0x2a0 bytes to the base's end, read from
vehicle.zrd's mode by 0x49ef40 (0x49ec90 calls it on the controls' block
with the vehicle's node, the mode, the vehicle's name and type and the
controls' +0x214 for the collision count). Defaults in brackets:

- The model's nodes `right_morphs`, `left_morphs`, `chassis`, `rtracks`,
  `ltracks` (+0x288..+0x298).
- The supports: the model's `support00..03` (hidden), else the `platform`
  list's points ("Cannot find support points for %s!"); +0x84 their count,
  +0x8c the points (16 fit), +0x14c a word each.
- The collision points: the model's `collide00..11` (hidden, kept in the
  order 0-2, 6-8, 3-5, 9-11), else the `collision` list's, whose count
  goes to the controls' +0x214 ("Cannot find collision points for %s!").
- `rates` (10, 30), `friction` (10000, 10, 0; the second held under 0.9 of
  the first), `stopping` (8), `quicksand_slowdown` (0.9), `lava_slowdown`
  (0.8), `turns` (0.6, 2), `turn_damping` (30), `rate_damping` (30, 30),
  `a_damping` (8), `mass` (1; its inverse at +0x24c), `gun_pitch` (-15
  degrees, 0.5: the gun's limits rebuilt with them, 0x4a4b30 picking the
  full-turn class 0x4a4a70 for the -2 pi..2 pi it is given), `mode` by its
  first two letters (basic 0, fly 1, sub 2, track 3, hover 4, amphib 5;
  track without the key, an unknown name keeps the mode it had), the wave
  values (`amphib_wave`, then `hover_wave`, then `sub_wave`: the last given
  wins, whatever the mode), `mode_alt` (2), `chas_smooth`, `chas_pitch`,
  `chas_roll` (0; their last value as its absolute), `collision_d` (0.5,
  0.15), the destroy animation by the vehicle's name, else its type ("No
  destroy animation for (%s) or (%s)").
- The limiter classes 0x4a4b30 chooses from (by the yaw range's width):
  0x4a42f0 (none: a fixed yaw), 0x4a4580 (under pi), 0x4a4800 (under 2 pi),
  0x4a4a70 (a full turn); only the last is ported (the turrets need the
  others).
- Quirks: without `chas_roll` the exe zeroes `chas_pitch`'s values (a
  copy's slip, FIX (bug) behind zfix bugs); without `platform` or
  `collision` (and the model's points) it reads through null (FIX (bug)).
  No shipped vehicle (MW3 or Pirate's Moon) has any `chas_*` or `*_wave`
  key, and every one has both lists.
- Open: with the model's twelve collide points the count is not written,
  so it stays what the controls had (0 from the zeroed block). Whether any
  shipped vehicle model has them (which would leave it no collision points)
  is to check against the models.

## The speeds and turns (vt+0x114..+0x13c, ported in step 1c2b)

What the AI asks a vehicle (the same slots the mechs answer, mech.h), from
the motion block: the top speed is `rates`' second value, the slowing
`rate_damping`'s second, the turn rate `turns`' second.

| Slot | Function | Gives |
|---|---|---|
| +0x114 | 0x4acfa0 | the throttle (IControls GetThrottle, the controls' +0x08) x top speed |
| +0x118 | 0x4acf70 | share x top speed |
| +0x11c | 0x4acfe0 | speed / top speed (0 for a top speed of 0 or NaN) |
| +0x120 | 0x4ad020 | speed / slowing: the stopping distance |
| +0x124 | 0x4ad050 | the stopping distance at the throttle's speed |
| +0x128 | 0x4ad0b0 | the stopping distance at a share's speed |
| +0x12c | 0x4ad100 | slowing x d / top speed, clamped to 0..1 (NaN 0): the share that stops within d |
| +0x130 | 0x4ad180 | angle / turn rate (at least 0.0001; a second argument unread) |
| +0x134 | 0x4ad250 | the turn time of the controls' \|+0x78\| |
| +0x138 | 0x4ad290 | the turn time of a share's speed |
| +0x13c | 0x4ad1c0 | \|turn rate x b\| x a / top speed, clamped to 0..1 |

Without controls the exe reads through null in +0x120..+0x128 (no vehicle
is without them); the port gives 0.

## The cameras (0x4ace00, vt+0x108..+0x110, ported 2026-10-23)

Every vehicle shares one camera list, the static at 0x60b130 (freed at exit
by 0x4ac300), filled once by 0x4ace00 from the first vehicle's constructor
(the flag [0x5e8578] starts at 1). Its three modes are the plain base
(0x40a950, 0x44 bytes, base vtable 0x5bc2c0), none of the mechs' classes, and
no definition block is read:

| id | dist | rotation | desired_pos | dist_vary_factor | otherwise |
|---|---|---|---|---|---|
| 0 behind | 15 (base) | pitch -0.06 | (0, 0.43, 1), not normalised | 1 | the base's |
| 1 on it | 1 | identity | (0, 1, 0) | 0 | the base's |
| 3 above | 150 | pitch -pi/2 | (0, 1, 0) | 0 | dist_vary 0, dist_factor 0 |

There is no mode 2. The list's current id (+0x10, [0x60b140]) is written only
by its static constructor, so vt+0x110 (0x4acdc0) and the view attach vt+0x108
(0x4acd90: 0x40adf0 on the view [0x5f4c8c] with the vehicle and that mode)
always give mode 0; vt+0x10c (0x4acde0) finds a mode by id. vt+0x108 is the
slot the mission launch (0x491150) calls on the player's entity; MW3's player
is always a mech, so a vehicle's view would come only from elsewhere.
`mech3/veh_basic_object` ports all four (`vehicle_cameras_make`,
`vehicle_camera`, `vehicle_camera_current`, `vehicle_view_attach`, the last
through the hooks: the program's view takes a mech), checked by
`test_veh_basic.py` at both x87 precisions (9 breaks; 8 caught, the ninth an
ulp inside the tolerance).

## Damage and death (step 1d, ported 2026-10-23)

zWeapon reaches a vehicle through the hit callback 0x4af550, registered for
every vehicle the mission start makes (0x49f8a0: 0x5833b0(node, 1,
0x4af550)): the amount doubled at difficulty 0 (low), halved at 2 (high),
then vt+0x160 with (weapon, the callback's first argument, amount). The
basic vehicle's slots (vtable 0x5c88e8; the derived class at 0x5c23d8 has
its own death and damage):

- **vt+0x160 0x4ad3f0, the damage.** Controls whose vt+0x1c says destroyed
  (the AI's 0x4e7470: the owner's +0x08 clear; the basic 0xa70 controls'
  0x4ae6a0: state +0x38 is 4; the base's 0x4a5270 always) only clear the
  vehicle's +0x08 and its engine sound. A weapon with def flag bit 22
  (`FREEZE` or `IMPACT HEAT` present, structs.md; every laser has impact
  heat) goes to the glow block (+0x398, zWeapon's 0x586a30): a freezing
  one first with amount x 0.01, and when that answers 2 (the block's +0x08
  below -0.5) the controls' +0x28 is set and nothing more happens; a heat
  one takes the amount off the health (+0x744). Both then pass 1 - health /
  max (+0x740) to the glow. Any other hit takes the amount, or, once +0x28
  is set, amount + +0x08 x health (+0x08 is negative while frozen, so such
  hits add health as written; no shipped weapon freezes, so this never
  happens). Health not above 0 (a NaN counts) kills: with a weapon, the
  attacker's object (0x581e40, 0x40eee0 asked twice) gets vt+0x80(vehicle)
  as the kill credit, 0x583710(1, 0), vt+0x70, answer 0; without one,
  vt+0x70 and answer 1. Alive, 0x583730(health / max), then a hit over 5 by
  a weapon without bit 22 jolts it: k = inv_mass (+0x764+0x24c) x amount,
  0x4accf0(normalised from - to of the shot line 0x581e30, k x 0.025,
  k x 1.667).
- **0x4accf0, the jolt.** The direction into the node's frame (the node's
  local 3x4 through the matrix stack, 0x522640 by the inverse rotation);
  angles.x (+0x32c) -= z x turn, angles.z (+0x334) += x x turn, and +0x70 x
  and z -= x and z x push.
- **vt+0x70 0x4ad2d0, the death,** once (+0x08 set): +0x08 and the engine
  sound cleared, health 0, state 4, IControls vt+0xa8 (the AI's
  0x4ea6b0), 0x402510 (the basic controls' path: entries with a negative
  id dropped; vt+0x00 gives the path controls, null for the others), the
  destroy animation (+0x764+0x29c) on the node, a beam that is on (+0x394)
  stopped (the first slot's +0x34, 0x581400), the glow ended (0x586af0),
  the weapons freed (0x4b48c0: 0x4b1110, then deleted) and 0x583710(1, 0).
- **vt+0x68 0x4ad390, TARGET's tint:** on, the node's object3d colour
  blend (0x4fb8c0) red (0x5e8580) at (1 - health / max) x 0.6; then the
  blend's switch (0x4fb840) on or off. Both ported (zcls object3d_set_blend / use_blend, render.md
  section 3). app_hud's TARGET view asks for it (object_view_flag, vt+0x68
  by the class: a mech's 0x41b9d0, a turret's 0x4a75b0, a basic vehicle's
  0x4ad390, a field base's the same; a target's and the marker's 0x4a5bc0 empty),
  and the red reaches the device as the model's fog colour.

The controls' health fields were named the wrong way round until this
step (+0x740 is the maximum, +0x744 the health); the setup writes both
alike, so nothing changed in play. `mech3/veh_basic_object` ports all six
(`vehicle_damage`, `vehicle_jolt`, `vehicle_die`, `vehicle_weapons_free`,
`vehicle_hit`, `vehicle_view_tint`); the controls' answers, the glow and
zWeapon's hit state are hooks. `test_veh_damage.py`: 600 runs at 24 bits
against the real vtable and matrix stack, 17 breaks caught. Salvage
(0x472ec0) was already ported (`mech_salvage`).

## The glow (zWeapon's 0x5869d0, 0x586a30, 0x586af0; ported 2026-10-25, item 11)

The controls' +0x398 block (0x18 bytes, `zwep::Glow`; the vehicle's node
follows at +0x3b0, the block's +0x18) is what a FREEZE or IMPACT HEAT hit
(def flag bit 22, every laser) leaves on a basic vehicle. 0x5869d0 clears it
(setup, end): the light, +0x08, the level and +0x14 to 0, flags bits 0-1
off (the weapon kept). A hit 0x586a30 (ECX the definition, EDX the block,
the share on the stack: the damage path passes amount x 0.01 for a FREEZE
weapon first, then 1 - health / max): the weapon at +0x04, flags |= 3; the
level (+0x0c) plus the share, minus for FREEZE (+0x6c bit 10), held to -1..1
(a NaN goes to -1); with no light yet one is taken from zWeapon's impact
light pool (0x586b20: [0x9a0ebc], linked through +0x40; its range set to
0.1..0.2, its colour IMPACT HEAT's last three numbers, the definition's
+0x1d4, put in the world 0x4f2d50) and made a child of the node (0x4eb9b0).
The answer reads +0x08: below -0.5 (or NaN) 2, "frozen solid", at most 0
1, else 0. The death's 0x586af0 (at 0x4ad36d): the light off the node
(0x4ebb40), back in the pool (0x586b70: range again, out of the world
0x4f2e00) and the block cleared.

What it shows: nothing in the exe writes +0x08 but the clear and nothing
reads the level, so every hit answers 1 (the freeze path's "2" never comes)
and the light is never scaled. Its range stays the pool's: 0.1 m near and
0.2 m far (zClass compares squared distances, +0xbc / +0xc0), so the
vehicle's own model counts as reached (its bounding sphere holds the node's
origin) but only what lies within 0.2 m of the origin is lit: in play a
laser-hit vehicle shows practically nothing. The designators' beams take
from the same pool and set their range from the definition's first two
numbers (zwep_beam.cpp); the glow does not, which looks like an omission
(fixes.md, Candidates).

The pool: the mission's weapons start makes it twice (0x583880 -> 0x586970
and the pool objects 0x584905 -> 0x586970), so it holds 16 lights, shared by
the designators' beams and the glows. Each laser-hit vehicle holds one
until it dies; a fight with more lit vehicles than lights leaves the later
ones (and any designator beam) without one. Only the death returns it (the
exe's one caller of 0x586af0, 0x4ad36d): a vehicle alive at the mission's
end keeps its light under its node, and the world's cleanup cannot free it
(0x4fa850: "ERROR deleting light; Light attached to 1 world nodes", still in
the world's light list); the next mission makes a new pool, 0x5869f0 freeing
only the lights still listed. FIX (bug): the vehicle's delete returns its
light first (fixes.md).

Ported in gamez/zweapon (`Glow`, `glow_hit`, `glow_end`); the vehicles'
`Block398` is it. Checked: `test_vehicle_glow.py` (probe
`tests/probe_glow.cpp`), the three on 1 to 3 glows over a pool of 0 to 4
lights at 24-bit x87: 2000 cases, 10,572 hits (3,341 lights taken, 3,272
with the pool empty), 3,056 ends, 1,539 clears, answers 2 / 1 / 0 from
random +0x08s; the calls, the block, the pool's chain identical. `mw3tests`
`mission_c1m1_laser_leaves_its_glow_on_a_truck_headless` (also under ASan):
Laser_Large on a c1 m1 truck takes a light (16 to 15), Laser_Large's colour,
range 0.1 / 0.2, under the truck's node; a second hit keeps it; the kill
returns it (16 again) and it leaves the node; another truck lit and left
alive to the cleanup gives its light back there (no zClass report).

## The debug readout (vt+0xe0 0x4a2ef0, step 1e, ported 2026-10-23)

The slot the mechs use for the HUD's frame (0x41cd40, the view target's)
is a developer readout on a vehicle: the HUD's text lines 1 to 4
([0x5fc848] + 0x4b0 n, each set through its vt+0x7c, a printf with the
text as the format):

1. With the controls' vt+0x20 at 1, "%s using %s dynamics" (" - A" when the
   controls' +0x20 is set) with the node's name and the motion mode's name
   (BASIC, FLY, SUB, TRACK, HOVER, AMPHIB, else UNKNOWN); else, on a path
   (IControls vt+0x70 at 1), "%s is in mode %d and had goal node %d"
   (vt+0x04, vt+0x08); else, destroyed (vt+0x1c at 1), "%s is DEAD!"; else
   empty. Every MW3 controls answers 0 to vt+0x20 (0x4d72a0), so the first
   never shows.
2. "POS %d %d %d YAW %d": zEntity's position (0x5a85e0, asked three times,
   z first) and the angles' y in degrees, through _ftol (truncated through
   64 bits, the low word).
3. "VEH ZONES(%d): " and the vehicle's zones: +0x64 is a 4-byte zone record
   (a count, then up to three ids, 0x4f6c80's), each as "%d ".
4. "CAM ZONES(%d): " likewise from the view's camera (its data +0x1e4).

Only a vehicle the view is on reaches it, which MW3's player never is.
`vehicle_debug_lines` in `mech3/veh_basic_object` (the lines through the
hooks); `test_veh_damage.py` runs it 200 times with MFC's CString modelled
(8 breaks caught). The AI slot 0x4d6de0 (vt+0x84, the lead point) was
ported earlier (`zai_firing`).

## The gun's aim (step 2b1, ported 2026-10-23)

Each vehicle controls has pitch limits at +0x790 (the pointer at +0x78c;
vtable 0x5c8340 over the base 0x5c82f0, 0x4a4000/0x4a4a70): the sines and
cosines of `gun_pitch`'s two values (defaults -0.2588 and 0.5 radians,
which every shipped vehicle keeps: none sets the key).

- **within** (vt+0x08, 0x4a4180): the direction's y not past either sine.
  Both compares are "not greater", so a NaN is within.
- **clamp** (vt+0x04, 0x4a41b0): within, the direction itself and true;
  else the limit passed (the low one first; a NaN counts as below) and
  false, with out = (x f, sin, z f), f = cos / hypot(x, y), or (0, sin,
  -cos) when that length is 0. The heading's length is hypot(x, z): the
  exe's y is a typo (FIX (bug) behind `bugs`; fixes.md). With it the
  clamped direction misses the limit (for one dead ahead: 24.6 degrees for
  a target 40 up, 43 for 60 up; off to the side it differs, and straight up
  gives (0, sin, 0)). It shows nowhere: the only vehicle caller, the AI's
  track 0x4b6e30, keeps the clamp's answer only for a dot product it never
  reads and aims (vt+0x154) the unclamped direction (zai_firing).
- The vehicle's slots: vt+0x88 / vt+0x90 (0x4ad740, 0x4ad7c0) ask a
  direction in the vehicle's frame, vt+0x8c / vt+0x94 (0x4ad780 / 0x4a4060,
  0x4ad7f0 / 0x4a40f0) a world one turned into it by the rows of the
  vehicle's matrix (+0x0c); vt+0x98 (0x4ad820) is health / max; vt+0x150
  and vt+0x154 (0x4ad6e0, 0x4ad700) pass to the controls' vt+0x24 and
  vt+0x28 (no controls: false, nothing). The base controls answer false and
  do nothing (0x4d6dc0, 0x48b940).

The AI controls' turret (their +0xcb0 aim and +0xcbc flag):

- **aim at** (vt+0x28, 0x4e7570): a direction in the vehicle's frame (the
  track turns it so by rows, 0x4b6ef3) back into the world by the
  matrix's columns, flag set; the second argument unread.
- **can aim** (vt+0x24, 0x4e7490): no turret node (+0x704), true; else the
  aim in the turret's frame (by the rows of its cached world matrix,
  object3d data +0x60; render.md), flattened
  and normalised (0x4020d0); a zero or NaN length false; else -z > the
  argument (strictly).
- **turn** (0x4e72f0, the +0xa04 base's vt+0x04 and IControls vt+0xbc,
  answering 1): the aim (else the owner's +0x18, vt+0x10) in the turret's
  frame as above; its heading atan2(-x, -z) past 0.001 turns the turret
  node about y (0x4fbef0) by (1 - f) of it, f = e^(-5.545 dt): the rate is
  -ln(0.5) / 0.125 (half the way every 0.125 s, made once at [0x60b880]),
  and the exponential is the float-bits trick ftol(-k dt x 12102200) +
  0x3f800000.

`TurretLimits` and `turret_*` in `mech3/veh_controls`, the slots in
`mech3/veh_basic_object`, `AiVehicleControls` (zai_brain) holding the aim;
`test_veh_aim.py`: 600 runs against the real vtables, 14 breaks, 13 caught
(the fourteenth is equivalent: a NaN never reaches the high limit's test).

## The AI vehicle controls' slots (step 2b2, ported 2026-10-23)

The AI vehicle controls (0x4e71c0, 0xcd8 bytes) answer the brain through
the same IControls slots as the mechs' AI controls, most by thunks that
subtract the vtordisp and 0x2bc to reach the vehicle controls base at
+0xa08 (so `ecx - 0xa00` is +0x08), the rest by `ecx - 0x10` into the
brain at +0xa04:

| IControls | function | does |
|---|---|---|
| +0x0c | 0x4e71a0 -> 0x4e9710 | the brain's frame |
| +0x10 | 0x4ad720 | the fire flag (+0x390) |
| +0x14 | 0x4ad640 | the commands cleared |
| +0x18 / +0x1c / +0x24 / +0x30 | 0x4a5b20 / 0x4a5b50 / 0x4a5b90 / 0x4aed90 | throttle < 0, throttle, turn, turn (the goal is the turn itself) |
| +0x20 / +0x28 | 0x4a5b70 / 0x4a5bb0 | throttle, turn set |
| +0x34 / +0x3c / +0x80 | 0x4a5bc0 / 0x4a5bc0 / 0x40bae0 | nothing (no jets; no scan aim) |
| +0x70 | 0x4e7790 -> 0x4a5270 | on a path: always |
| +0x94 / +0x98 / +0xa4 / +0xb4 | 0x4ea520 / 0x4ea500 / 0x4e94f0 / 0x4ea630 | the brain's hit, ally hit, tick, kill (the mechs' too) |
| +0xa0 | 0x4aed40 | hit within a second (the hit record +0xcc8) |
| +0xa8 | 0x4e7800 -> 0x4ea6b0 | the death: the tick, then the radio's ejecting line (no triggers handed on, unlike the mechs' 0x4d8a00) |

The primary vtable (0x5cd880): +0x1c destroyed 0x4e7470 (the owner's +0x08
clear), +0x20 0 (0x4d72a0), +0x04 / +0x08 the path's mode and goal (0, -1:
0x40bae0, 0x4a5260), +0x24 / +0x28 the turret (2b1). The brain's smoothing
(+0xa04 vt+0x04) is the turret's turn 0x4e72f0. The base controls' answers
are the defaults of `VehicleControls`' virtuals (destroyed: yes, 0x4a5270).
`AiVehicleControls` (zai_brain) implements the brain's `BrainControls`;
`test_ai_brain.py` calls the original's slots on its 690 vehicle cases and
compares. 0x4e95b0 (+0x84), 0x4d99e0 (+0x88), 0x4ea700 (+0x60) and 0x4d1e60
(+0x54) are shared with the mechs and answered where they are called.

## The setup's points and zone (0x49f660, 0x49f710, ported 2026-10-23)

After the motion block, the mission start (0x49f8a0) calls 0x49f660 on the
vehicle: the controls' vt+0x18 (0x4accd0, the gravity word), then each of
the controls' points (+0x160, count +0x214) is the motion's collision point
(+0x764+0x194) off the entity's position (0x5a85e0, asked each time). Then
0x49f710 finds its zone: the node's flag 8 saved and cleared (so the query
misses the vehicle itself), the ground under its x and z from 500 up
(0x4edab0), the flag put back, the pick 0x4abd90 (step 4, water not
ground); when the query found ground, the picked polygon's 4-byte zone
record becomes the vehicle's +0x64 (zEntity's 0x5a80d0) and the node's zone
byte (0x5a80c0 -> 0x4eb810) the first id the record shares with the view's
camera (0x53cd20: 0xff with zones off, either list empty or an 0xff met),
else the record's first id. `vehicle_points_place` and
`vehicle_zone_from_ground` (veh_basic_object), `zmod::zone_common`
(gmod_cull); `test_veh_setup.py`, 600 runs, 12 breaks caught. zEntity's
per-frame zone update 0x5a80f0 (vt+0x38) is left for the frame.

## The movement (step 2c, in progress)

The vehicle's frame (vt+0x34 0x4a1040, from the entity manager's walk) runs
the controls' IControls vt+0x0c (the AI's: the brain's frame); destroyed
controls stop the engine sound and the motion animation; on a path, the
movement 0x4a1240: by the motion's mode, BASIC 0x4a1d90 or TRACK 0x4a12c0,
nothing for FLY, SUB, HOVER and AMPHIB (every shipped MW3 vehicle but the
stealth object is TRACK; no vehicle file uses the others), then the
orientation normalised and the controls' world and local velocities copied
to the entity (+0x30, +0x3c). The pieces ported so far (2c1, `mech3/veh_move`):

- **The turn rate** (the controls' vt+0x14): the base's 0x4a2a00 adds the
  turn x turns[0] x dt (none: damped by turn_damping; against the turn:
  first stopped) held to +-top_turn (+0x8c); the AI's 0x4a2ae0 sets it to
  the turn x top_turn outright.
- **Turning to a heading** (0x4a28c0, while +0x760): a turn command cancels
  it; a full turns[1] x dt a frame while the heading is further than that,
  then the yaw (+0x330) put on it.
- **The speed** (0x4a2b10): the local z speed (+0x78) less throttle x
  rates[0] x dt, damped by rate_damping[1] with no throttle or against it,
  held to |throttle| x top_speed (+0x88); the side slip (+0x70) damped by
  rate_damping[0] and dropped under 0.01.
- **Sliding** (+0x34; 0x4a2dd0 instead): the world velocity (+0x64) turned
  into the local one, the speed as above but held to top_speed itself, and
  the side force (0x4a2c90) applied: the slope's (the matrix's [1] x -60)
  plus the turn's (-z speed x (forward x +0x314) / dt); standing, a force
  past friction[0] starts a slide less friction[0]; sliding, less
  friction[1] against the slip; the slide ends when the slip turns over.
- The x87 compares: "below" tests pass NaNs and "above" ones do not, so a
  NaN speed clamps to the low bound and a NaN slip counts as none.

`test_veh_move.py`: 1200 runs through the AI controls' real thunks, 14
breaks, 13 caught (the heading's exact-equality case is never drawn).

## The suspension (TRACK, 0x4a1760, step 2c2, ported 2026-10-23)

A track vehicle rides on its four supports (`platform`, the motion's
support[0..3]; their world places at support[8..11]); support_word[0..7]
are the supports' vertical speeds (floats), [8..15] their in-air flags.

- **The ground** (0x4a23f0): the supports' world places, one query for the
  four from 500 up (0x4ee2f0, the vehicle's node left out by its flag 8),
  each support's ground the pick (0x4abd90, step 4, water not ground) and
  its slope the world velocity against the polygon's normal (-(vx nx + vz
  nz)); support 0's polygon gives the zone (as the setup's). A support with
  no ground at all: nothing more this frame (answer 0). Each support's sum
  of downhill pushes on polygons steeper than [0x5e812c] (0x4a2770) goes
  into a local the exe never reads.
- **The top speeds**: rates[1] and turns[1], times quicksand_slowdown with
  two supports counted in quicksand, else lava_slowdown with two in lava.
  The count is by the pick's water flag, which is only 0 or 1, so the
  quicksand (3) and lava (4) counts never rise: vehicles are never slowed
  (and the 0.3 sink of such supports never happens). A candidate for Owen
  (fixes.md).
- **The supports**: the lowest-ground support is left as it is; each other
  one steps (0x4a2820: gravity x1c, falling while above its ground, landing
  with the slope for its speed, else following the ground); in the air when
  all three are (+0x20).
- **Pitch and roll rates** (+0x50, +0x58): from the other support with the
  least vertical speed and the next two round the four (skipping the
  lowest), solved as the plane through their speeds; all in the air, back
  toward level instead ((pitch + 0.5236) x -0.7, no lower than -0.7; roll x
  -0.7); smoothed by a_damping (e^(-a_damping dt)); the three supports'
  speeds then set from the rates. The chosen support's speed is the
  vehicle's vertical speed (+0x68, +0x74).
- **The attitude**: up the plane through the three non-lowest supports
  (0x524020), the position put on it (0x4abf70) and set (vt+0x18), the
  forward tilted onto the up (0x4a3e40), the matrix rebuilt (0x4a3ed0:
  forward x up, up, -forward, the position), pitch and roll from the up
  turned by -yaw (0x523e70): asin(z), asin(-x). All in the air, each held
  to +-0.5236 (30 degrees), its rate and any rising support speed zeroed.

The exe leaves two picks unset in impossible cases (no support under 5000,
every speed past 10000); the port starts them at the first. `vehicle_suspension`
and its pieces in `mech3/veh_move`; `test_veh_suspension.py`, 600 runs with
the original's own helpers (the pick, exp_neg, asin through a stand-in for
the CRT's, the normal, the turn), 23 breaks, 22 caught (the water surface
feeds only the dead quicksand branch).

## The frame (vt+0x34 0x4a1040, step 2c3, ported 2026-10-23)

- **The frame** (0x4a1040, the entity manager's per-entity vt+0x34): the
  controls' frame (IControls vt+0x0c; the AI's is the brain's); destroyed
  controls make the vehicle inactive, its engine sound and motion animation
  off. When the frame answered 1 and the controls are on a path (the AI's
  always): the movement, the motion animation, the player's mech's lock
  position kept at +0x6f0, the weapons (0x4b4880: the trigger is fire ==
  1, then the manager's update), zEntity's refresh.
- **The motion animation** (0x4addc0, `motion_anim` at +0xf0): it runs
  while the throttle is past 0.9 either way, and stops the first frame it
  is not (its hold time, [0x60b15c], is never set).
- **The movement** (0x4a1240): nothing for a frozen vehicle (+0x28); BASIC
  0x4a1d90 or TRACK 0x4a12c0, then the orientation (+0x48) the turn from
  (0, 0, -1) onto the forward (0x525a50) and the controls' velocities
  copied to the entity.
- **TRACK's step** (0x4a12c0): the heading's turn, the turn rate (vt+0x14;
  all in the air: damped by e^-dt), the yaw kept within +-2 pi, angles0
  kept, the matrix from the angles (0x4ab990), the up, the speed (in the
  air: both damped by e^(-dt/2)), the world velocity (-forward x z + its
  side x x) and the position moved by it (vt+0x18), the collision points
  placed (+0xac), the collisions 0x49ff80 when the controls' vt+0x20 is 0
  (every MW3 controls), BASIC's ground 0x4a1f00 or the suspension, the
  shadow shown or hidden once two frames agree (on the ground, in the
  air), the node's rotation and matrix, the points again (and their copies
  at +0x160), the matrix and forward kept (+0x278, +0x314), and the tracks'
  textures scrolled by (turn rate x 2.25 -+ speed) x 1.72. (0x526ae0 then
  0x541ad0, ported: render.md section 3, Texture scroll; the treads move in
  the program from 2026-10-25.)

`vehicle_frame`, `vehicle_move`, `vehicle_track_step`,
`vehicle_motion_anim_step`, `vehicle_weapons_step` in `mech3/veh_move`;
`test_veh_frame.py`: 600 track steps and moves on test_veh_suspension.py's
vehicles with the base controls' real slots, and 300 frames, 24 breaks
caught. The collisions (0x49ff80) followed; BASIC's two steps (0x4a1d90,
0x4a1f00, the stealth object's) wait for the end (The stealth object).

## The collisions (0x49ff80, step 2c4, ported 2026-10-23)

Each TRACK step, unless the controls' vt+0x20 says otherwise (none in MW3),
sweeps collision points from last frame's places (+0x160) to this frame's
(+0xac):

- **Which** (0x4a0180): turning more than moving (|speed| below |turn rate
  x 3.3|) the corners 0, 2, 3, 4, 5; moving (speed squared past 0.01) the
  front 0, 1, 2 or, going back, 3 and 5, with 4; sliding more than 1 a side
  the right's 6..8 or the left's 9..11 (a NaN slip counts as left), those
  placed afresh by this and last frame's matrices.
- **The query** (0x4a0910): one world segment query for all, the vehicle's
  node left out (its INTERSECT flag off, then on); each crossing sorted
  (0x4a09e0): one on an object (its node's object, 0x40eee0) to objects;
  else a normal y below -0.9 a ceiling, below 0.71 a wall; a floor of soil
  5 (fire) sets +0x2c. The exe then adds that floor to a list that is the
  segment's own start on its first crossing (a pointer written into its x)
  and null after (a crash): FIX (bug), the flag only. +0x36c is 1 when
  nothing was sorted.
- **The responses** (0x49ff80), each by its list's head (the last added):
  objects and walls 0x4a0b40, ceilings 0x4a0f50 (rising speeds turned
  down, the height put back to last frame's, the points with it); the lists
  freed. With no object or wall response, a remote side's collision record
  (+0xd8, multiplayer: on and active, of this frame, sent within the last
  four) answers as a wall with its normal and point.
- **A wall** (0x4a0b40): the hit node's touch with damage 1 when it has its
  own handlers (slot 1); nothing more for a normal flat in x and z (or NaN);
  else, in the ground plane: the vehicle moved out by the segment's end
  reflected about the normal from the crossing (zmth::reflect), the points
  with it; the velocity the slide along the wall plus collision_d[0] of the
  part into it, its height kept, and the local velocity turned from it; the
  turn rate kicked by collision_d[1] x the speed toward the side hit.

`vehicle_collisions` and its pieces in `mech3/veh_move` (CollisionHit now in
`mech3/collision_hit.h`, shared with the mechs'); `test_veh_collide.py`, 600
runs with the original's own sweep, sorting and responses, 24 breaks
caught; `test_fixes` fix_vehicle_collision_fire_floor.

## In the program (step 3a, 2026-10-23)

`mech3/app_vehicle` wires the basic vehicle's calls out to the ports (the
node lookups, copies and names, the world's node list, zEntity's list,
zEffect's animations, zWeapon's hit state and handlers, zEntity3d's
refresh: `Vehicle::entity_moved`, now on the shared base) and gives the
mission start (player.cpp, app_mission's `WiredPlayerHooks`) its vehicle:
made over the AI's controls, named, on its team, placed, set up (the
controls' setup through vt+0x0c), the motion block, the points and zone,
zWeapon's hit handler (0x4af550's port) on its node. c1m1's four (ttruck_01,
ttruck_02, apc_01, apc_02) are made, in the world, with their health and
weapons (`mission_start_reads_c1m1_vehicles_headless`), and on zEntity's
list with the player, the Owens and the command marker. Their frame, brain,
sensing and engine sound since step 3b (below); their teardown and kill
credit since 3c. The mobile field bases since 4a1 (below).

**The teardown and the kill credit (3c).** The mission's end deletes every
zEntity entry through vt+0x00; a vehicle's is 0x4ac630(1) (`vehicle_delete`,
`program_vehicle_free`). A vehicle killed by a weapon credits the shooter's
entity (obj.vt+0x80): a mech's 0x41b420 counts one vehicle and 75 points
and, in the campaign for the player or an ally, salvages it (0x472ec0: on a
coin, its weapons); the victim answers itself to vt+0x2c and vt+0x40, 0 to
vt+0x48 and vt+0x50. A vehicle's own vt+0x80 (0x4a5bc0) is an empty
`ret 4`: a vehicle that kills scores nothing.

**The collision count.** Every shipped vehicle's model has the twelve
collide00..11 nodes, so the motion reader takes the points from the model,
and that path never writes the count (+0x214): it stays 0 (nothing else
in the exe writes it). The TRACK step then places no collision points, and
the sweeps' segments run between unset ones, except a slide's side points
(6..11), placed afresh. So vehicles cross walls and buildings unless
sliding. The list path writes 12; a candidate for Owen (fixes.md): giving
the model path the count would make vehicles collide as their data says,
which may change how the AI's vehicles get round obstacles.

## Brain, sensors and the engine sound (step 3b, 2026-10-23)

The entity walk (0x4a1140) runs a vehicle's vt+0x34, the frame 0x4a1040,
when its +0x09 is set, as a mech's. What the AI's brain, the sensors and
valves.zrd ask of an entity goes through its vtable in the exe; the
program answers by kind (`is_program_vehicle`), from the basic vehicle's
vtable 0x5c88e8:

| slot | function | answer |
|---|---|---|
| +0x0c | 0x5a82d0 | the velocity, +0x30 |
| +0x1c | 0x5a85a0 | the node takes the matrix (`entity_moved`) |
| +0x24, +0x28, +0x2c, +0x40, +0xec | 0x4d2a70 | itself |
| +0x50, +0xa0 | 0x40bae0 | 0: no mech, never powered down |
| +0xb4, +0x144 | 0x4d7290 | 0: not hidden from radar, never airborne |
| +0x148 | 0x45b960 | [0x5c23a0], 0.0: no jump fuel |
| +0x70 | 0x4ad2d0 | the death (valves' destroy) |
| +0x98 | 0x4ad820 | the health share |
| +0x8c, +0x90, +0x150, +0x154 | 0x4ad780, 0x4ad7c0, 0x4ad6e0, 0x4ad700 | the gun's aim (above) |
| +0x114..+0x12c | 0x4acfa0.. | the speeds (above) |
| +0x140 | 0x4020b0 | its controls (+0xe4) as IControls |
| +0xc8 | 0x4add90 | the brain's empty vt+0x08, then the engine sound released |
| +0xcc | 0x4aee00 | active, and then the brain's vt+0x0c (+0x54 clear); true without a brain |
| +0xd0 | 0x4aee40 | the brain's vt+0x10 (sees its target); true without one |
| +0xdc | 0x4a3630 | the placement |

The sensors' +0xd0 false clears +0x09, so a vehicle whose brain does not
see its target (the player or the player's lance within sight range)
skips its frame: it stands frozen until someone comes near. The brain's
+0x88 (aiv.zrd's p8 item 10; 1 on c1m1's trucks) makes it always see.
This is the mechs' level of detail in another form.

**The engine sound.** The vehicles of one engine type share one entry of
the sound cache (0x60b110), with one voice. Each frame the sensors release
every vehicle's entry (vt+0xc8: 0x4adcd0, 0x4adc30): the claim is dropped and the
voice moved to a far point (0x4adb60: about 5e18 out on x, -y and z, too
far to hear). The radar ping (0x4aa400) then walks the view's contacts
nearest first; the first whose vt+0x40 has a vt+0xec (a vehicle answers
itself; a mech 0) claims its entry at its place (0x4adce0, 0x4adc60): the
voice moved there (0x571f30, no velocity, no doppler), or made there at
full volume (0x56e7e0) and locked (0x56f3f0) on the first claim. So only
the nearest vehicle in sensor range is heard, and it is heard at its place.
Ported (`EngineSoundEntry::claim` / `release`, `EngineSound::play` /
`release`), checked by `test_veh_engine.py`.

c1m1 (`mission_c1m1_vehicles_run_their_frames_headless`): after bramp the
trucks drive their net_01 (node_00/01 to node_08/09, where they wait for
truck_go); truck_go sends them on to node_15/16, whose valve_assign sets
destroy_truck_01/02, and the valves' destroy kills them (vt+0x70). The APCs
drive their own nets, more than 100 m.

## The mobile field base (0x4589d0, mech_map / mech_objective region; step 4a1)

`mech3/mech_fieldbase`, ported 2026-10-24. The mission start makes every
vehicle.zrd type `mobilehq` (matched whole) with 0x4589d0, the rest with
0x4ac320. c1m1's mobilehq_01..03 (aiv kind 2) are AI-driven over their net,
health 75.

**The class** (0x160 bytes, vtable 0x5c23d8): the basic vehicle's slots
but vt+0x00 (the destructor 0x458b30 / 0x458bc0), vt+0x34 (the frame
0x459640), vt+0x70 (the death 0x4590b0), vt+0xf0 and vt+0xf4 (0x4d2a70:
itself; the basic vehicle's and the mechs' 0x40bae0 give 0: so the AI's
station keeping, 0x4cc150, takes a field base as one), vt+0x158 (the setup
0x458d40) and vt+0x160 (the damage 0x458f50). Its own fields: a std::list of
queued steps at +0xfc..+0x107 and a "begin the front one" flag at +0x108,
the seven nodes `arm01..04`, `cover_left`, `cover_right`, `cover_middle` at
+0x120 (found in its tree, 0x4f9d60), the arm script at +0x13c, its parts
by the same names at +0x140, and an animation the dock starts at +0x15c
(0x458cf0 / 0x458d20, the registry's [0x605c44]). The limiter classes
0x4a42f0 / 0x4a4580 / 0x4a4800 are the gun's (the motion block); the field
base makes none.

**Made** (0x4589d0, 0x458a70): its node always a copy of the world's
`type` node, renamed and put in the world (the basic vehicle first looks
for a node of its own name); the basic vehicle made over it without a node
(0x4ac530(ctl, 0)), then given it (0x4d6c90, onto zEntity's list), 16 high
for the AI (+0x68, the basic 12); then the registry's add, and the node's
flag 8 cleared (0x4eb620).

**The registry** (the static at 0x605ae0, constructed by 0x459b80, emptied
at each mission start by 0x459e80 -> 0x459e50): up to three bases (+0x14)
with a valve each (+0x20), named after the base's node and set to 0
(objectives.md: the field-base objective reads them), +0x10 the count. In a
campaign no more than the campaign's +0xa8 (the bases left: 3 at the start)
are taken: a base past them is put out of play at once (+0x08 cleared, its
engine sound dropped, its node hidden 0x4eb170) but still counted. So after
losing a base in one mission, the next missions show one fewer: the third
is there but hidden and inert. A fourth base would be written past the three
over the first valve (FIX (bug); none of the shipped missions has one).
The alive count 0x45a000 counts the registered bases still active. The
mission start (0x459e80) also finds the dock's eleven radio lines
(+0x8c..+0xb4: mcb_friendly_fire, mcb_under_attack, mcb_lost_vehicle,
mcb_occupied, mcb_pos_wait, mcb_pos_cancel, mcb_contact, mcb_waiting,
mcb_deploy, mcb_abort, mcb_complete) and its effects animation
`mfbeffects` ([0x605c44]); no shipped sound set has `mcb_deploy`, so that
line (the deploy state's, once the bases have opened) is silent. The rest
of the registry is the repair dock (below).

**The setup** (vt+0x158 0x458d40): the basic one (0x49ec00), the seven
nodes, the scripts' loose-file path `..\data\common\mechs\animations`
added to [0x736584] (0x576140; the mechs' set-up 0x413e60 made it, and it is
never trimmed, so it grows by one entry per base per mission; the port's
motions come from motion.zbd only), the arm script (0x458890: the script
`mobilehq` from motion.zbd at 30 frames a second, its loop 1 s, played once:
+0x70 clear; its target +0x88 and time +0x8c 0, shut), its seven parts, +0x09
set (the entity walk runs it), and the AI record: never asleep (+0x88, p8
item 10), +0x5c 0, +0x60 1, +0x80 1, can_leave (+0x3c) and the three scan
switches (+0x98..+0xa0) off.

**The frame** (vt+0x34 0x459640), when +0x09 is set: the front queued step
is begun (when +0x108 says so), else ended and dropped when done (the next
then begun), else stepped (vt+0x04; true marks it running, +0x04). Then the
basic frame's controls update and destroyed test (as 0x4a1040), but when
the update ran: the throttle set to 0 unless [0x60b4d0] (the dock lets the
bases drive; 0 at the mission start, 1 from 0x45a780), the move 0x4a1240,
the motion animation 0x4addc0 and the refresh, without the basic frame's
path test, player aim and weapons step. A destroyed base still runs the
front step's after (vt+0x08) and the arms. So a field base on its net
stands where it was put until the dock moves it.

**The arms** (0x4597d0 over 0x458900): the script's time moves toward its
target (0 shut, 1 open) at 0.2 a second: five seconds to open; each frame it
moves, every part's pose at that time (translation, rotation as a
quaternion, 0x525740) is set on its node (0x4fc3f0, 0x4fc300). A NaN time
goes to 0; a NaN difference stops.

**The damage** (vt+0x160 0x458f50): nothing to an inactive base. Off
multiplayer, a base on the player's side (the player's mech's allies
+0xc4 against its team) takes the difficulty's share as the player's mech
does ([0x608e5c]: 0.7, 1.0, 1.3). Then the basic damage (0x4ad3f0); a base
it kills: the radio's "base lost" line (+0x94, 0x4e6d50) while any base is
still active, else 0x47a720 on [0x60b544] (the last base lost); a base hit
and alive: the attacker's entity (0x581e40, 0x40eee0) on its side gets the
friendly-fire line (+0x8c), any other the under-attack line (+0x90), and the
base's controls are told (IControls vt+0x94: the AI's 0x4ea520). The lines
are mcb_lost_vehicle, mcb_friendly_fire and mcb_under_attack.

**The death** (vt+0x70 0x4590b0): the basic death (0x4ad2d0, which acts
once), then in a campaign the loss below (A mobile field base lost). The
loss is not guarded by the death's once: each call takes a base off the
campaign's count. The valves' destroy and the damage both call vt+0x70;
the damage only for an active base. Unported (`base_lost`): the cut of
what the player carries (step 4b or later).

**The destructor** (0x458bc0): the dock's animation stopped, the node
taken from its first parent and freed (with zEffect's animations on it,
0x506bc0(node, 1, 1)) when it has one, off zEntity's list, the arm script
deleted, the steps' list emptied (the steps are the dock's), then the basic
vehicle's (0x4ac650, with no node left). The registry keeps the base's
pointer until the next mission start empties it.

In the program: made by the mission start (WiredPlayerHooks'
mobilehq_create), its slots by the class (`BasicVehicle::field_base`, ours:
the death and damage hooks, the frame, setup and free in app_vehicle),
vt+0xf4 for the AI's station keeping, the registry emptied by the mission
load's 0x459e80 hook. The debug launch has no campaign behind its game, so
the bases left read 3 there (a new campaign's). Checked:
`test_veh_fieldbase.py` (300 runs of each piece, 45 branches, 25 breaks)
and c1m1's headless test (three registered and alive, held in place, the
arms of one opened through its frames, one shot down, all freed at the
end).

## The repair dock (0x459b80..0x45b9c0; states and drives 4b1, frame and request 4b2)

`mech3/mech_fieldbase_dock`, ported 2026-10-24. The registry (0x605ae0)
holds the dock: a list of states (+0x00..+0x0b, a "begin the front one"
flag at +0x0c) run by the dock's frame (0x45a270, from the mission's frame
0x48f4ad, off multiplayer) as a field base runs its steps: the front one
begun (that frame does nothing more), else stepped until it says it is
finished, then ended, dropped and the next begun. A step that answers
finished after its own abort has unwound the list (the repair's) marks
the dropped state, not the new front: the close it unwound to was begun by
the unwind and runs from the next frame, so the bases close as they
should. The seven states are members of the
registry, each with the registry at +0x08 and the radio line it says at
+0x0c (vtable 0x5c2600 their base and the drives': begin clears +0x04, step
and done return it, the message aborts, the abort clears the dock's marker
0x47a720 on [0x60b544] and sets +0x04):

| state | vtable | what it does |
|---|---|---|
| idle +0xbc | 0x5c25e0 | waits (+0xb8 set); its message 0x45a780 is a mech's request (below), which queues the six others in the order below |
| places +0xe0 | 0x5c2620 | five frames on, says mcb_contact ("move into position"?); done when every active base with a valve has its valve set (each base's net sets it at its place) |
| contact +0xd0 | 0x5c25c0 | says mcb_waiting once (in play the bases' "we have arrived and are ready to deploy", Owen 2026-10-24); done when the requester is within 12 m (squared 144, [0x5e4a18]) of the dock point and powered down (vt+0xa0); [0x60b4cc] set |
| deploy +0xf4 | 0x5c2640 | busy (+0x78); each active base's drive out queued at the front of its steps; the player's view onto camera mode 4 (its own kept at +0x80); the mech at rest at the dock point (0x418e90); done with the three drives, saying mcb_deploy (no shipped sound); the mech powering up aborts |
| repair +0x110 | 0x5c25a0 | busy; the effects `mfbeffects` on each active base (0x458cf0); twelve seconds, then the mech repaired (0x4154e0 with that frame's time); powering up aborts; its end says mcb_complete and stops the effects |
| close +0x124 | 0x5c2580 | each base's drive in; the view back to its camera if still on mode 4; done with the drives; [0x60b4cc] cleared |
| reset +0x140 | 0x5c2560 | the bases held again ([0x60b4d0] 0), "mcbdeploy" 0; its end queues the idle state again |

The aborts: the places' and contact's (0x45ade0) say mcb_pos_cancel and
unwind to the reset; the deploy's (0x45b1b0) says mcb_abort, drops its
drives (0x459af0) and unwinds to the close; the repair's (0x45b3b0)
unwinds to the close, its line said by its own end (the unwind ends it
after it set mcb_abort). The
unwind drops the list's front states until the target is the front, ends
the old front and begins the target. While the dock is busy, another mech's
request (the message 0x45b770 of deploy, repair and close) hears
mcb_occupied. A line is said only once [0x605c4c] (the quiet time) has
passed; the places state holds the next one five seconds.

**The drives** (0x18 bytes, vtable 0x5c2540, 0x4598a0; their slots
0x4598d0..0x459af0, 0x45b9a0, 0x45b9c0): out, the base's arms open and it
backs at a quarter throttle until its travel (the stand-off 15.4 less its
distance from the dock point at the start, then plus its local z speed
each frame) passes -0.2; in, the arms shut and it drives forward from its
distance to its place in (+0x110) until the travel is under 0.2. The
throttle drops to 0 as soon as the stopping distance (vt+0x124) would
carry it there. Done when it reached it with its arms there (open: the
arm time 1, shut: 0; NaN counts as there), or with no base or an inactive
one. Its abort drops its base's front step and begins the next.

The drives are freed by the state's end, but a drive can still be on its
base's steps then (the base drops it a frame later). The exe goes on
reading the freed drive until the base drops it; the port keeps it until
then (FIX (bug), no change in play). A mission with fewer than three bases
(none shipped) would have the drives and the places' check follow a
missing base (FIX (bug): passed).

**The request** (0x45a780, the idle state's message; the registry's
0x45a500 passes a mech's ask to the front state, which the player's MFB
command 0x47a7a0 sends on its second press): the requester kept (+0x74),
the mcbwaynet's dock point ([0x60b544] the waynet "mcbwaynet" (0x4d5460),
its element by its +0x20, 0x58b060) and its name (vt+0x24); with both the
bases may drive ([0x60b4d0] 1), "mcbdeploy" 1, the point's own valve
(found by its name, 0x4d3d20, +0x88) 1 and the bases' valves 0. Each base's
brain (vt+0x140, IControls vt+0x54), on its current order's net, finds the
net's node of the point's name (0x58b0a0: an element by name) and goes
(0x4c84b0) to that node's link i (base i's place; c1m1's net_02 has the
node "MFB Baker" with three links), its way finder dropped first. Then the
rings around the dock point (0x45a560: three points on the near ring,
15.4 m, the first in +z and the others 120 degrees round, and three on
the far ring, 45.4 m; each base given its two and the point, +0x10c, +0x110,
+0x114) and the six states queued: places, contact, deploy, repair, close,
reset. 0x4c84b0 (the brain's go-to, `Brain::go_to`): on the current
order's net, the way finder (+0x238, 0x4c7c10) from the current state's
behaviour to the target; when that behaviour is +0x23c's (none), the
current order is dropped first and the new one's behaviour asked, its
answer unused. The way finder starts from the vehicle's position
(0x5a85e0, read by 0x4c7e80): with none it finds no way.

**Playtest W10** (c1m3, 2026-10-30; a port bug, not the exe's): the
program's brain hooks gave the way finder no position (`pilot_of` left the
pilot's `pos` null), so every go-to failed. The bases then followed net_01's
links as any net walk does, round MFB 2, 3 and 4 before Charlie, and took
whichever slot their link picks led to: in one run base 02 came up behind
base 01 on node 22 and stopped 30 m short, its valve never set; in Owen's
game one drove on past the slots (each slot links on toward nodes 32 and
33) into the enemy. With the position, each base gets its way to its own
slot (link i of "MFB Charlie": nodes 22, 23, 24), and all three park there.
mw3tests `mission_c1m3_field_bases_park_at_charlie_headless` (the enemies
taken out first, the MFB command from Charlie's region): all three on their
slots and the dock's places step done; without the position, 3 checks fail.

**Playtests W8 and W16** (2026-10-30): the dock waits for good when the
bases are called to the point they last docked at. The close drives each
base back onto its slot and the reset holds it there (field_bases_drive
off), still in its slot's behaviour on its net; the request then sends it
to that same behaviour, the way finder has nothing to do, no node is
entered, its valve is never set, and the places step (0x0045ab70) has no
give-up. A base already on its slot stays while the others drive ("only one
moved"); called to a new point all three drive. This is the exe's own flow.
The port's routing is right: in c2m2 all three reach each of the four
points (MFB 3 and 4 by a long way round through MFB 2's road). FIX (bug)
behind `bugs`: at the request a base already in its slot's behaviour counts
as arrived (its valve set); in the places step a base that has not moved 2
m in 30 s short of its place is given up on. Test:
`mission_c2m2_field_bases_answer_a_second_call_headless` (with `bugs`
the dock is parked after the first repair, below, so the second call at
the same point goes straight to the contact; the arrived valve still
answers a call from the idle state, and the give-up a first call with a
base stuck). Earlier, not
reproduced: the player on the point, on a slot, standing or powered down
(`mission_c1m1_dock_runs_with_the_player_waiting_headless`).

The dock point comes from the mcbwaynet and the request from the player's
MFB command (below).

**Multiplayer** (game type 3, the same frame): no states; with the game's
repairs on (0x479480, +0xa8), the player's mech active and powered down,
and the nearest station ([0x609980], 0x4d51d0) within 20 m (squared 400): an
enemy (the nearest contact, 0x4d6b90(3), with a mech) within 100 m gives
"%s is too close for MFB deployment" (its name); else within 6.5 m (squared
42.5) the repair at the station (0x45a180), past it "too far from MFB
depolyment point" (sic). The repair: "mfb_station_01" on the station's node
(by its name, +0x2c); a local mech shut down (+0xdd), the view on camera 4,
at rest at the station, the animation's end (0x45a0f0) repairs it
(0x4154e0), clears +0xdd, powers it up and puts the view's camera back
(+0x5c); the other players told (the stations' vt+0x14, 0x46ff90). A remote
mech only shows the animation; the network's message (0x46ffd0 ->
0x45a140) finds the station by id.

The repair itself (0x4154e0) is the mech's: next section.

[0x60b4cc] and [0x60b4d0] are read by the AI's repair state (0x4d0ce0,
below: The AI at the dock). The mission start (0x459e80) ends any dock left running: the
list back to the idle state alone, to begin, and "mcbdeploy" 0 (made
there when missing: every mission has it). Checked: `test_veh_dock.py`
(600 state runs, 300 drive runs, 600 frame, request, ring and station
runs; 100 branches; 30 + 26 breaks) and c1m1's headless tests (the dock as
the mission starts; with the dock point stood in at "MFB Baker", the
player's request, the bases to their places, the player onto the point
powered down, the deploy, the repair 12 s on and once, the close, the
reset and idle again).

## The repair (Mech::repair 0x4154e0, mech3\mech.cpp; step 4b4)

The dock's repair state (0x45b320, with that frame's time, unused) and the
multiplayer station's end (0x45a0f0, with 0) call it; it does everything at
once and returns 1. Ported in `mech3/mech_repair.cpp`, the store's two calls
in `mech_repair_store.cpp` (pilot.h's Handle is zgame's).

1. Internals: `set_internal(1, 0)` (vt+0x3c) on both legs, both arms, the
   head, then the torso's sections 0, 1, 2 (centre, left, right; the rear
   sections share them). A destroyed part's own set_internal keeps 0, so a
   lost arm stays lost, but a side torso at 0 comes back to full (its
   arm does not).
2. Ammo, from the store 0x479430 ([0x60861c]'s current game +0x08): for
   each weapon of the manager (+0xd4) with an index (+0x28 not -1) that uses
   ammo (0x484960, the table at 0x5e6504) and a bin (+0x58): the need is
   capacity - left (0x4b4d00, 0x4b4cf0: 0 for an infinite bin); an
   `AmmoItem(type id, 1)` (0x41ebd0; the def's +0x1f0 WeaponType +0x00)
   finds its like (0x4714c0). None: nothing. An unlimited item (+0x10):
   the need, free. Else the lesser of the need and its count, taken off
   the count (not below 0; the item stays at 0). Then `bin->add` (0x4b4dc0).
   Bins are shared, so a second weapon of the type finds its bin full.
3. Armour: an `EquipmentItem(ferro ? 2 : 1, the config's armor_tech, 1)`
   (0x41ec00; config +0x34, +0x30). Only when the store has one:
   - the armour left: the sum over the eleven sections (legs, arms, head,
     torso 0..5) of armour / armour scale (vt+0x44 / vt+0x4c; the scale is
     100 / the section's points), times 100, floored: the points left;
   - points = 0x424760(tech, ferro, armor_tons): floor(tons x 16.0001, x1.2
     Clan ferro-fibrous, x1.12 Inner Sphere ferro);
   - the cost: floor(0.5 + 2 (points - left) / k), k 16, Clan ferro 19.2,
     IS ferro 17.92 (0x5bcd5c..0x5bcd64), so one item (half a ton,
     0x484aa0's weight 0.5) per half ton missing, rounded; above 0 it goes
     through the store's remove (vt+0x14 0x471d20: an unlimited item costs
     nothing, the count stops at 0 and the item is deleted at 0);
   - then `set_armor(1, s)` (vt+0x40) on every section, the cost paid or not.
   In play's 24-bit precision; the port uses float (the test sets it).
4. Coolant (+0x56c) 1; jump fuel (+0x564) 1 with jump jets (+0x3d8, the
   equipment's [11], above 0), else 0; the AMS (+0x3f0) rounds (+0x18) =
   AMS count (+0x3ac) x 100. Heat is not touched.
5. 0x415a90: limping (+0x5ac 0 or 1) with the blend (anims[11], +0x450):
   it freed (0x4152c0, then the delete), the slot 0, limp -1.
6. The player's view ([0x5f4c8c] +0x64 the mech): the HUD (0x430aa0 on
   +0x684) has its weapon lines refreshed (0x446520 on +0x6520), then its
   reset (0x43be10).
7. +0x680 = 0x1fe: the network's section bits.

The port: no store (the program's debug launch has no pilot) finds
nothing, so the internals and the rest are restored but no ammo or armour;
the exe always has a pilot. Checked: `test_mech_repair.py` (800 repairs
over random parts, weapons, bins and stores, the store and items the
original's own; 19 branches; 29 breaks) and c1m1's headless test (the
player's Mad Cat worn to 25 % armour, 50 % internals, the ammo bins
emptied, coolant 0: after the 12 s all back, 440 rounds from the store,
18 half tons of armour).

**After a repair, and the bases staying (2026-10-30, Owen; ours behind
`bugs`).** In the exe a repair is followed by three steps:
1. The close: the bases drive in to their far points and fold their arms.
2. The reset: "drive" and "contact" cleared, "mcbdeploy" 0, the bases
   released to their nets.
3. The idle state.
The repair step also clears the marker (0x47a720), so the MFB command
starts over. A lancemate's refit (0x4ea260 through 0x47a730 and
0x45b740) is taken only while the bases are called and driving
([0x60b4d0]) and the dock is not busy (+0x78). So in the exe a lancemate
can use only a dock the player has called, and only while it waits for its
mech. Then the lancemate replaces the player as the requester. After a
repair, his F10 is refused (disregard_order).

Owen: the bases should stay. With `bugs` (FIX (bug), ours):
- A completed repair marks the dock (`FieldBases::park`). The reset keeps
  "drive" and "mcbdeploy" and queues `DockParked` in place of the idle
  state.
- Parked, the dock reads as idle to the MFB command and has no requester.
  It ends when a mech asks: a lancemate's refit (set_requester, the bases
  driving and not busy), or the player powered down within 12 m of the dock
  point. A player still down on the dock from his own repair must power up
  or leave first.
- The next mech's contact, deploy, repair, close and reset follow (no
  places: the bases are on them), and the dock parks again.
- M-M while parked: at the same dock point the caller is taken. At another
  point it is a new call, as from the idle state. The third M cancels as in
  the exe (the contact's abort, then the reset unmarked), and the bases are
  released.
Test `mission_c2m1_field_bases_stay_after_a_repair_headless`:
- With `bugs`:
  - The player is repaired and the dock parks.
  - The player leaves. Dominic, worn to 25 % armour, takes F10, walks in,
    docks and is repaired to full with no second call. The dock parks
    again.
  - The player powers down on the dock again and is repaired a third time.
  - M, M calls the bases back, and the third M releases them.
- Without `bugs` (the exe's path): the dock goes idle after the player's
  repair, and Dominic's F10 is refused.
The c1m1 dock tests take either end.

## The AI at the dock (0x4ea260, 0x4d08d0..0x4d0ce0; step 4b6)

A lancemate's way to the field bases: the player's refit order (lance
orders 7 and 12, `confirm_refit`) runs the brain's vt+0x3c, 0x4ea260.
Ported in `mech3/zai_repair.cpp`; the dock and the mcbwaynet through
BrainHooks (`bases_alive`, `mfb_ask`, `has_mcb`, `dock_point`,
`dock_drive`, `dock_contact`, `mech_power`), the program's in app_vehicle.

- **The order** (0x4ea260): with a field base up (0x45a000) the dock is
  asked to take the pilot's vehicle (0x47a780: the mcbwaynet there, then
  0x47a730, the dock's 0x45b740, which takes it only while the bases drive
  and no repair runs: so only after the player's own MFB request, the
  lancemate then becoming the requester in the player's place); if the
  dock point is there and the vehicle a mech (0x4d0920), the repair
  behaviour (0x4d08d0, vtable 0x5cc970, made on the stack) is pushed on
  the current order's net (0x4e88c0), and the order answers 1. A dock
  that took the vehicle when the point then fails keeps it as requester.
- **The state** (vt+0x8c 0x4d0990 -> 0x4d0a10, vtable 0x5cca28, built in
  place in the order with the behaviour's copy at +0x08; no break-ins, no
  fire): the dock point read (the mcbwaynet's selected point's place,
  only for a mech); none: the first run pops it. An approach point 40 m
  from the dock point on the pilot's side, the way snapped to a third of
  a turn (k = floor(atan2(-dx, -dz) x 3/2pi + 0.5), at k x 120 degrees).
- **The run** (0x4d0ce0), by phase (+0x24):
  1-2: toward the approach point. When the mission boundary pulls the
  point (0x4c3000), the pilot turns straight for the pulled point
  (0x4cdd40, throttle the brain's +0xec when within 45 degrees, else 0;
  phase 2, every frame while it pulls); else the brain's steering is set
  from the pilot to the approach point (0x4d0c60: +0x298 the pilot's
  place, speed vt+0x118 on +0xec, stopping distance vt+0x120 kept with its
  inverse at +0x18 / +0x1c) and walked (0x4cee80, near at half the
  stopping distance, slack 0.04 each way), phase 3, until the carrot has
  fallen back to its floor and the pilot is within the stopping distance.
  Then the own fights (params +0x80) are saved (+0x14) and switched off,
  and the steering set again to the dock point: phase 4, the same walk;
  there the distance is quartered (the inverse x4) for phase 5; there the
  pilot stops. 6: while the bases drive ([0x60b4d0]) and are not yet in
  contact ([0x60b4cc]) it waits; then (or with the bases not driving) it
  stops, powers down (vt+0x50, 0x41cc30(1)), keeps its brain running
  while down (+0x2a4 1) and gives itself 30 s (+0x20). 7: it stays while
  the bases drive and are in contact, it is still powered down (vt+0xa0)
  and neither the 30 s are past nor it is more than 12 m from the dock
  point (0x51e060, squared 144: the dock's own 12 m). Otherwise, at once
  from 6 too: powered up, +0x2a4 0, the own fights back, the order popped.
- **Its end** (0x4d0ba0): the context read; in phases 4-6 the own fights
  back; in 7 also powered up and +0x2a4 0. A normal end powers up twice
  (the run's, then this one on the pop).

The repair itself is the dock's (Mech::repair at the repair state's end):
the lancemate is repaired as the player would be, from the player's store.
The exe keeps the dock point's pointer (+0x34) into the mcbwaynet's point;
the port keeps a copy (the point does not move; a mission's end frees it).
Checked: `test_ai_repair.py` (300 cases on test_ai_lance's world: 1,800
refits, about 600 taken, 4,600 runs; 20 branches; 28 of 31 breaks, the
rest not observable: the state maker's mcbwaynet test after the refit's
own, the copied flag never read, the run's own fights restore repeated by
the end; the exe's state slot zeroed as the constructor starts, since
+0x14..+0x43 hold the order's old bytes until written, never read before).
Not checked in play: no shipped single-player mission was run with a
lancemate sent to the dock (c1m1 has none).

## The mcbwaynet (mech_waynet.cpp, 0x479640..0x47a950; step 4b3)

`mech3/mech_waynet`, ported 2026-10-24. A waynet is a compiled AI net
(0x4bfe40: element 0 the default behaviour 0x604068) of named points with
one selected (+0x20) (0x479640, vtable 0x5c37b0; its name +0x1c, a list at
+0x28 that 0x47a270 keeps), shown in the world by the world's node
"waypoint" ([0x608ba4]), which its first begin (vt+0x2c 0x479a50, from the
nets' 0x4d52b0 at the mission start) finds, puts in the world when it has
no parent and places on point 0 (the default behaviour, with no place:
nothing then). Selecting (0x479b90) places the marker on the point and
shows it, an index out of range hides it; next and prev (vt+0x60 /
+0x64 0x479b10 / 0x479b30) wrap round all the elements, the default
behaviour included (the marker stays put on it); nearest (vt+0x68
0x479b50) takes the AI's nearest element with a place (0x4d51d0) when it
is a point (vt+0x78), else the selected; find (0x479be0) is by the
point's auxname, case not counted. The first waynet made counts the HUD's
two waypoint icons in ([0x608ba0], 0x4798f0), the last deleted out
(0x479850, which forgets the marker).

**The points** (0x47a320, 0x38 bytes, vtable 0x5c3828, a Behaviour):
the record's name (+0x28) and auxname (+0x2c, what it answers by, vt+0x24),
its place (+0x1c, vt+0x18), its first link (+0x30), flags +0x0c..+0x10
(a dock point: +0x0c and +0x0d set), and for a non-dock point a nav line
("nav<auxname>", 0x5e5adc) said the first time it is reached (0x47a500).

**The mcbwaynet** ([0x60b544], 0x4d5460, vtable 0x5c38e0; id 10000 up, kept
by znet's clear and deleted at the mission's end by 0x4d54f0, after
0x47a720 clears it): as the nets compile (0x4bde20), each arrangenodes
node not already there by its auxname becomes a dock point at its place
(on the ground), with the record's hidden byte. c1m1 has one: net_02's
"MFB Baker". Its next and prev (0x47a5c0 / 0x47a620) count how often the
picked point comes round (+0x30); its nearest (0x47a680) picks.

**The MFB command** (MCB_COMMAND, key command 0x73: 0x47a910 -> 0x47a7a0
on the mcbwaynet with the mech; nothing while no field base is up):
1. With the dock idle (+0xb8): the mission's waynet ([0x609980], the
   missions' +0xb0) kept (+0x40, its point +0x3c) and the mcbwaynet made
   the mission's (0x491d00), so N / Shift+N / Ctrl+N cycle the dock
   points; the point nearest the mech picked (+0x34), "mcb_pos_wait".
   The dock busy: "mcb_occupied".
2. The request (0x45a500) with the selected point; the point onto the
   kept waynet's end (+0x38) when there is one (the nav waynet), which is the
   mission's again.
3. The front state's abort (0x45a540).
The dock's marker clear (0x47a720 -> 0x47a6b0) undoes it: the mission's
waynet back (or the kept one's point reselected and the point taken off
its end), the marker hidden, the command back to its first press. The
dock calls it when a run is aborted (each state's abort) and when the
repair's twelve seconds are up.

0x47a730 (and 0x47a780 through the mcbwaynet, from the AI's 0x4ea2b7):
a mech (its vt+0x50) made the dock's requester while a base is up, when
the dock is neither busy nor holding the bases.

The nav waynet, the HUD's waypoint icons and arrows: next section (step
4b3b). (Corrected: 4b3 took the nav waynet for multiplayer only; every
campaign and training mission has one.)

Checked: `test_veh_waynet.py` (600 runs of the selection, the mcbwaynet's
slots, clear and the command; 300 points; 25 branches; 25 of 26 breaks,
the 26th, nearest's fallback, not reachable: element 0 is always there)
and c1m1's headless repair test (the MFB command picks "MFB Baker" and asks;
the dock does the rest).

## The nav waynet (0x4797c0, 0x4be690, 0x479c50, 0x479dd0..0x479ef0; step 4b3b)

Every mission has its nav points: `waynet_N.zrd` (version 107) in its
reader archive, N the mission number (`waynet_iaN` for Instant Action,
`waynet_mpN` multiplayer; game types 0, 1 and 4 take the plain name). c1m1's
waynet_1: Start (MSG_WAYNET_START), Able (MSG_WPT_ABLE), Baker
(MSG_WPT_BAKER). Ported in `mech3/mech_waynet` and the builder
(`zainetedit`), wired in the program (app_mission, app_vehicle, app_hud).

- **The make** (0x4797c0, from the mission load 0x490720 after its readers
  0x490630, into the missions' +0xb0, the mission's waynet [0x609980], and
  +0xb4, which the cleanup deletes): a short-lived builder with the build
  flag (0x4bcba0, an id of 10000 up, 0x58b1c0) reads the file; version 107
  builds a waynet (0x4be690); if it is one (vt+0x3c, 0x4d2a70) it is
  counted in for the icons (0x4798f0) and its marker placed (0x479ac0).
  The port's mission reset had left the readers out; it opens them now, as
  the exe.
- **The build** (0x4be690): a waynet (0x479640) of the file's id and name;
  its slots counted as a compiled net's but with no steps per link (the
  counter's vtable 0x5ca48c answers 0 where 0x4beb90 would count the
  properties): one per positioned node, an edge node starting at it its
  links but the first; all null steps, then a point for each node in order
  from slot 0 (0x47a320: name, auxname as its label, place on the ground
  while a world is loaded, its links as slots, MFB 0, then the record's
  visible, fixed, autonext and hidden bytes as +0x0f, +0x0d, +0x0e and
  +0x10; its nav line "nav<label>" looked up). The waynet's default
  behaviour is gone: element 0 is the first point. FIX (bug): a link to a
  node that is not there reads the slot map at -1 or past its end, an edge
  node's links past the links buffer, a point past the counted slots past
  the elements; here -1, a buffer for any node, the point dropped. No
  shipped file has any of them; version 105 (0x4bed90) is in no shipped
  file and stays unported.
- **The frame** (vt+0x6c 0x479c50, each mission frame on the mission's
  waynet with the player's vehicle; the mcbwaynet's is 0x4cd490, nothing):
  the watched points (+0x28, the objectives' "nav" points, 0x45cf9d ->
  0x47a270) within 25 m (squared 625, all three axes) are reached
  (0x47a500: the nav line once); then the selected one: reached within 25 m
  and, with autonext (+0x0e, the record's default 1), the next one selected
  (vt+0x60). The answer, the selected point's reached, is not used. FIX
  (bug), behind `bugs`: the exe's loop compares the point before with the
  selected one, so once past the selected point it skips the rest of the
  list (their lines unsaid until selected); a selected element with no
  place is read through null (an element 0 default behaviour, a null step).
- **The radar's marker** (0x43dd00's, docs/hud.md: once taken for a network
  player's): the mission waynet's selected point, its icon by +0x0c
  (hud_mfbwaypoint_icon, else hud_waypoint_icon), placed by its distance,
  and TEXT2 "OP: <name> [<km>km]" (message 0x228, "%.2f" past 1000 m) or
  "OP: <name> [<m>m]" (0x229, rounded up), the name the record's localized
  one. FIX (bug): the exe reads the selected element's +0x0c and place
  unchecked; the default behaviour, element 0 of the mcbwaynet that the
  MFB picking's N / Shift+N wraps onto, has none (its vt+0x18 0x4d72a0 is
  0), so the radar would read through null there (not seen in play yet).
  Here only a point is drawn. The marker's name hook (HudHooks::marker_name)
  answers a string now (the exe's 32-bit value was the pointer).
- **The edge arrow** (0x479ef0 in the HUD's data update, 0x479ed0 in its
  frame, both while [0x609980]): the arrow, a zTwoD image at 0x608ae0 made
  at start-up (0x59b970(0)), hidden (0x59ff50); with the HUD active the
  selected point projected (0x53cf10): off the screen to the left, right,
  above or below (the first of bits 1, 2, 4, 8), the arrow of that side
  (the nav set, or the MFB set for a dock point) placed at the edge,
  along it at the point's projected y (or x), held on the screen, shown
  for good; on the screen, none (the world's "waypoint" marker shows it).
  The right and bottom edges are measured by the image shown before this
  one (the exe reads the size first). Drawn unless hidden.
- **The icons** (0x4798f0 on the first waynet counted, [0x608ba0]): zImage's
  directory "..\\data\\common\\images\\hud", the two radar icons (zTwoD
  images) and the eight arrow images (hud_navarrow_left / right / up /
  down, hud_mfbarrow_...). 0x479a30 counts a waynet out; the last frees
  them (0x479850, also at the mission's end): a count left over is reported
  ("memory leak of %d waynet%s detected: should be unloaded at end of
  mission") and zeroed, the marker forgotten, the icons deleted. FIX (bug),
  behind `bugs`: its loop releases two images and steps four on, so the up
  and down arrows are never released (a zImage reference more each
  mission); here all eight.

Checked: `test_nav_waynet.py` (all 47 shipped version-107 files and 300
made-up ones, 1,117 points; the exe's own builder), `test_nav_hud.py` (600
runs of the frame, the watched list, the arrow, its draw, the icons and the
make; 20 branches; the port's null-place guard on its own), 36 of 37 breaks
over both (the edge count's k - 1 > 0 against k > 0 is not observable);
c1m1 headless (`mission_c1m1_nav_waynet_headless`: Start, Able and Baker;
the player starts on Start, so autonext has moved on to Able; set on Able,
it is reached and Baker selected).

## The turrets (turret.cpp, 0x4a4000..0x4a7af0; step 4c1)

`mech3/turret` (`mech3::tur`). Turrets are not vehicles: no controls, no
movement, an AiTarget with zEntity3d's few fields (like the targets.zrd
objects), 0x284 bytes, vtable 0x5c8770 (55 slots, the shared ones
zEntity3d's and AiTarget's).

**Where they come from.** The mission load (when the app's +0x1b2 is set:
single player and training, mission-load.md) calls 0x4a76b0 on
`aiTurrets.zrd` (zbd/reader.zbd). It first makes the three HUD markers
(`hud_turret_ally_sm`, `_enemy_sm`, `_other_sm`, [0x609a38..0x609a40],
vt+0x3c 0x4a5fc0 hands them out), reads the file ("Failed to read %s"
without it; the tree is kept at [0x609a44] until 0x4a7970 frees it with
the markers), takes `GENERIC_DESTROY_ANIM`, then walks `TURRET`'s pairs:
each entry's name is a **prefix** on zClass's list 6 (0x4f8b80), and every
node starting with it whose `healthy` node (found in its tree,
0x4f9d60) is active gets a turret: the constructor, then vt+0x38 (zEntity3d
0x5a80f0: the world's +4 and the caller's EDX) and its AUX_ANIMs reset
(0x4a7af0). Nothing keeps the turret but zEntity's list (the port keeps
`turret_list()` as well). Two entries that are prefixes of the same node
would make two turrets on it; no shipped pair does. The shipped worlds'
turret nodes: c1 tur_srm01..08, tur_stk03/04, tur_boss01/02, triturret01/02;
c2 tur_srm, tur_lbx, tur_medlas, tur_smalas, mineturret01, choppr01..03; c3
tur_srm, tur_las, tur_ams, tur_ult01..07, tur_wallppc01/02, choppr01..06;
c4 tur_srm, tur_lrm01, tur_ppc01/02, tur_ams, tur_ult01..07, tur_carve01,
tur_train, choppr01..03; c4b tur_gauss01..03, tur_ult01..04, choppr01..06;
t1 none. The helicopters (`choppr`) are turrets too.

**The constructor** 0x4a5c60(node, generic destroy anim, definition):
AiTarget 0x4d56e0(0, 0), the limits at +0x210 a full turn (0x4a4af0), the
four animation lists, the team the enemies' (0x4d5af0(1) = 2), active 0,
IS_IDLE 0, its id the count at [0x609a48] (then counted up; the pick takes
turns by it, 4c2), the defaults (ammo 50, detection range 200, the facing
and fire direction (0, 0, -1), the fire rate and next shot 1, the yaw
-2 pi..2 pi), the limits again (0x4a4ac0: pitch -1.570796..1.570796, a full
turn); on its node (0x4a6230: 0x4d6c90, zEntity's list 0x5a7f50), the setup.

**The setup** 0x4a6270, the definition's keys in this order:

| key | what it does |
|---|---|
| (the node's name) | an animation of that name (0x50b450) replaces the generic destroy animation |
| `PARTS` | only with a `healthy` node in its tree (0x4eb0d0: the node itself, then its children depth-first, first to last; then active 1): 4 names turret, gun, two firepoints; 3 turret, gun, firepoint; 2 gun, firepoint (each 0x4f9d60 in the node's tree: the same search, the children last to first); +0x154 the firepoints |
| `DEACTIVATE` | a node by name (0x4f7fe0, list 6), then each next name searched in the last one's tree (0x4eb0d0) |
| `EFFECT` | a node under it and a time (the second value's bits, not converted); its model's texture scrolls (0x526ae0(1, 10, 0)) |
| `HEALTH` | +0x244 = 1 / HEALTH (no HEALTH: 1); +0x240 the health fraction, 1 |
| `DESTROY_ANIM` | replaces the destroy animation, even with none found |
| `ONFIRE_ANIM`, `WAKEUP_ANIM`, `SLEEP_ANIM`, `AUX_ANIM` | the animations found, in lists (+0x25c, +0x250, +0x274, +0x268); AUX_ANIM's are claimed (0x4d4f20, ai.md section 18) |
| `IS_IDLE` | +0x27c |
| `SOUNDS` / `FIRE` | the fire sound (0x56f6f0) |
| `LIMITS` / `YAW` | degrees (default -360, 360) into +0x204 / +0x208 radians and the limits rebuilt with them |
| `BBOX` | the healthy node's box collision flag (0x4eb750; 1 without it) |
| `WEAPON` | `NAME` (one name: zWeapon's definition 0x581ed0), `AMMO`, `DETECTION_RANGE` (else the weapon's +0x1c), `FIRE_LIMITS` (two values' bits: a burst's length and the pause after it), `FIRE_RATE` |
| `AMS_RANGE` | an AMS (0x4b3ab0: range, period 0, 100 rounds), +0x138 set |
| `TURRET_HEIGHT` | the AiTarget's height (+0x68), else the box's larger side x 1.15 |

Then the node's world position (0x4ed650) and rotation (0x4fbe90,
normalised into +0x124), the aim point +0xec: the position raised by the
turret's and gun's matrix heights and the firepoints' mean height (0 / 0
without a firepoint: NaN, on turrets that are inactive anyway); the effect
hidden, the box flag; a beam weapon (definition flag 2) gets its beam
(0x584a70) and FIRE_LIMITS' first value as its rate; with a destroy
animation it is reset onto the node, the node's `destroyed` child hidden and
the weapons callback 0x4a7940 installed (0x5833b0; "Failed to install
weapons callback for (%s)"); without one only "No destroy animation for
(%s)": **weapons do not hurt such a turret** (no callback). Last, the box
(0x4ebd50; height 8, width 4 without one) places the centroid and target
nodes at half and 0.65 of its height (0x4fc0b0, 0x5a8500), the range goes
to the AiTarget (0x4d6d00), zEntity3d refreshes (0x5a85c0).

**The turn limits** (+0x210, built in place by 0x4a4b30; +0x20c points at
it): the pitch's sines and cosines (0x4a4000), and by the yaw's range
(high minus low, in the x87 against a float 0.001, the double pi and 2 pi):
0.001 or less (or NaN) 0x4a42f0 (one heading: cos, 0, -sin of the low yaw;
vtable 0x5c8310); under pi 0x4a4580, under 2 pi 0x4a4800 (both over
0x4a4270: the edges (-cos lo, 0, sin lo) and (cos hi, 0, -sin hi); 0x5c8320,
0x5c8330); else a full turn 0x4a4a70 (0x5c8340, the vehicles'
TurretLimits). A new kind is built over the old one in place (the old one's
destructor runs, nothing is freed). Only triturret has a yaw (-30..30:
narrow). Their within / clamp are the aim's (4c2).

**Damage and death.** The callback 0x4a7940 hands a hit to 0x4a7530: the
health fraction loses damage / HEALTH, the attacker's object (0x581e40,
0x40eee0) is kept at +0xd4; at 0 or below the turret is shut down destroyed
(0x4a7620) and the hit reported as a kill (0x583710(1, 0)), else its health
(0x583730). **0x4a7620** (also message 0x34): the attacker credited (its
vt+0x80 with the turret), the destroy animation run when destroyed, active
0, the effect put out only when it is not showing (the flag test is the
wrong way round: The turrets' frame), the node's weapon handlers
off (0x583400), a beam stopped, +0xd4 cleared. **vt+0x70 0x4a7600**, the
outright kill (the valves' `destroy`, ai.md): health 0, the attacker set to
the player's vehicle ([0x60861c]+0x14), then 0x4a7620: the player gets the
kill. The messages (vt+0x74 0x4a74f0): 0x32 clears IS_IDLE, 0x33 sets it,
0x34 shuts it down, the rest go to AiTarget's 0x4d7270 (empty for a
turret). They come from the turret's own AUX_ANIMs (ai.md section 18):
c1's boss turrets are raised by `raise_bturret1` / `2` (0x32), c4b's gauss
turrets raised and lowered (0x32, 0x33), c3's wall turrets idled or shut
down as their walls fall (0x33, 0x34).

**The other slots:** the target view's tint (vt+0x68 0x4a75b0: while active,
red (1, 0, 0) by (1 - health) x 0.6, 0x4fb8c0, then 0x4fb840); vt+0x98
0x46b780 1 while active, else 0; vt+0xa0 0x4a7a60 IS_IDLE; vt+0xcc
0x4a7a70 (the sensors' "alive": active, not idle and its zones meeting the
player's, 0x53cc90); vt+0xd0 0x4a7ab0 (active and the player's contact
inside the detection range). The destructor 0x4a5ff0 (0x4a5fd0): the AMS
freed, the beam (0x584b40), off its node (its handlers, 0x4d6c90, 0x5a7f70),
the lists, AiTarget's 0x4d58a0.

FIX (bug), the port's two: a WEAPON whose NAME finds no weapon and has no
DETECTION_RANGE reads the null definition's range in the exe (the range
stays 200); a key that is not a list or an item past a list's end reads as
none. No shipped turret has either.

`test_turret.py`: the shipped aiTurrets.zrd and 300 made-up files through
the original's loader, constructor and setup (its zReader in the emulator
on the same tree the port reads; prefixes matching 0 to 3 nodes, healthy
or not, PARTS of one to five names, each limit kind, a beam weapon, AMS,
odd values), then every slot in random order, the destructors and the end;
the traces, each turret's 0x284 bytes, its lists and AMS after every step;
40 branches. 40 of 43 breaks (the three missed not observable: 2 pi's
`>=` against `>`, as no float equals the double; a TURRET list of odd
length, which the exe reads past; a null marker, which new never gives).

Left: the program: the mission load's call, their frames, the HUD markers,
the kill count (+0x3fc) (4c3).

## The turrets' frame (0x4a6be0, 0x4a70d0, 0x4a71c0; step 4c2)

**The frame** (vt+0x34 0x4a6be0, its argument unused), in order:

1. Health at 0 or below (or NaN): shut down destroyed (0x4a7620), every
   frame it runs. IS_IDLE: nothing at all (an idle turret neither looks nor
   turns).
2. The node's world position into +0xe0 (0x4ed530): **the turret follows its
   node**, so the helicopters, which their missions' animations fly
   (`chopper_path1`, `callm3_choppers`; woken by `turn_on_choptur1..3`),
   aim and shoot from where they are. The setup's aim point +0xec (and its
   NaN height on a turret without firepoints) is never read again.
3. Its `healthy` node, its own node or its DEACTIVATE node hidden (flag 4
   clear): shut down, not destroyed (0x4a7620(0)), every frame.
4. A weapon with definition flag 0x4000 (the mine turret's `mine_turr`): on
   a target, the destroy animation and the weapon goes off where the turret
   stands (0x5820c0 at +0xe0), nothing else; the frame does nothing more to
   it (whether the blast destroys it is the weapon's, through its hit).
5. The effect (EFFECT) shown and the next shot not due: its model animated
   (0x541ad0, its texture scrolled at 10 widths a second along u; ported).
6. The pick (0x4a70d0); none: not aimed, the beam stopped.
7. The aim (0x4a71c0) at the picked place; refused or not aimed, or the
   next shot not due (+0x188 above the clock): nothing more.
8. An effect not showing is lit first (0x4eb170(1)) and the shot put off by
   EFFECT's time: a charge. Otherwise the shot: a burst (FIRE_LIMITS)
   begins its clock if none runs, the effect put out, the firepoint the next
   of two (else the first), the shot's start the firepoint's translation
   through the gun's world matrix (0x4ed280(gun, 3), 0x5227a0) and its
   direction at the target, normalised; a weapon with gravity (definition
   +0x4c) takes its arc's slope (0x581ff0, -1 none) as the direction's pitch
   (0x4abea0: the heading kept, its flat part scaled by the same bit-trick
   square root; no heading: (f, slope, f), or (0, 0, 1) at slope 0); an AMS turret
   fires its AMS (0x4b3bb0(1)); the fire sound at +0xe0 (0x56e7e0, 1.0); a
   beam weapon starts its beam once (0x581f60 shooter, 0x581230), any other
   is fired (0x581fb0 from the firepoint when the definition's +0x84 bit 1,
   0x581f60 with the lock at +0x1a4: the player's vehicle, the picked
   place, no velocity; 0x4b4a40(def, node, 0, from, dir, +0x178, 0, 0, the
   shot hostile to the player: the player's vehicle's vt+0x24, vt+0x28,
   +0xc0 against +0xc8), 0x581f60(0, 0)) and
   ONFIRE_ANIMs run; then the next shot: a burst over (its clock
   passed) waits FIRE_LIMITS' second value and drops the aim, else
   FIRE_RATE.
9. A beam on without the aim: stopped (0x581400).

**The pick** 0x4a70d0 (FIX (B), Owen 2026-10-24, behind `per_second`: the
turn by game time, below): a turret without an AMS looks only on the frames
whose number (zClass's [0x72f338]) over the turrets made ([0x609a48])
leaves its id: one turret a frame, each in turn. Then the turn count
[0x609a4c] moves on, and unless it wrapped to 0 the nearest contact that is
an enemy (its vehicle's AiTarget team against +0xc8) in sight (0x4d5980)
becomes the target (AiTarget's 0x4d72b0, vt+0xc0); the target's entity's
place (0x5a8630) is where it aims. On every other frame the pick finds
nothing, so **a turret aims, turns and fires only on its own frame**: with
14 turrets made each acts once every 14 frames (at 30 fps about twice a
second), and how often depends on the frame rate. The count of turrets
made is never reset (only the constructor counts it up), so the next
mission's turrets act once every so many frames as turrets have been made
since the game started (FIX (bug), behind `bugs`: counted from the
mission's own; fixes.md). An AMS turret instead takes the nearest shot on
the radar every frame (0x4b3ae0 at +0xe0, 0x4b3ba0). Either way a pick then
makes the player's contact the target (0x4a7b40 / 0x4a7ba0; none when the
player is not among the contacts), while the shot goes at what was picked:
so once a turret has picked, its target is the player, and on the turns
the nearest-enemy search is skipped or finds no one in sight it aims at the
player's place, line of sight or not (the aim's range and cone still
apply).

**The shot's flag and the AMS side.** 0x4b4a40's last argument is the
shot hostile to the player: the player's team among the shooter's enemies
(+0xc8), for a mech's shots (0x4b3550) and a turret's (0x4a6fa9) alike; it
colours the radar's pixel red and picks the AMS that may take it. An AMS
asks with side 0 for the shots not hostile to the player (a mech that is
the player's enemy, 0x41927e), side 1 for the hostile ones; a turret always
asks side 0, right for them all (team 2). So the player's AMS takes hostile
turrets' missiles, and tur_ams shoots down the player's.

**The aim** 0x4a71c0: the gun's translation (its matrix +0x24) through the
node's world matrix, the direction to the target (its length the distance)
taken into the node's frame (0x522640); the facing (+0x124) eased toward it,
the old one kept by e^-(3 dt) (zMath's table 0x523f70; 8 dt for an AMS) and
normalised; aimed (+0x14c) when the distance is under the detection range
(+0x148) and the direction within acos 0.89 (27 degrees) of the facing.
Then the facing's flat length by a bit trick: the float's bits halved, plus
0x1fc00000 (half the exponent's bias), **0 to 6.1 % over the square root**
(exact at the powers of 4, worst near 2 x 4^n); the heading's cosine and
sine are the facing's -z and -x over it. The limits (vt+0x88, the turn
limits' vt+0x08) may refuse the facing: the old one back, not aimed, false.
Else a turret node gets the heading (its matrix's +0x00, +0x08, +0x18,
+0x20: cos, -sin, sin, cos) and the gun the pitch (+0x10, +0x14, +0x1c,
+0x20: the flat length, y, -y, the flat length); a gun alone gets both
(+0x00..+0x20); each set on its node (0x4fc300). The flat length being too
long makes those rows up to 6 % short and long: the turret's yaw part is
drawn up to 6 % narrower and the gun's pitch part off square by as much
(the heading and pitch angles themselves are right, as cosine and sine are
over the same length).

**The turn limits' within** (vt+0x08): the pitch (the direction's y between
the two sines; NaN passes), then by kind: Fixed 0x4a4330, its heading's
across part squared under 1e-5 of the flat length squared (along the one
heading, forward or back); Narrow 0x4a45b0, on the inner side of both edges;
Wide 0x4a4830, of either; Full 0x4a4180, the pitch alone. Unordered edge
tests fail. Nothing in the turret asks their clamp (vt+0x04).

**The lead's three questions** (2026-10-24):

- The effect check in 0x4a7620. The frame shows the effect as a charge
  before each shot and puts it out at the shot; the deactivate puts it out
  only when it is not showing, which does nothing, so an effect charging
  as the turret dies would stay lit. A bug, the intent being clear (FIX
  (bug), behind `bugs`: put out when showing). It never shows in the
  shipped game: tur_ppc is the only turret with an EFFECT (`lightning`),
  and no world has a node of that name, so its effect is never found.
- The aim point's NaN height (a turret without firepoints): +0xec is
  written by the setup only; nothing reads it.
- The helicopters: their nodes are moved by the missions' animations; the
  frame reads the node's world position each frame and the aim the node's
  world matrix, so they aim from where they are.

FIX (bug), the port's own: a turret without a `healthy` node (no PARTS)
reads its flags through null in the frame (shut down instead); a turret
without a gun reads the gun's matrix through null in the aim (it cannot
aim). No shipped turret has either.

`test_turret_frame.py`: 120 runs (40 on the shipped file, 80 made-up)
through the original's loader, contacts for each turret (the player among
them or not, of the enemy team or not, in sight or not), then 40 to 120
frames each (the clock, dt 1/60 to 1/15 s and the frame count moving, the
count of turrets raised as if from earlier missions, nodes hidden and shown,
hits, idle messages; some turrets skipping a frame); the traces, each
turret's 0x284 bytes, its turret's and gun's matrices and the turn after
every frame; 44 branches; 47 of 47 breaks over the frame, the pick, the
aim and the limits. The limits' within on every kind directly in
`test_turret.py` (directions on and either side of the edges).

## The turrets in the program (app_turret.cpp; step 4c3)

The mission load (mission.cpp, the two cases at 0x490c8e / 0x490d25, when
the app's +0x1b2 [0x608e3a] is set) calls `program_turrets_load`: the
loader, its hooks on the ports (zClass's lists, nodes and matrices, zEffect,
zSound, zWeapon's definitions, beams, locks and shots, the hit callback, the
matrix stack). The turret goes on zEntity's list (0x5a7f50) and so into
everything that walks it:

- **The sensors** (app_sensors) answer its slots as vtable 0x5c8770 does:
  vt+0x20..+0x2c, +0x48, +0x4c itself, +0xa0 IS_IDLE, +0xcc awake, +0xd0 in
  range, the rest 0; its AiTarget is the turret, its nodes its node and
  centroid. So it gets contacts and the player senses it.
- **Its frame** runs from the entity walk while the sensors keep its +0x09
  set (vt+0xcc: active, not idle, the zones meeting the player's), so an
  idle or dead turret's frame does not run at all.
- **The AI** (the brain hooks) sees it like a targets.zrd object: its node,
  centroid and target node, active, still; health 1 while active (vt+0x98).
- **Its place** (zEntity3d's 0x5a80f0, vt+0x38, ported here): the zone
  record of the ground under its node (from 10 m up, its flag 8 off, water
  skipped; "No ground intersections for node ..." without one), its zone id
  the one shared with the camera's zones, else its first, onto the node.
  On c1, tur_boss01 / 02 have no ground under them.
- **The kill credit**: a mech killing it gets a "building" (on_kill's
  vt+0x48: +0x3fc, 200 points); a turret killing anything credits nothing
  (vt+0x80 0x4a5bc0).
- **The HUD**: the three markers; vt+0x3c hands them to the radar, which
  here still draws mechs' contacts only (hud.md), so turrets have no blip
  yet.
- **The end**: the entity list's walk (0x49dc40) deletes each turret
  (vt+0x00), then 0x4a7970 frees the markers and the file.
- **Shots** (playtest #27): a projectile or beam hit anywhere in a
  turret's tree reaches its damage handler (0x4a7940 -> Turret::hit
  0x4a7530, set on its node and handed down), so c1m1's four SRM
  emplacements each die to two PPC hits on their `healthy` part, their
  `healthy` part off and `destroyed` on; with the reticle on tur_srm04 the
  Mad Cat's lasers kill it in three volleys
  (`mission_c1m1_shots_destroy_the_srm_turrets_headless`,
  `mission_c1m1_player_destroys_an_srm_turret_headless`). What stops shots
  is the level: the node's origin is at its base, so a shot aimed there
  hits the ground in front, and tur_srm02 sits in the rocket trench
  rktun01, whose walls (g15988) take every shot along world x; it dies to
  shots along z, down the trench. A record on the turret's root node (at its
  x and z) after a wall hit took 0.09 where a hit on a part takes a PPC's
  0.73: likely the splash's sphere query, not traced.

A port gap closed with it: the game clock [0x72c530] (zTimer's scaled
total, the turrets' and the AMS's) was never copied into the weapons' world
in the program, so the AMS's period never passed and a turret never took a
turn; `sync_frame_step` copies it each frame with the step.

**The turn by game time** (FIX (B), Owen 2026-10-24, behind `per_second`):
the pick's turn comes on game time in 1/30 s steps instead of frames: a
turret's turn is the step whose number over the turrets made leaves its id,
at most one a frame (`turn_due`), so with N turrets each looks every N/30 s
at any frame rate; [0x609a4c] moves on per turn as before.
`fix_turret_turns_by_time` (mw3tests): 14 turrets over two seconds take 60
turns at 30 fps and 60 fps alike, 4 or 5 each; the exe's frame count takes
twice as many at 60.

`mission_c1m1_turrets_headless`: c1m1 makes its 14 turrets, all on zEntity's
list; with the player walked up to tur_srm08 it is sensed, picks, targets
the player and fires; killed outright (vt+0x70) it shuts down and the
player's kills of buildings go 0 to 1 (200 points).

## The stealth object and BASIC mode (2026-10-23; ported 2026-11-01, editor S1)

The mission load makes it in every mission ("Creating Stealth", 0x4a4de0,
[0x609a2c], vehicle.zrd's `stealth`: no model, sounds or weapons, health
10000, `mode` basic, rates 40 / 65, turns 2.5 / 3.5); the unload deletes
it (0x490480, its destructor 0x4a4fc0). It is appended to zEntity's list
(0x5a8820 is a push_back; the unload takes it out and puts it back, so it
ends last); its +0x09 is 0, which the entity walk (0x5a8850) checks before
each entity's frame, so the walk never runs it. Its frame runs from the
player's tick only while the view is on it.

Ported (mech3/veh_stealth, editor.md S1): the object, its overlay (a zTwoD
layer with the image "ret_off" and a readout of its movement mode, drawn
while the view is on it) and its own controls (0x4a51b0, below). In the
program (app_vehicle.cpp, app_mission.cpp, app_mech.cpp), three places
assumed nothing outlives a mission's vehicles and were made to keep it as
the original does:
- it is made by the world's load, before the mission's vehicles, so the
  mission start's forget (`program_vehicles_forget`) keeps it among the
  program's vehicles (the sensors ask it the vehicle slots, vt+0x2c and
  others, as the original's do);
- the world node vehicles are added to (`world_hooks().world`) was set by
  the world parameters' read, after the world's load: the stealth's node
  went into no world (or, after a cleanup without the unload, a freed one).
  It is set now where the world object is made (0x0047f2b0, [0x00608c84]+4),
  as the original's is;
- the debug start emptied zEntity's list before each mission; the cleanup
  (0x00491780) keeps the stealth out of what it frees and puts it back, so
  the start keeps it too (a restart on the same world keeps the world and
  its stealth).
The entity walk asks its +0x09 before anything else, and the unload's
delete knows it. Its load
costs nothing measurable: c1m1 "Ready To Launch" 391 to 414 ms before, 392
to 394 ms after (three runs each, the final build). The vehicle.zrd `stealth` entry has no
model, so its setup reports, as the original does, "Cannot find support
points for stealth!", "Cannot find collision points for stealth!" and "No
destroy animation for (stealth) or (stealth)".

The view goes onto it in two places: the debug key setup 0x40e430 (its own
key map, keys 0x39 and 0x439; the view attached, vt+0x108), the handler of
command 0x9a in 0x40dba0's switch, which nothing in the release registers
(0x40db60 registers 0x61, 0x6c, 0x70 to it; also unregistered, 0x84 calls
the mech swap); and the mech swap 0x4783c0 (the mech lab's resume(4) in
0x481170 with another mech, the multiplayer respawn 0x46dde0), which parks
the view on it while the mech is rebuilt and returns it in the same call.

**Its controls** (0x4a51b0 over the base 0x4ac750, 0xa1c bytes; vtables
0x5c86b0, IControls 0x5c85f8): the base's but for vt+0x1c (0x4d72a0: never
destroyed), vt+0x20 (0x4a5270: 1) and the frame (IControls vt+0x0c,
0x4a5ae0 -> 0x4a5280): the player's own movement commands (0x51d300:
THROTTLE_UP/DN 0x18/0x19, TURN_LEFT/RIGHT 2/3, PITCH_UP/DN 4/5,
CENTER_TORSO 6) and the mouse (0x51ba60), each clamped to -1..1 and
smoothed (0x4abcf0(x, 0.5, 1)) into the throttle and turn (IControls +0x20,
+0x28) and two more axes (+0x9c0, +0x9bc). So the stealth is a camera flown
with the original's own keys while the view is on it (editor.md S3).
Ported 2026-11-01 (`stealth_controls_frame`). Read from the exe: the
controls' base is the IControls less 0xa08, so the frame's fields are
+0x10 (a ramp switch: set, the throttle keys add or take the frame time,
[0x72c52c], and a released key leaves the throttle; clear, a key gives 1
or -1 and none 0), +0x40 / +0x48 (an input nothing here sets, clamped and
smoothed), +0x44 / +0x4c (the pitch keys' 1 / -1 / 0; CENTER_TORSO minus
the pitch's sign), +0xa4 / +0xa8 (the mouse's fx, fy; a device error,
0x8007001e, zeroes them and ends the frame). TURN_RIGHT is read only when
TURN_LEFT is not held. The curve 0x4abcf0(x, 0.5, 1): 2a^2 to 0.5, then
1 - 2(a - 1)^2, 1 past 1, with x's sign. The throttle's curve goes to
SetThrottle, the turn's to SetTurn. An
earlier reading here (and in editor.md) took the basic vehicle's own
controls for its: those are below.

**BASIC** (0x4a1d90, its ground 0x4a1f00), the stealth object's movement:
by camera mode, a car's steering (0x4a20d0: the turn into the heading
+0x330, wrapped at 2 pi) or, in mode 3 (straight down), north-up panning
(0x4a2000: heading 0, the input on the sideways speed, stopped dead before
reversing); the speed step 0x4a2b10; the move on the horizontal plane;
then the four platform points' ground (0x4a2190), the highest taken plus
an offset (+0x88 of the motion block, likely `mode_alt`), pitch and roll 0:
a level sled glued above the ground (editor.md S2). Ported 2026-11-01
(veh_move.cpp: `vehicle_basic_step`, `_steer`, `_pan`, `_ground`,
`vehicle_platform_ground`; test_veh_basicmode.py on the suspension's and
the frame's vehicles, each entry and the step). Details from the exe:
the steering's heading asked for is atan2(-heading.z, -heading.x) (z
first, the reverse of 0x4a28c0's); the panning zeroes the sideways speed
when the turn input has its sign (it would carry it further), then takes
turn x rates[0] x dt off it; the supports' ground (0x4a2190, beside the
suspension's 0x4a23f0) raises each support by the vertical speed x dt
before the one query from 500 up, picks within max(1 - that rise, 4) with
water as ground, and refuses no empty record (its pick gives 0); the
ground puts the vehicle at the highest support plus mode_alt and sets the
turn rate's top to turns[1]. No shipped mission vehicle moves in BASIC; the
stealth object does when the view is on it, through its own frame 0x4a5050
(the controls, the overlay's reticle 0x4a5a00, the move 0x4a1240, the
matrix; no path test as a basic vehicle's 0x4a1040 has), ported 2026-11-01
(editor.md S4).

## The basic vehicle's own controls (open; deferred 2026-11-01)

A vehicle made without AI controls makes its own (0x4ac860(0xa70), then
0x4ae270; vtables 0x5c8c50, IControls 0x5c8b98): settings from the
vehicle's zrd (0x4ae3c0, 0x4ae6b0) and a frame (0x4ae850, through
0x4aea10) that runs a path follower, 0x401050's switch on +0xa14: mode 0
follows the waypoints at +0xa0c (0x401100, 0x401940; 0x401400, 0x401500),
modes 1..3 (0x401680, 0x401c30, 0x401e30) other approaches, mode 4 stops.
Not ported: no shipped mission makes such a vehicle. Checked 2026-11-01 by
a log line (`mw3: vehicle NAME (TYPE) has no AI controls`, at
vehicle_create and field_base_create, kept so a community mission that
does would show) over the campaign by its objectives (all 20 missions),
t1m1..t1m4, every Instant Action map (c1..c4 ia1..ia3, t1ia1) and every
multiplayer map (c1mp1..2, c2mp1..2, c3mp1..4, c4mp1..2, t1mp1, hosted
alone): 48 missions, none. The line's break check (its condition
inverted) names c1m1's four vehicles.

## Why 2.1 waits for the AI (2026-10-21)

Even standing still, the basic vehicle lives through its controls (+0xe4):
when none is given, its constructor makes its own (0x4ac860 then 0x4ae270
over 0x4ac750, 0xa70 bytes: the basic vehicle controls, the same family as
`ai::AiVehicleControls`), else it hands itself to the given one (vt+0x08).
Its placement (vt+0xdc, 0x4a3630) writes the controls' +0x330 and +0x70 and
the +0x764 block's per-point table (+0x84 count, +0x14c), and calls their
IControls vt+0x14; 0x49f660 (after the setup) calls their vt+0x18 and fills
their +0x160 points from the +0x764 block's (+0x194); the setup (vt+0x158)
ends in their vt+0x0c; the frame and the damage go through vt+0x1c and the
IControls base. So the vehicle object cannot be ported apart from the AI's
vehicle controls (mac 4's `zai_brain` / `AiVehicleControls`, data side only so
far). Also open: the vehicle as a second kind of entity, since the program's
sensors, kill credit and teardown take every zEntity entry to be a Mech
(app_sensors `mech()`, `mech_free`, killer_notify): a kind tag or a common
base is needed first.

## A split

1. The basic vehicle as an object: construction, placement (vt+0xdc), the
   definition's weapons, its model node and touch handler, damage and
   death, the kill credit's salvage 0x472ec0. It stands where placed.
2. Its frame with the controls (the AI's; on hold).
3. The mobile field base, then the turrets (turret.cpp, 903 lines; 4c1
   the object, 4c2 its combat, 4c3 the program: The turrets).

## Telling a mech from another entity (2026-10-21)

zEntity's list holds every entity, mechs and vehicles alike. The exe tells
them apart through the entity's own vtable: vt+0x2c, then vt+0x50 returns the
mech (a mech returns itself; the basic vehicle, vtable 0x5c88e8, returns 0).
The program keeps no vtables, so `app_mech.cpp` keeps the same answer as a
registry: `entity_register` adds each mech the program builds, its free
removes it, `missions_start` clears it, and `entity_is_mech` /
`entity_mech(handle)` ask it. Every place that treated a list entry as a mech
(sensors' `mech()`, `mech_free`, `killer_notify`, contacts, the player's
target, the update loop, debrief and salvage, `describe_mech`) asks first and
skips anything else, so a vehicle can join the list without being read as a
mech. The update loop runs a basic vehicle's frame (step 3b) and logs any
other entity as the unported "entity vt+0x34". `test_program`'s c1m1 death run puts a
stranger entry in the list and checks it is skipped (treating every entry
as a mech crashes that run).

## The node binding (0x4d6c90 -> 0x40eaa0; ported 2026-10-25, item 16)

A hit, a pick or a sight line meets a node; the game finds the object behind
it from the node itself (0x40e920, `node_entity`): +0x28 holds a kind bit and
+0x40 the object. zEntity3d's node set (0x4d6c90, ECX the entity, from every
entity's set-node) unbinds the old node (0x40ed00), sets the node
(0x5a8390) and binds the new one (0x40eaa0) over its whole tree (0x40ea30;
each call keeps its own node holder, so the walk is sound). The bind asks the
entity's casts in turn, and the first that answers names the kind and the
owner the nodes get:

| cast | then | kind | who |
|---|---|---|---|
| vt+0x48 | | bit 17 (0x20000) | the turrets (vtable 0x5c8770: itself) |
| vt+0x40 | its vt+0x50 | 0x1000 | the mechs |
| | its vt+0xec | bit 19 (0x80000) | the basic vehicles and the field bases (0x5c88e8, 0x5c23d8: vt+0x40 and vt+0xec themselves) |
| | neither | bit 18 (0x40000) | the base itself |
| vt+0x60 | its vt+0xe4 | bit 22 | traps |
| | its vt+0xdc | bit 21 | targets |
| | neither | bit 20 | the base itself |
| none | | bit 16 | the entity (the pocket commander's marker) |

The unbind reads the node's own bits the same way (0x40e990 bit 17;
0x40e960 bit 18, else a vehicle class's owner 0x40e9d0, which is a part's
mech through 0x460000 for a part's node, else bit 19; 0x40e8f0 bit 20, 21,
22, whose owner's vt+0xe4 / vt+0xdc pick trap or target) and takes the kind
and owner off the tree's nodes whose owner is the one found (0x40ec20).

**In the program**: `mech3/entity_bind` is 0x40eaa0 and 0x40ed00. Before item
16 the vehicles, field bases and turrets were never bound (their nodes id 0,
no owner), so nothing found them from a node: a shot's shooter credit, the
HUD's reticle pick, the mechs' collider sorting what it bumps, the commands'
picks. Now the vehicle set-node (`app_vehicle.cpp`, field bases too) and the
turret's (`app_turret.cpp`) unbind and bind as the exe; mechs are bound by
their set-up (0x415ae0, the same bits), targets and traps by their own
(`app_mission.cpp`), the pocket marker by its. The port's `Turret::set_node`
now stores the node after the hook (0x4a6230 does not store it: 0x5a8390
does, inside 0x4d6c90), so the hook sees the old node to unbind. The AI's
sight line (0x4d5980) does not use the binding: it turns both entities'
INTERSECT off and asks only whether anything blocks.

Found with it: the AI's volley pick (0x4b1860's pilot) read the weapon
manager's owner as a mech in the port; a truck's AI firing at a player near
it crashed. It now takes the brain from a mech's AI controls or a vehicle's
(`ai_volley_pick`, app_mech.cpp; a port bug, the exe asks the owner's vtable).

**Checked.** `test_entity_bind.py` (probe `tests/probe_entity_bind.cpp`):
5000 binds and unbinds over trees of 1 to 12 nodes with kinds from every bit
that matters, owners among six objects whose vtables answer the casts at
random, compared node by node. `mw3tests`
`mission_c1m1_truck_nodes_name_the_truck_headless`: every node of a truck
carries bit 19 and the truck, node_entity and the HUD's pick find it from
the root and a deep child; the 14 turrets carry bit 17 and the 3 field bases
bit 19; the player's PPC shot stops on the truck's node and damages it (20 to
9); the truck's own shot leaves it as the player's last attacker (+0x664)
from the hit to the next player tick, whose 0x41aba0 clears it (damage.md
section 2, as the exe).
The glow test (`mission_c1m1_laser_leaves_its_glow_on_a_truck_headless`)
now lights the truck with a real beam fired by the player.

## A mobile field base lost (0x004590b0; found 2026-10-21, ported in step 4b5)

Run when a field base dies (it starts with the basic vehicle's death
0x004ad2d0, `field_base_die`). In a campaign (0x00479440) the campaign's
+0xa8, the field bases, goes down by one: +0xa8 is the base count (3 at the
start, reset to 3 after Op 1 Mission 1 and Op 4 Mission 4), and each base is
the 300 t and 2 mechs of room the briefing's split allows (0x00470cc0,
0x00470cd0; campaign.md section 9). With bases left (none: nothing more),
what the player carries (the game's store +0x08) is cut on the spot, by a
fraction f: 0.7 with two bases left, 0.5 with one. Ported in
`mech3/mech_fieldbase_cut.cpp` (pilot.h's stores; the program's
`base_lost` calls it), the draws, names, lab rounds and messages through
`FieldCutHooks`.

1. Message 0xf07, "MCB Destroyed, Updating Inventory...  Please Stand
   By...", always (0x0040fe70, 3 s).
2. The mechs: one entry per mech held (the store's kind-0 items by count),
   the scan stopping once 64 are listed (checked after each item). With at
   least as many entries n as bases left, keep = min(ceil(f n), 2 x bases)
   entries are drawn off the list at random (rand / 32767 x n, floored;
   an empty slot draws again); each one left goes: its name and two spaces
   (0x00484650, 0x005e4aa0) in message 0xf05 "Chassis Lost: %1", one into
   +0x78 (0x00471c10) and one out of the store (0x00472a30). FIX (bug): the
   exe's list is a 64-entry stack array; the item that crosses 64 writes past
   it, and with exactly 64 a draw of 32767 reads (and zeroes) the slot after
   it, the saved exception frame. The port's is a vector, the draw past the
   end an empty slot.
3. The rest by weight: the tons (0x0045b900, a float sum of the items'
   weight(-1); mechs weigh 0), the limit min(f x tons, 300 t x bases). While
   over it: an item drawn at random (rand / 32767 x the count, floored; past
   the end the last one); a mech is passed over; anything else moves one of
   it (ammo: 0x004b4900's lot, the weapon's rounds per ton or what is left)
   into +0x78 (add_copy, vt+0) and out of the store (remove, vt+0x14), then
   the tons summed again. Ten draws in a row that change nothing end it,
   six after one that did. A weightless item (Clan equipment of weight 0)
   changes nothing when drawn, nor does an unlimited (+0x10) one: remove
   keeps it, but add_copy has already put a copy in +0x78.
   After that the exe means to move whole items from the list's end while
   still over the limit, but its search ends past the list every time
   (0x0045951d..0x00459563: the loop leaves with the head, and the head
   test breaks out), so it never moves one. Kept as the original: a
   candidate in fixes.md.
4. Message 0xf06, "Lost %1 tons of equipment", with the tons moved ("%.2f",
   the first tons less the last).
5. [0x00605c3c] = 3 x 0x00470cd0, once: written, never read.

The +0x78 store (`CampaignGame::store78`) comes back only on a replay:
0x00434cb0 adds it (and +0x50) back to the store and resets +0xa8 to +0xac
on the game it is called on. When the mission fails or is aborted that is
the game the player replays from, so the gear returns. At the next mission's
game, though, 0x00434e00 first copies the old +0x08 store and the old +0xa8
into the new game (434e89..434e9a), then calls 0x00434cb0 on the old one:
the new game keeps the cut store and the reduced base count. On a win, what
a lost base cut is gone for good (campaign.md section 9). No sign in
0x00458xxx..0x0045bxxx of repairs looking at the mechs carried: the store
is touched only here and by the repair (above).

## High frame rates: why a vehicle tips and hops (W57, 2026-11-04)

Reported on Reddit as APCs leaving the ground near the town in c1m2 on very fast
machines. Two things, the first the original's at any frame rate:

**1. APCs jump off banks at top speed, at 30 fps too.** An APC at 30 m/s that runs
off the road onto a steep bank (c2m3's `world1/g13358`, normals down to 0.78, about
39 degrees) goes up it at 20 m/s and leaves the ground for a moment, 8 m up,
nose high. At 33 ms frames: c2m3's APCs reach 30 to 40 degrees and 20 to 25 m/s,
7 times a minute. Seen in a hidden shot (`w57-c2m3-apc-airborne-33ms.png`). The
exe's own suspension (0x4a1760 with its support step 0x4a2820) run in Unicorn on
the port's dumped state, frame by frame through that launch (80 frames at 33 ms,
150 at 6 ms with a roll of 84 degrees), gives the port's numbers exactly. So it is
the original's, and stays (no change without Owen's word).

**2. Short frames make it much worse.** A support on the ground takes its vertical
speed from the ground's step over the frame, `(ground - y) / dt` (0x4a2820): the
same 0.2 m is 6 m/s at 33 ms and 66 m/s at 3 ms. Measured, the original's path and
the fix (below), the largest tilt / vertical speed of each 58 s run, the player idle:

| | 40..20 ms | 16 | 8 | 6 | 5 | 4 |
|---|---|---|---|---|---|---|
| c1m1, the original | 12..14 / 5..7 | 17 / 6 | 82 / 35 | 82 / 45 | 82 / 47 | 8x |
| c1m1, fixed | 12 / 4..6 | 12 / 4 | 12 / 2 | 12 / 2 | 12 / 2 | 12 / 2 |
| c2m3, the original | 31..89 / 17..28 | 80 / 40 | 86 / 64 | 85 / 67 | | |
| c2m3, fixed | 32..65 / 10..26 | 33 / 11 | 40 / 10 | 34 / 8 | 3x / 9 | 16 / 9 |

c1m2 is like c1m1 (the original 73 to 80 degrees at 5 and 6 ms, fixed 11 or less),
except one fixed run at 20 ms that took another route and met a bank (74 degrees).
From 20 to 40 ms the two agree within a run's spread: no hops come back near 30 fps
(W50b's worry). c2m1's Bulldogs and Harassers do not change with the fix; their
time in the air grows on short frames either way, by another cause, not looked at.

**The fix** (FIX (B), behind `per_second`): on frames under 1/30 s the step counts
as over a 1/30 s frame, `(ground - y) * min(1/dt, 30)`; frames of 1/30 s and longer
are the exe's. Test `fix_vehicle_support_rise_per_second`.

What the fix leaves: c2m3 at short frames. Its vehicles still spend far longer past
20 degrees than at 33 ms (about 170 s against 1 s over the run's vehicles), with peak
speeds lower than the original's; some other per-frame term keeps them tilted, not
found. c1m1 and c1m2 match 33 ms.

**A possible later fix (Owen chose the one-liner for now, 2026-11-04): the move at a
fixed 1/30 s with drawn-position blending.** Time accumulated per vehicle in
`vehicle_frame`, the move (0x4a1240, the track step and suspension) run once per
1/30 s of game time, the controls still per frame. Measured on a prototype, events
past 20 degrees / seconds past / peak m/s per run, c2m3: 33 ms 4 / 1.8 / 23, 16 ms
7 / 3.0 / 22, 8 ms 4 / 1.7 / 23, 4 ms 5 / 2.0 / 18 (one parked mobile HQ stayed tilted
at 2 ms). c1m2's few events at 8 and 4 ms all sit at about -1418 -1710, where no
33 ms run drives: 30 fps physics on another route, not frame time. A first try
without many runs had called this "worse"; that was the same route effect. What it
needs before it ships: the vehicles move in 30 Hz steps, so at 144 fps they would
visibly stutter; the drawn position and rotation need blending between steps (the
node is set inside the step), and the step's own tests.

Not covered: a fight (the APCs drive their routes with the player idle or held
near), the other vehicle classes (BASIC, flying), PM.
