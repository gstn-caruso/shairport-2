#pragma once

#include <optional>
#include <string_view>

class ParameterBody {
public:
  explicit ParameterBody(std::string_view bytes) noexcept : bytes_(bytes), remaining_(bytes) {}

  bool requestsVolume() const noexcept {
    return bytes_.size() == 8 && bytes_.starts_with("volume");
  }

  std::optional<std::string_view> nextLine() noexcept {
    if (remaining_.empty())
      return std::nullopt;
    auto end = remaining_.find_first_of("\r\n");
    auto line = remaining_.substr(0, end);
    if (end == std::string_view::npos) {
      remaining_ = {};
    } else {
      auto separator = remaining_[end];
      remaining_.remove_prefix(end + 1);
      if (separator == '\r' && remaining_.starts_with('\n'))
        remaining_.remove_prefix(1);
    }
    return line;
  }

private:
  std::string_view bytes_;
  std::string_view remaining_;
};
