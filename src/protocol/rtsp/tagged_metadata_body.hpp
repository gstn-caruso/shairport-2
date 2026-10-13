#pragma once

#include <cstdint>
#include <string_view>

class TaggedMetadataBody {
public:
  explicit TaggedMetadataBody(std::string_view bytes) noexcept : bytes_(bytes) {}

  bool isComplete() const noexcept {
    if (bytes_.size() < 8 || lengthAt(4) != bytes_.size() - 8)
      return false;
    std::size_t offset = 8;
    while (bytes_.size() - offset >= 8) {
      auto length = lengthAt(offset + 4);
      offset += 8;
      if (length > bytes_.size() - offset)
        return false;
      offset += length;
    }
    return offset == bytes_.size();
  }

private:
  uint32_t byteAt(std::size_t index) const noexcept {
    return static_cast<unsigned char>(bytes_[index]);
  }

  uint32_t lengthAt(std::size_t offset) const noexcept {
    return (byteAt(offset) << 24) | (byteAt(offset + 1) << 16) |
           (byteAt(offset + 2) << 8) | byteAt(offset + 3);
  }

  std::string_view bytes_;
};
