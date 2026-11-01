// The 1.2 data layer's record edits (docs/patch12.md): the values, keys and
// items the patch changed in 1.0's reader entries, as a table
// (patch12_edits.inc, written by tools/analysis/patch12_edits.py).
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mech3/patch12.h"
#include "mech3/patch12_tree.h"

namespace mech3::patch12 {
namespace {

using zrdr::Node;

enum Op { Set, Erase, Insert };

struct Edit {
  const char *archive, *entry;
  Op op;
  // Keys down from the entry's record: KEY (the item after the string KEY),
  // KEY#n (after its n-th), @i (item i from 0). Insert's last step names
  // what the new items go after (empty: the start) or, @i, before.
  const char* path;
  const char* a;  // Set, Erase: the old value; Insert: the key, or empty
  const char* b;  // Set: the new value; Insert: the value
};

constexpr Edit kEdits[] = {
#include "mech3/patch12_edits.inc"
};

// A value as the table writes it: (lists), "strings", ints, floats with a '.'.
bool parse_at(const char*& p, Node* out) {
  while (*p == ' ') ++p;
  if (*p == '(') {
    ++p;
    std::vector<Node> items;
    for (;;) {
      while (*p == ' ') ++p;
      if (*p == ')') break;
      Node n{};
      if (!*p || !parse_at(p, &n)) {
        for (Node& i : items) zrdr::free_contents(&i);
        return false;
      }
      items.push_back(n);
    }
    ++p;
    *out = tree::list(std::move(items));
    return true;
  }
  if (*p == '"') {
    const char* end = std::strchr(p + 1, '"');
    if (!end) return false;
    *out = tree::str(std::string(p + 1, end).c_str());
    p = end + 1;
    return true;
  }
  const char* start = p;
  while (*p && *p != ' ' && *p != ')') ++p;
  const std::string t(start, p);
  if (t.empty()) return false;
  *out = t.find('.') != std::string::npos ? tree::real(std::strtof(t.c_str(), nullptr))
                                          : tree::num(static_cast<int32_t>(std::strtol(t.c_str(), nullptr, 10)));
  return true;
}

bool parse(const char* text, Node* out) { return parse_value(text, out); }

std::vector<std::string> split(const char* path) {
  std::vector<std::string> out(1);
  for (const char* c = path; *c; ++c) {
    if (*c == '/') out.emplace_back();
    else out.back() += *c;
  }
  return out;
}

// The slot of step `s` in list `l` (KEY's value, or item @i), or 0. For KEY
// steps `*key_at` gets the key's own slot.
int32_t slot(const Node* l, const std::string& s, int32_t* key_at = nullptr) {
  if (!l || l->type != zrdr::List) return 0;
  const int32_t count = l->list[0].i;
  if (s.size() > 1 && s[0] == '@') {
    const int32_t i = std::atoi(s.c_str() + 1) + 1;
    return i < count ? i : 0;
  }
  const size_t hash = s.find('#');
  const std::string key = s.substr(0, hash);
  int32_t want = hash == std::string::npos ? 1 : std::atoi(s.c_str() + hash + 1);
  for (int32_t i = 1; i + 1 < count; ++i) {
    const Node& n = l->list[i];
    if (n.type == zrdr::String && n.s && key == n.s && --want == 0) {
      if (key_at) *key_at = i;
      return i + 1;
    }
  }
  return 0;
}

Node* walk(Node* l, const std::vector<std::string>& steps, size_t n) {
  for (size_t k = 0; k < n && l; ++k) {
    const int32_t i = slot(l, steps[k]);
    l = i ? &l->list[i] : nullptr;
  }
  return l;
}

// One edit on `top`; false when the tree is not what the edit expects.
bool apply_one(Node* top, const Edit& e) {
  const std::vector<std::string> steps = split(e.path);
  Node* parent = walk(top, steps, steps.size() - 1);
  if (!parent || parent->type != zrdr::List) return false;
  const std::string& last = steps.back();
  Node old{}, now{};
  bool ok = false;
  switch (e.op) {
    case Set: {
      const int32_t i = slot(parent, last);
      if (i && parse(e.a, &old) && tree::equal(parent->list[i], old) && parse(e.b, &now))
        tree::replace(&parent->list[i], now), now = Node{}, ok = true;
      break;
    }
    case Erase: {
      int32_t key = 0;
      const int32_t i = slot(parent, last, &key);
      if (i && parse(e.a, &old) && tree::equal(parent->list[i], old)) {
        if (key) tree::erase(parent, key, 2);
        else tree::erase(parent, i, 1);
        ok = true;
      }
      break;
    }
    case Insert: {
      int32_t at = 1;
      if (!last.empty() && last[0] == '@') {
        at = std::atoi(last.c_str() + 1) + 1;  // before item i; the item count: the end
        if (at > parent->list[0].i) break;
      } else if (!last.empty()) {
        const int32_t i = slot(parent, last);
        if (!i) break;
        at = i + 1;
      }
      std::vector<Node> items;
      Node key{};
      if (*e.a && !parse(e.a, &key)) break;
      if (!parse(e.b, &now)) {
        zrdr::free_contents(&key);
        break;
      }
      if (*e.a) items.push_back(key);
      items.push_back(now), now = Node{};
      tree::insert(parent, at, std::move(items));
      ok = true;
      break;
    }
  }
  zrdr::free_contents(&old), zrdr::free_contents(&now);
  return ok;
}

}  // namespace

bool parse_value(const char* text, Node* out) {
  const char* p = text;
  return parse_at(p, out);
}

bool has_edits(const std::string& archive, const std::string& entry) {
  for (const Edit& e : kEdits)
    if (archive == e.archive && entry == e.entry) return true;
  return false;
}

bool apply_edits(const std::string& archive, const std::string& entry, Node* root) {
  if (!root || root->type != zrdr::List || !has_edits(archive, entry)) return false;
  // On a copy, so an entry that is not 1.0's after all (an edit's old value
  // does not match) is left whole.
  Node copy = tree::clone(*root);
  Node* top = &copy;
  while (top->list[0].i == 2 && top->list[1].type == zrdr::List) top = &top->list[1];
  for (const Edit& e : kEdits) {
    if (archive != e.archive || entry != e.entry) continue;
    if (!apply_one(top, e)) {
      zrdr::free_contents(&copy);
      return false;
    }
  }
  tree::replace(root, copy);
  return true;
}

}  // namespace mech3::patch12
