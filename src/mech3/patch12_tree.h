#pragma once
#include <initializer_list>
#include <string>
#include <vector>

#include "gamez/zreader/zreader.h"

// Small edits to zReader trees for the 1.2 data layer (mech3/patch12.h).
// Nodes made here are owned like read ones: strings and lists malloc'd, so
// zrdr::free_node frees them.
namespace mech3::patch12::tree {

zrdr::Node str(const char* s);
zrdr::Node num(int32_t v);
zrdr::Node real(float v);
zrdr::Node list(std::vector<zrdr::Node> items);  // takes the items
zrdr::Node clone(const zrdr::Node& n);

// The value after `key` in this list only (zrdr::find_flat), or null.
zrdr::Node* value(zrdr::Node* list, const char* key);
// The same down a path of keys, each in the value found before.
zrdr::Node* path(zrdr::Node* list, std::initializer_list<const char*> keys);

// Inserts `items` before slot `at` (1 = first; the item count + 1 = the end).
void insert(zrdr::Node* list, int32_t at, std::vector<zrdr::Node> items);
// Removes `n` items from slot `at`, freeing them.
void erase(zrdr::Node* list, int32_t at, int32_t n);
// Replaces `*slot` with `with`, freeing the old.
void replace(zrdr::Node* slot, zrdr::Node with);

bool equal(const zrdr::Node& a, const zrdr::Node& b);
std::string text(const zrdr::Node& n);  // one line, for messages

}  // namespace mech3::patch12::tree
