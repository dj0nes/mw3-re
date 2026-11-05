#pragma once
#include <cstdint>

#include "mech3/mech_parts.h"

// The mech's command handler (mech_command.cpp, docs/controls.md, The
// mech's commands): the calls it makes into code not ported (the HUD, the
// view's camera, lance orders and a few of the mech's own). Unset ones are
// recorded through `unported` when set, else do nothing. The screen and the
// sounds are heat_calls()'s.
namespace mech3 {

struct Mech;
struct MechControls;

struct CommandCalls {
  uint32_t (*unported)(const char* key, int32_t cmd) = nullptr;  // records a command that reached unported code
  uint32_t (*handle)(Mech&) = nullptr;                 // the mech as targets.zrd's hooks see it (0: none)
  // For the differential test: the mech's and its manager's own calls
  // (by address, with their arguments) answered here instead of made.
  uintptr_t (*own)(uint32_t addr, uintptr_t a, uintptr_t b) = nullptr;
  void (*torso_center)(Mech&, int32_t how) = nullptr;  // 0x00465a00 on the torso (1, 2)
  void (*view_camera)(int32_t mode) = nullptr;        // [0x005f4c8c] +0x58 vtable +0x10
  void (*set_view_option)(int32_t v) = nullptr;        // [[0x005f4d44]] (MouseMode) = the controls' view state
  // The HUD's keys on the view's screen (null for 0x15 off the view): 0x15 the
  // zoom lock (0x00442e00), 0x43 the objectives, 0x49 the mech's target set
  // (0x0041d890's HUD part: TARGET on, 0x00441780), 0x68, 0x6b, 0x74, 0x75,
  // 0xa5 (docs/controls.md).
  void (*hud)(void* screen, int32_t cmd) = nullptr;
  // Z and Q (0x14, 0x48): the node the reticle's aim hit (0x00444330 on the
  // HUD's reticle, the record's +0x24), the game object it belongs to
  // (0x0040eee0, as a handle; 0: none), its team (vt+0x28 +0xc0) and whether
  // it refuses the pick (vt+0xac for Z, vt+0xb0 for Q: a mech never, a
  // targets.zrd object without zoom_select / reticule_select).
  void* (*hud_aimed_node)(void* screen) = nullptr;
  uint32_t (*node_entity)(void* node) = nullptr;
  uint32_t (*entity_team)(uint32_t entity) = nullptr;
  bool (*entity_refuses)(uint32_t entity, bool zoom) = nullptr;
  void* (*hud_weapon)(void* screen) = nullptr;         // +0x1795c: the weapon the HUD highlights
  // 0x00479480 on [0x0060861c] (multiplayer: none) then 0x00469930 on +0x37c:
  // the controls an order makes, null when none.
  MechControls* (*lance_order)(Mech&, int32_t order) = nullptr;
  void (*target_part)(Mech&, int32_t part) = nullptr;  // 0x0041d810; unset: Mech::target_part
  const float* (*seen_position)(uint32_t seen) = nullptr;
  // [0x00609980], the mission's +0xb0: multiplayer's stations (the op
  // points); null outside multiplayer, where N, Shift+N and Ctrl+N do nothing.
  void* (*stations)() = nullptr;  // 0x005a8630 on a contact's +0x04 (an ai::Handle)
  void (*other)(Mech&, int32_t cmd) = nullptr;         // 0x36..0x38 ([0x00609980]), 0x3f (0x0041d070), 0x71 (vtable
                                                       // +0xb8, 0x0041ded0), 0x73 (0x0047a910), 0x76 / 0x77 (0x0041e4e0),
                                                       // 0xa6 (0x0040f070)
};
CommandCalls& command_calls();

}  // namespace mech3
