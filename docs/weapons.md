# Weapons at run time

Mech3.exe 1.2, 2026-10-21. Two halves, like the source tree: zWeapon in the
engine (`gamez/zweapon`, 0x581230..0x586b70 and `zwep_init.c` 0x583880) and
the game's `mech3\weapon.cpp` (0x4af5a0..0x4b5000). Read with
`docs/structs.md` (Weapons), `docs/heat.md` and `docs/damage.md`
(TakeDamage, the receiving end). Every claim gives its address; "likely"
marks an inference.

## Status

Ported and emulator-checked (24-bit x87, floats bit for bit, every call out
logged on both sides):

| Piece | Port | Test |
|---|---|---|
| weapons.zrd into the definitions: 0x583880, effect blocks 0x584bb0, find 0x581ed0, shutdown 0x584930; the MW3 half WeaponType 0x4af5a0 / 0x4b0020 / 0x4afb10; the mech lab's weights 0x484b70..0x484c00 | `gamez/zweapon/zwep_init`, `mech3/weapon` | `test_weapons_zrd.py`: the real file (49 defs) and 300 variants, every def, impact block and WeaponType byte for byte, 41 paths |
| The manager, groups, bins, projectile and beam weapons (0x4b00d0..0x4b4f00 but the AI planner), the def callbacks (salvo marks 0x4afc00, airstrike 0x4afed0, LB-X burst 0x4b44d0), the AI's LRM wait, the mech lab's rounds | `mech3/weapon` | `test_weapons_run.py`: 600 managers, 44,000 operations, 125,000 calls, 60 paths |
| zWeapon's fire state setters (0x581f60..0x581fd0), the damage path (beam damage 0x583020, hand-off 0x583480, impact 0x5826b0, splash 0x582920 / 0x5829b0, hit 0x586350, air burst 0x582320) and the flight step 0x585cc0 | `gamez/zweapon/zweapon` | `test_weapons_hit.py`: 20,000 operations on the real defs, 48 paths |
| Vector and quaternion helpers 0x51dfa0, 0x51e3d0, 0x51e740, 0x525490, 0x525840 | `gamez/zmath/zmth_vector` | through the two tests above |
| zWeapon's projectiles: the pool 0x584df0 / 0x584e60 / 0x584f40 / 0x584fb0 / 0x584fe0 and its nodes 0x5847c0, the FLYOUT model pool 0x5830b0 / 0x5830e0 / 0x583120, spawn 0x585010 with the static cast, the tick 0x5823c0, update 0x585710, homing 0x585900, collision 0x5861f0 (0x586160 / 0x5861c0), the pick 0x583130, classify 0x582b10, commit 0x5858b0 / 0x5864c0 / 0x586600, mines 0x586670 / 0x586740, bursts 0x5821c0 / 0x5820c0 / 0x5822c0, the aim slope 0x581ff0, the counts 0x581e50, sounds 0x582f60 / 0x582fc0, craters 0x5825b0 / 0x5824b0, the node handler slots 0x583220..0x583440 and touch 0x5836b0, the light pool 0x586970 / 0x5869f0 / 0x586b20 / 0x586b70; the maths 0x51e0e0, 0x51e000, 0x51e280, 0x51e460, 0x524247 | `gamez/zweapon/zwep_shot`, `zweapon`, `zmth_vector` | `test_weapons_fly.py`: 200 sessions (more until every path is reached) of 40 operations (spawns, frames, bursts, beams, handler tables, the AMS) on the real defs with unshipped features poked in, every call out, every pool slot, beam and scene node compared; trig-fed floats within 3e-5, 155 paths (287 sessions, 318,000 calls on the last run) |
| The beams: make 0x584a70 (0x584db0), free 0x584b40, start 0x581230, stop 0x581400, the frame 0x5814b0 with DESIGNATE and BOUNCES, MULTI_TARGET 0x582c10 / 0x582de0, draw 0x582f00 | `gamez/zweapon/zwep_beam` | `test_weapons_fly.py` |
| weapon.cpp: the radar list 0x4b49c0..0x4b4b96, the AMS 0x4b3ab0..0x4b3c05, the HUD ammo gauge 0x4b0d80, the vehicle builder 0x4b4660 with the named add 0x4b0460, the mech's configuration step 0x414501..0x414732 | `mech3/weapon` | radar and AMS in `test_weapons_fly.py`; the rest in `test_weapons_run.py` (600 managers, 73 paths) |
| The engine's soil names and LoadSoils 0x535f20 / 0x537cb0 / 0x537270, read by the impact table | `gamez/zmodel/gmod_matl` | `test_weapons_zrd.py`: LoadSoils on soils.zrd and 40 variants; weapons.zrd read with the mission's 14 soils |

TAG end to end (the section of that name): `test_tag.py` runs the original's
configuration step, manager and beam weapon at 30, 60 and 144 fps, then
zWeapon's beam frame, the airstrike and the missiles' flights over a
scripted world (flat ground, a mech as a capsule), the port beside it; the
flights compare within 2e-3 of the vectors' size (the CRT's acos against
the x87's over about a thousand homing frames). Five break tests
(`TAG_TEST_BREAK=1..5`).

