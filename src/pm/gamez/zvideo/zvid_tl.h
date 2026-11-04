#pragma once
#include "pm/gamez/zmodel/gmod_draw.h"

// Pirate's Moon's draw table for the model draw (the exe's 0x0059d450..0x005a0ea0,
// file unknown; zvid_ddd3d.c's successor): MW3's device calls, with the vertices
// ready (docs/pm-map.md, The device side).
namespace pm::zvid {

// One call of the model draw to the device. FIX (A): the device calls go to Render3D.
void device_call(const ::pm::zmod::DeviceCall& d);

}  // namespace pm::zvid
