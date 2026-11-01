#pragma once
#include <cstdint>

#include "mech3/veh_basic_object.h"

// The vehicles' movement (docs/vehicles.md, The movement). Original file:
// these sit in 0x004a1000..0x004a3000, before mech3\veh_basic.cpp's named
// functions; named after the movement. The frame time and its inverse are
// [0x005e8110] / [0x005e8114] (the player tick's), through the hooks.
namespace mech3 {

// Mech3.exe 1.2 @ 0x004a28c0: while turning to the controls' heading (x760),
// a turn command cancels it; else a full turn a frame (turns[1] x dt) while
// the heading is further than that, then the yaw set to it and the turn 0.
void vehicle_turn_to_heading(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004a2890: zEntity's refresh, then the controls' up as
// the matrix's second row.
void vehicle_up_refresh(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004a2b10: the local speed from the throttle (rates[0],
// damped by rate_damping, held to |throttle| x top_speed) and the slide
// damped away; sliding, 0x004a2dd0 instead.
void vehicle_speed_step(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004a2dd0: sliding, the world velocity into the local
// one, the speed as above (held to top_speed), the side force applied, the
// slide ending when it turns over.
void vehicle_slide(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004a2c90: the side force, the slope's (x 60) and the
// turn's against the friction (friction[0] to start a slide, [1] in one).
float vehicle_side_force(BasicVehicle& v);

// The track's ground following (2c2; docs/vehicles.md, The suspension).
// Mech3.exe 1.2 @ 0x004a23f0: the supports' world places (the motion's
// support[8..], from support[0..]), the ground under each (one query from
// 500 up, the vehicle's node left out), each one's ground height (the pick,
// step 4, water not ground) into heights[], its slope against the world
// velocity into slopes[], the highest water surface into *top, a count by
// the pick's water flag into counts[100]; support 0's polygon gives the zone.
// False when a support has no ground under it.
bool vehicle_support_ground(BasicVehicle& v, float* heights, float* top, float* slopes, int32_t* counts);
// Mech3.exe 1.2 @ 0x004a2820: a support's height *y and vertical speed *vs
// a frame on (gravity g): falling while above ground (*air), landing at the
// ground with `landing` for its speed, else following it.
void vehicle_support_step(float* y, float* vs, float ground, int32_t* air, float g, float landing);

// BASIC, the stealth object's movement (editor.md S2; vehicles.md, The
// stealth object): its step 0x004a1d90, steering 0x004a20d0, the overhead
// camera's panning 0x004a2000, ground 0x004a1f00 over the supports'
// 0x004a2190.
void vehicle_basic_step(BasicVehicle& v);
void vehicle_basic_steer(BasicVehicle& v);
void vehicle_basic_pan(BasicVehicle& v);
void vehicle_basic_ground(BasicVehicle& v);
void vehicle_platform_ground(BasicVehicle& v, float* heights, float* top, int32_t flag, int32_t* counts);
// Mech3.exe 1.2 @ 0x004a1760 (TRACK): the top speeds (slowed by quicksand
// or lava under two supports, which the pick never reports: never), the
// supports stepped, the pitch and roll rates from three of them (or, all in
// the air, back toward level), the plane through the three for the up, the
// position on it, the matrix, pitch and roll; in the air both held to 30
// degrees. 0 when a support has no ground.
int32_t vehicle_suspension(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004abf70: pos's height on the plane through `at` with normal n.
void height_on_plane(float pos[3], const float at[3], const float n[3]);
// Mech3.exe 1.2 @ 0x004a3e40: the forward (+0x18) tilted onto the up.
void vehicle_forward_from_up(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004a3ed0: the matrix from forward and up: (forward x up,
// up, -forward, the position).
void vehicle_matrix_build(BasicVehicle& v);

// The frame (2c3; docs/vehicles.md, The frame).
// Mech3.exe 1.2 @ 0x004a12c0 (TRACK): the heading's turn, the turn rate
// (in the air: damped by e^-dt), the yaw kept within +-2 pi, the matrix
// from the angles, the speed (in the air: damped by e^(-dt/2)), the world
// velocity and the position moved by it, the collision points placed, the
// collisions (unless the controls' vt+0x20), the suspension, the shadow
// (shown or hidden once two frames agree), the node's rotation and matrix,
// the points again (their copies at +0x160), the last matrix and forward
// kept, the tracks' textures scrolled by the speed and turn.
void vehicle_track_step(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004a1240: unless frozen, the mode's step (BASIC
// 0x004a1d90, TRACK above; the others none), then the orientation turned
// from (0, 0, -1) onto the forward (0x00525a50) and the controls' world and
// local velocities copied to the entity (+0x30, +0x3c).
void vehicle_move(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004addc0: the motion animation runs while the
// throttle's size is past 0.9 and stops the frame after (its hold,
// [0x0060b15c], is never set).
void vehicle_motion_anim_step(BasicVehicle& v, float throttle);
void vehicle_weapons_step(BasicVehicle& v, float dt);  // 0x004b4880
// Mech3.exe 1.2 @ 0x004a1040 (vt+0x34): the controls' frame (IControls
// vt+0x0c); destroyed, the vehicle inactive, its engine sound and motion
// animation off; else, on a path after a frame that answered 1, the
// movement, the motion animation by the throttle, the player's lock
// position into the controls (+0x6f0), the weapons, zEntity's refresh.
void vehicle_frame(BasicVehicle& v, float dt);

// The collisions (2c4; docs/vehicles.md, The collisions).
// Mech3.exe 1.2 @ 0x004a0180: which collision points to sweep from last
// frame's place (+0x160) to this one (+0xac): turning more than moving, the
// corners (0, 2, 3, 4, 5); moving, the front (0, 1, 2) or back (3, 5) and 4,
// and sliding past 1 a side's three (6..8 right, 9..11 left) placed afresh
// by this and last frame's matrices; then the query (0x004a0910) into the
// three lists and +0x36c (none crossed).
void vehicle_collision_sweep(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004a0910: the world's segments with the vehicle's own
// node left out, each crossing sorted (0x004a09e0): an object's into
// `objects`, else by its normal's y a ceiling (below -0.9) or a wall (below
// 0.71); a floor of soil 5 sets +0x2c. True when none.
bool vehicle_collision_query(BasicVehicle& v, float* segs, int32_t points);
// Mech3.exe 1.2 @ 0x004a0f50: a ceiling: rising speeds turned down, the
// height back to last frame's, the points' heights with it.
void vehicle_ceiling_response(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x0049ff80: the sweep, then each list's response by its
// head (objects and walls 0x004a0b40, ceilings above), the lists freed
// (0x004a0100); without an object or wall, a remote side's collision
// record of this or the last four frames answers as a wall.
void vehicle_collisions(BasicVehicle& v);
// Mech3.exe 1.2 @ 0x004a0b40: a wall or object: the hit node's touch (damage
// 1); nothing more against a flat normal; else, all flattened, the vehicle
// moved out of it (the segment's end reflected about the normal from the
// crossing), the points with it; the velocity the slide along it plus
// collision_d[0] of the bounce off it (its height kept), the local velocity
// from it, and the turn kicked by collision_d[1] x the speed toward the
// side hit.
void vehicle_wall_response(BasicVehicle& v, const CollisionHit& hit);

}  // namespace mech3
