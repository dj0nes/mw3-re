#pragma once

namespace zcls { struct Node; }

// C:\zipper\branchep1\gamez\zClass\Light.c (MW3's: gamez/zclass/light.cpp).
namespace pm::zcls {

// Pirate's Moon 1.0 @ 0x0050e470: PM's 0x100-byte light (the shared Light,
// its colours in pm_colors) with PM's defaults, active, on list 9. Shared
// code reaches it as zcls::light_create.
::zcls::Node* light_create();

}  // namespace pm::zcls
