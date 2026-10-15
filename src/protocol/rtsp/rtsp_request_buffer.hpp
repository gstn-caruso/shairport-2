#pragma once

#include <string>

struct RtspRequestBuffer {
  std::string bytes;
  bool skipLineFeed = false;
};
