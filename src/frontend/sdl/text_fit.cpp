#include "frontend/sdl/text_fit.h"

namespace sdlfe {

std::string fit_middle(const std::string& s, size_t keep, int width, const std::function<int(const std::string&)>& width_of) {
  if (width_of(s) <= width || keep >= s.size()) return s;
  const std::string head = s.substr(0, keep), rest = s.substr(keep);
  for (size_t cut = 1; cut < rest.size(); ++cut) {  // `cut` characters out of the middle
    const size_t a = (rest.size() - cut) * 2 / 5, b = rest.size() - cut - a;
    const std::string t = head + rest.substr(0, a) + "..." + rest.substr(rest.size() - b);
    if (width_of(t) <= width) return t;
  }
  return head + "...";
}

std::string widen_spaces(const std::string& s, int space_advance, int size) {
  if (space_advance * 5 >= size || s.find(' ') == std::string::npos) return s;
  std::string out;
  out.reserve(s.size() * 2);
  for (char c : s) {
    out += c;
    if (c == ' ') out += ' ';
  }
  return out;
}

}  // namespace sdlfe
