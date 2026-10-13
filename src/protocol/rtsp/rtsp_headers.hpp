#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

class RtspHeaders {
public:
  struct Header {
    std::string name;
    std::string value;
  };

  bool add(std::string_view name, std::string_view value) {
    if (headers_.size() == 16)
      return false;
    headers_.push_back({std::string(name), std::string(value)});
    return true;
  }

  const char *valueFor(std::string_view name) const noexcept {
    for (const auto &header : headers_)
      if (sameName(header.name, name))
        return header.value.c_str();
    return nullptr;
  }

  const std::vector<Header> &inOrder() const noexcept { return headers_; }

private:
  static bool sameName(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
      return false;
    for (std::size_t index = 0; index < left.size(); ++index)
      if (std::tolower(static_cast<unsigned char>(left[index])) !=
          std::tolower(static_cast<unsigned char>(right[index])))
        return false;
    return true;
  }

  std::vector<Header> headers_;
};
