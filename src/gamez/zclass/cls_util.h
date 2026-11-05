#pragma once
#include <cstdint>

// The bounding sphere of eight box corners. Original file unknown: it sits
// between zClass\cls_util.c and Window.c, most likely cls_util.c.
namespace zcls {

// ECX corners, EDX centre, stack &radius: the centre of their axis-aligned
// box and its half-diagonal through the approximate square root.
void corners_sphere(const float corners[8][3], float centre[3], float* radius);  // 0x004f9c40

// ECX box (min xyz, max xyz), EDX centre, stack &radius: the box's centre
// and half-diagonal (the partitions' sphere after the area update).
void box_sphere(const float box[6], float centre[3], float* radius);  // 0x004f9bc0

struct Node;

// Copying trees (cls_util.c, docs/mechlib.md "Copying a tree"). How a
// copy takes its nodes' models: shared, or each one copied (and then each
// polygon's material too, unless `shared` and it does not change).
struct CopyState {
  int32_t copy_models = 1;     // 0x005ea838
  int32_t own_materials = 0;   // 0x0060bf40
  int32_t shared = 0;          // 0x0060bf44
};
CopyState& copy_state();
// ECX node, EDX copy_models, stack own_materials: the tree copied with
// those settings (restored after). 0 for a null node.
Node* tree_copy(Node* node, int32_t copy_models, int32_t own_materials);  // 0x004f9af0
// Pirate's Moon 0x0050d830 sets the model copy too (MW3's 0x004f9b50 kept it).
Node* tree_copy_materials(Node* node, int32_t copy_models, int32_t own_materials, int32_t shared);
// One node and its children by class: object3d, LoD and camera; a node
// marked shared (flag 0x4000000) is handed out itself; worlds, lights,
// sounds, sequences, animates and switches are refused (reported, 0).
Node* node_copy(Node* node);                                // 0x004f99f0
int copy_node_base(Node* src, Node* dst);                   // 0x004f9190 ECX src, EDX dst
int copy_node_model(Node* src, Node* dst);                  // 0x004f90e0 ECX src, EDX dst

// A node without parents and its whole tree below (each child that has no
// other parent), models let go: 0 gone, 1 held, else an error.
int tree_destroy(Node* node);  // 0x004f9050

// The engine's square root guess (bits / 2 + 0x1fc00000), up to ~6% off.
float approx_sqrt(float x);

}  // namespace zcls
