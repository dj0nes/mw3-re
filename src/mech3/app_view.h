#pragma once
#include <cstdint>

// The program's player view (our addition, docs/view.md section 6): the port
// (mech3/player_view) made where the mission load makes the view and called
// where the Core state, the launch and the options call it; its hooks onto
// zClass, the mech's ports and zReader. Fields the app code reads by address
// ([0x005f4c8c] +0x58 the mode, its +4 the id; +0x64 the target) are copied
// into the view's block after each change.
namespace mech3 {

struct Mech;
struct Vehicle;
class CameraMode;
class PlayerView;

// What the glue asks of the engine (GameEngine, which cannot share a file
// with mech.h's types).
struct ViewProgram {
  void* ctx = nullptr;
  uint32_t (*unported)(void* ctx, const char* key) = nullptr;  // logged once
  int32_t (*hardware)(void* ctx) = nullptr;                    // [0x0072f348]
  void (*sync)(void* ctx) = nullptr;                           // after each change
};
void wire_view(const ViewProgram* p);       // null: unwired, the view dropped
int32_t program_view_mode_id();             // the view's camera id, -1 without one
bool program_view_look(int32_t* look);      // its camera's +0x40 (0, 1 the side views); false without one
void* program_view_target();                // its target: a mech, or the stealth object (program_stealth())
Mech* program_view_mech();                  // its target as a mech: null while the view is on the stealth
const uint8_t* program_view_camera_zones();
// For the tests: the view's mode id (-1 none) and the cockpit camera's +0x64
// (fixed: no mech rotation, set through a fall, 0x00421d50).
int32_t program_view_mode(bool* fixed);  // its camera's zone list ([0x005f4c8c] +4, data +0x1e4), null without
// For the tests: the camera node's world position and the target's torso
// node's; false without a view, a target or a torso.
bool program_view_positions(float camera[3], float torso[3]);
PlayerView* program_view();
void program_view_make(void* camera_node);  // 0x0040aca0
void program_view_free();                   // the view deleted (the mission unload's operator delete)
void program_view_attach(Mech& m);          // the mech's 0x0041ce30
// 0x0040adf0 onto a vehicle that is not a mech (the stealth's vt+0x108,
// 0x004acd90): `handle` how the program names it.
void program_view_attach_vehicle(Vehicle& v, void* handle, CameraMode* mode);
void program_view_detach();                 // 0x0040adf0(view, 0, 0): no target, no mode
void program_view_set_camera(int32_t id);   // 0x0040ae70
int32_t program_view_camera_id();           // the view's +0x58 +0x04, -1 without one
void program_view_look(int32_t look);       // [0x005f4c8c] +0x58 vt+0x10 (the free look)
int32_t program_view_look_state();
// For the tests: the camera put at (x, y, z), then the view's ground and water
// check (0x0040af30); the camera's y after it, whether it found ground, and
// whether it is under water (the world taking the water's fog).
bool program_view_ground_probe(float x, float y, float z, float* y_after, bool* under);
// For the tests: a point within `range` of (x, z) where water (soil 1) lies
// over the ground: its x, the surface's y, its z, and the ground's y under it.
bool program_view_find_water(float x, float z, float range, float out[4]);          // the mode's free look (+0x40), -1 without a mode
void* program_cameras(int32_t roster);       // 0x00430e70
void program_cameras_free(void* list);      // 0x0040abf0
// The options' camera choice (0x0040ce70 / 0x0040ce10; defined with the options).
int32_t program_camera_option();
const float* program_mission_fog(bool water);  // world.zrd's fog: range 2, altitude 2, colour 3 (app_mission.cpp)
void program_camera_set(int32_t mode);
int32_t program_mouse_mode();
void program_set_mouse_mode(int32_t v);  // [[0x005f4d44]], the options' MouseMode: the controls' view state

}  // namespace mech3
