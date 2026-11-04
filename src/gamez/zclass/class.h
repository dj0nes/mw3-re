#pragma once
#include "gamez/zclass/cls_node.h"

// zClass\class.c: a node's matrix to the world and its box's world corners
// (docs/render.md section 1, docs/drafts/scenewalk.md).
namespace zcls {

// gwNodeUpdate (ECX node): a changed node's model box (+0x2c bit 4) and its
// children's box (bit 2) rebuilt, its local matrix from its euler angles
// when dirty, then its bounds (+0x74) and its place in its world's cells;
// its ancestors (not worlds) are marked changed for theirs. A world pushes
// its fog and area updates, a sound starts or moves. Flag 2 and the item's
// hold on list 7 are released. 3 for a class with no update.
int node_update(Node* node);        // 0x004eca00
// A test's observer, told of each node_update before it runs (null in the game).
using NodeObserver = void (*)(Node* node);
NodeObserver& node_update_observer();
int node_model_box(Node* node);     // the running game's (zgame::Table)
int mw3_node_model_box(Node* node);  // 0x004ed220  +0x8c from the model, flag 0x200
int node_children_box(Node* node);  // 0x004ecf70  +0xa4 from the children's, flag 0x400
int node_bound_box(Node* node);     // 0x004ecc00  +0x74 = model box u children's box, flag 0x100

// The frame update of changed nodes (list.cpp: 0x004f7e00, 0x004f7f70,
// 0x004f7e50). The callers reach them through these hooks, the port by
// default; a test may stand others in (null skips the call).
void update_node(Node* node);       // 0x004f7e00
void frame_update();                // 0x004f7f70
int update_changed();               // 0x004f7e50
struct UpdateHooks {
  void* ctx = nullptr;
  void (*update_node)(void* ctx, Node* node) = [](void*, Node* n) { zcls::update_node(n); };  // 0x004f7e00
  void (*frame_update)(void* ctx) = [](void*) { zcls::frame_update(); };                     // 0x004f7f70
  void (*pending)(void* ctx) = [](void*) { zcls::update_changed(); };  // 0x004f7e50: the nodes queued at [0x0060bea0]
};
UpdateHooks& update_hooks();

// ECX node, EDX multiply mode (0x0051fee0): the top of the matrix stack
// becomes node-to-world times what it held. 0 ok, 1 several parents, 3 a
// class with no matrix, 5 null node.
int node_to_world(Node* node, int mode);   // 0x004ed280
// ECX node, EDX out: its box (+0x74..+0x88) corners through its local matrix
// and the top of the stack. 0 ok, 1 no box (flag 0x100), 3 bad class, 5 null.
int node_box_corners(Node* node, float out[8][3]);  // 0x004ebdb0
// The same box's eight corners as (x, z) pairs, for the ground and segment
// queries' box tests (cls_di.c): bottom (min x, max z), (max x, max z),
// (max x, min z), (min x, min z), then the top in that order.
int node_box_xz(Node* node, float out[8][2]);       // 0x004ec150
// The same box through the same matrices, axis aligned again, in x and z
// only: out[0] min x, out[2] min z, out[3] max x, out[5] max z (the point
// query's box test).
int node_box_extent_xz(Node* node, float out[6]);   // 0x004ec430
// The same in x, y and z (out: min xyz, max xyz), the shot queries' box test.
int node_box_extent(Node* node, float out[6]);      // 0x004ec6b0
// ECX node, EDX out: the node's world rotation as euler angles (0x00523bc0)
// and its world position, from an object3d's cached world matrix (+0x60) when
// it is valid, else through node_to_world. 0 ok, 1 null node.
int node_world_euler(Node* node, float out[3]);     // 0x004ed5d0
int node_world_position(Node* node, float out[3]);  // 0x004ed530
// ECX node, EDX point: a point in the node's frame to the world, in place;
// (0, 0, 0) gives the node's world position. 1 for a null node.
int node_world_point(Node* node, float p[3]);       // 0x004ed650
// 0x004ec9c0: the node's box (+0x74: min, max) when it has one (flag 0x100), else null.
const float* node_box(const Node* node);
int node_world_frame(Node* node, float p[3], float out[3]);  // 0x004ed6e0, also the angles

}  // namespace zcls
