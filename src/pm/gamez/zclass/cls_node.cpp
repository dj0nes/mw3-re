#include "pm/gamez/zclass/cls_node.h"

#include <cstdio>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/sound.h"
#include "gamez/zerror/zerr.h"

namespace pm::zcls {

using ::zcls::Node;

namespace {
const char kClassFile[] = "C:\\zipper\\branchep1\\gamez\\zClass\\Class.c";
}  // namespace

Node* node_alloc() {
  ::zcls::NodePool& p = ::zcls::nodes();
  if (p.free_head == -1) ::zcls::node_pool_grow();  // 0x0050c790, MW3's 0x004f8c80
  if (p.free_head == -1) ::zcls::gamez_abort("GameZ node resource failure: restarting your game usually solves this.");
  Node* n = ::zcls::node_at(p.free_head);
  p.free_head = static_cast<int32_t>(n->link << 8) >> 8;
  const uint32_t link = n->link;
  *n = Node{};
  n->link = link | 0x2000000;
  ++p.live;
  ::zcls::push_front(::zcls::kAllNodes, n);
  n->set_flags(0x0108001c);
  n->put<int32_t>(0x44, 1);
  n->put<int32_t>(0x4c, -1), n->put<int32_t>(0x50, -1);  // PM's int16 area x, z at +0x4c/+0x4e
  n->raw[0x30] = 0xff;
  std::snprintf(reinterpret_cast<char*>(n->raw), 0x24, "%s", "Default_node_name");
  n->put<int32_t>(0xbc, 0);
  n->pm_box = reinterpret_cast<const float*>(n->raw + 0x74);  // PM +0x70
  return n;
}

int node_set_active(Node* node, int on) {
  if (!node) return zerr::report(0x400, kClassFile, 0x4cd, "Null node pointer."), 5;
  switch (node->cls()) {
    case 1: case 2: case 5: case 6:
      break;
    case 9: {
      auto* light = static_cast<::zcls::Light*>(node->data);
      if (light->get<uint32_t>(0xb0) & 0x200) {  // PM +0xe0
        const uint32_t f = on ? ~node->flags() : node->flags();
        if ((f >> 2) & 1) ::zcls::node_touch_frame();  // 0x004ff750
      }
      break;
    }
    case 10:
      ::zcls::sound_set_active(node, on);  // 0x00514fe0, MW3's 0x004fd520
      return 0;
    default: {
      char msg[120];
      std::snprintf(msg, sizeof msg, "gwNodeSetActive(): Unrecognized node class type:  node = %s class_type = %d\n",
                    node->name(), node->cls());
      zerr::report(0x400, kClassFile, 0x4e9, msg);
      return 3;
    }
  }
  if (on == 1) node->set_flags(node->flags() | 4);
  else if (on == 0) node->set_flags(node->flags() & ~4u);
  return 0;
}

}  // namespace pm::zcls
