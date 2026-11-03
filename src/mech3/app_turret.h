#pragma once
#include <cstdint>

#include "mech3/app_mech.h"

// The turrets in the program (app_turret.cpp; docs/vehicles.md, The
// turrets in the program). Ours, not the exe's.
namespace mech3 {
namespace tur {
class Turret;
}

void wire_turrets(const MechWiring* w);   // from wire_mech_build; null: the defaults again
tur::Turret* program_turret(const void* p);          // one of this mission's turrets, else null
tur::Turret* program_turret_by_handle(uint32_t h);
bool program_turrets_load(const char* file);  // 0x004a76b0 (the mission load's)
bool program_turret_free(void* p);        // its vt+0x00 from the entity list's walk; false: not a turret
void program_turrets_end();               // 0x004a7970
void program_turret_frame(void* p);       // vt+0x34 (0x004a6be0)
void* program_turret_marker(int32_t side);  // vt+0x3c (0x004a5fc0)
void program_turret_dt(float dt);         // for the tests: the frame time a turret's aim eases by ([0x0072c52c])

}  // namespace mech3