The engine calls (scene graph, object3d, animations, sounds, the crater
module, cls_di.c's segment, ground and sphere queries) go through
`zwep::RunHooks` / `HitHooks`; the queries are ported separately in cls_di.c
by another piece of work and are hooked here. Left: see the last section.

## Three layers

| Layer | Made by | Size | What it is |
|---|---|---|---|
| WeaponDef | 0x583880 (zwep_init.c) | 0x1f4 | one per `BALLISTICS` entry in `weapons.zrd`, a table at 0x9a0eac (count 0x9a0ea8) |
| WeaponType | 0x4af5a0 | 0xc8 | the MW3 half of each def, at def +0x1f0 |
| MechWeaponTable | data at 0x5e64f0 | 37 x 0x18 | the weapons the mech lab knows: name, message id, family, index, availability, uses ammo |
| WeaponManager | ctor 0x4b00d0, dtor 0x4b01d0 | 0x94 | one per vehicle, at Vehicle +0xd4 |
| WeaponGroup | ctor 0x4b1d80 | 0x54 | five per manager |
| Weapon (projectile) | ctor 0x4b2530, vtable 0x5c9088 | 0x98 | one per mounted weapon |
| BeamWeapon | ctor 0x4b3c10, vtable 0x5c90c0 | 0xa4 | for defs with BEAM (def flag bit 1) |
| AmmoBin | in the weapon ctors | 0x18 | ammo pool, or a beam's charge |
| Projectile | pool at 0x9a0eb4 | 0x5d8 | a shot in flight (zWeapon) |
| Beam | 0x584a70 | 0x150 | a beam in the air (zWeapon) |

## Definitions: weapons.zrd (0x583880)

Called at mission load (0x490173) with ECX the world, EDX `weapons.zrd`, a
zero argument kept at 0x9a0ec0 and the callback 0x4b0020 that makes each
WeaponType. Top level: `VERSION` must be 2 (else error 0x400 and -1);
`LOCK_ON_WARNING`, `NO_AMMO_WARNING`, `TRIGGER_INACTIVE`, `WEAPON_INACTIVE`
are sounds (0x9a177c, 0x9a1794, 0x9a1798, 0x9a179c, none in the file);
`MAX_CRATER_RADIUS` (0x9a1784, 30 by the startup 0x583750); `BALLISTICS` is
`name (keys...)` pairs.

Each def starts as: ammo limit 50, range 500, velocity 120, damage 0.1, turn
and pitch rate 0.16, flag DETONATE_ON_WATER, index = (short) its position,
six zeroed impact blocks. Then, in this order (field offsets in
`zwep_init.h`):

- `NAME` (+4, else the key), `DESC` and `MILITARY_NAME` through the message
  table, strdup'd (+8, +0xc, else the key). Damage's "killed by a nuke" test
  compares +8 (`docs/damage.md`).
- `ACCELERATION` +0x38, `AMMO_LIMIT` +0x18.
- `BEAM`: flags BEAM and TIMED_DAMAGE, velocity 10000, energy 1, damage at max
  range 0.25, pulse (1, 0.5, 0.75); inside, `BOUNCES`, `TIMED_DAMAGE`,
  `ENERGY`, `TIME_TO_MAX_RANGE` (velocity = 1 / it, 10000 when <= 0),
  `DAMAGE_AT_MAX_RANGE`, `PULSE` (three values: 1 / the first, 0.125 when
  <= 0, then the other two). The file writes `BOUNCE`: never read.
- `CATCHES_FIRE`; `CRATER` (flag, the crater module's defaults twice from
  0x60bfc0, then `points`, `radius`, `depth` as (min max): minimums at
  +0x174.., spans at +0x19c..); `DAMAGE` +0x48; `DETONATION_DISTANCE`
  (squared, +0x44); `DETONATE_AT_RANGE`, `DETONATE_ON_WATER`;
  `DESTROY_ANIMATION` +0x168; `RANDOM_DEVIATION`; `EXPIRES`; `FIRE_RATE`
  (+0x14 = 1 / it: the period, seconds per shot); `FIXED_ROTATE`; `GRAVITY`
  +0x4c (behind a LOCK_ON test that is dead: LOCK_ON is read later);
  `HIT_OWNER`; `IMPACT_PROXIMITY` (+0x3c and squared +0x40); `IMPACT_TYPE`;
  `INSTANT`; `LOCK_ON` (+0x50 and the flag); `LOCK_ON_LEAD` (+0x70..+0x78);
  `MINE`; `MULTI_TARGET`; `QUICKSAND` (flag; a crater block if CRATER did not
  give one); `RANGE` (+0x1c, squared +0x20); `RANGE_MINIMUM` (flag, +0x24,
  +0x28); `RELATIVE_SPEED`, `REMOTE_DETONATE`, `TETHER_GUIDED`; `TURN_RATE`,
  `TURN_SUSPEND_TIME`, `PITCH_RATE`; `VELOCITY` (not for beams); `RELOAD`.
- The WeaponType callback, then the `FIRE` (+0x84) and `FLYOUT` (+0xe0)
  effect blocks, `FLYOUT_HEALTH` (+0x80 as a float, else -1),
  `SHOW_ON_RADAR`.
- `IMPACT`: `default`, then one block per soil kind in 0x5edbc0's order:
  water, seafloor, quicksand, lava, fire. A kind not listed copies default; a
  listed one without `ANIMATION_MIN_RANGE` takes default's. Then
  `ANIMATION_ALWAYS`, `FREEZE` / `HEAT` / `DESIGNATE` (six values into
  +0x1c8; DESIGNATE also sets +0x44 from its sixth, drops the heat flags and
  sets bit 12), `KILL_ANIMATION` +0x16c, `DAMAGE_ANIMATION` (count 1, health
  1.0), and on Direct3D only (0x72f348) `DAMAGE_ANIM_ON_HEALTH`, (health anim)
  pairs from +0x148.
- After each def: with gravity, +0x1e0 = v^2 / 2g and the velocity is replaced
  by sqrt(2 g range) through the bit trick (none shipped: every GRAVITY is 0);
  a FLYOUT model is set active and bit 9 records whether no node in its tree
  is unlit-data (0x4f9e00 with 0x4f9de0); a projectile def adds
  1 + ftol(range / velocity / period) slots to the pool.

The effect block (0x5c, 0x584bb0): +0 flags (bit 0 `RANDOM_ROTATE`, bit 1
`ANIMATION`'s second value: the fire animation follows the firepoint),
+4 `ANIMATION`, +0xc `ANIMATION_ATTACHED`, +0x10 `MODEL_ANIMATION`, +0x14
`SURFACE_ANIMATION`, +0x18 `ANIMATION_MIN_RANGE`, +0x1c `EFFECT` (else
`MODEL` +0x20, a node of list 6), +0x24 `SOUND` count and four sounds,
+0x38 `BOUNCE_SOUND` count and four, +0x4c / +0x50 the next shot's sound and
animation choice (0x581f70 / 0x581f90), +0x54 / +0x58 the node and offset it
is attached to (0x581fd0), cleared by the reader.

The pool: 164 projectiles on the shipped file for the whole mission, a free
list through +0 (0x9a0eb8). An empty pool drops the shot (0x585053) after the
weapon has spent its ammo and heat.

The IMPACT table has one block per soil name the engine knows when the file is
read (0x5edbb8, names 0x5edbc0): six built in (default, water, seafloor,
quicksand, lava, fire), then whatever the mission script's `LoadSoils
soils.zrd` (0x537cb0) added before the weapons are read at 0x490173: dirt,
mud, grass, concrete, snow, mech, silt, noslip (6..13). A material's +0x20 is
an index in that list, so a hit on dirt plays the `dirt` block: the pock
marks of the autocannons, Gauss, missiles and MGUN show (28 weapons have a
dirt, mud or snow SURFACE_ANIMATION). The game's own soil table 0x608c00
(grass 2, dirt 3, ...) maps those engine indices for the feet.

### WeaponType (0x4af5a0)

| Offset | Key | Notes |
|---|---|---|
| +0x00 | — | mech-lab index (0x4848a0 on the def's NAME), -1 if none |
| +0x08, +0x0c | HEAT | firing heat, then heat added to the target (0 in every weapon) |
| +0x10 | SHOW_ON_RADAR | an int list item only |
| +0x14, +0x18 | ZAPS_HUD | int or float; the second slot reads the **first** value again (0x4af71b): ERPPC's `(0.5 2.0)` gives 0.5, 0.5 |
| +0x1c..+0x30 | MASS `clan`, `sphere` | tons, slots (ftol), present |
| +0x34 | — | tons per round, 1 / +0x38 |
| +0x38 | MASS `ammo` | rounds per ton x SALVO truecount when SALVO exists |
| +0x3c, +0x40, +0x44 | SALVO count, delta, truecount | count 1, delta 0.1, truecount = count by default |
| +0x48 | IMPACT_FORCE | |
| +0x4c | ISNARC | |
| +0x50, +0x54, +0x58 | BURST radius, count, `wep` | with a known `wep`: +0x70 += its range, def flag DETONATE_AT_RANGE, callback 0x4b44d0 |
| +0x5c, +0x60 | U_LOOPED, U_RECHARGE_SND | MGUN alone has a loop |
| +0x64..+0x6c | NAMESOUND | name, `mech_part_damage`, `mech_part_destroy` |
| +0x70..+0x7c | — | range, range^2, range / 2, (range / 2)^2 (the halves not raised by BURST) |
| +0x80 | — | the low byte of the ctor's first argument, the reader node's address: meaningless |
| +0x84, +0x88 | — | the salvo marks list (0x4afc00) |
| +0x8c, +0x90 | SALVO radius (squared), delay | both > 0: callback 0x4afe70 |
| +0x94 | RECOIL | |
| +0x98 | U_RECHARGE_TIME | stored as period x fire time / recharge time |
| +0x9c | U_FIRE_TIME | 0.5 default |
| +0xa0 | U_DESIGNATE_AIRSTRIKE | the strike's def (TAG: TAG_MISSILE); callback 0x4afea0 |
| +0xa4..+0xbc | — | the airstrike's lock: point, flag 1, no entity, a pointer to the point, no velocity |
| +0xc0 | — | LRMs (index 0x11..0x14): range x 0.7 / clamp(velocity, 100, 5000) |
| +0xc4 | — | the AI fires this type again from this game time (0x4b4c70, set by the AI through 0x4b4c90 to now + +0xc0; 0x4b4bd0 resets them) |

The mech lab reads the weights and slots through 0x581ed0 then +0x1f0
(`mech3::weapon_weight`, `ammo_weight`, `weapon_slots`); 0x4b4900 gives the
lab's rounds for a weapon: min(rounds per ton, n), or 100 / 20 / 1 without a
def.

## The manager (Vehicle +0xd4)

Made in the mech constructor (0x414daf) and, for other vehicles, by 0x4b4660
(`build_vehicle_weapons`, from the vehicle's `weapons` list: "Vehicles" below).

| Offset | Field |
|---|---|
| +0x00 | trigger down now (0x4b0c20 copies it to +0x01 first) |
| +0x01 | trigger down last frame |
| +0x04 | 1 pressed, 2 held, 3 released, 0 neither (0x4b0c30) |
| +0x08 | alpha-strike end, clock time ([0x72c530]); 0 none |
| +0x0c..+0x18 | aim point, valid (0x4b0bc0) |
| +0x1c | fire mode: 0 chain, 1 single, 2 group |
| +0x20 | weapons destroyed (0x4b1040, 0x4b1110) |
| +0x24 | weapon count |
| +0x28 | selected group: **0 at start** (the ctor writes 0, not -1) |
| +0x2c..+0x3c | WeaponGroup*[5] |
| +0x40..+0x7c | Weapon*[16] |
| +0x80 | the mech (vehicle vt+0x2c, vt+0x50), 0 for other vehicles |
| +0x84 | the vehicle |
| +0x88 | has a target: lock-on is tried only with it (0x4b3330) |
| +0x89 | weapons online |
| +0x8c | sum over bins of ammo x damage |
| +0x90 | 1 / the largest that sum has been (0x4b4d10) |

Methods: 0x4b0400 add a loadout item, 0x4b0460 add by index on a node,
0x4b03c0 find by index, 0x4b0c30 update, 0x4b04f0 / 0x4b0610 next / previous
group, 0x4b0730 select, 0x4b0790 / 0x4b07e0 next / previous weapon, 0x4b0830
mode sound (0x16 chain, 0x14 single, 0x15 group), 0x4b0880 release, 0x4b08c0
network fire, 0x4b0a70 alpha strike, 0x4b0ae0 jettison, 0x4b0bf0 online,
0x4b0de0 critical, 0x4b1040 part lost, 0x4b1110 destroy all, 0x4b0220
unlimited ammo, 0x4b0370 forget a target, 0x4b2da0 arm restriction.

### WeaponGroup (0x54)

+0 manager, +4 index, +8 count, +0xc current (-1), +0x10 current weapon,
+0x14 Weapon*[16]. Add 0x4b1eb0 (up to 16, then selects weapons[0]), remove
0x4b1ef0 (shifts down, selects weapons[0]: after the last one goes, current
keeps pointing at it), find 0x4b1db0, select 0x4b1de0. Next / previous
(0x4b1f40 / 0x4b2000) skip weapons with no ammo and destroyed ones; `current`
is compared unsigned, and a group of more than one weapon with current -1
would loop four billion times (never happens: add selects). Trigger 0x4b2100,
held 0x4b21f0, release 0x4b20b0: mode 2 the whole group, else the current
weapon.

### AmmoBin (0x18)

+0 references, +4 amount, +8 max (grows when more is put in), +0xc
infinite (amount and max read -1), +0xd "out of ammo" played, +0x10 manager
and +0x14 damage (from the first weapon, 0x4b4cc0). SetAmount 0x4b4d10 keeps
the manager's +0x8c / +0x90; Add 0x4b4dc0. A weapon of a type already on
the manager takes that bin, adding a reference **for every** weapon of the
type it finds, so three LRM20s count 1 + 1 + 2. A beam's bin is its own and
has no manager.

## From the loadout to weapons

`dfn_<mech>.zrd` → configurations → `weapons` (`docs/mechdef-configs.md`).
When the mech takes a configuration (0x414501..0x414732, mech code, ported as
`mech3::configure_weapons`):

1. The manager is reset (0x4b0290: weapons and groups remade; group and
   destroyed count stay).
2. Every weapon item → 0x4b0400 → 0x4b2230: the group clamped to 0..4, a
   BeamWeapon (0x4b3c10) or Weapon (0x4b2530); a weapon that uses no ammo by
   the mech-lab table gets an infinite bin; SALVO count to +0x64.
3. Every ammo item → the first weapon of its index → its bin `Add(N)`.
4. Equipment counts; TAG mounts weapon 0x20 into group 5 through 0x4b0460
   (no 16-weapon check); the AMS gets 100 rounds per AMS (0x4146f9).

### Weapon (0x98; BeamWeapon adds +0x98..+0xa0)

| Offset | Field |
|---|---|
| +0x04..+0x10 | lock: flag, target (target vt+0x24), its id (0x5a8630), target vt+0xc |
| +0x14 | the target handed to the missile (with [0x605fb4] set) |
| +0x18 | rating: shots a second x damage (BURST: pellets x pellet damage); salvos not counted (set before +0x64) |
| +0x1c | its slot on the manager |
| +0x20, +0x24 | the firing part's node, the part (0x41d510 on the firepoint's model part, every update) |
| +0x28 | mech-lab index |
| +0x2c | WeaponDef* |
| +0x30 | reload timer |
| +0x34..+0x54 | aim point, muzzle, direction (0x4b36e0) |
| +0x58 | AmmoBin* |
| +0x5c | locked this shot |
| +0x5d | FCS in the firing part |
| +0x60 | group |
| +0x64, +0x68 | SALVO count, shots still to fire |
| +0x70 | damage 0..1, 1 destroyed |
| +0x74 | enabled (arms and water, 0x4b2f90) |
| +0x78, +0x7c | loop sound, recharge sound |
| +0x80 | firepoint |
| +0x84..+0x8c | offset from a weapon sharing the firepoint (share x (rand - 16383.5) / 16383.5 in x and y, unless the weapon has a SALVO truecount) |
| +0x90 | loadout item |
| +0x94 | manager |
| beam +0x98, +0x9c, +0xa0 | firing, the aim point's address, the zWeapon beam |

Vtables: projectile 0x5c9088 / beam 0x5c90c0: +0 dtor, +4 fire, +8 is beam,
+0xc firing, +0x10 damage a second with salvo (beam: per fire-and-recharge
cycle), +0x14 ammo (beam -1), +0x18 update, +0x1c lock on, +0x20 try lock
(on the entity's target record, only with manager +0x88), +0x24 aim and fire,
+0x28 trigger, +0x2c release, +0x30 held, +0x34 ready 0..1.

## Firing

### Trigger and modes

Commands (handler 0x41be80): CMD_TRIGGER1 0x3d sets manager +0;
CMD_WEP_NEXT / PREV; CMD_FIREMODE (mode + 1) % 3 with its sound;
CMD_WEPGRP_NEXT / PREV; CMD_WEPGRP_1..5; CMD_MOVETOWG_1..5;
CMD_ALPHASTRIKE 0x4b0a70(0.2); CMD_JETTISON_AMMO.

`Update(dt)` (0x4b0c30, from the mech update at 0x41a814): every weapon's
update, then without an alpha strike: pressed → the group's trigger (if
nothing fired the press is dropped); held → held; released → release, and in
chain mode the group steps to its next weapon. During an alpha strike the
state is 2 until the clock passes its end, then everything is released.

In group mode with more than one beam, every beam but the best rated clears
[0x9a17cc] before firing: zWeapon then plays no ground impact for it (only
the animation, not damage).

### CanFire (0x4b3a20)

Def present, enabled, weapons online, reload <= 0, ammo > 0 or infinite,
damage < 1.

### Projectile weapons

- Trigger (0x4b3190): aim and fire; on a shot, a SALVO weapon queues
  count - 1 more and a looped sound (MGUN) starts at the muzzle. A refused
  trigger on an empty bin plays "out of ammo" (mech sound 0xe) once for the
  player.
- Held (0x4b3280): a SALVO weapon that has reloaded triggers again; others
  try to fire. The out-of-ammo note here keys on "triggered again", not on a
  shot.
- Update (0x4b2c20): reload -= dt; the recharge sound for the mission's
  player while reloading; one more salvo projectile a frame, with the fire
  sound and animation muted (0x581f70 / 0x581f90 with -1), re-aimed.
- Fire (0x4b3450): FCS in the firing part; the lock (if the owner's vt+0x58
  is 0); the fire effect attached to the firepoint when its flag says so;
  spawn (0x4b4a40 → 0x585010; its last argument, the shot hostile to the player (the player's team among the shooter's enemies, +0xc8), decides the radar's colour and which AMS takes it);
  stats, the network event 0x16; reload = period; ammo on the salvo's first
  shot only (truecount, capped at what is left); the HUD; the firing heat and
  recoil (0x461670).
- Aim (0x4b36e0): the muzzle is the firepoint (salvo shots scattered by its
  `radius`, others moved by their offset) in the part node's frame
  (0x4ed650). Off the centre torso (node id 1), the direction keeps within
  acos(0.89), about 27 degrees, of the part's facing; centre-torso weapons
  aim anywhere. With the bug fixes, for the player, the cone widens with
  the view past 4:3 (fixes.md).

### Beams

A beam's bin is its charge: full = ftol(U_FIRE_TIME x 10000).

- Trigger (0x4b3fc0): with a recharge time only from a full charge; aims,
  makes the zWeapon beam once (0x584a70), takes one unit, starts it
  (0x581230), half range under water ([0x9a17d4] = 0.5), heat once.
- Update (0x4b4230): n = ftol(max(10000 dt, 1)); firing drains n (timed
  beams heat in proportion) and stops at 0; not firing refills
  ftol(+0x98 x f x dt), f = 1 to heat 0.5, then 0.5 / heat, floored at 0.5.
- Release: only beams with a fire time over 0.5 s (pulse lasers) stop; held:
  fire again when ready, or keep steering.
- The muzzle (+0x40), which the zWeapon beam draws and casts from each
  frame (its +0x20 points at it), is aimed only by the trigger (0x4b3f40)
  and the held frames (0x4b41b0): the exe's aim (0x4b36e0) has no other
  beam caller (0x4b2d2d is the projectile weapons' update). So a short
  laser let go before its charge is spent fires on from the last held
  frame's muzzle: a Mad Cat at full speed leaves its medium laser's beam
  0.2 m behind a frame after the release and 1.3 m by its end (~140 ms;
  playtest #24, `sdl_program_mouse_click_fires_the_laser`). FIX (bug),
  behind bugs: the update re-aims a firing beam each frame, as the held
  path does (fixes.md).

### Heat (0x461670)

Projectiles: heat, then recoil on the part. Beams: heat once, or heat x dt
for TIMED_DAMAGE. AddHeat 0x419e40 (`docs/heat.md`).

### Arm restriction (0x4b2da0) and water (0x4b2f90)

For the player: the left and right side views keep only the matching arm
(firepoint part 2 / 3). In free look (controls +0xc == 1) with the torso's
two frames (0x465940 and the torso's +0xa4) more than 35 degrees apart, the
side comes from the sign of their cross product's y. Separately, a weapon
that is not a beam in a part with +0x49 (under water) is disabled.

## Crits, cook-off and jettison (0x4b0de0)

The candidate with the smallest value wins (from +inf). Weapon crit: weapons
on the part not destroyed, value the rating for plain beams, |ammo x rating|
otherwise; the winner is destroyed (0x4b3020: its sounds, HUD reselection).
Ammo: the configuration's ammo items in the part (any part for -1), value
|ammo x rating| of the first weapon of the index; the winner's bin is
emptied. Nothing here applies damage; CASE is checked by the caller
(`docs/damage.md`). Jettison empties the current weapon's bin, or the whole
group's in group mode (unlimited bins too).

## zWeapon run time

The rest of 0x581230..0x586b70 (0x580550 is DirectDraw's error strings; from
0x586ba0 it is the network session).

### Projectile (0x5d8, `zwep::Projectile`)

| Offset | Field |
|---|---|
| +0x00 | next (free list, then the def's active list at def +0x7c) |
| +0x04 | the shooter's node |
| +0x08 | the shooter's zone set ([vehicle +0x64]); with no zone only the low byte is set, to 4: "use the tick's" |
| +0x0c | scene node ("Projectile_%d", made by 0x5847c0) |
| +0x10, +0x14, +0x18, +0x1c | FLYOUT animation (following the node), attached animation, the model animation, the pooled copy of the FLYOUT model |
| +0x20 | state: 1 flying, 0 remove, 2 a mine lying in wait |
| +0x24 | spawn position |
| +0x30 | base velocity: the shooter's, for a locked shot only |
| +0x3c, +0x48, +0x54, +0x60 | direction, position, the move's end this frame, velocity |
| +0x6c..+0x74 | lock: entity, the target's position and velocity pointers (a `LockRef`) |
| +0x78, +0x7c | static hits cast at the spawn, the frame of the cast ([0x72f338]) |
| +0x80 | the static hits (a 0x504-byte list: count, then 0x28-byte records) |
| +0x584 | the next static record to reach |
| +0x588 | homing time; for a mine in a network game, its expiry |
| +0x58c, +0x590 | speed, maximum speed |
| +0x598 | RANDOM_DEVIATION's drift velocity |
| +0x5a4 | the drift so far, held within +-1 (y +-0.75) by pushing back at 10 a second (0x585f07) |
| +0x5b0, +0x5b4 | distance travelled, age |
| +0x5b8 | flyout health, -1 none; 0 means shot down |
| +0x5bc | damage scale ([0x9a17d0]) |
| +0x5c0, +0x5c4, +0x5c8 | copies of [0x9a17d8], [0x9a17dc] (both unread) and [0x9a17e0] (terrain following) |
| +0x5cc | spawn's fifth argument (likely a network id; the network gate may change it) |
| +0x5d0, +0x5d4 | per-frame callback (the radar's 0x4b4a30 / 0x4b4a20), its argument (the shot) |

Query answers (`HitList`, 0x504 bytes): the segment query 0x4eed90 and the
sphere query 0x4f0ac0 write the count in byte 0; the nearest-hit query
0x4eece0 moves it to byte 1 and puts the nearest index in byte 0, which is
what the pick reads.

### Spawn (0x585010)

ECX def, EDX the shooter's node, then the zone pointer, muzzle, direction,
velocity, p5, a slot to reuse (`ret 0x18`). With the network flag ([0x9a0ec0])
the gate [0x72fcac] can refuse it; a pool slot (0x584df0: the FLYOUT model
put under the node, a pooled copy when the model is animated) or nothing.
Positions and direction; the fire globals copied and reset to 1, 1, 1, 0.
Without ACCELERATION and not INSTANT: speed = VELOCITY, velocity = shooter's +
direction x speed (RELATIVE_SPEED is never tested), and, without gravity or
lock, the **static cast**: the segment query over the whole RANGE, with the
shooter (and a shootable shot's own node) out of it and only nodes that have
not moved since the frame (0x4ed9d0); the records' distances, then a sort
that compares the shifted record with the one before it instead of the one
being placed, so it is one bubble pass: only the farthest is sure to be last.
With ACCELERATION (or INSTANT) the shot starts at 0.0001 plus the shooter's
speed, the maximum raised by it, and the velocity the shooter's less its
speed along the direction. Then the FIRE sound at the muzzle, the FIRE effect,
or the FIRE animation (the choice from 0x581f90; RANDOM_ROTATE rolls it; on the
firepoint when attached); the node turned to the direction for animated
flyouts; the FLYOUT animations started; a lock's three dwords copied
(LOCK_ON, with the lock flag above 0) and the shooter's velocity kept as the
base; FLYOUT_HEALTH makes the node shootable (+0x24 bit 27, +0xbc = 1, +0x40 =
the shot). The fire and flyout choices are set back to 0.

### The frame (tick 0x5823c0)

The tick saves the zone ([0x74a328], the camera's list; the port's
`query_zone`, one copy with the render's, view.md section 7), runs every
def's update (beams: 0x5814b0), then every projectile def's commit, and
puts the zone back.

Update (0x585710), per shot: state 1; its callback; the zone set to the
shot's (or the tick's); then **shot down** (health exactly 0: the
DESTROY_ANIMATION, or an air burst without one), a **mine** at rest
(0x586670), or flying: homing if locked, then the step (INSTANT: the whole
RANGE at once). A flying shot's move is cast (0x5861f0) and what it meets is
hit (0x586350) or, for a mine, landed on (0x586740). A stopped shot that is
not a mine would move by the last shot's move (the original's local; no
shipped weapon stops). INSTANT shots end after one frame.

Collision (0x5861f0): without a static cast, the segment query from the
position to the move's end and the pick. With one, the next static record
whose node has not moved since the cast, once the distance travelled reaches
it (one record a frame), against a query of only what moved since (0x4ed9e0);
the nearer wins.

The pick (0x583130): the query's nearest record, but water stops only
DETONATE_ON_WATER shots: the others play the water animation and take the
nearest record that is not water (none when the list has one record; the
static list's byte 1 is never written, so a static water hit is dropped).

Commit (0x5858b0): dead shots leave the list (0x586600: animations stopped,
the node out of the world, back to the pool; a TETHER_GUIDED shot is not
returned to the pool, its slot is lost), the others move (0x5864c0): gravity
shots face their velocity (not FIXED_ROTATE), a mine spins 200 degrees a
second and grows from 1 to 5 over its first second, the node is put where the
shot was at the frame's start ([0x5cec0c] is the constant 1 that picks it),
then the position takes the move's end.

### Flight (0x585cc0, ported)

Each frame: speed += ACCELERATION x dt up to the maximum (rebuilding the
velocity from base + direction x speed unless locked and not tethered);
gravity; the move = velocity x dt; RANDOM_DEVIATION adds a random drift of up
to 5 dt per axis, turned to agree with the drift so far, except within 20 m
(or 2 x DETONATION_DISTANCE) of the target; a tethered shot is held under
[0x9a17a0] (30) above the ground height; the distance travelled grows by the
move's length (mines count half). At RANGE: a homing shot without EXPIRES, or
DETONATE_AT_RANGE, bursts in the air (0x582320); every shot ends. Short of
it, a locked shot inside DETONATION_DISTANCE bursts.

### Homing (0x585900)

The wanted direction is to the target; LOCK_ON_LEAD (no shipped weapon)
blends in the intercept (0x524247) from lead min to lead max of age (slerp
0x51e460), then uses it. The turn allowed this frame is TURN_RATE x
[0x9a17d8] (the global, which spawn has reset to 1: the FCS doubling never
applies; FIX (bug) behind `bugs`, the shot's own copy +0x5c0, 2 for a shot
fired locked with FCS, fixes.md) x factor x dt, the factor 0 until TURN_SUSPEND_TIME, then
(t - suspend) / LOCK_ON, or 1 when there is no suspend time; t is the homing
time, which stops one frame past LOCK_ON, so with a suspend time the factor
never reaches 1 (LRMs about 0.93 at 30 fps). Tethered shots and FCS shots
(+0x5c8) probe the ground (0x4eda10): a tethered shot within 5 m of its
ceiling stops climbing; an FCS shot under 10 m climbs (pitch rate x 0.64)
when the line to the target is blocked by something without a damage
handler; with no ground under it the shot expires. The angle is acos of the
dot clamped to 0..1 (beyond 90 degrees counts as 90); the direction turns by
at most the allowed turn (slerp, normalised), and the speed is multiplied by
0.8 + 0.2 cos(turn). Until LOCK_ON the shooter's velocity fades out of the
velocity; the node faces the direction.

In the program (playtest #25, 2026-10-25): the lock's hooks were unset, so
0x4b3330 never found a record and every LRM flew straight. Now
`app_weapons.cpp` gives the mech's target selected (+0x6c, a contact), its
object (contact +4), that object's mech (for the ECM count, +0x3cc, and the
lock warning 0x41abd0: pilot sound 9 on the view's mech), the aim point as
0x5a8630's live pointer into the target node's world matrix (`app_sensors`
`weapon_lock_point`) and the velocity as vt+0xc's pointer (a mech's 0x41d480
writes its lead velocity into +0x64c when asked, so the lock keeps the
velocity at the lock; a basic vehicle's live +0x30; the rest still). FCS
(+0x3b8) and 0x460c10 are wired with them. LRM5 has TURN_RATE 0.25, so a
missile turns at most about 14 degrees a second: at 150 m an LRM still finds
an Owens 10 degrees off the reticle (`mission_c1m1_lrm_homes_on_the_lock_headless`;
without the lock it misses), not one 22 degrees off.

Nothing drops an in-flight shot's lock when its target dies: the dead stay
as wrecks (a mech's node, a vehicle's after its death 0x004ad2d0; freed
only by the mission's cleanup or a valve's destroy), so the pointer stays
good and the shot flies into the wreck. A shooter with no target selected
fires unlocked (0x004b3330 returns before 0x004b3370), and an unlocked shot
never turns. A part's weapon whose target is over about 27 degrees off its
facing fires along the facing (0x004b36e0, cos 0.89) and a locked shot then
turns toward the target in flight. Tested in a fight
(`mission_c3m5_homing_*_headless`, 2026-11-02, Owen's c3m5 report): every
live shot of every projectile weapon watched each frame (the weapons'
active lists: a retired slot keeps state 1 in the pool) for its lock against
the shooter's selected target, its aim on the target and finite, unlocked
shots straight (the player's LRMs fired with none), the ground probe
(0x004eda10) under every flying shot (in the shot's zone set: at a zone
border the ground can be another zone's, which the shot does not see), and a
lancemate's salvo whose target is killed mid-flight reaching the wreck. On
the 1.0 data before W48 the ground check fails (zero boxes on c3's models);
six break tests.

### Mines (0x586670, 0x586740; no shipped weapon)

A mine lands on the first thing it meets: something with a damage handler
sets it off (the impact); BOUNCES reflects it (0x582b10) at a quarter of its
speed with the bounce sound, over half the bounce's length, until it is under
0.2: it lies down (state 2, scale 5, in a network game expiring after 120 s).
Lying, it goes off when the splash query finds anyone but its layer within
IMPACT_PROXIMITY (REMOTE_DETONATE ones only by 0x5820c0), or when its time is
up: the splash, then a ground burst without splash. A pass without a bounce
reuses the last bounce length (the original's stack; 0 at first here).

### Bursts (0x5821c0, 0x5820c0, 0x5822c0)

A ground burst casts from 1 m above the shot down to a tenth of
IMPACT_PROXIMITY below it, impacts what it finds, then splashes from 1 m above
(the shot keeps the raised position). REMOTE_DETONATE (0x5820c0) bursts every
stopped shot of the weapon (of one owner, or all) or one at a point, and
tells the network ([0x72fcb4]); 0x5822c0 bursts at a point for no one.

### Craters and quicksand (0x5825b0, 0x5824b0)

On a node with +0x24 bit 0x10000: the crater module's defaults, then points,
radius and depth each the minimum plus rand / 32767 x the span, the material,
the point and the shooter; 0x4ffaa0 makes it (1 when made). Quicksand only on
the default soil, through 0x5020f0 / 0x502110 the same way.

### Node handler slots (0x583220..0x583440, 0x5836b0)

Node +0xbc is a 16-byte table of two {object, function} slots, 0 damage, 1
touch, shared by a subtree: 0x5832f0 sets a slot of the node's own table, or
makes a table (a new node, or splitting from the parent's, the other slot left
empty) and hands it down to every child that had the old one (0x5832a0).
0x5833b0 (flag, then fn) sets the damage slot and with the flag sets a node
flag (0x4eb790); 0x5833f0 the touch slot; 0x583400 frees the node's own table
off its subtree. The touch (0x5836b0): slot 1 with the damage, the shot line
set to the contact (collision's 20 per frame against buildings).

### The impact light pool (0x586970..)

Eight "Thermal glow" lights at load, range 0.1..0.2, a list through node
+0x40; DESIGNATE beams take one (coloured with DESIGNATE's 4th..6th values:
TAG's is green 0.51, 1.0, 0.03), put it on what they touch and give it back.

### Hits (ported)

- Hit 0x586350: the shot line to 0x9a0ec4..0x9a0ed8 (what TakeDamage reads);
  inside RANGE_MINIMUM with a zero second value the shot is a dud, else the
  damage scale x= that value (LRMs 90 m, 0.25: a quarter of the damage and of
  the splash radius) with the min-range animation; then the impact and, with
  IMPACT_PROXIMITY, splash on everyone but the direct target.
- Impact 0x5826b0: the def callback (salvo marks 0x4afe70: a later shot of a
  salvo within its radius of an earlier one, before its delay, skips sound,
  craters and animation; LB-X burst 0x4b44d0; airstrike 0x4afea0), the damage
  DAMAGE x scale through 0x583480, craters / quicksand (also where a kill
  fell), the impact sound, animation, surface animation (oriented to the
  hit's normal), and effect.
- Hand-off 0x583480: the hit node's +0xbc: 0 nothing; 1 a projectile with
  FLYOUT_HEALTH (any damage wears it down, LRM_BG only); else {object,
  function}, called as function(object, def, record, damage). For a mech that
  is 0x4161a0 → **Mech::TakeDamage(def, record, damage)**. The attacker goes
  to 0x9a0edc; the handler reports a kill (0x583710) and the health left
  (0x583730), which pick the kill animation, the damage animation by health,
  or a beam's ground animation.
- Splash 0x5829b0: damage x (1 - d^2 / IMPACT_PROXIMITY^2) x scale (MINE:
  full) to each record of the query (0x4f0ac0, radius IMPACT_PROXIMITY x
  scale), skipping the excluded handler. A queue for more than 31 hits
  (0x9a0ef4) is dead: nothing puts the count there.
- Air burst 0x582320: an impact on the shot's own node at its position, then
  splash on everyone (the shooter included, likely).
- Beam damage 0x583020: factor = max(0.5 (1 + cos(pi d / (RANGE x range
  scale))), DAMAGE_AT_MAX_RANGE), d the beam's length; DAMAGE x factor x the
  beam's scale, once a shot (the beam's flag 2), or x dt for TIMED_DAMAGE.
  Under water the range scale is 0.5: the falloff runs over half the range.

### Beams (`zwep::Beam`, 0x150)

+0 next, +4 prev on the def's list (+0x7c), +8 def, +0xc the owner's node,
+0x10 zone, +0x14 the looping FLYOUT sound, +0x1c light, +0x20 / +0x24 the
weapon's muzzle and direction (followed every frame), +0x28 length, +0x2c the
designate timer, +0x30 age, +0x34 damage scale, +0x38 range scale, +0x3c
segments made (up to 8, 1 for DESIGNATE; the game asks for 1), +0x40 shown,
+0x44 segments (node, point, direction, length), +0x144 / +0x148 the lock
(MULTI_TARGET: a count and an array), +0x14c flags (bit 0 ground effect, bit
1 damage dealt).

- Make 0x584a70: "BeamReflect_%d" nodes with the FLYOUT model, in the world,
  hidden. Free 0x584b40.
- Start 0x581230: the FIRE sound, the FLYOUT sound looping (silent for
  DESIGNATE), the FIRE effect or animation (kept in the fire block's +0x08
  while it fires), the fire state taken (damage and range scale, the ground
  flag, the lock), DESIGNATE's light; onto the list. Stop 0x581400: the loop
  stopped, the FIRE sound again, the animation freed, off the list, the light
  back, the segments hidden.
- The frame 0x5814b0: the length grows toward RANGE x range scale by
  (reach - length) x VELOCITY (1 / TIME_TO_MAX_RANGE) x dt, snapping within
  0.1; each segment is placed, scaled by the PULSE wave ((sin(age x rate x
  2 pi) + 1) / 2 between PULSE's two values) and cast (the owner out of the
  first). What it meets plays the impact sound (every frame) and: with a
  damage handler, takes the damage (0x583020, the beam stopping there), or
  for DESIGNATE adds to the timer and, once past the sixth DESIGNATE value
  (TAG: 1 s), impacts it with a stand-in shot (the airstrike); without one,
  the ground animation (if the beam's ground flag) and, for DESIGNATE, the
  timer counts down and sets the loop's volume. BOUNCES reflects into the next
  segment. Nothing met: DESIGNATE goes silent, its timer and light off.
- MULTI_TARGET (no shipped weapon): with 2..4 locks the targets are sorted
  along the direction to the farthest (0x582c10, on the weapon's own array),
  the chain kinked at random between points 40% of the way to each, with a
  branch to each target (0x582de0 casts each segment, damaging what has a
  handler); one lock kinks once at half way; none is a straight full-reach
  ray.

### Specific weapons

- LB-X: DAMAGE 0, RANGE 30, BURST sets DETONATE_AT_RANGE; each shell bursts at
  30 m (or on impact) into 3 pellets (0x4b44d0): the shell's velocity plus up
  to `radius` sideways at a random angle (a cone of about 1 degree); spawn
  then adds the pellet's own 400 (likely about 800 m/s).
- NARC: a homing missile of DAMAGE 0; TakeDamage marks with ISNARC.
- TAG: the strike (0x4afed0) is 4 TAG_MISSILEs from 1500 back along the beam
  and 400 up, each moved by up to 25 in x and in y (z is shared), homing on
  the point ("TAG, end to end" below).
- AMS (0x4b3ab0..0x4b3c05, weapon.cpp; the mech makes it with range 150,
  period 0.6, 100 rounds, and the configuration step sets 100 rounds per
  AMS): it takes the nearest shot on the radar list within 150 by |dx| + |dz|
  (the player's foes' shots; an enemy's AMS the player side's) and shoots it
  down (health 0) once the period, shared over the AMS count, has passed.
  Only shots on the radar (SHOW_ON_RADAR: the missiles) can be shot down.
  A mech's AMS frame is 0x00419200 (`update_ams`, mech.cpp), run by the
  mech's update (0x0041a822): with a player in play, an AMS fitted and on,
  from the mech's place raised by its height, the shots not hostile to the
  player for a mech whose team the player's enemies hold, else the hostile
  ones; a find is fired at. No sound or effect: 0x4b3bb0 only zeroes the
  shot's health and spends a round. Until playtest W18 (2026-10-30) the
  port's update called it through a hook nothing set (UpdateCalls'
  equipment_update, whose "unset runs the port" was not so), so no mech's
  AMS fired. Test: `mission_c1m1_ams_shoots_down_incoming_missiles_headless`
  (35 LRM10s at the Mad Cat: on, 10 shot down, a round each; off, none;
  break: the fallback removed).
- The radar list (0x4b49c0..0x4b4b96): a std::list of {shot, hostile to the player}; every
  frame each radar shot's tick puts itself on it, and the radar object's
  action (priority 2) empties it.

## TAG, end to end

Does it work? Yes, as coded, end to end: the original's own code, run in the
emulator from the trigger to the missiles' hits (`test_tag.py`, the port
beside it, every call out and every pool slot compared). But it only works
on a target that stands still, after a hold with no room for error, and the
missiles take seven seconds to arrive from far behind the player. That is
why it looks as if it does nothing. The numbers below are the original's.

**The mount.** One stock loadout carries TAG: the Cauldron-Born's
`MSG_CONFIG_ALTERNATE_B`, `("tag" "ltorso")` (the mech lab can add it to
others). The configuration step (0x414679..0x4146f7) mounts weapon 0x20 on
the node named like `firepoints[0]` (`firepoint01`, the right arm on the
Cauldron), in group index 4, "Weapon Group 5": F5 (WEPGRP_5, 0x41c460) selects
it, and `[` / `]` reach it too (0x4b04f0 counts beams). The vehicle
constructor 0x4b28d0 matches the node's name against the dfn's firepoints and
takes the part from the node, so the TAG has firepoint 0 and the right arm's
part (emulator: the run harness's configuration step, `test_tag.py` part 1).
So it fires from the right arm, aims like an arm weapon, and is lost with the
right arm, wherever the equipment sits. Weapons the loadout puts in group 5
share the group with it (none on the Cauldron's Alternate B).

**The frame order.** The player's mech update (Mech::Update vt+0x34, which runs
the weapon manager 0x4b0c30 at 0x41a814) and every other entity's
(0x5a8850) run from the action 0x4a1160 at priority 2 (set at 0x49e85a); zWeapon's
tick 0x5823c0 is the priority 3 action (0x584759); the lists run 0 to 5
(0x4f7d80). So in a frame the charge is drained, stopped or the beam started
first, and the beam is cast after: it is cast on the frame it starts, and its
last cast is the frame before the update that finds the charge empty.

**The hold** (DESIGNATE's sixth value, 1.0 s; the charge 10000, drained
ftol(10000 dt) a frame; U_RECHARGE_TIME 60):

| fps | drain a frame | frames cast | on the mech needed | to spare | refill |
|---|---|---|---|---|---|
| 30 | 333 | 32 (1.067 s) | 30 | 2 | 2000 frames, 66.7 s |
| 60 | 166 | 62 (1.033 s) | 61 | 1 | 5000 frames, 83.3 s |
| 144 | 69 | 146 (1.014 s) | 145 | 1 | 10000 frames, 69.4 s |

The timer adds the frame's dt in float, so 60 x (1/60) and 144 x (1/144)
fall just short of 1.0 and take one frame more. Off the mech: on the ground
(no damage handler) the timer counts down by dt, so a frame there costs two;
on the sky (nothing hit) it goes back to 0. At 30 fps one frame on the
ground still calls the strike, on the last frame cast; two do not, nor one
on the sky. At 60 and 144 fps any one frame off the mech, ground or sky, and
there is no strike. Letting go stops it at once (U_FIRE_TIME 1.0 is over the
0.5 that release 0x4b4180 keeps), the charge already drained stays spent,
and the trigger wants a full charge again: let go after 10 frames at 30 fps,
6669 left, and it fires again only 666 frames (22.2 s) later. The recharge
truncates each frame (frame-rate dependence above): hot, 0.5 / heat of it
(heat 1.0 at 30 fps: 60 a second, 167 s); at 144 fps with heat above 0.58,
or at any heat above 166 fps, it never refills. The port carries the
fraction (FIX (B), fixes.md).

**The strike.** On the frame the timer passes 1.0, the impact 0x5826b0 runs
with a stand-in shot (the beam's owner, origin and direction), TAG's def
callback 0x4afea0 calls WeaponType::strike 0x4afed0 with the point the beam
touched: four TAG_MISSILEs (spawn 0x4b4a40 -> 0x585010, on the radar) from
the point less 1500 along the beam plus 400 up, each moved up to 25 in x and
y, aimed at the point and locked on it (a fixed point, no entity, no
velocity). The beam is at most 600 long, so they start some 900..1500 m
behind the shooter and 400 m up, out of sight. ACCELERATION 60 from 0.0001
m/s: 400 m/s only after 6.7 s and 1333 m. The homing (LOCK_ON 0.1, suspend
0.05) has them on the line at once, and the path (about 1550 m) is short of
RANGE 2000. On flat ground against a mech (a capsule 12 m tall, radius 3)
300 m away:

| the mech | the missiles land | from the mech | its damage |
|---|---|---|---|
| standing (30, 60, 144 fps) | 7.2 s after the strike | on it: 4 direct hits | 72 (4 x 18) |
| walking away at 5 m/s (18 kph) | 7.3 s | 7..17 m | 58 (splash only) |
| walking away at 7 m/s (25 kph) | 7.3 s | 23..34 m | 15 (two splashes) |
| walking away at 10 m/s (36 kph) | 7.3 s | 45..56 m | 0 |
| crossing at 10 m/s | 7.3 s | 72..75 m | 0 |
| running away at 20 m/s | 7.3 s | 120..126 m | 0 |

Splash is IMPACT_PROXIMITY 30 with the damage falling off with the square of
the distance; the test measures the distance to the mech's surface (the
sphere query 0x4f0ac0 is not ported: an assumption). So a mech moving faster
than about 8 m/s (30 kph) over the 8 seconds from the first frame of the
beam is not touched, and only one standing still takes the direct hits. A
building with a damage handler works. Nothing else stops it: the network
gate (0x585010) applies only in multiplayer (not traced further), the four
missiles take four of the 164 pool slots the mission shares (a full pool
would drop them silently), and the owner is the shooter's node, so they cannot hit
the shooter.

**Why Owen saw nothing.** Any of: the target moved (the missiles hit where
it stood seven seconds earlier, 1.2 km behind and above the player, so
neither the launch nor the flight is in view); the beam left the target for
one frame (at 60 fps there is no frame to spare, and a frame on open sky
resets it); the beam was on the ground or a building without a damage
handler (the timer never counts up there); the trigger was let go before the
second was up (the beam stops and the charge is gone for 22..67 s); or the
next try came before the minute-long recharge was full (a trigger from a
partial charge does nothing at all).

**The fix** (Owen 2026-10-20: hard but consistent; `zfix bugs`, off in the
probes, so the parts above stay the exe's):

- The hold compares the time on the target in whole milliseconds
  (`zwep::designate_held`): 30, 60 and 144 frames at 30, 60 and 144 fps.
- The beam lasts its 30 fps time in the exe, (ceil((charge - 1) /
  ftol(rate / 30)) + 1) / 30 = 1.067 s for TAG, at any rate: it stops at the
  first update at or past it (in whole ms), the charge drained evenly
  meanwhile (`BeamWeapon::fired_time`). 32, 64 and 154 frames: 67, 67 and
  69 ms of slack. Only from a full charge, as before; let go and what was
  left stays.
- The strike locks on the marked unit when it is a mech: the hit node's
  damage handler (`zwep::damage_object`) is a mech's, so the lock is its
  live position and velocity (`WeaponCalls::strike_target`); a building or
  anything else keeps the point. Flown with the original's own homing
  (`TAG_UNIT=1 test_tag.py`, the missiles' lock pointed at the walking
  capsule): all four hit, 72 damage, at 30, 60 and 144 fps, standing,
  walking away or crossing at 10 m/s and running at 20, arriving after
  7.2..7.7 s.

Checked: `fix_tag_hold_and_beam_in_milliseconds` (the exe's 30/61/145 and
32/62/146 frames with the fix off, a second and 1.067 s with it),
`fix_tag_strike_homes_on_the_unit`; test_tag.py unchanged.

## Discoveries

See `docs/discoveries.md`, Weapons.

## Frame-rate dependence

- Salvos leave one shot a frame.
- A beam grows by (reach - length) x VELOCITY x dt toward its reach: with the
  shipped 10000 it reaches it in one frame at any rate.
- The homing clock stops one frame past LOCK_ON, so the top turn factor of a
  missile with a suspend time depends on that frame's length.
- Beam recharge adds ftol(rate x dt) a frame: at 420 fps and above a plain
  laser never recharges, TAG above 166 fps (or at 144 fps once hot). The
  port carries the fraction (FIX (B), `mech3::beam_recharge`, fixes.md).
- A beam's charge drains ftol(10000 dt) a frame, so it lasts a little longer
  than U_FIRE_TIME, less so the faster the game runs (TAG: 1.067 s at 30 fps,
  1.014 s at 144).
- Homing slows a turning shot per frame (0.8 + 0.2 cos(turn)), so the loss
  depends on the frame rate (likely less at a high one).

## Vehicles and the configuration step

- The vehicle builder 0x4b4660: a vehicle's `weapons` items are (name 1 1
  ammo aim... "firepoint" (names)). The firepoint name is looked up in the
  whole list (0x5569f0 searches nested lists), not the item, so every weapon
  takes the first item's first firepoint name; the node is found by name
  (0x4eb0d0, then again in 0x4b0460 by 0x4abb60). Ammo 0 is an endless bin
  (beams keep theirs); weapons go in group 4, then 0. Errors at lines
  0xd94 / 0xd97 / 0xd9a.
- The mech's configuration step 0x414501..0x414732: the manager reset, each
  weapon item mounted (an error at 0x1b1 when refused), the configuration kept
  at +0x3ec, then each ammo item into the first weapon of its index (weapons
  that use ammo), equipment counted at +0x3ac (jump jets, kind 11, through
  0x417300), the AMS given 100 rounds per AMS, and TAG (kind 13) mounted as
  weapon 0x20 on the first firepoint's node by name, in group 5 (index 4).
  The vehicle constructor 0x4b28d0 finds the TAG's firepoint by the node's
  name (`firepoint01`, so `firepoints[0]`) and its part from the node, as
  BindFirepoints 0x46cb80 does for every firepoint, so the arm check reads a
  real firepoint (an earlier note here guessed it had none; the emulator
  shows it has: `test_tag.py`).

### The vehicles' weapons in the program (2026-10-29)

Until the hooks audit (hooks-audit.md) the program built every vehicle
without weapons, silently, because the builder's two finds by name were
unset. Now:

- **The finds.** `WeaponCalls.node_find` is 0x4eb0d0 (`zcls::node_find`: the
  node, then each child's tree first to last, the name whole; a null node
  reports "Null node pointer."). `node_by_name` is 0x4abb60
  (`node_find_prefix`: the same walk, the name a prefix, strncmp to its
  length). `test_node_find.py` checks both against the exe on the six
  worlds, with made-up trees and the null node.
- **The owner.** A manager's +0x80 is its vehicle's mech: vt+0x2c, then
  vt+0x50, which is 0 for a basic vehicle. Its +0x84 is the vehicle itself.
  - The fire 0x4b3450 takes the shot's owner node, zone and enemies from
    +0x84's entity.
  - The muzzle 0x4b36e0 asks for a firepoint's part only when the weapon's
    firepoint and +0x80 are both set.
  - So a vehicle's shot leaves its weapon's own node (the builder's), aimed
    at the target, and 0x40ef30 is never asked about it.
  - The port had one owner for both, so the update's part lookup crashed on
    a vehicle. Now `vehicle_mech` answers 0 for a basic vehicle, as the
    exe's vt+0x50 does, and `ai_volley_pick` finds the brain through +0x84.
- **The zone.** A shot queries the zones in its shooter's +0x64 record (a
  count, then up to three ids). `entity_x64` copied only the count byte, so
  every shooter's shots queried zone 0 as many times. A shooter outside zone
  0 shot through everything: the vehicles' shots flew through the player.
  The mechs' shots still hit in play (c1m2's Clanner Alpha), so the zones
  they fought in came out right; which shots it cost them is not checked.
- **Checked in play** (`mission_c1m1_c1m2_vehicles_fire_at_the_player_headless`):
  - c1m1: 2 of 7 vehicles armed (the APCs; the trucks carry nothing), 150
    shots, 41 hits on the player.
  - c1m2: 10 of 13 armed (APCs and Bulldogs), 119 shots, 39 hits.
  - Every shot leaves one of its vehicle's weapon nodes (within 2 m) and is
    of a weapon that vehicle carries.
  - `mw3 --mission c1m1 --vehicle-ahead 40 --vehicle-hold` shows an APC
    firing (the player faces it each frame).

## In the program (`mw3 --mission`)

The player's weapons fire, fly and hit in the program (`mech3/app_weapons.cpp`,
`wire_weapons`, called with the mech build's wiring). What they show and play
(muzzle flashes, tracers, impact animations, sounds, impact lights) is
`weapon-effects.md`.

- **Firing**: Mech::update's `weapons_fire` / `weapons_update` run the
  manager's trigger (0x004b0c20, the controls' fire byte, CMD_TRIGGER1
  Space) and update (0x004b0c30); the manager's calls into the mech are the
  port's: parts by id (0x0041d510), their nodes, the node's world point
  (0x004ed650) and facing (node +0x38 +0x78), the entity's velocity, zone
  byte (+0x64) and enemies (+0xc8), the configuration, heat and add_heat
  (0x00419e40). The shots and beams are zWeapon's ports (0x00585010,
  0x00584a70).
- **The aim**: each frame the HUD (0x0041cd40) hands the mech its reticle's
  point (screen +0x1d48) through 0x0041bc20 (`Mech::aim_at`, ported: a
  point behind the torso's half depth along the facing clears the aim; the
  torso's own aim 0x00465ea0 is a hook, not ported), which sets the
  manager's aim (0x004b0bc0). **Every weapon aims at that point**
  (0x004b3190 passes it unconditionally): the HUD is not ported, so without
  it the shots go toward the aim's last value (the world's origin at
  first). The headless test sets it as the reticle would.
- **zWeapon's engine calls** (`RunHooks`): the scene graph, object3d, the
  impact lights (zClass's lights and the world's light list), the node
  handler slots (node +0xbc, `Node::damage`), and the shot queries
  (`segment_list` 0x004eed90, the nearest hit 0x004eece0, their options
  0x004ed9d0 / 0x004ed9e0 / 0x004eda00; movement-collision.md). The load
  makes the projectile pool's nodes (`InitHooks::pool_objects`,
  0x005847c0). Animations, sounds, craters, surface marks, the ground probe
  0x004eda10 and the network are unset.
- **Hits** (`HitHooks`): a node's handler table, its damage slot called on
  the hit; the splash query is `sphere_query` 0x004f0ac0 (ranged, sighted).
  A mech's node gets its damage handler at build (0x005833b0 with
  0x004161a0: vtable +0x164, `Mech::take_damage`), handed down its subtree;
  the hit node's part and owner (0x0040ea10, 0x0040e9d0) by the +0x28 id
  and +0x40 owner the build stamps on each part's subtree.
- **The frame's step** [0x0072c52c] is one global in the exe and a copy in
  the mech, weapon and zWeapon modules here: the program copies it into
  each before the vehicles update (`sync_frame_step`); without it the shots
  never moved.
- **Heat**: only with the Heat option on (0x0040cfc0, now wired;
  `HeatCalls::heat_option`). Machine guns make none (HEAT 0 in weapons.zrd).
- 0x0041aba0 on the player tick: the ammo gauge and the last attacker
  cleared; the target's chosen part 0x0041d6f0 (HUD) is logged when there
  is a target.

`mw3tests` `mission_c1m1_player_fires_on_the_owens_headless`: the Mad Cat,
moved 80 m west of the powered-down Owens and turned to it, holds the fire
key with the reticle on the Owens: shots in the air, the Owens' armor drops
(machine-gun hits of 0.8 on its torso); then a group that makes heat (F1..F5
through the manager: the command handler that takes the keys is not
ported) heats it.

Left on this path: the HUD (its reticle, the aim's source; the gauges), the
mech's command handler (groups, the power key), the torso's aim 0x00465ea0,
the target's part 0x0041d6f0, effects and sounds of shots and hits, the
craters.

## Left to port

- Ported 2026-10-22: the AI's volley pick 0x4b1860 with 0x4b1370..0x4b17d0
  (`mech3/weapon_planner`, ai.md 17.7; with the HUD's three); 0x4b1170
  builds a static nothing reads.
- The startup 0x583750's network registration (0x5a8a20 with 0x583850,
  only in the network mode 0x583740). The glow objects 0x5869d0 / 0x586a30
  / 0x586af0 (the vehicles' freeze and heat effects) are ported in
  gamez/zweapon and wired in app_vehicle.cpp (vehicles.md, The glow).
- Ported 2026-10-21: the startup 0x583750 (`zwep::startup`: the globals
  back to their first values, the definitions and pool emptied; it returns
  0, the start's "zWepInit: PASSED"), the network mode 0x583740, and the
  mission cleanup's 0x584fe0 (`kill_every_shot`: every definition's live
  shots killed through 0x584fb0); the fire test plants a shot before its
  cleanup and finds none after.
- The engine pieces zWeapon calls, hooked here: cls_di.c's queries (ported
  separately), the crater module (0x4ff920 / 0x4ffaa0 / 0x5020f0 / 0x502110),
  object3d and the animations.
- Wiring: the mission load runs `zwep::init` (app_mission.cpp, 2026-10-20),
  which registers `zwep::tick` as the weapon_tick object's action (priority
  3); the Core state's action lists run it each frame (`action-lists.md`). The
  mech's own setup (its constructor and 0x414430, at launch: mission-start.md
  section 10) still has to call `configure_weapons` and build the AMS.
