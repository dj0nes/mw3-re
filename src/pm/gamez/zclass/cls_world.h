#pragma once
#include <cstdint>

namespace zcls { struct Node; struct Partition; struct World; }

// C:\zipper\branchep1\gamez\zClass\cls_world.c's and Class.c's changes in Pirate's Moon: a node's range of
// cells (docs/pm-map.md). A node fitting one cell is on that cell's list with its y range; one spanning
// more is among the world's children, with the range kept (shared node +0x4c, +0x50, pm_virtual).
namespace pm::zcls {

// Pirate's Moon 1.0 @ 0x004fe740: the box from the model's and the children's, by the pointer +0x70
// (pm_box), +0x74 only for the union; each world parent refiles the node.
int node_bound_box(::zcls::Node* node);

void world_box_range(const ::zcls::World* w, const float box[6], int16_t range[4]);  // 0x00505300
int world_touch_node(::zcls::Node* world, ::zcls::Node* node, const float box[6]);   // 0x00505550
int world_remove_node(::zcls::Node* world, ::zcls::Node* node);                      // 0x00506000
int world_link_node(::zcls::Node* world, ::zcls::Node* node, const float box[6], const int16_t range[4]);  // 0x00505c80
int node_remove_child(::zcls::Node* parent, ::zcls::Node* child);                    // 0x004fd8d0
void node_set_tree_valid(::zcls::Node* node, int on);                                // 0x004ff660

}  // namespace pm::zcls
