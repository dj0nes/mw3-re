#include "frontend/sdl/tvos_args.h"

namespace sdlfe {

std::vector<std::string> tvos_default_args(const std::string& base) {
  return {"mw3", base + "game", "--assets", base + "media", "--no-launcher"};
}

}  // namespace sdlfe
