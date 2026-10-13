#include "protocol/rtsp/rtsp_message.hpp"
#include <array>
#include <format>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace {
std::string_view nextRequestToken(std::string_view &line) noexcept {
  auto first = line.find_first_not_of(' ');
  if (first == std::string_view::npos)
    return {};
  line.remove_prefix(first);
  auto end = line.find(' ');
  auto token = line.substr(0, end);
  line.remove_prefix(token.size());
  return token;
}
}

std::optional<int> RtspMessage::readLine(std::string_view line) {
  if (!requestLineRead_) {
    auto method = nextRequestToken(line);
    auto path = nextRequestToken(line);
    auto version = nextRequestToken(line);
    if (method.empty() || path.empty() || (version != "RTSP/1.0" && version != "HTTP/1.1"))
      return std::nullopt;
    request(method, path);
    return -1;
  }
  if (line.empty()) {
    auto length = headerValue("Content-Length");
    return length ? std::atoi(length) : 0;
  }
  auto separator = line.find(": ");
  if (separator == std::string_view::npos)
    return std::nullopt;
  addHeader(line.substr(0, separator), line.substr(separator + 2));
  return -1;
}

void RtspMessage::request(std::string_view method, std::string_view path) {
  method_ = method.substr(0, 15);
  path_ = path.substr(0, 255);
  requestLineRead_ = true;
}

bool RtspMessage::requestsMethod(std::string_view method) const noexcept { return method_ == method; }
bool RtspMessage::requestsPath(std::string_view path) const noexcept { return path_ == path; }
const char *RtspMessage::methodName() const noexcept { return method_.c_str(); }
const char *RtspMessage::requestPath() const noexcept { return path_.c_str(); }

bool RtspMessage::addHeader(std::string_view name, std::string_view value) {
  return headers_.add(name, value);
}

const char *RtspMessage::headerValue(std::string_view name) const noexcept {
  return headers_.valueFor(name);
}

const std::vector<RtspMessage::Header> &RtspMessage::headers() const noexcept {
  return headers_.inOrder();
}

void RtspMessage::replaceBody(std::string_view bytes) {
  if (bytes.size() > std::numeric_limits<uint32_t>::max())
    throw std::length_error("RTSP body exceeds protocol length");
  if (bytes.empty())
    body_.clear();
  else
    body_.assign(bytes.data(), bytes.size());
}

std::string_view RtspMessage::bodyText() const noexcept { return body_; }
const char *RtspMessage::bodyData() const noexcept { return body_.c_str(); }
uint32_t RtspMessage::bodyLength() const noexcept { return static_cast<uint32_t>(body_.size()); }
bool RtspMessage::bodyStartsWith(std::string_view prefix) const noexcept { return body_.starts_with(prefix); }

void RtspMessage::respondWith(int code) noexcept { responseCode_ = code; }
int RtspMessage::responseCode() const noexcept { return responseCode_; }
bool RtspMessage::hasResponseCode(int code) const noexcept { return responseCode_ == code; }

std::string_view RtspMessage::responseReason() const noexcept {
  static constexpr std::array<std::pair<int, std::string_view>, 9> reasons{{
      {200, "OK"}, {400, "Bad Request"}, {403, "Unauthorized"}, {404, "Not Found"},
      {451, "Unavailable"}, {456, "Header Field Not Valid for Resource"},
      {470, "Connection Authorization Required"}, {500, "Internal Server Error"},
      {501, "Not Implemented"}}};
  for (const auto &[code, reason] : reasons)
    if (responseCode_ == code)
      return reason;
  return "Unauthorized";
}

std::expected<std::string, RtspMessage::FramingError> RtspMessage::responsePacket() const {
  auto packet = std::format("RTSP/1.0 {} {}\r\n", responseCode_, responseReason());
  for (const auto &header : headers_.inOrder()) {
    packet += std::format("{}: {}\r\n", header.name, header.value);
    if (packet.size() >= 3072)
      return std::unexpected(FramingError::headersTooLong);
  }
  packet += std::format("Content-Length: {}\r\n", body_.size());
  if (packet.size() >= 3072)
    return std::unexpected(FramingError::lengthTooLong);
  packet += "\r\n";
  packet += body_;
  if (packet.size() >= 3072)
    return std::unexpected(FramingError::bodyTooLong);
  return packet;
}
