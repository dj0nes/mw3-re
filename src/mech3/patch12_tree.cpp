#include "mech3/patch12_tree.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mech3::patch12::tree {

using zrdr::Node;

Node str(const char* s) {
  Node n{};
  n.type = zrdr::String;
  const size_t len = std::strlen(s);
  n.s = static_cast<char*>(std::malloc(len + 1));
  std::memcpy(n.s, s, len + 1);
  return n;
}

Node num(int32_t v) {
  Node n{};
  n.type = zrdr::Int, n.i = v;
  return n;
}

Node real(float v) {
  Node n{};
  n.type = zrdr::Float, n.f = v;
  return n;
}

Node list(std::vector<Node> items) {
  Node n{};
  n.type = zrdr::List;
  const int32_t count = static_cast<int32_t>(items.size()) + 1;
  n.list = zrdr::alloc_list(count);
  n.list[0].type = zrdr::Int, n.list[0].i = count;
  for (size_t i = 0; i < items.size(); ++i) n.list[i + 1] = items[i];
  return n;
}

Node clone(const Node& n) {
  if (n.type == zrdr::String) return str(n.s ? n.s : "");
  if (n.type != zrdr::List) return n;
  std::vector<Node> items;
  for (int32_t i = 1; i < n.list[0].i; ++i) items.push_back(clone(n.list[i]));
  return list(std::move(items));
}

Node* value(Node* l, const char* key) { return zrdr::find_flat(l, key, 1); }

Node* path(Node* l, std::initializer_list<const char*> keys) {
  for (const char* k : keys) {
    l = value(l, k);
    if (!l) return nullptr;
  }
  return l;
}

void insert(Node* l, int32_t at, std::vector<Node> items) {
  const int32_t count = l->list[0].i, add = static_cast<int32_t>(items.size());
  Node* grown = zrdr::alloc_list(count + add);
  std::memcpy(grown, l->list, sizeof(Node) * static_cast<size_t>(at));
  for (int32_t i = 0; i < add; ++i) grown[at + i] = items[static_cast<size_t>(i)];
  std::memcpy(grown + at + add, l->list + at, sizeof(Node) * static_cast<size_t>(count - at));
  grown[0].i = count + add;
  std::free(l->list);
  l->list = grown;
}

void erase(Node* l, int32_t at, int32_t n) {
  const int32_t count = l->list[0].i;
  for (int32_t i = at; i < at + n; ++i) zrdr::free_contents(&l->list[i]);
  std::memmove(l->list + at, l->list + at + n, sizeof(Node) * static_cast<size_t>(count - at - n));
  l->list[0].i = count - n;
  std::memset(&l->list[count - n], 0, sizeof(Node));  // the spare slot after the list (zreader.cpp)
}

void replace(Node* slot, Node with) {
  zrdr::free_contents(slot);
  *slot = with;
}

bool equal(const Node& a, const Node& b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case zrdr::Int: return a.i == b.i;
    case zrdr::Float: return std::memcmp(&a.f, &b.f, 4) == 0;
    case zrdr::String: return std::strcmp(a.s ? a.s : "", b.s ? b.s : "") == 0;
    case zrdr::List:
      if (a.list[0].i != b.list[0].i) return false;
      for (int32_t i = 1; i < a.list[0].i; ++i)
        if (!equal(a.list[i], b.list[i])) return false;
      return true;
    default: return true;
  }
}

std::string text(const Node& n) {
  char buf[32];
  switch (n.type) {
    case zrdr::Int: return std::to_string(n.i);
    case zrdr::Float: std::snprintf(buf, sizeof buf, "%g", n.f); return buf;
    case zrdr::String: return std::string("\"") + (n.s ? n.s : "") + "\"";
    case zrdr::List: {
      std::string s = "(";
      for (int32_t i = 1; i < n.list[0].i; ++i) s += (i > 1 ? " " : "") + text(n.list[i]);
      return s + ")";
    }
    default: return "?";
  }
}

}  // namespace mech3::patch12::tree
