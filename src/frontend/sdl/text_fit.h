// Ours: text shortened to a width (the launcher's install folder).
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace sdlfe {

// `s` shortened from the middle with "..." until `width_of` it is at most
// `width`; its first `keep` characters stay whole, and more of its end than
// of its start is kept (a folder's name).
std::string fit_middle(const std::string& s, size_t keep, int width, const std::function<int(const std::string&)>& width_of);

// A font whose space is under a fifth of its size (DIN Condensed's is 2
// pixels at 13, and its digits draw a pixel past their advance: "3 was" read
// "3was") gets each space doubled, drawn and measured alike.
std::string widen_spaces(const std::string& s, int space_advance, int size);

}  // namespace sdlfe
