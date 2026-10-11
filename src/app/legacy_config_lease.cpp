#include "app/legacy_config_lease.hpp"
#include "runtime/common.h"
#include <algorithm>
#include <utility>

extern const char *default_channel_layouts;

LegacyConfigLease::LegacyConfigLease(ReceiverSettings settings) : settings_(std::move(settings)) {
  config.debugger_show_elapsed_time = settings_.diagnostics_.debugger_show_elapsed_time;
  config.debugger_show_relative_time = settings_.diagnostics_.debugger_show_relative_time;
  config.debugger_show_file_and_line = settings_.diagnostics_.debugger_show_file_and_line;
  config.statistics_requested = settings_.diagnostics_.statistics_requested;
  config.logOutputLevel = settings_.diagnostics_.logOutputLevel;
  config.disable_resend_requests = settings_.diagnostics_.disable_resend_requests;
  config.diagnostic_drop_packet_fraction = settings_.diagnostics_.diagnostic_drop_packet_fraction;
  config.appName = settings_.network_.appName.data();
  config.configfile = settings_.network_.configfile.data();
  config.service_name = settings_.network_.service_name.data();
  config.password = settings_.network_.password ? settings_.network_.password->data() : nullptr;
  config.regtype = settings_.network_.regtype.data();
  config.regtype2 = settings_.network_.regtype2.data();
  config.interface = settings_.network_.interface ? settings_.network_.interface->data() : nullptr;
  config.interface_index = settings_.network_.interface_index;
  config.port = settings_.network_.port;
  config.udp_port_base = settings_.network_.udp_port_base;
  config.udp_port_range = settings_.network_.udp_port_range;
  config.missing_port_dacp_scan_interval_seconds = settings_.network_.missing_port_dacp_scan_interval_seconds;
  config.model = settings_.network_.model.data();
  config.srcvers = settings_.network_.srcvers.data();
  config.osvers = settings_.network_.osvers.data();
  config.firmware_version = settings_.network_.firmware_version.data();
  config.airplay_device_id = settings_.network_.airplay_device_id.data();
  std::copy(settings_.network_.ap1_prefix.begin(), settings_.network_.ap1_prefix.end(), config.ap1_prefix);
  std::fill(std::begin(config.hw_addr), std::end(config.hw_addr), 0);
  std::copy(settings_.network_.hw_addr.begin(), settings_.network_.hw_addr.end(), config.hw_addr);
  config.nqptp_shared_memory_interface_name = settings_.network_.nqptp_shared_memory_interface_name.data();
  config.packet_stuffing = settings_.audio_.packet_stuffing;
  config.playback_mode = settings_.audio_.playback_mode;
  config.audio_backend_buffer_desired_length = settings_.audio_.audio_backend_buffer_desired_length;
  config.audio_backend_buffer_interpolation_threshold_in_seconds = settings_.audio_.audio_backend_buffer_interpolation_threshold_in_seconds;
  config.audio_decoded_buffer_desired_length = settings_.audio_.audio_decoded_buffer_desired_length;
  config.audio_backend_silent_lead_in_time_auto = settings_.audio_.audio_backend_silent_lead_in_time_auto;
  config.audio_backend_silent_lead_in_time = settings_.audio_.audio_backend_silent_lead_in_time;
  config.buffer_start_fill = settings_.audio_.buffer_start_fill;
  config.userSuppliedLatency = settings_.audio_.userSuppliedLatency;
  config.fixedLatencyOffset = settings_.audio_.fixedLatencyOffset;
  config.minimum_free_buffer_headroom = settings_.audio_.minimum_free_buffer_headroom;
  config.resync_threshold = settings_.audio_.resync_threshold;
  config.tolerance = settings_.audio_.tolerance;
  config.format_set = settings_.audio_.format_set;
  config.rate_set = settings_.audio_.rate_set;
  config.channel_set = settings_.audio_.channel_set;
  config.pa_server = settings_.audio_.pa_server ? settings_.audio_.pa_server->data() : nullptr;
  config.pa_sink = settings_.audio_.pa_sink ? settings_.audio_.pa_sink->data() : nullptr;
  config.pa_application_name = settings_.audio_.pa_application_name ? settings_.audio_.pa_application_name->data() : nullptr;
  config.six_channel_layout = settings_.audio_.six_channel_layout;
  config.eight_channel_layout = settings_.audio_.eight_channel_layout;
  config.mixdown_enable = settings_.audio_.mixdown_enable;
  config.mixdown_channel_layout = settings_.audio_.mixdown_channel_layout;
  config.output_channel_mapping_enable = settings_.audio_.output_channel_mapping_enable;
  std::fill(std::begin(config.output_channel_map), std::end(config.output_channel_map), nullptr);
  config.output_channel_map_size = settings_.audio_.output_channel_map.size();
  for (unsigned i = 0; i < config.output_channel_map_size; ++i)
    config.output_channel_map[i] = settings_.audio_.output_channel_map[i].c_str();
  config.ignore_volume_control = settings_.volume_.ignore_volume_control;
  config.volume_max_db_set = settings_.volume_.volume_max_db_set;
  config.volume_max_db = settings_.volume_.volume_max_db;
  config.volume_range_db = settings_.volume_.volume_range_db;
  config.volume_range_hw_priority = settings_.volume_.volume_range_hw_priority;
  config.volume_control_profile = settings_.volume_.volume_control_profile;
  config.default_airplay_volume = settings_.volume_.default_airplay_volume;
  config.active_state_timeout = settings_.session_.active_state_timeout;
  config.allow_session_interruption = settings_.session_.allow_session_interruption;
  config.timeout = settings_.session_.timeout;
  config.dont_check_timeout = settings_.session_.dont_check_timeout;
  config.cmd_start = settings_.session_.cmd_start ? settings_.session_.cmd_start->data() : nullptr;
  config.cmd_stop = settings_.session_.cmd_stop ? settings_.session_.cmd_stop->data() : nullptr;
  config.cmd_set_volume = settings_.session_.cmd_set_volume ? settings_.session_.cmd_set_volume->data() : nullptr;
  config.cmd_unfixable = settings_.session_.cmd_unfixable ? settings_.session_.cmd_unfixable->data() : nullptr;
  config.cmd_active_start = settings_.session_.cmd_active_start ? settings_.session_.cmd_active_start->data() : nullptr;
  config.cmd_active_stop = settings_.session_.cmd_active_stop ? settings_.session_.cmd_active_stop->data() : nullptr;
  config.cmd_blocking = settings_.session_.cmd_blocking;
  config.cmd_start_returns_output = settings_.session_.cmd_start_returns_output;
  config.resend_control_first_check_time = settings_.session_.resend_control_first_check_time;
  config.resend_control_check_interval_time = settings_.session_.resend_control_check_interval_time;
  config.resend_control_last_check_time = settings_.session_.resend_control_last_check_time;
  config.endianness = settings_.network_.endianness;
  config.cfg = settings_.nativeConfiguration();
  default_channel_layouts = settings_.audio_.defaultChannelLayouts ?
      settings_.audio_.defaultChannelLayouts->c_str() : nullptr;
  config.output_format_auto_requested = 1;
  config.output_rate_auto_requested = 1;
  config.decoder_in_use = 1 << decoder_ffmpeg_alac;
  config.log_fd = -1;
}

char *LegacyConfigLease::configurationRealPath() {
  return settings_.network_.realPath ? settings_.network_.realPath->data() : nullptr;
}
