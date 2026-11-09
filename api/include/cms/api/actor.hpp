#pragma once

#include <string>

namespace cms::api {

/// Who is acting: which account table the id names, and the id. A surface produces this; a request may
/// never carry it.
struct Actor {
  std::string kind;
  std::string id;
};

}  // namespace cms::api
