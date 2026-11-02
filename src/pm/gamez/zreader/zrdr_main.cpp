#include "pm/gamez/zreader/zrdr_main.h"

namespace pm::zrdr {

// Pirate's Moon 1.0 @ 0x00578f70
bool get_int_value(Node* node, int32_t* out) {
  if (!node) return false;
  if (node->type == ::zrdr::List && node->list[0].i > 1) node = &node->list[1];
  if (node->type != ::zrdr::Int) return false;
  *out = node->i;
  return true;
}

// Pirate's Moon 1.0 @ 0x00579090
bool get_float_value(Node* node, const char* key, float* out) {
  return ::zrdr::get_float_value(::zrdr::find(node, key), out);
}

// Pirate's Moon 1.0 @ 0x005790b0
bool get_string_value(Node* node, const char* key, const char** out) {
  return ::zrdr::get_string_value(::zrdr::find(node, key), out);
}

}  // namespace pm::zrdr

// The text files. PM reads every byte through a stack of open files (handles
// 0x008c1974, the top 0x00612f08, the current 0x008c19f4): an #include pushes
// one, its end pops it. FIX (A), as MW3's: stdio for the Win32 file calls.
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gamez/zutil/zutl_path.h"

namespace pm::zrdr {
namespace {
using ::zrdr::List;

std::vector<std::FILE*> g_files;  // [0] the file parse_text was given, not closed here

// Pirate's Moon 1.0 @ 0x00578720: push, at most 32 (the top below 0x1f).
bool push(std::FILE* f) {
  if (g_files.size() >= 32) return false;
  g_files.push_back(f);
  return true;
}

// Pirate's Moon 1.0 @ 0x00578250: one byte; at a file's end an included one
// is closed (0x005782a0) and the reading goes on in the file before it.
bool read_char(char* c) {
  for (;;) {
    const int v = std::fgetc(g_files.back());
    if (v != EOF) return *c = static_cast<char>(v), true;
    if (g_files.size() < 2) return false;
    std::fclose(g_files.back());
    g_files.pop_back();
  }
}

bool space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }  // MSVCRT isspace, as zreader.cpp's
void too_long(const char* token) { std::fprintf(stderr, "token[%s] too long\n", token); }  // printf

// Pirate's Moon 1.0 @ 0x00578150: MW3's 0x005562a0 on the current file.
bool balanced() {
  int32_t depth = 0;
  bool comment = false, quote = false;
  char c;
  while (read_char(&c)) {
    switch (c) {
      case '\n':
      case '\r':
        if (!quote) comment = false;
        break;
      case '"':
        if (!comment) quote = !quote;
        break;
      case '(':
        if (!quote && !comment) ++depth;
        break;
      case ')':
        if (!quote && !comment) --depth;
        break;
      case ';':
        if (!quote) comment = true;
        break;
      default:
        break;
    }
  }
  std::fseek(g_files.back(), 0, SEEK_SET);
  return depth == 0;
}

// The word after a '#' (0x005795e0), an #ifdef (0x005797a0) or an #include
// (0x00579440): up to a space, which is consumed. False at the end of the
// files or a word too long.
bool read_word(char* c, char (&word)[0x102]) {
  int32_t n = 0;
  word[0] = '\0';  // the exe's buffer is as its stack was
  if (!read_char(c)) return false;
  while (!space(*c)) {
    if (n > 0xff) return too_long(word), false;
    word[n] = *c;
    word[n + 1] = '\0';
    ++n;
    if (!read_char(c)) return false;
  }
  return true;
}

// Pirate's Moon 1.0 @ 0x005795e0: 0 ifdef, 1 else, 2 endif, 3 include, 4
// anything else (MW3's 0x00557650 had no include).
int directive_word(char* c) {
  char w[0x102];
  if (!read_word(c, w)) return 4;
  if (std::strcmp(w, "ifdef") == 0) return 0;
  if (std::strcmp(w, "else") == 0) return 1;
  if (std::strcmp(w, "endif") == 0) return 2;
  return std::strcmp(w, "include") == 0 ? 3 : 4;
}

// Pirate's Moon 1.0 @ 0x005797a0: the name after #ifdef is defined. Nothing
// in the exe defines one (the list at 0x008c176c stays empty), as in MW3.
bool defined(char* c) {
  char w[0x102];
  if (!read_word(c, w)) return false;
  for (const std::string& d : ::zrdr::state().defines)
    if (d == w) return true;
  return false;
}

bool skip_to_hash(char* c) {
  while (*c != '#')
    if (!read_char(c)) return false;
  return true;
}

// Ours: an included name comes from a data file (a mod's too), so it is opened
// only inside the mounts: an absolute name, a drive or a ".." part opens nothing.
bool inside_mounts(const char* name) {
  if (name[0] == '/' || name[0] == '\\' || std::strchr(name, ':')) return false;
  for (const char* p = name; *p;) {
    const size_t n = std::strcspn(p, "/\\");
    if (n == 2 && p[0] == '.' && p[1] == '.') return false;
    p += n + (p[n] != '\0');
  }
  return true;
}

// Pirate's Moon 1.0 @ 0x00578750: the file found as load's text branch finds
// one (FIX (A): zutl::open for the exe's search 0x005780e0), pushed.
void include(const char* name) {
  std::FILE* f = name[0] && inside_mounts(name) ? zutl::open(name, "rb") : nullptr;
  if (f && !push(f)) std::fclose(f);  // ours: the exe leaves a 33rd file open
}

// Pirate's Moon 1.0 @ 0x00579440, MW3's 0x00557550 with #include: the file
// is opened then and there and true returned, so an #include met while a
// false #ifdef is skipped is read, and ends the skip (a quirk kept).
bool directive(char* c, bool skipping) {
  int32_t& depth = ::zrdr::state().if_depth;  // PM's 0x008c19fc, kept across files as MW3's
  const int32_t start = depth;
  switch (directive_word(c)) {
    case 0:
      ++depth;
      if (defined(c)) return false;
      for (;;) {
        if (depth <= start) return false;
        // FIX (bug), as MW3's: the exe scans on forever when the files end here.
        if (!skip_to_hash(c)) return false;
        if (directive(c, true)) return true;
      }
    case 1:
      if (skipping) return true;
      while (start <= depth) {
        if (!skip_to_hash(c)) return false;  // FIX (bug), as above
        directive(c, false);
      }
      return false;
    case 2:
      --depth;
      return false;
    case 3: {
      char w[0x102];
      if (read_word(c, w)) include(w);
      return true;
    }
    default:
      return false;
  }
}

// Pirate's Moon 1.0 @ 0x00578410: MW3's 0x00556510 through the stack.
int read_token(Node** out) {
  char buf[0x102];
  int32_t n = 0;
  bool quoted = false;
  char c;
  if (!read_char(&c)) return 0;
  for (;;) {
    if (c == ';') {
      do {
        if (!read_char(&c)) return 0;
      } while (c != '\n');
    } else {
      if (n == 0 && c == '#') directive(&c, false);
      if (c == '(' || c == ')' || space(c)) {
        *out = n ? ::zrdr::token_value(buf, quoted) : nullptr;
        return c;
      }
      if (n >= 0x100) {
        *out = nullptr;
        too_long(buf);
        return 0;
      }
      if (c == '"') {
        quoted = true;
        // FIX (bug), as MW3's: the exe reads on past the end of the files
        // and past its 256-byte buffer; the string stops at either.
        bool more = read_char(&c);
        while (more && c != '"') {
          if (n < 0xff) buf[n++] = c;
          more = read_char(&c);
        }
        buf[n++] = '\0';
      } else {
        buf[n++] = c;
        buf[n] = '\0';
      }
    }
    if (!read_char(&c)) return 0;
  }
}

// Pirate's Moon 1.0 @ 0x00578300: MW3's 0x005563f0.
Node* parse_list() {
  std::vector<Node> items;
  Node* out = nullptr;
  for (;;) {
    const int r = read_token(&out);
    if (r == 0) break;
    if (out) {
      items.push_back(*out);
      std::free(out);
    }
    if (r == '(') {
      out = parse_list();
      items.push_back(*out);
      std::free(out);
    } else if (r == ')') {
      break;
    }
  }
  Node* node = ::zrdr::new_nodes(List, 1);
  const int32_t count = static_cast<int32_t>(items.size()) + 1;
  node->list = ::zrdr::alloc_list(count);
  node->list[0].type = ::zrdr::Int;
  node->list[0].i = count;
  for (size_t i = 0; i < items.size(); ++i) node->list[i + 1] = items[i];
  return node;
}
}  // namespace

// Pirate's Moon 1.0 @ 0x005782f0. Ours: files an early ')' leaves included
// are closed here (the exe pops one of them in load and leaks the rest).
Node* parse_text(std::FILE* file) {
  g_files.assign(1, file);
  Node* root = balanced() ? parse_list() : nullptr;
  for (size_t i = 1; i < g_files.size(); ++i) std::fclose(g_files[i]);
  g_files.clear();
  return root;
}

}  // namespace pm::zrdr
