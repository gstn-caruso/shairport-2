#include "app/configuration_loader.hpp"
#include "app/legacy_config_lease.hpp"
#include "runtime/common.h"
#include <gtest/gtest.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unistd.h>

TEST(ConfigurationLease, RetainsMappedStringsAndParsedTreeAfterSourceRemovalAndSettingsMove) {
  const auto file = std::filesystem::temp_directory_path() /
      ("receiver-lease-" + std::to_string(getpid()) + ".conf");
  std::ofstream(file) << R"(general = { name = "Owned Receiver"; password = "test credential";
      output_channel_mapping = ("FR", "FL"); port = 7200; udp_port_base = 6300;
      default_airplay_volume = -15; volume_control_profile = "flat";
      volume_control_combined_hardware_priority = "yes"; volume_range_db = 50;
      audio_backend_buffer_desired_length_in_seconds = 0.45;
      audio_backend_latency_offset_in_seconds = -0.04;
      interpolation = "basic"; playback_mode = "both right";
      run_this_when_volume_is_set = "volume-hook"; };
      diagnostics = { statistics = "yes"; log_output_level = "yes";
          log_show_time_since_startup = "yes"; log_show_time_since_last_message = "no"; };
      pulseaudio = { server = "owned-server"; default_channel_layouts = "pulseaudio"; };
      sessioncontrol = { run_this_before_play_begins = "owned-hook";
          run_this_after_play_ends = "stop-hook";
          run_this_before_entering_active_state = "active-hook";
          run_this_after_exiting_active_state = "inactive-hook";
          run_this_if_an_unfixable_error_is_detected = "error-hook";
          session_timeout = 180; active_state_timeout = 7.5;
          allow_session_interruption = "yes"; };)";
  const auto path = file.string();
  const std::array<std::string_view, 2> arguments{"--config", path};
  ConfigurationEnvironment environment{.defaultPath = path, .hostname = "receiver",
      .packageVersion = "1.2.3", .detailedVersion = "1.2.3", .firmwareVersion = "1.2.3"};
  auto loaded = ConfigurationLoader::load(*StartupOptions::parse(arguments), environment);
  ASSERT_TRUE(loaded);
  ASSERT_EQ(pthread_mutex_init(&config.lock, nullptr), 0);
  ASSERT_EQ(pthread_mutex_lock(&config.lock), 0);
  LegacyConfigLease lease(std::move(*loaded));
  EXPECT_EQ(pthread_mutex_trylock(&config.lock), EBUSY);
  EXPECT_EQ(pthread_mutex_unlock(&config.lock), 0);
  EXPECT_EQ(pthread_mutex_destroy(&config.lock), 0);
  std::filesystem::remove(file);
  EXPECT_STREQ(config.service_name, "Owned Receiver");
  EXPECT_STREQ(config.password, "test credential");
  EXPECT_STREQ(config.pa_server, "owned-server");
  EXPECT_STREQ(config.cmd_start, "owned-hook");
  EXPECT_STREQ(config.cmd_stop, "stop-hook");
  EXPECT_STREQ(config.cmd_active_start, "active-hook");
  EXPECT_STREQ(config.cmd_active_stop, "inactive-hook");
  EXPECT_STREQ(config.cmd_unfixable, "error-hook");
  EXPECT_STREQ(config.cmd_set_volume, "volume-hook");
  EXPECT_STREQ(config.output_channel_map[0], "FR");
  EXPECT_STREQ(config.output_channel_map[1], "FL");
  EXPECT_EQ(config.port, 7200);
  EXPECT_EQ(config.udp_port_base, 6300);
  EXPECT_EQ(config.udp_port_range, 10);
  EXPECT_EQ(config.statistics_requested, 1);
  EXPECT_EQ(config.logOutputLevel, 1);
  EXPECT_EQ(config.debugger_show_elapsed_time, 1);
  EXPECT_EQ(config.debugger_show_relative_time, 0);
  EXPECT_DOUBLE_EQ(config.audio_backend_buffer_desired_length, 0.45);
  EXPECT_DOUBLE_EQ(lease.settings().audio().audio_backend_latency_offset, -0.04);
  EXPECT_EQ(config.packet_stuffing, ST_basic);
  EXPECT_EQ(config.playback_mode, ST_right_only);
  EXPECT_DOUBLE_EQ(config.default_airplay_volume, -15);
  EXPECT_EQ(config.volume_control_profile, VCP_flat);
  EXPECT_EQ(config.volume_range_hw_priority, 1);
  EXPECT_EQ(config.volume_range_db, 50u);
  EXPECT_EQ(config.timeout, 180);
  EXPECT_DOUBLE_EQ(config.active_state_timeout, 7.5);
  EXPECT_EQ(config.allow_session_interruption, 1);
  EXPECT_EQ(config.output_channel_map_size, 2u);
  EXPECT_EQ(config.output_channel_map[2], nullptr);
  EXPECT_STREQ(config.appName, "shairport-2");
  EXPECT_STREQ(config.model, "ShairportSync");
  EXPECT_STREQ(config.firmware_version, "1.2.3");
  ASSERT_NE(config.cfg, nullptr);
  std::unique_ptr<FILE, decltype(&fclose)> dump(tmpfile(), &fclose);
  ASSERT_NE(dump, nullptr);
  config_write(config.cfg, dump.get());
  rewind(dump.get());
  std::array<char, 1024> bytes{};
  const auto length = fread(bytes.data(), 1, bytes.size(), dump.get());
  const std::string text(bytes.data(), length);
  EXPECT_NE(text.find("Owned Receiver"), std::string::npos);
  EXPECT_NE(text.find("owned-server"), std::string::npos);
  EXPECT_NE(text.find("owned-hook"), std::string::npos);
}
