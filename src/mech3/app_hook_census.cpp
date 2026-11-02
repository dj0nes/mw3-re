// The hooks the program leaves unset where the port would skip the call
// (hook_census.inc, kept by hand from tools/analysis/hookaudit.py): a
// stand-in each, which tells the census under the exe address it stands for
// and returns what the skip gives (nothing, 0, null, false). Put in at each
// mission's start (Missions::begin's end), after every wiring has run, and
// only where the field is still null (hooks-audit.md). Not original code.
#include <type_traits>

#include "mech3/mech.h"
#include "gamez/zclass/camera.h"
#include "gamez/zdeclient/zdec_init.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zweapon/zweapon.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_control.h"
#include "mech3/mech_dead.h"
#include "mech3/mech_hud.h"
#include "mech3/mech_move.h"
#include "mech3/mech_parts.h"
#include "mech3/mech_pocket.h"
#include "mech3/mech_skin.h"
#include "mech3/mech_skis.h"
#include "mech3/pocket_panel.h"
#include "mech3/mech_net.h"
#include "mech3/weapon.h"
#include "mech3/zainetedit.h"
#include "mech3/zrdraiv.h"

namespace mech3 {

void hook_census_report(const char* key);  // app_mission.cpp: the engine's census

namespace {
template <const char* Key, class F>
struct CensusStub;
template <const char* Key, class R, class... A>
struct CensusStub<Key, R (*)(A...)> {
  static R call(A...) {
    hook_census_report(Key);
    if constexpr (!std::is_void_v<R>) return R{};
  }
};
#define HC_CAT2(a, b) a##b
#define HC_CAT(a, b) HC_CAT2(a, b)
#define SKIP(acc, f, key) constexpr char HC_CAT(kHookKey, __LINE__)[] = key;
#include "mech3/hook_census.inc"
#undef SKIP
}  // namespace

void hook_census_install() {
#define SKIP(acc, f, key) \
  if (!acc().f) acc().f = &CensusStub<HC_CAT(kHookKey, __LINE__), decltype(acc().f)>::call;
#include "mech3/hook_census.inc"
#undef SKIP
}

}  // namespace mech3
