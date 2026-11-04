#pragma once

namespace zcls { struct Node; }

// C:\zipper\branchep1\gamez\zClass\Class.c: Pirate's Moon's changes to a new
// node and to the active switch (MW3's: gamez/zclass/cls_node.cpp). Shared
// code reaches them through the game table (zcls::node_alloc, node_set_active).
namespace pm::zcls {

// Pirate's Moon 1.0 @ 0x004fc550: MW3's defaults, and link bit 25 set, the
// virtual partition 0 (MW3's +0x50 is -1) and the box pointer at the first box.
::zcls::Node* node_alloc();
// Pirate's Moon 1.0 @ 0x004fca50: MW3's, with a light's static bit at
// PM's 0x200 (+0xb0 of the shared Light, PM's +0xe0).
int node_set_active(::zcls::Node* node, int on);
// Pirate's Moon 1.0 @ 0x004fef40: MW3's on PM's model. Its 0x0055b1c0 and
// 0x0055b360 read the same header offsets as MW3's, so zmod::model_box is shared.
int node_model_box(::zcls::Node* node);

}  // namespace pm::zcls
