#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class RtspMessage {
public:
  struct Header {
    std::string name;
    std::string value;
  };

  std::optional<int> readLine(std::string_view line);
  void request(std::string_view method, std::string_view path = "/");
  bool requestsMethod(std::string_view method) const noexcept;
  bool requestsPath(std::string_view path) const noexcept;
  const char *methodName() const noexcept;
  const char *requestPath() const noexcept;
  bool addHeader(std::string_view name, std::string_view value);
  const char *headerValue(std::string_view name) const noexcept;
  const std::vector<Header> &headers() const noexcept;
  void replaceBody(std::string_view bytes);
  std::string_view bodyText() const noexcept;
  const char *bodyData() const noexcept;
  uint32_t bodyLength() const noexcept;
  bool bodyStartsWith(std::string_view prefix) const noexcept;
  void respondWith(int code) noexcept;
  int responseCode() const noexcept;
  bool hasResponseCode(int code) const noexcept;
  enum class FramingError { headersTooLong = -1, lengthTooLong = -2, bodyTooLong = -3 };
  std::expected<std::string, FramingError> responsePacket() const;

private:
  std::string_view responseReason() const noexcept;
  std::string method_;
  std::string path_;
  std::vector<Header> headers_;
  std::string body_;
  bool requestLineRead_ = false;
  int responseCode_ = 0;
};
