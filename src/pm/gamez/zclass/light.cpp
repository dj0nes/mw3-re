#include "pm/gamez/zclass/light.h"

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"

namespace pm::zcls {

using ::zcls::Node;

namespace {
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zClass\\Light.c";
}  // namespace

// PM's offsets: +0x00..+0xaf as MW3's, +0xb0..+0xdf pm_colors, +0xe0 on
// MW3's +0xb0 on.
Node* light_create() {
  Node* node = ::zcls::node_alloc();
  if (!node) return zerr::report(0x400, kFile, 0xd5, "Null node pointer."), nullptr;
  node->put(0x74, 1.0f), node->put(0x78, 1.0f), node->put(0x7c, -2.0f);  // through the box pointer (+0x70)
  node->put(0x80, 2.0f), node->put(0x84, 2.0f), node->put(0x88, -1.0f);
  node->set_flags(node->flags() | 0x100);
  node->put<int32_t>(0x34, 9);
  auto* l = new ::zcls::Light{};
  node->data = l;
  l->put(0x84, 0.0f), l->put(0x88, 1.0f), l->put(0x8c, 0.0f);
  l->put(0x60, 0.0f), l->put(0x64, 0.0f), l->put(0x68, 0.0f);
  l->put(0xa4, 1.0f), l->put(0xa8, 1.0f), l->put(0xac, 1.0f);
  l->put(0x9c, 0.0f), l->put(0xa0, 1.0f);
  const float colors[12] = {1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1};  // PM +0xb0..+0xdc
  for (int i = 0; i < 12; ++i) l->pm_colors[i] = colors[i];
  l->put(0xb4, 32.0f), l->put(0xb8, 64.0f), l->put(0xbc, 1024.0f), l->put(0xc0, 4096.0f);  // PM +0xe4..
  l->put(0xc4, 0.03125f);
  l->put<uint32_t>(0xb0, (l->get<uint32_t>(0xb0) & 0xfffff8b3u) | 0x8b3);  // PM +0xe0
  ::zcls::node_set_active(node, 1);
  l->put<int32_t>(0xc8, 0);  // PM +0xf8, +0xfc
  l->nodes = nullptr;
  ::zcls::push_front(::zcls::kLights, node);
  return node;
}

}  // namespace pm::zcls
