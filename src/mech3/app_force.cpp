#include "mech3/force_feedback.h"

#include "gamez/zmath/zmth_matrix.h"
#include "mech3/mech.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_move.h"
#include "mech3/mech_net.h"
#include "mech3/mech_parts.h"
#include "mech3/weapon.h"

// The program's side of the force feedback (ours): the game's calls to the
// object at [0x00608620] (the mechs', the weapons', the damage's, each gated
// on the mission player and the object's flag as the exe's are) go to the
// port while one is up.
namespace mech3 {
namespace {
ForceFeedback* g_force = nullptr;
}  // namespace

void program_force_feedback(ForceFeedback* f) {
  g_force = f;
  world().force_feedback = weapon_world().force_feedback = f != nullptr;
  net_calls().ff_stop = [] { if (g_force) g_force->stop_all(); };  // the network end's 0x00433530
  net_calls().ff_spawn = [] { if (g_force) g_force->spring(true); };  // the network launch's 0x004338a0(1)
  net_calls().ff_respawn = [] {  // the respawn's 0x004338d0(0), 0x004338a0(1)
    if (g_force) g_force->dead_spring(false), g_force->spring(true);
  };
  if (!f) return;
  damage_calls().ff = [](uint32_t fn, int arg) {  // the death's (0x00433530, 0x00433900, 0x004338d0), the fall's
    if (!g_force) return;
    switch (fn) {
      case 0x433530: g_force->stop_all(); break;
      case 0x433900: g_force->dead_dance(arg != 0); break;
      case 0x4338d0: g_force->dead_spring(arg != 0); break;
      case 0x433840: g_force->fall(); break;
      default: break;
    }
  };
  damage_calls().ff_hit = [](int w, const Vec3* i, float force) {
    if (g_force && i) g_force->hit(w, &i->x, force);
  };
  weapon_calls().ff_weapon = [](int32_t kind) { if (g_force) g_force->weapon(kind); };
  move_calls().ff_jets = [](int on) { if (g_force) g_force->jets(on != 0); };
  move_calls().ff_landing = [](float j) { if (g_force) g_force->landing(j); };
  move_calls().wading_effect = [](int on) { if (g_force) g_force->water(on != 0); };
  part_calls().footstep_ff = [](float speed) { if (g_force) g_force->footstep(speed); };
  collide_calls().ff_jolt = [](const float n[3], float a) { if (g_force) g_force->jolt(n, a); };
}

}  // namespace mech3
