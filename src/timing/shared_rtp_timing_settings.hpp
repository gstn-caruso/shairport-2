#pragma once
#include <atomic>

class SharedRtpTimingSettings {
public:
  explicit SharedRtpTimingSettings(double initialOffsetSeconds);
  double offsetSeconds() const;
  void disableForInsufficientLatency();

private:
  std::atomic<double> offsetSeconds_;
};
