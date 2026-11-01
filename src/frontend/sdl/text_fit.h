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

}  // namespace sdlfe
