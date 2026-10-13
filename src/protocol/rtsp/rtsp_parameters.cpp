module;
#include "protocol/rtsp/rtsp_message.hpp"
#include "protocol/rtsp/parameter_body.hpp"
#include "volume/volume_quantities.hpp"
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module receiver.protocol.rtsp.parameters;

std::optional<AirPlayVolume> RtspParameterHandler::get(const RtspMessage &request,
                                                      RtspMessage &response) {
  std::optional<AirPlayVolume> requestedVolume;
  if (ParameterBody(request.bodyText()).requestsVolume()) {
    requestedVolume = volume_.suggestedVolume();
    response.replaceBody(std::format("\r\nvolume: {:.6f}\r\n", requestedVolume->value()));
  }
  response.respondWith(200);
  return requestedVolume;
}

ParameterDiagnostics RtspParameterHandler::set(const RtspMessage &request, RtspMessage &response) {
  const auto *contentType = request.headerValue("Content-Type");
  if (!contentType) {
    response.respondWith(200);
    return {ParameterContent::missing, {}};
  }
  const std::string_view type(contentType);
  if (type.starts_with("application/x-dmap-tagged")) {
    response.respondWith(request.containsCompleteMetadata() ? 200 : 400);
    return {ParameterContent::metadata, {}};
  }
  if (type.starts_with("image/")) {
    response.respondWith(200);
    return {ParameterContent::image, {}};
  }
  if (!type.starts_with("text/parameters")) {
    response.respondWith(200);
    return {ParameterContent::unknown, {}};
  }
  ParameterDiagnostics diagnostics{ParameterContent::text, {}};
  ParameterBody parameters(request.bodyText());
  while (auto parameter = parameters.nextLine()) {
    if (parameter->starts_with("volume: ")) {
      const std::string value(parameter->substr(8));
      volume_.acceptVolume(AirPlayVolume::fromWireParameter(value.c_str()));
    } else if (!parameter->starts_with("progress: "))
      diagnostics.unrecognizedParameters.emplace_back(*parameter);
  }
  response.respondWith(200);
  return diagnostics;
}
