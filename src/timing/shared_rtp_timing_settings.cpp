#include "timing/shared_rtp_timing_settings.hpp"

SharedRtpTimingSettings::SharedRtpTimingSettings(double initialOffsetSeconds)
    : offsetSeconds_(initialOffsetSeconds) {}

double SharedRtpTimingSettings::offsetSeconds() const {
  return offsetSeconds_.load();
}

void SharedRtpTimingSettings::disableForInsufficientLatency() {
  offsetSeconds_.store(0);
}
