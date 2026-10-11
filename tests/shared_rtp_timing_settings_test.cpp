#include "timing/shared_rtp_timing_settings.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <thread>

TEST(SharedRtpTimingSettings, RetainsInitialOffsetUntilPermanentlyDisabled) {
  SharedRtpTimingSettings settings(-0.125);
  EXPECT_DOUBLE_EQ(settings.offsetSeconds(), -0.125);
  settings.disableForInsufficientLatency();
  EXPECT_DOUBLE_EQ(settings.offsetSeconds(), 0);
  settings.disableForInsufficientLatency();
  EXPECT_DOUBLE_EQ(settings.offsetSeconds(), 0);
  SharedRtpTimingSettings independent(0.25);
  EXPECT_DOUBLE_EQ(independent.offsetSeconds(), 0.25);
}

TEST(SharedRtpTimingSettings, ConcurrentReadersObserveOnlyInitialOrDisabledOffset) {
  SharedRtpTimingSettings settings(-0.125);
  std::atomic<bool> ready{false}, valid{true};
  std::thread reader([&] {
    ready.store(true);
    for (unsigned index = 0; index < 100000; ++index) {
      const double offset = settings.offsetSeconds();
      if (offset != -0.125 && offset != 0) valid.store(false);
    }
  });
  while (!ready.load()) std::this_thread::yield();
  settings.disableForInsufficientLatency();
  reader.join();
  EXPECT_TRUE(valid.load());
  EXPECT_DOUBLE_EQ(settings.offsetSeconds(), 0);
}
