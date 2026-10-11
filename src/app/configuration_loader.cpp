/*
 * Utility routines. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * The volume to attenuation function vol2attn copyright (c) Mike Brady 2014
 * Further changes and additions (c) Mike Brady 2014--2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "app/configuration_loader.hpp"
#include "app/configuration_validation.hpp"
#include "audio/format/native_format.hpp"
#include "packets/packet_limits.h"
#include "platform/utilities/string_utilities.hpp"
#include <libconfig.h>
extern "C" {
#include <libavutil/channel_layout.h>
}
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <inttypes.h>
#include <stdexcept>
#include <strings.h>
#include <unistd.h>

struct ReceiverSettings::ParsedConfiguration {
  config_t tree{};
  bool loaded = false;
  ParsedConfiguration() { config_init(&tree); }
  ~ParsedConfiguration() { config_destroy(&tree); }
};

ReceiverSettings::ReceiverSettings() : parsed_(std::make_unique<ParsedConfiguration>()) {}
ReceiverSettings::~ReceiverSettings() = default;
ReceiverSettings::ReceiverSettings(ReceiverSettings &&) noexcept = default;
ReceiverSettings &ReceiverSettings::operator=(ReceiverSettings &&) noexcept = default;
config_t *ReceiverSettings::nativeConfiguration() const {
  return parsed_ && parsed_->loaded ? &parsed_->tree : nullptr;
}

struct ConfigurationValues {
  config_t *cfg = nullptr;
  int verbosity = 0;
  int debugger_show_elapsed_time{};
  int debugger_show_relative_time{};
  int debugger_show_file_and_line{};
  int statistics_requested{};
  int logOutputLevel{};
  int disable_resend_requests{};
  double diagnostic_drop_packet_fraction{};
  const char *appName{};
  const char *configfile{};
  const char *service_name{};
  const char *password{};
  const char *regtype{};
  const char *regtype2{};
  const char *interface{};
  unsigned interface_index{};
  int port{};
  int udp_port_base{};
  int udp_port_range{};
  double missing_port_dacp_scan_interval_seconds{};
  const char *model{};
  const char *srcvers{};
  const char *osvers{};
  const char *firmware_version{};
  const char *airplay_device_id{};
  uint8_t ap1_prefix[6]{};
  uint8_t hw_addr[8]{};
  const char *nqptp_shared_memory_interface_name{};
  stuffing_type packet_stuffing{};
  playback_mode_type playback_mode{};
  double audio_backend_buffer_desired_length{};
  double audio_backend_buffer_interpolation_threshold_in_seconds{};
  double audio_backend_latency_offset{};
  double audio_decoded_buffer_desired_length{};
  int audio_backend_silent_lead_in_time_auto{};
  double audio_backend_silent_lead_in_time{};
  int buffer_start_fill{};
  uint32_t userSuppliedLatency{};
  uint32_t fixedLatencyOffset{};
  uint32_t minimum_free_buffer_headroom{};
  double resync_threshold{};
  double tolerance{};
  uint32_t format_set{};
  uint32_t rate_set{};
  uint32_t channel_set{};
  const char *pa_server{};
  const char *pa_sink{};
  const char *pa_application_name{};
  uint64_t six_channel_layout{};
  uint64_t eight_channel_layout{};
  int mixdown_enable{};
  uint64_t mixdown_channel_layout{};
  int output_channel_mapping_enable{};
  const char *output_channel_map[8]{};
  unsigned output_channel_map_size = 0;
  int ignore_volume_control{};
  int volume_max_db_set{};
  int volume_max_db{};
  uint32_t volume_range_db{};
  int volume_range_hw_priority{};
  volume_control_profile_type volume_control_profile{};
  double default_airplay_volume{};
  double active_state_timeout{};
  int allow_session_interruption{};
  int timeout{};
  int dont_check_timeout{};
  const char *cmd_start{};
  const char *cmd_stop{};
  const char *cmd_set_volume{};
  const char *cmd_unfixable{};
  const char *cmd_active_start{};
  const char *cmd_active_stop{};
  int cmd_blocking{};
  int cmd_start_returns_output{};
  double resend_control_first_check_time{};
  double resend_control_check_interval_time{};
  double resend_control_last_check_time{};
};

class ConfigurationReader {
public:
  explicit ConfigurationReader(const ConfigurationEnvironment &input)
      : environment(input), config_file_stuff(settings.parsed_->tree) {}
  ReceiverSettings read(const StartupOptions &options) {
    config.appName = copyText("shairport-2");
    config.configfile = copyText(options.configurationPath().value_or(environment.defaultPath));
    if (options.configurationPath() && access(config.configfile, R_OK) != 0)
      rejectConfiguration("Unable to read configuration %s: %s", config.configfile, strerror(errno));
    config.debugger_show_file_and_line = 1;
    config.debugger_show_relative_time = 1;
    config.timeout = 60;
    config.buffer_start_fill = 220;
    config.resync_threshold = 0.050;
    config.tolerance = 0.002;
    config.packet_stuffing = ST_vernier;
    config.audio_backend_buffer_desired_length = 0.15;
    config.audio_decoded_buffer_desired_length = 0.75;
    config.udp_port_base = 6001;
    config.udp_port_range = 10;
    config.output_channel_mapping_enable = 1;
    config.mixdown_enable = 1;
    std::copy(environment.hardwareAddress.begin(), environment.hardwareAddress.end(), config.hw_addr);
    loadReceiverConfiguration(options);
    if (config.userSuppliedLatency != 0 &&
        (config.userSuppliedLatency < 4410 || config.userSuppliedLatency > BUFFER_FRAMES * 352 - 22050))
      rejectConfiguration("An out-of-range fixed latency has been specified. It must be between 4410 and %d (at "
          "44100 frames per second).", BUFFER_FRAMES * 352 - 22050);
    return finish();
  }
  const std::vector<ConfigurationDiagnostic> &diagnostics() const { return messages; }

private:
  [[gnu::format(printf, 1, 0)]] static std::string formatted(const char *format, va_list arguments) {
    va_list measuring;
    va_copy(measuring, arguments);
    const auto length = vsnprintf(nullptr, 0, format, measuring);
    va_end(measuring);
    if (length < 0) return "Configuration diagnostic could not be formatted";
    std::string text(static_cast<size_t>(length), '\0');
    vsnprintf(text.data(), text.size() + 1, format, arguments);
    return text;
  }
  [[gnu::format(printf, 2, 3)]] void recordWarning(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    auto message = formatted(format, arguments);
    va_end(arguments);
    messages.push_back({ConfigurationDiagnostic::Kind::warning, std::move(message)});
  }
  [[gnu::format(printf, 2, 3)]] void recordNotice(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    auto message = formatted(format, arguments);
    va_end(arguments);
    messages.push_back({ConfigurationDiagnostic::Kind::notice, std::move(message)});
  }
  [[gnu::format(printf, 3, 4)]] void recordDebug(int level, const char *format, ...) {
    if (level > config.verbosity) return;
    va_list arguments;
    va_start(arguments, format);
    auto message = formatted(format, arguments);
    va_end(arguments);
    messages.push_back({ConfigurationDiagnostic::Kind::debug, std::move(message)});
  }
  [[noreturn, gnu::format(printf, 2, 3)]] void rejectConfiguration(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    auto message = formatted(format, arguments);
    va_end(arguments);
    throw std::runtime_error(message);
  }
  const char *copyText(std::string_view value) {
    ownedText.emplace_back(value);
    return ownedText.back().c_str();
  }
void reject_removed_settings(config_t *tree) {
  const char *removed[] = {"alsa", "jack", "sndio", "ao", "soundio", "pipewire", "pipe",
                           "stdout", "dummy", "dsp", "metadata", "dbus", "mpris", "mqtt",
                           "general.service_type", "general.output_backend", "general.mdns_backend",
                           "general.alac_decoder", "diagnostics.get_plist_metadata",
                           "sessioncontrol.daemonize_with_pid_file",
                           "sessioncontrol.daemonize_without_pid_file",
                           "sessioncontrol.daemon_pid_dir", "general.soxr_delay_threshold",
                           "general.dbus_service_bus", "general.mpris_service_bus",
                           "diagnostics.retain_cover_art"};
  for (size_t index = 0; index < sizeof(removed) / sizeof(removed[0]); index++) {
    if (config_lookup(tree, removed[index]) != NULL)
      rejectConfiguration("%s is a removed option in this AirPlay 2 Linux PulseAudio fork.", removed[index]);
  }
}
int config_lookup_non_empty_string(const config_t *cfg, const char *path, const char **value) {
  int response = CONFIG_FALSE;
  config_setting_t *s = config_lookup(cfg, path);
  if (s != NULL) {
    // the setting exists, but might not be a string
    if (config_setting_type(s) == CONFIG_TYPE_STRING) {
      if (value != NULL) {
        *value = config_setting_get_string(s);
        response = CONFIG_TRUE;
        // the string might be empty...
        if ((*value == NULL) || (*value[0] == 0)) {
          recordWarning("The \"%s\" parameter is an empty string and has been ignored.", path);
          response = CONFIG_FALSE;
        }
      }
    } else {
      recordWarning("the \"%s\" parameter is not a string, as required, and has been ignored.", path);
    }
  }
  return response;
}

int config_set_lookup_bool(config_t *cfg, const char *where, int *dst) {
  const char *str = NULL;
  int response = CONFIG_FALSE;
  config_setting_t *s = config_lookup(cfg, where);
  if (s != NULL) {
    if (config_setting_type(s) == CONFIG_TYPE_STRING) {
      str = config_setting_get_string(s);
      if (strcasecmp(str, "no") == 0) {
        (*dst) = 0;
        response = CONFIG_TRUE;
      } else if (strcasecmp(str, "yes") == 0) {
        (*dst) = 1;
        response = CONFIG_TRUE;
      } else {
        rejectConfiguration("invalid boolean parameter \"%s\" option choice \"%s\". It should be \"yes\" or \"no\"",
            where, str);
        return 0;
      }
    } else {
      recordWarning("the \"%s\" parameter is not a string with a value of \"yes\" or \"no\", as required, "
           "and has been ignored.",
           where);
    }
  }
  return response;
}

// remember to free the returned array of strings.
// you don't need to free the strings themselves -- they belong to libconfig.
unsigned int config_get_string_settings_as_string_array(config_setting_t *setting,
                                                        const char ***result) {
  unsigned int count = 0;
  int error = 0;
  *result = NULL;
  const char **arr = NULL;
  if (setting != NULL) { // definitely a setting
    const char *str = config_setting_get_string(setting);
    if (str != NULL) { // definitely a string
      arr = static_cast<const char **>(malloc(sizeof(const char *)));
      arr[0] = str;
      count = 1;
    } else { // it might be a list, an array or a group
      count = config_setting_length(setting);
      if (count != 0) {
        arr = static_cast<const char **>(malloc(sizeof(const char *) * count));
        unsigned int i;
        for (i = 0; i < count; i++) {
          config_setting_t *item = config_setting_get_elem(setting, i);
          if (config_setting_type(item) == CONFIG_TYPE_STRING)
            arr[i] = config_setting_get_string(item);
          else
            error = i + 1;
        }
      } else {
        error = 1;
      }
    }
  }
  if (error != 0) {
    if (arr != NULL) {
      free(arr);
    }
    count = -error; // signify an error
  } else {
    *result = arr;
  }
  return count;
}

// remember to free the returned array of ints.
unsigned int config_get_int_settings_as_int_array(config_setting_t *setting, int **result) {
  int error = 0;
  unsigned int count = 0;
  *result = NULL;
  int *arr = NULL;
  if (setting != NULL) { // definitely a setting there
    if (config_setting_type(setting) == CONFIG_TYPE_INT) {
      arr = static_cast<int *>(malloc(sizeof(int)));
      arr[0] = config_setting_get_int(setting);
      count = 1;
    } else if (config_setting_is_aggregate(setting) == CONFIG_TRUE) {
      count = config_setting_length(setting);
      if (count != 0) {
        arr = static_cast<int *>(malloc(sizeof(int) * count));
        unsigned int i;
        for (i = 0; i < count; i++) {
          config_setting_t *item = config_setting_get_elem(setting, i);
          if (config_setting_type(item) == CONFIG_TYPE_INT)
            arr[i] = config_setting_get_int(item);
          else
            error = i + 1;
        }
      }
    } else {
      error = 1; // subtract 1 from the error number to get the element number
    }
  }
  if (error != 0) {
    if (arr != NULL) {
      free(arr);
    }
    count = -error; // signify an error
  } else {
    *result = arr;
  }
  return count;
}

void parse_audio_options(const char *named_stanza, uint32_t default_format_set,
                         uint32_t default_rate_set, uint32_t default_channel_set) {
  /* this must be called after the output device has been initialised, so that the default values
   * are set before any options are chosen */
  int value;
  double dvalue;
  const char *str = 0;
  if (config.cfg != NULL) {

    /* Get the desired buffer size setting (deprecated). */
    if (config_lookup_int(config.cfg, "general.audio_backend_buffer_desired_length", &value)) {
      recordNotice("The setting general.audio_backend_buffer_desired_length is no longer supported. "
             "Please use general.audio_backend_buffer_desired_length_in_seconds instead.");
    }

    /* Get the desired backend buffer size setting in seconds. */
    /* This is the size of the buffer in the output system, e.g. in the DAC itself in ALSA */
    if (config_lookup_float(config.cfg, "general.audio_backend_buffer_desired_length_in_seconds",
                            &dvalue)) {
      if (dvalue < 0) {
        rejectConfiguration("Invalid audio_backend_buffer_desired_length_in_seconds value: \"%f\". It "
            "should be 0.0 or greater."
            " The default is %.3f seconds",
            dvalue, config.audio_backend_buffer_desired_length);
      } else {
        config.audio_backend_buffer_desired_length = dvalue;
      }
    }

    /* Get the desired decoded buffer size setting in seconds. */
    /* This is the size of the buffer of decoded and deciphered audio held in the player's output
     * queue prior to sending it to the output system */
    if (config_lookup_float(config.cfg, "general.audio_decoded_buffer_desired_length_in_seconds",
                            &dvalue)) {
      if (dvalue < 0) {
        rejectConfiguration("Invalid audio_decoded_buffer_desired_length_in_seconds value: \"%f\". It "
            "should be 0.0 or greater."
            " The default is %.3f seconds",
            dvalue, config.audio_decoded_buffer_desired_length);
      } else {
        config.audio_decoded_buffer_desired_length = dvalue;
      }
    }

    /* Get the minimum buffer size for fancy interpolation setting in seconds. */
    if (config_lookup_float(config.cfg,
                            "general.audio_backend_buffer_interpolation_threshold_in_seconds",
                            &dvalue)) {
      if ((dvalue < 0) || (dvalue > config.audio_backend_buffer_desired_length)) {
        rejectConfiguration("Invalid audio_backend_buffer_interpolation_threshold_in_seconds value: \"%f\". It "
            "should be between 0 and "
            "audio_backend_buffer_desired_length_in_seconds of %.3f, default is %.3f seconds",
            dvalue, config.audio_backend_buffer_desired_length,
            config.audio_backend_buffer_interpolation_threshold_in_seconds);
      } else {
        config.audio_backend_buffer_interpolation_threshold_in_seconds = dvalue;
      }
    }

    /* Get the latency offset (deprecated). */
    if (config_lookup_int(config.cfg, "general.audio_backend_latency_offset", &value)) {

      recordNotice("The setting general.audio_backend_latency_offset is no longer supported. "
             "Please use general.audio_backend_latency_offset_in_seconds instead.");
    }

    /* Get the latency offset in seconds. */
    if (config_lookup_float(config.cfg, "general.audio_backend_latency_offset_in_seconds",
                            &dvalue)) {
      config.audio_backend_latency_offset = dvalue;
    }

    /* Check if the length of the silent lead-in ia set to \"auto\". */
    if (config_lookup_string(config.cfg, "general.audio_backend_silent_lead_in_time", &str)) {
      if (strcasecmp(str, "auto") == 0) {
        config.audio_backend_silent_lead_in_time_auto = 1;
      } else {
        if (config.audio_backend_silent_lead_in_time_auto == 1)
          recordWarning("Invalid audio_backend_silent_lead_in_time \"%s\". It should be \"auto\" or the "
               "lead-in time in seconds. "
               "It remains set to \"auto\". Note: numbers should not be placed in quotes.",
               str);
        else
          recordWarning("Invalid output rate \"%s\". It should be \"auto\" or the lead-in time in seconds. "
               "It remains set to %f. Note: numbers should not be placed in quotes.",
               str, config.audio_backend_silent_lead_in_time);
      }
    }

    /* Get the desired length of the silent lead-in. */
    if (config_lookup_float(config.cfg, "general.audio_backend_silent_lead_in_time", &dvalue)) {
      if ((dvalue < 0.0) || (dvalue > 4)) {
        if (config.audio_backend_silent_lead_in_time_auto == 1)
          recordWarning("Invalid audio_backend_silent_lead_in_time \"%f\". It "
               "must be between 0.0 and 4.0 seconds. Omit the setting to use the automatic value. "
               "The setting remains at \"auto\".",
               dvalue);
        else
          recordWarning("Invalid audio_backend_silent_lead_in_time \"%f\". It "
               "must be between 0.0 and 4.0 seconds. Omit the setting to use the automatic value. "
               "It remains set to %f.",
               dvalue, config.audio_backend_silent_lead_in_time);
      } else {
        config.audio_backend_silent_lead_in_time = dvalue;
        config.audio_backend_silent_lead_in_time_auto = 0;
      }
    }
  }

  if (named_stanza != NULL) {
    config.format_set = get_format_settings(named_stanza, "output_format");
    config.rate_set = get_rate_settings(named_stanza, "output_rate");
    config.channel_set = get_channel_settings(named_stanza, "output_channels");
  }
  // use the supplied defaults if no settings were given
  if (config.format_set == 0)
    config.format_set = default_format_set;
  if (config.rate_set == 0)
    config.rate_set = default_rate_set;
  if (config.channel_set == 0)
    config.channel_set = default_channel_set;
}

uint32_t get_format_settings(const char *stanza_name, const char *setting_name) {
  uint32_t format_set = 0;
  if (config.cfg != NULL) {
    char setting_path[256];
    snprintf(setting_path, sizeof(setting_path) - 1, "%s.%s", stanza_name, setting_name);
    // get any format settings -- can be "auto", a single format e.g. "S16" or a list of formats
    config_setting_t *format_setting = config_lookup(config.cfg, setting_path);
    if (format_setting != NULL) {
      const char **format_settings;
      int format_settings_count =
          config_get_string_settings_as_string_array(format_setting, &format_settings);
      if (format_settings_count > 0) {
        int i;
        for (i = 0; i < format_settings_count; i++) {
          recordDebug(3, "format setting %u: \"%s\".", i, format_settings[i]);
          if (strcmp(format_settings[i], "auto") == 0) {
            if (format_settings_count != 1)
              recordWarning("in the \"%s\" setting in the \"%s\" section of the configuration file, "
                   "multiple formats, including \"auto\", "
                   "are specified, but \"auto\" includes all "
                   "formats anyway...",
                   setting_name, stanza_name);
            format_set = SPS_FORMAT_SET; // all valid formats
          } else if (strcmp(format_settings[i], "S16") == 0) {
            format_set |= (1 << SPS_FORMAT_S16_LE) | (1 << SPS_FORMAT_S16_BE);
          } else if (strcmp(format_settings[i], "S24") == 0) {
            format_set |= (1 << SPS_FORMAT_S24_LE) | (1 << SPS_FORMAT_S24_BE) |
                          (1 << SPS_FORMAT_S24_3LE) | (1 << SPS_FORMAT_S24_3BE);
          } else if (strcmp(format_settings[i], "S32") == 0) {
            format_set |= (1 << SPS_FORMAT_S32_LE) | (1 << SPS_FORMAT_S32_BE);
          } else {
            sps_format_t f;
            int valid = 0;
            for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE;
                 f = static_cast<sps_format_t>(f + 1)) {
              if (strcmp(format_settings[i], sps_format_description_string(f)) == 0) {
                format_set |= (1 << f);
                valid = 1;
              }
            }
            if (valid == 0) {
              recordWarning("in the \"%s\" setting in the \"%s\" section of the configuration file, an "
                   "invalid format: \"%s\" has been detected.",
                   setting_name, stanza_name, format_settings[i]);
            }
          }
        }
        free(format_settings);
      } else {
        recordDebug(1,
              "in the \"%s\" setting in the \"%s\" section of the configuration file, an error has "
              "been detected at argument %d.",
              setting_name, stanza_name, -format_settings_count);
      }
    }
  }
  sps_format_t f;
  uint32_t t_format_set = format_set;
  char buf[256];
  char *p = buf;
  for (f = SPS_FORMAT_UNKNOWN; f <= SPS_FORMAT_S32_BE; f = static_cast<sps_format_t>(f + 1)) {
    if ((t_format_set & (1 << f)) != 0) {
      snprintf(p, sizeof(buf) - (p - buf) - 1, "%s", sps_format_description_string(f));
      p = p + strlen(sps_format_description_string(f));
      t_format_set -= (1 << f);
      if (t_format_set != 0) {
        snprintf(p, sizeof(buf) - (p - buf) - 1, ", ");
        p = p + strlen(", ");
      }
    }
  }
  if (p != buf) {
    *p = '.';
    p++;
  }
  *p = '\0';
  if (strlen(buf) == 0)
    recordDebug(3, "No \"%s\" output format settings.", stanza_name);
  else
    recordDebug(3, "The \"%s\" output format settings are: \"%s\".", stanza_name, buf);
  return format_set;
}

uint32_t get_rate_settings(const char *stanza_name, const char *setting_name) {
  uint32_t rate_set = 0;
  if (config.cfg != NULL) {
    char setting_path[256];
    snprintf(setting_path, sizeof(setting_path) - 1, "%s.%s", stanza_name, setting_name);
    config_setting_t *rate_setting = config_lookup(config.cfg, setting_path);
    if (rate_setting != NULL) {
      if (config_setting_type(rate_setting) == CONFIG_TYPE_STRING) {
        // see if it is "auto"
        if (strcmp(config_setting_get_string(rate_setting), "auto") == 0) {
          rate_set = SPS_RATE_SET; // all valid rates
        } else {
          recordWarning("In the \"%s\" setting in the \"%s\" section of the configuration file, an invalid "
               "character string -- \"%s\" -- has been detected. (Note that numbers must not be "
               "enclosed in quotes.)",
               setting_name, stanza_name, config_setting_get_string(rate_setting));
        }
      } else {
        int *rates;
        int rate_settings_count = config_get_int_settings_as_int_array(rate_setting, &rates);
        if (rate_settings_count > 0) {
          recordDebug(3, "%d rate settings found.", rate_settings_count);
          int i;
          for (i = 0; i < rate_settings_count; i++) {
            recordDebug(3, "rate setting %d: %d.", i, rates[i]);
            sps_rate_t r;
            int valid = 0;
            for (r = SPS_RATE_5512; r <= SPS_RATE_384000; r = static_cast<sps_rate_t>(r + 1)) {
              if ((unsigned int)rates[i] == sps_rate_actual_rate(r)) {
                valid = 1;

                rate_set |= (1 << r);
              }
            }
            if (valid == 0) {
              recordWarning("In the \"%s\" setting in the \"%s\" section of the configuration file, an "
                   "invalid rate: %d has been detected.",
                   setting_name, stanza_name, rates[i]);
            }
          }
          free(rates);
        } else {
          recordWarning("in the \"%s\" setting in the \"%s\" section of the configuration file, an error "
               "has been detected at argument %d. (Note that numbers must not be enclosed in "
               "quotes.)",
               setting_name, stanza_name, -rate_settings_count);
        }
      }
    }
  }
  sps_rate_t r;
  char buf[256];
  char *p = buf;
  uint32_t t_rate_set = rate_set;
  char numbuf[32];
  for (r = SPS_RATE_UNKNOWN; r <= SPS_RATE_384000; r = static_cast<sps_rate_t>(r + 1)) {
    if ((t_rate_set & (1 << r)) != 0) {
      snprintf(numbuf, sizeof(numbuf) - 1, "%u", sps_rate_actual_rate(r));
      snprintf(p, sizeof(buf) - (p - buf) - 1, "%s", numbuf);
      p = p + strlen(numbuf);
      t_rate_set -= (1 << r);
      if (t_rate_set != 0) {
        snprintf(p, sizeof(buf) - (p - buf) - 1, ", ");
        p = p + strlen(", ");
      }
    }
  }
  if (p != buf) {
    *p = '.';
    p++;
  }
  *p = '\0';
  if (strlen(buf) == 0)
    recordDebug(3, "No \"%s\" output rate settings.", stanza_name);
  else
    recordDebug(3, "The \"%s\" output rate settings are: \"%s\".", stanza_name, buf);
  return rate_set;
}

uint32_t get_channel_settings(const char *stanza_name, const char *setting_name) {
  uint32_t channel_set = 0;
  if (config.cfg != NULL) {
    char setting_path[256];
    snprintf(setting_path, sizeof(setting_path) - 1, "%s.%s", stanza_name, setting_name);
    // now get the channels count set
    config_setting_t *channels_setting = config_lookup(config.cfg, setting_path);
    if (channels_setting != NULL) {
      if (config_setting_type(channels_setting) == CONFIG_TYPE_STRING) {
        // see if it is "auto"
        if (strcmp(config_setting_get_string(channels_setting), "auto") == 0) {
          channel_set = SPS_CHANNEL_SET; // all valid channels
        } else {
          recordWarning("in the \"%s\" setting in the \"%s\" section of the configuration file, an invalid "
               "setting: \"%s\" has been detected.",
               setting_name, stanza_name, config_setting_get_string(channels_setting));
        }
      } else {
        int *channel_counts;
        int channel_counts_count =
            config_get_int_settings_as_int_array(channels_setting, &channel_counts);
        if (channel_counts_count > 0) {
          recordDebug(3, "%d channel count settings found.", channel_counts_count);
          int i;
          for (i = 0; i < channel_counts_count; i++) {
            recordDebug(3, "channel count setting %d: %d.", i, channel_counts[i]);

            if ((channel_counts[i] >= 1) && (channel_counts[i] <= SPS_GREATEST_CHANNEL_COUNT)) {
              channel_set |= (1 << channel_counts[i]);
            } else {
              recordWarning("in the \"%s\" setting in the \"%s\" section of the configuration file, an "
                   "invalid channel count: %d has been detected.",
                   setting_name, stanza_name, channel_counts[i]);
            }
          }
          free(channel_counts);
        } else {
          recordDebug(1,
                "in the \"%s\" setting in the \"%s\" section of the configuration file, an error "
                "has been detected at argument %d.",
                setting_name, stanza_name, -channel_counts_count);
        }
      }
    }
  }
  char buf[256];
  char *p = buf;
  char numbuf[32];
  unsigned int c;
  uint32_t t_channel_set = channel_set;
  for (c = 0; c <= SPS_GREATEST_CHANNEL_COUNT; c++) {
    if ((t_channel_set & (1 << c)) != 0) {
      snprintf(numbuf, sizeof(numbuf) - 1, "%u", c);
      snprintf(p, sizeof(buf) - (p - buf) - 1, "%s", numbuf);
      p = p + strlen(numbuf);
      t_channel_set -= (1 << c);
      if (t_channel_set != 0) {
        snprintf(p, sizeof(buf) - (p - buf) - 1, ", ");
        p = p + strlen(", ");
      }
    }
  }
  if (p != buf) {
    *p = '.';
    p++;
  }
  *p = '\0';
  if (strlen(buf) == 0)
    recordDebug(3, "No \"%s\" output channel settings.", stanza_name);
  else
    recordDebug(3, "The \"%s\" output channel settings are: \"%s\".", stanza_name, buf);
  return channel_set;
}

void load_pulseaudio_settings() {
  // recordDebug(1, "pa_init");
  // set up default values first
  config.audio_backend_buffer_desired_length = 0.35;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.02; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.

  config.audio_backend_latency_offset = 0;

  // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
  // Note, these options may be in the "general" stanza or the named stanza
  parse_audio_options("pulseaudio", SPS_FORMAT_SET, SPS_RATE_SET, SPS_CHANNEL_SET);

  // now the specific options
  if (config.cfg != NULL) {
    const char *str;

    /* Get the PulseAudio server name. */
    if (config_lookup_non_empty_string(config.cfg, "pulseaudio.server", &str)) {
      config.pa_server = (char *)str;
    }

    // get the default channel mapping setting basis -- "alsa" or "pulseaudio".

    if (config_lookup_non_empty_string(config.cfg, "pulseaudio.default_channel_layouts",
                                       &default_channel_layouts)) {
      if ((strcasecmp(default_channel_layouts, "alsa") == 0) ||
          (strcasecmp(default_channel_layouts, "pulseaudio") == 0)) {
        recordDebug(1, "pulseaudio default_channel_layouts setting: \"%s\".", default_channel_layouts);
      } else {
        recordDebug(1, "Invalid pulseaudio default_channel_layouts setting. Must be \"alsa\" or "
                 "\"pulseaudio\".");
        default_channel_layouts = NULL;
      }
    };

    /* Get the Application Name. */
    if (config_lookup_non_empty_string(config.cfg, "pulseaudio.application_name", &str)) {
      config.pa_application_name = (char *)str;
    }

    /* Get the PulseAudio sink name. */
    if (config_lookup_non_empty_string(config.cfg, "pulseaudio.sink", &str)) {
      config.pa_sink = (char *)str;
    }
  }

}
void loadReceiverConfiguration(const StartupOptions &options) {
  char *raw_service_name = nullptr;
  config.audio_backend_silent_lead_in_time_auto =
      1; // start outputting silence as soon as packets start arriving
  config.default_airplay_volume = -24.0;
  config.fixedLatencyOffset = 11025; // this sounds like it works properly.
  config.diagnostic_drop_packet_fraction = 0.0;
  config.active_state_timeout = 10.0;
                                              // is to be chosen automatically.
  config.volume_range_hw_priority =
      0; // if combining software and hardware volume control, give the software priority
         // i.e. when reducing volume, reduce the sw first before reducing the software.
         // this is because some hw mixers mute at the bottom of their range, and they don't always
  // advertise this fact
  config.resend_control_first_check_time =
      0.10; // wait this many seconds before requesting the resending of a missing packet
  config.resend_control_check_interval_time =
      0.25; // wait this many seconds before again requesting the resending of a missing packet
  config.resend_control_last_check_time =
      0.10; // give up if the packet is still missing this close to when it's needed
  config.missing_port_dacp_scan_interval_seconds =
      2.0; // check at this interval if no DACP port number is known

  config.minimum_free_buffer_headroom = 125; // leave approximately one second's worth of buffers
                                             // free after calculating the effective latency.
  // e.g. if we have 1024 buffers or 352 frames = 8.17 seconds and we have a nominal latency of 2.0
  // seconds then we can add an offset of 5.17 seconds and still leave a second's worth of buffers
  // for unexpected circumstances

  config.model = copyText("ShairportSync");
  // config.model = copyText("AirPort10,115");
  //  config.model = copyText("AudioAccessory5,1");

  // config.srcvers = copyText(PACKAGE_VERSION);
  // config.srcvers = copyText("760.13.1");

  config.srcvers = copyText("366.0");

  // config.osvers = copyText(VERSION);
  config.osvers = copyText("15.0");

  // make up a firmware version
config.firmware_version = copyText(environment.firmwareVersion);




  // config_setting_t *setting;
  const char *str = NULL;
  int value = 0;
  double dvalue = 0.0;

  // recordDebug(1, "Looking for the configuration file \"%s\".", config.configfile);

  // use the MAC address placed in config.hw_addr to generate the default airplay_device_id
  uint64_t temporary_airplay_id = 0;
  for (const auto byte : environment.hardwareAddress)
    temporary_airplay_id = (temporary_airplay_id << 8) | byte;

  resolvedPath.reset(realpath(config.configfile, NULL));
  config_file_real_path = resolvedPath.get();
  if (config_file_real_path == NULL) {
    if (options.configurationPath() || errno != ENOENT)
      rejectConfiguration("Unable to read configuration %s: %s", config.configfile, strerror(errno));
    recordDebug(2, "can't resolve the configuration file \"%s\".", config.configfile);
  } else {
    recordDebug(1, "looking for configuration file at full path \"%s\"", config_file_real_path);
    /* Read the file. If there is an error, report it and exit. */
    if (config_read_file(&config_file_stuff, config_file_real_path)) {
      config_set_auto_convert(&config_file_stuff,
                              1); // allow autoconversion from int/float to int/float
      // make config.cfg point to it
      config.cfg = &config_file_stuff;
      settings.parsed_->loaded = true;
      reject_removed_settings(config.cfg);
      if (const auto valid = validateConfigurationTypes(*config.cfg); !valid)
        rejectConfiguration("%s", valid.error().c_str());

      /* See if a specific service type has been requested */
      if (config_lookup_non_empty_string(config.cfg, "general.service_type", &str)) {
        rejectConfiguration("general.service_type is a removed option; only AirPlay 2 is supported.");
      }
      /* Get the Service Name. */
      if (config_lookup_non_empty_string(config.cfg, "general.name", &str)) {
        raw_service_name = (char *)str;
      }


      /* Get the port setting. */
      if (config_lookup_int(config.cfg, "general.port", &value)) {
        if ((value < 0) || (value > 65535))
          rejectConfiguration("Invalid port number  \"%d\". It should be between 0 and 65535, default is 7000",
              value);
        else
          config.port = value;
      }

      /* Get the udp port base setting. */
      if (config_lookup_int(config.cfg, "general.udp_port_base", &value)) {
        if ((value < 0) || (value > 65535))
          rejectConfiguration("Invalid port number  \"%d\". It should be between 0 and 65535, default is 6001",
              value);
        else
          config.udp_port_base = value;
      }

      /* Get the udp port range setting. This is number of ports that will be tried for free ports ,
       * starting at the port base. Only three ports are needed. */
      if (config_lookup_int(config.cfg, "general.udp_port_range", &value)) {
        if ((value < 3) || (value > 65535))
          rejectConfiguration("Invalid port range  \"%d\". It should be between 3 and 65535, default is 10", value);
        else
          config.udp_port_range = value;
      }

      /* Get the password setting. */
      if (config_lookup_non_empty_string(config.cfg, "general.password", &str))
        config.password = (char *)str;

      if (config_lookup_string(config.cfg, "general.interpolation", &str)) {
        if (strcasecmp(str, "basic") == 0)
          config.packet_stuffing = ST_basic;
        else if (strcasecmp(str, "vernier") == 0)
          config.packet_stuffing = ST_vernier;
        else if (strcasecmp(str, "auto") == 0)
          config.packet_stuffing = ST_auto;
        else if (strcasecmp(str, "soxr") == 0)
          rejectConfiguration("soxr is a removed option; use auto, basic or vernier interpolation.");
        else
          rejectConfiguration("Invalid interpolation option choice \"%s\". It should be \"auto\", \"basic\", "
              "\"vernier\" or "
              "\"soxr\"",
              str);
      }


      /* Get the statistics setting. */
      if (config_set_lookup_bool(config.cfg, "general.statistics",
                                 &(config.statistics_requested))) {
        recordWarning("The \"general\" \"statistics\" setting is deprecated. Please use the \"diagnostics\" "
             "\"statistics\" setting instead.");
      }

      /* The old drift tolerance setting. */
      if (config_lookup_int(config.cfg, "general.drift", &value)) {
        recordNotice("The drift setting  is deprecated and ignored. Please use "
               "drift_tolerance_in_seconds instead");
      }

      /* The old resync setting. */
      if (config_lookup_int(config.cfg, "general.resync_threshold", &value)) {
        recordNotice("The resync_threshold setting is deprecated and ignored. Please use "
               "resync_threshold_in_seconds instead");
      }

      /* Get the drift tolerance setting. */
      if (config_lookup_float(config.cfg, "general.drift_tolerance_in_seconds", &dvalue))
        config.tolerance = dvalue;

      /* Get the resync setting. */
      if (config_lookup_float(config.cfg, "general.resync_threshold_in_seconds", &dvalue))
        config.resync_threshold = dvalue;

      /* Get the verbosity setting. */
      if (config_lookup_int(config.cfg, "general.log_verbosity", &value)) {
        recordWarning("The \"general\" \"log_verbosity\" setting is deprecated. Please use the "
             "\"diagnostics\" \"log_verbosity\" setting instead.");
        if ((value >= 0) && (value <= 3))
          config.verbosity = value;
        else
          rejectConfiguration("Invalid log verbosity setting option choice \"%d\". It should be between 0 and 3, "
              "inclusive.",
              value);
      }


      /* Get the verbosity setting. */
      if (config_lookup_int(config.cfg, "diagnostics.log_verbosity", &value)) {
        if ((value >= 0) && (value <= 3))
          config.verbosity = value;
        else
          rejectConfiguration("Invalid diagnostics log_verbosity setting option choice \"%d\". It should be "
              "between 0 and 3, "
              "inclusive.",
              value);
      }

      /* Get the config.debugger_show_file_and_line in debug messages setting. */
      if (config_lookup_string(config.cfg, "diagnostics.log_show_file_and_line", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.debugger_show_file_and_line = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.debugger_show_file_and_line = 1;
        else
          rejectConfiguration("Invalid diagnostics log_show_file_and_line option choice \"%s\". It should be "
              "\"yes\" or \"no\"",
              str);
      }

      /* Get the show elapsed time in debug messages setting. */
      if (config_lookup_string(config.cfg, "diagnostics.log_show_time_since_startup", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.debugger_show_elapsed_time = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.debugger_show_elapsed_time = 1;
        else
          rejectConfiguration("Invalid diagnostics log_show_time_since_startup option choice \"%s\". It should be "
              "\"yes\" or \"no\"",
              str);
      }

      /* Get the show relative time in debug messages setting. */
      if (config_lookup_string(config.cfg, "diagnostics.log_show_time_since_last_message", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.debugger_show_relative_time = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.debugger_show_relative_time = 1;
        else
          rejectConfiguration("Invalid diagnostics log_show_time_since_last_message option choice \"%s\". It "
              "should be \"yes\" or \"no\"",
              str);
      }

      /* Get the statistics setting. */
      if (config_lookup_string(config.cfg, "diagnostics.statistics", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.statistics_requested = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.statistics_requested = 1;
        else
          rejectConfiguration("Invalid diagnostics statistics option choice \"%s\". It should be \"yes\" or "
              "\"no\"",
              str);
      }

      if (config_lookup_string(config.cfg, "diagnostics.log_output_level", &str)) {
        if (strcasecmp(str, "yes") == 0)
          config.logOutputLevel = 1;
        else if (strcasecmp(str, "no") == 0)
          config.logOutputLevel = 0;
        else
          rejectConfiguration("Invalid diagnostics.log_output_level: expected yes or no");
      }

      /* Get the disable_resend_requests setting. */
      if (config_lookup_string(config.cfg, "diagnostics.disable_resend_requests", &str)) {
        config.disable_resend_requests = 0; // this is for legacy -- only set by -t 0
        if (strcasecmp(str, "no") == 0)
          config.disable_resend_requests = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.disable_resend_requests = 1;
        else
          rejectConfiguration("Invalid diagnostic disable_resend_requests option choice \"%s\". It should be "
              "\"yes\" "
              "or \"no\"",
              str);
      }

      /* Get the drop packets setting. */
      if (config_lookup_float(config.cfg, "diagnostics.drop_this_fraction_of_audio_packets",
                              &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.diagnostic_drop_packet_fraction = dvalue;
        else
          rejectConfiguration("Invalid diagnostics drop_this_fraction_of_audio_packets setting \"%f\". It should "
              "be "
              "between 0.0 and 1.0, "
              "inclusive.",
              dvalue);
      }

      /* Get the diagnostics output default. */
      if (config_lookup_string(config.cfg, "diagnostics.log_output_to", &str)) {

        recordWarning("the diagnostic \"log_output_to\" setting is obsolete and is ignored. All logging is to STDERR, which is directed to the system log when shairport-2 is running as a service.");
      }


      /* Get the ignore_volume_control setting. */
      if (config_lookup_string(config.cfg, "general.ignore_volume_control", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.ignore_volume_control = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.ignore_volume_control = 1;
        else
          rejectConfiguration("Invalid ignore_volume_control option choice \"%s\". It should be \"yes\" or \"no\"",
              str);
      }

      /* Get the optional volume_max_db setting. */
      if (config_lookup_float(config.cfg, "general.volume_max_db", &dvalue)) {
        // recordDebug(1, "Max volume setting of %f dB", dvalue);
        config.volume_max_db = dvalue;
        config.volume_max_db_set = 1;
      }

      /* Get the optional default_volume setting. */
      if (config_lookup_float(config.cfg, "general.default_airplay_volume", &dvalue)) {
        // recordDebug(1, "Default airplay volume setting of %f on the -30.0 to 0 scale", dvalue);
        if ((dvalue >= -30.0) && (dvalue <= 0.0)) {
          config.default_airplay_volume = dvalue;
        } else {
          recordWarning("The default airplay volume setting must be between -30.0 and 0.0.");
        }
      }

      if (config_lookup_non_empty_string(config.cfg, "general.run_this_when_volume_is_set", &str)) {
        config.cmd_set_volume = (char *)str;
      }

      /* Get the playback_mode setting */
      if (config_lookup_string(config.cfg, "general.playback_mode", &str)) {
        if (strcasecmp(str, "stereo") == 0)
          config.playback_mode = ST_stereo;
        else if (strcasecmp(str, "mono") == 0)
          config.playback_mode = ST_mono;
        else if (strcasecmp(str, "reverse stereo") == 0)
          config.playback_mode = ST_reverse_stereo;
        else if (strcasecmp(str, "both left") == 0)
          config.playback_mode = ST_left_only;
        else if (strcasecmp(str, "both right") == 0)
          config.playback_mode = ST_right_only;
        else
          rejectConfiguration("Invalid playback_mode choice \"%s\". It should be \"stereo\" (default), \"mono\", "
              "\"reverse stereo\", \"both left\", \"both right\"",
              str);
      }

      /* Get the volume control profile setting -- "standard" or "flat" */
      if (config_lookup_string(config.cfg, "general.volume_control_profile", &str)) {
        if (strcasecmp(str, "standard") == 0)
          config.volume_control_profile = VCP_standard;
        else if (strcasecmp(str, "flat") == 0)
          config.volume_control_profile = VCP_flat;
        else if (strcasecmp(str, "dasl_tapered") == 0)
          config.volume_control_profile = VCP_dasl_tapered;
        else
          rejectConfiguration("Invalid volume_control_profile choice \"%s\". It should be \"standard\" (default), "
              "\"dasl_tapered\", or \"flat\"",
              str);
      }

      config_set_lookup_bool(config.cfg, "general.volume_control_combined_hardware_priority",
                             &config.volume_range_hw_priority);

      /* Get the interface to listen on, if specified Default is all interfaces */
      /* we keep the interface name and the index */

      if (config_lookup_string(config.cfg, "general.interface", &str)) {

        config.interface = copyText(str);
        config.interface_index = environment.interfaceIndex ? environment.interfaceIndex(config.interface) : 0;

        if (config.interface_index == 0) {
          recordNotice(
              "The mdns service interface \"%s\" was not found, so the setting has been ignored.",
              config.interface);

          config.interface = NULL;
        }
      }

      /* Get the regtype -- the service type and protocol, separated by a dot. Default is
       * "_raop._tcp" */
      if (config_lookup_non_empty_string(config.cfg, "general.regtype", &str))
        config.regtype = copyText(str);

      /* Get the volume range, in dB, that should be used If not set, it means you just use the
       * range set by the mixer. */
      if (config_lookup_int(config.cfg, "general.volume_range_db", &value)) {
        if ((value < 30) || (value > 150))
          rejectConfiguration("Invalid volume range  %d dB. It should be between 30 and 150 dB. Zero means use "
              "the mixer's native range. The setting reamins at %d.",
              value, config.volume_range_db);
        else
          config.volume_range_db = value;
      }

if (config_lookup(config.cfg, "general.alac_decoder") != NULL)
  rejectConfiguration("general.alac_decoder is a removed option; FFmpeg is required.");

      /* Get the resend control settings. */
      if (config_lookup_float(config.cfg, "general.resend_control_first_check_time", &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.resend_control_first_check_time = dvalue;
        else
          recordWarning("Invalid general resend_control_first_check_time setting \"%f\". It should "
               "be "
               "between 0.0 and 3.0, "
               "inclusive. The setting remains at %f seconds.",
               dvalue, config.resend_control_first_check_time);
      }

      if (config_lookup_float(config.cfg, "general.resend_control_check_interval_time", &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.resend_control_check_interval_time = dvalue;
        else
          recordWarning("Invalid general resend_control_check_interval_time setting \"%f\". It should "
               "be "
               "between 0.0 and 3.0, "
               "inclusive. The setting remains at %f seconds.",
               dvalue, config.resend_control_check_interval_time);
      }

      if (config_lookup_float(config.cfg, "general.resend_control_last_check_time", &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 3.0))
          config.resend_control_last_check_time = dvalue;
        else
          recordWarning("Invalid general resend_control_last_check_time setting \"%f\". It should "
               "be "
               "between 0.0 and 3.0, "
               "inclusive. The setting remains at %f seconds.",
               dvalue, config.resend_control_last_check_time);
      }

      if (config_lookup_float(config.cfg, "general.missing_port_dacp_scan_interval_seconds",
                              &dvalue)) {
        if ((dvalue >= 0.0) && (dvalue <= 300.0))
          config.missing_port_dacp_scan_interval_seconds = dvalue;
        else
          recordWarning("Invalid general missing_port_dacp_scan_interval_seconds setting \"%f\". It should "
               "be "
               "between 0.0 and 300.0, "
               "inclusive. The setting remains at %f seconds.",
               dvalue, config.missing_port_dacp_scan_interval_seconds);
      }

      /* Get the default latency. Deprecated! */
      if (config_lookup_int(config.cfg, "latencies.default", &value))
        config.userSuppliedLatency = value;



      if (config_lookup_non_empty_string(config.cfg, "sessioncontrol.run_this_before_play_begins",
                                         &str)) {
        config.cmd_start = (char *)str;
      }

      if (config_lookup_non_empty_string(config.cfg, "sessioncontrol.run_this_after_play_ends",
                                         &str)) {
        config.cmd_stop = (char *)str;
      }

      if (config_lookup_non_empty_string(
              config.cfg, "sessioncontrol.run_this_before_entering_active_state", &str)) {
        config.cmd_active_start = (char *)str;
      }

      if (config_lookup_non_empty_string(
              config.cfg, "sessioncontrol.run_this_after_exiting_active_state", &str)) {
        config.cmd_active_stop = (char *)str;
      }

      if (config_lookup_float(config.cfg, "sessioncontrol.active_state_timeout", &dvalue)) {
        if (dvalue < 0.0)
          recordWarning("Invalid value \"%f\" for \"active_state_timeout\". It must be positive. "
               "The default of %f will be used instead.",
               dvalue, config.active_state_timeout);
        else
          config.active_state_timeout = dvalue;
      }

      if (config_lookup_non_empty_string(
              config.cfg, "sessioncontrol.run_this_if_an_unfixable_error_is_detected", &str)) {
        config.cmd_unfixable = (char *)str;
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.wait_for_completion", &str)) {
        if (strcasecmp(str, "no") == 0)
          config.cmd_blocking = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.cmd_blocking = 1;
        else
          recordWarning("Invalid \"wait_for_completion\" option choice \"%s\". It should be "
               "\"yes\" or \"no\". It is set to \"no\".",
               str);
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.before_play_begins_returns_output",
                               &str)) {
        if (strcasecmp(str, "no") == 0)
          config.cmd_start_returns_output = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.cmd_start_returns_output = 1;
        else
          rejectConfiguration("Invalid \"before_play_begins_returns_output\" option choice \"%s\". It "
              "should be "
              "\"yes\" or \"no\"",
              str);
      }

      if (config_lookup_string(config.cfg, "sessioncontrol.allow_session_interruption", &str)) {
        config.dont_check_timeout = 0; // this is for legacy -- only set by -t 0
        if (strcasecmp(str, "no") == 0)
          config.allow_session_interruption = 0;
        else if (strcasecmp(str, "yes") == 0)
          config.allow_session_interruption = 1;
        else
          rejectConfiguration("Invalid \"allow_interruption\" option choice \"%s\". It should be "
              "\"yes\" "
              "or \"no\"",
              str);
      }

      if (config_lookup_int(config.cfg, "sessioncontrol.session_timeout", &value)) {
        if (value == 0) {
          config.dont_check_timeout = 1;
        } else if (value < 60) {
          recordWarning("Invalid value \"%d\" for \"session_timeout\". It must be 0 (i.e. no timeout) or at "
               "least 60. "
               "The default of %d will be used instead.",
               value, config.timeout);
          config.dont_check_timeout = 0;
        } else {
          config.timeout = value;
          config.dont_check_timeout = 0;
        }
      }


      long long aid;

      // replace the airplay_device_id with this, if provided
      if (config_lookup_int64(config.cfg, "general.airplay_device_id", &aid)) {
        temporary_airplay_id = aid;
      }

      // add the airplay_device_id_offset if provided
      if (config_lookup_int64(config.cfg, "general.airplay_device_id_offset", &aid)) {
        temporary_airplay_id += aid;
      }


    } else {
      if (config_error_type(&config_file_stuff) == CONFIG_ERR_FILE_IO)
        rejectConfiguration("Error reading configuration file \"%s\": \"%s\".", config_file_real_path,
            config_error_text(&config_file_stuff));
      else {
        rejectConfiguration("Line %d of the configuration file \"%s\":\n%s", config_error_line(&config_file_stuff),
            config_error_file(&config_file_stuff), config_error_text(&config_file_stuff));
      }
    }
  }

  char shared_memory_interface_name[256] = "";
  snprintf(shared_memory_interface_name, sizeof(shared_memory_interface_name), "/%s-%" PRIx64 "",
           config.appName, temporary_airplay_id);
  // recordDebug(1, "smi name: \"%s\"", shared_memory_interface_name);

  config.nqptp_shared_memory_interface_name = copyText(environment.timingInterfaceName);

  // create the config.ap1_prefix[i]
  char apids[6 * 2 + 5 + 1]; // six pairs of digits, 5 colons and a NUL
  apids[6 * 2 + 5] = 0;      // NUL termination
  char hexchar[] = "0123456789abcdef";
  for (int i = 5; i >= 0; i--) {
    // In AirPlay 2 mode, the AP1 name prefix must be
    // the same as the AirPlay 2 device id less the colons.
    config.ap1_prefix[i] = temporary_airplay_id & 0xFF;
    apids[i * 3 + 1] = hexchar[temporary_airplay_id & 0xF];
    temporary_airplay_id = temporary_airplay_id >> 4;
    apids[i * 3] = hexchar[temporary_airplay_id & 0xF];
    temporary_airplay_id = temporary_airplay_id >> 4;
    if (i != 0)
      apids[i * 3 - 1] = ':';
  }

  config.airplay_device_id = copyText(apids);

  /* if the regtype hasn't been set, do it now */
  if (config.regtype == NULL)
    config.regtype = copyText("_raop._tcp");
  if (config.regtype2 == NULL)
    config.regtype2 = copyText("_airplay._tcp");

  const shairport::ServiceNameFormatter formatter(environment.hostname, environment.packageVersion,
                                                 environment.detailedVersion);
  config.service_name = copyText(formatter.format(raw_service_name ? raw_service_name : "%H"));



  if (config.port == 0) config.port = 7000;
  load_pulseaudio_settings();
#if LIBAVUTIL_VERSION_MAJOR >= 57

  // default multichannel on
  {
    AVChannelLayout default_layout =
        AV_CHANNEL_LAYOUT_7POINT1; // big fat macro to initialise the default layout
    config.eight_channel_layout = default_layout.u.mask;
  }
  {
    AVChannelLayout default_layout =
        AV_CHANNEL_LAYOUT_5POINT1; // big fat macro to initialise the default layout
    config.six_channel_layout = default_layout.u.mask;
  }

  if ((config.cfg != NULL) &&
      (config_lookup_string(config.cfg, "general.eight_channel_mode", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.eight_channel_layout = 0; // 0 on initialisation
    } else if ((strcasecmp(str, "on") == 0) || (strcasecmp(str, "yes") == 0)) {
      // AVChannelLayout default_layout =
      //     AV_CHANNEL_LAYOUT_7POINT1; // big fat macro to initialise the default layout
      // config.eight_channel_layout = default_layout.u.mask;
    } else {
      AVChannelLayout channel_layout;
      if (av_channel_layout_from_string(&channel_layout, str) == 0) {
        if (channel_layout.nb_channels == 8) {
          config.eight_channel_layout = channel_layout.u.mask;
        } else {
          recordWarning("the eight_channel_mode setting \"%s\" is a %u-channel layout. If a channel layout "
               "is "
               "given, it must be an 8-channel layout. eight_channel_mode is set to \"off\".",
               str, channel_layout.nb_channels);
        }
        av_channel_layout_uninit(&channel_layout);
      } else {
        recordWarning("the eight_channel_mode setting \"%s\" is not recognised -- it should be \"off\" or "
             "\"on\" or an eight-channel FFmpeg channel layout, e.g. \"7.1\". "
             "eight_channel_mode is set to \"off\".",
             str);
      }
    }
  }

  if ((config.cfg != NULL) &&
      (config_lookup_string(config.cfg, "general.six_channel_mode", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.six_channel_layout = 0; // 0 on initialisation
    } else if ((strcasecmp(str, "on") == 0) || (strcasecmp(str, "yes") == 0)) {
      // AVChannelLayout default_layout =
      //     AV_CHANNEL_LAYOUT_5POINT1; // big fat macro to initialise the default layout
      // config.six_channel_layout = default_layout.u.mask;
    } else {
      AVChannelLayout channel_layout;
      if (av_channel_layout_from_string(&channel_layout, str) == 0) {
        if (channel_layout.nb_channels == 6) {
          config.six_channel_layout = channel_layout.u.mask;
        } else {
          recordWarning("the six_channel_mode setting \"%s\" is a %u-channel layout. If a channel layout is "
               "given, it must be a 6-channel layout. six_channel_mode is set to \"off\".",
               str, channel_layout.nb_channels);
        }
        av_channel_layout_uninit(&channel_layout);
      } else {
        recordWarning("the six_channel_mode setting \"%s\" is not recognised -- it should be \"off\" or "
             "\"on\" or a six-channel FFmpeg channel layout, e.g. \"5.1\". "
             "six_channel_mode is set to \"off\".",
             str);
      }
    }
  }

  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "general.mixdown", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.mixdown_enable = 0; // 0 on initialisation
      recordDebug(1, "mixdown disabled.");
    } else if (strcasecmp(str, "auto") == 0) {
      config.mixdown_enable = 1;
      config.mixdown_channel_layout = 0; // 0 means auto
      recordDebug(1, "mixdown target: auto.");
    } else {
      AVChannelLayout channel_layout;
      if (av_channel_layout_from_string(&channel_layout, str) == 0) {
        config.mixdown_enable = 1;
        config.mixdown_channel_layout = channel_layout.u.mask;
        av_channel_layout_uninit(&channel_layout);
        recordDebug(1, "mixdown target: \"%s\".", str);
      } else {
        recordWarning("the mixdown setting \"%s\" is not recognised -- it should be \"off\" or \"auto\" or "
             "an "
             "FFmpeg channel layout, e.g. \"stereo\". the mixdown is set to \"auto\".",
             str);
        config.mixdown_enable = 1;
        config.mixdown_channel_layout = 0; // 0 means auto
      }
    }
  }
#else

  // default on
  config.eight_channel_layout = AV_CH_LAYOUT_7POINT1;
  config.six_channel_layout = AV_CH_LAYOUT_5POINT1;

  const char *str;

  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "general.eight_channel_mode", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.eight_channel_layout = 0; // 0 on initialisation
    } else if ((strcasecmp(str, "on") == 0) || (strcasecmp(str, "yes") == 0)) {
      // config.eight_channel_layout = AV_CH_LAYOUT_7POINT1;
    } else if (av_get_channel_layout(str) != 0) {
      if (av_get_channel_layout_nb_channels(av_get_channel_layout(str)) == 8) {
        config.eight_channel_layout = av_get_channel_layout(str);
      } else {
        recordWarning("the eight_channel_mode setting \"%s\" is a %u channel layout. If a channel layout is "
             "given, it must be an 8-channel layout. eight_channel_mode is set to \"off\".",
             str, av_get_channel_layout_nb_channels(av_get_channel_layout(str)));
      }
    } else {
      recordWarning("the eight_channel_mode setting \"%s\" is not recognised -- it should be \"off\" or "
           "\"on\" or an 8-channel FFmpeg channel layout, e.g. \"7.1\". "
           "eight_channel_mode is set to \"off\".",
           str);
    }
  }

  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "general.six_channel_mode", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.six_channel_layout = 0; // 0 on initialisation
    } else if ((strcasecmp(str, "on") == 0) || (strcasecmp(str, "yes") == 0)) {
      // config.six_channel_layout = AV_CH_LAYOUT_5POINT1;
    } else if (av_get_channel_layout(str) != 0) {
      if (av_get_channel_layout_nb_channels(av_get_channel_layout(str)) == 6) {
        config.six_channel_layout = av_get_channel_layout(str);
      } else {
        recordWarning("the six_channel_mode setting \"%s\" is a %u channel layout. If a channel layout is "
             "given, it must be a 6-channel layout. six_channel_mode is set to \"off\".",
             str, av_get_channel_layout_nb_channels(av_get_channel_layout(str)));
      }
    } else {
      recordWarning("the six_channel_mode setting \"%s\" is not recognised -- it should be \"off\" or "
           "\"on\" or a 6-channel FFmpeg channel layout, e.g. \"5.1\". "
           "six_channel_mode is set to \"off\".",
           str);
    }
  }

  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "general.mixdown", &str))) {
    if ((strcasecmp(str, "off") == 0) || (strcasecmp(str, "no") == 0)) {
      config.mixdown_enable = 0; // 0 on initialisation
    } else if (strcasecmp(str, "auto") == 0) {
      config.mixdown_enable = 1;
      config.mixdown_channel_layout = 0; // 0 means auto
    } else if (av_get_channel_layout(str) != 0) {
      config.mixdown_enable = 1;
      config.mixdown_channel_layout = av_get_channel_layout(str);
    } else {
      recordWarning("the mixdown setting \"%s\" is not recognised -- it should be \"off\" or \"auto\" or an "
           "FFmpeg channel layout, e.g. \"stereo\". the mixdown is set to \"auto\".",
           str);
      config.mixdown_enable = 1;
      config.mixdown_channel_layout = 0; // 0 means auto
    }
  }
#endif

  if (config.cfg != NULL) {
    config_setting_t *output_channel_mapping_setting =
        config_lookup(config.cfg, "general.output_channel_mapping");
    if (output_channel_mapping_setting != NULL) {
      const char *sstr = config_setting_get_string(output_channel_mapping_setting);
      if (sstr != NULL) { // definitely a string
        if (strcasecmp(sstr, "auto") == 0) {
          config.output_channel_mapping_enable = 1; // this is the default anyway
          config.output_channel_map_size = 0;       // use the device's channel map
          recordDebug(1, "device output channel map chosen");
        } else if ((strcasecmp(sstr, "off") == 0) || (strcasecmp(sstr, "no") == 0)) {
          config.output_channel_mapping_enable = 0; // no mapping
        } else {
          recordWarning("the output_channel_mapping setting \"%s\" is not recognised -- it should be "
               "\"auto\", \"off\" or a "
               "bracketed comma-separated list of short channel names, e.g. (\"FL\", \"FR\", "
               "\"LFE\");",
               sstr);
        }
      } else {
        if (config_setting_length(output_channel_mapping_setting) >
            static_cast<int>(8))
          rejectConfiguration("general.output_channel_mapping supports at most eight channels");
        int i = 0;
        for (i = 0; i < config_setting_length(output_channel_mapping_setting); i++) {
          // is a list or array, so okay
          const char *channel_id =
              config_setting_get_string_elem(output_channel_mapping_setting, i);
          if (channel_id != NULL) { // definitely a string
            int found = 0;
            if (strcmp(channel_id, "--") == 0) {
              found = 1;
            } else {
#if LIBAVUTIL_VERSION_MAJOR >= 57
              const int buffer_size = 32;
              char buffer[buffer_size];
              enum AVChannel channel_index;
              for (channel_index = AV_CHAN_NONE;
                   ((channel_index < AV_CHAN_BOTTOM_FRONT_RIGHT) && (found == 0));
                   channel_index = static_cast<AVChannel>(channel_index + 1)) {
                found = av_channel_name(buffer, buffer_size, channel_index);
                if (found > 0) {
                  found = ((av_channel_name(buffer, buffer_size, channel_index) > 0) &&
                           (strcmp(channel_id, buffer) == 0));
                } else {
                  found = 0;
                }
              }
#else
              uint64_t channel_index;
              for (channel_index = 0; ((channel_index < 64) && (found == 0)); channel_index++) {
                found = ((av_get_channel_name(1 << channel_index) != NULL) &&
                         (strcmp(channel_id, av_get_channel_name(1 << channel_index)) == 0));
              }
#endif
            }
            if (found != 0) {
              config.output_channel_map[i] = copyText(channel_id);
              recordDebug(2, "output channel %d is \"%s\".", i, config.output_channel_map[i]);
            } else {

              recordWarning("during channel mapping, \"%s\" was not recognised as a channel name -- as a "
                   "result, output channel %d will be silent.",
                   channel_id, i);
              config.output_channel_map[i] = copyText("--");
            }
            config.output_channel_map_size++;
          }
        }
        if (config.output_channel_map_size == 0)
          recordWarning("the output_channel_mapping setting was empty. No output channel mapping will be "
               "done.");
        else
          config.output_channel_mapping_enable = 1;
      }
    }
  }



}




ReceiverSettings finish() {
  settings.diagnostics_.debugger_show_elapsed_time = config.debugger_show_elapsed_time;
  settings.diagnostics_.debugger_show_relative_time = config.debugger_show_relative_time;
  settings.diagnostics_.debugger_show_file_and_line = config.debugger_show_file_and_line;
  settings.diagnostics_.statistics_requested = config.statistics_requested;
  settings.diagnostics_.logOutputLevel = config.logOutputLevel;
  settings.diagnostics_.disable_resend_requests = config.disable_resend_requests;
  settings.diagnostics_.diagnostic_drop_packet_fraction = config.diagnostic_drop_packet_fraction;
  settings.network_.appName = config.appName ? config.appName : "";
  settings.network_.configfile = config.configfile ? config.configfile : "";
  settings.network_.service_name = config.service_name ? config.service_name : "";
  if (config.password) settings.network_.password = config.password;
  settings.network_.regtype = config.regtype ? config.regtype : "";
  settings.network_.regtype2 = config.regtype2 ? config.regtype2 : "";
  if (config.interface) settings.network_.interface = config.interface;
  settings.network_.interface_index = config.interface_index;
  settings.network_.port = config.port;
  settings.network_.udp_port_base = config.udp_port_base;
  settings.network_.udp_port_range = config.udp_port_range;
  settings.network_.missing_port_dacp_scan_interval_seconds = config.missing_port_dacp_scan_interval_seconds;
  settings.network_.model = config.model ? config.model : "";
  settings.network_.srcvers = config.srcvers ? config.srcvers : "";
  settings.network_.osvers = config.osvers ? config.osvers : "";
  settings.network_.firmware_version = config.firmware_version ? config.firmware_version : "";
  settings.network_.airplay_device_id = config.airplay_device_id ? config.airplay_device_id : "";
  std::copy_n(config.ap1_prefix, 6, settings.network_.ap1_prefix.begin());
  std::copy_n(config.hw_addr, 6, settings.network_.hw_addr.begin());
  settings.network_.nqptp_shared_memory_interface_name = config.nqptp_shared_memory_interface_name ? config.nqptp_shared_memory_interface_name : "";
  settings.audio_.packet_stuffing = config.packet_stuffing;
  settings.audio_.playback_mode = config.playback_mode;
  settings.audio_.audio_backend_buffer_desired_length = config.audio_backend_buffer_desired_length;
  settings.audio_.audio_backend_buffer_interpolation_threshold_in_seconds = config.audio_backend_buffer_interpolation_threshold_in_seconds;
  settings.audio_.audio_backend_latency_offset = config.audio_backend_latency_offset;
  settings.audio_.audio_decoded_buffer_desired_length = config.audio_decoded_buffer_desired_length;
  settings.audio_.audio_backend_silent_lead_in_time_auto = config.audio_backend_silent_lead_in_time_auto;
  settings.audio_.audio_backend_silent_lead_in_time = config.audio_backend_silent_lead_in_time;
  settings.audio_.buffer_start_fill = config.buffer_start_fill;
  settings.audio_.userSuppliedLatency = config.userSuppliedLatency;
  settings.audio_.fixedLatencyOffset = config.fixedLatencyOffset;
  settings.audio_.minimum_free_buffer_headroom = config.minimum_free_buffer_headroom;
  settings.audio_.resync_threshold = config.resync_threshold;
  settings.audio_.tolerance = config.tolerance;
  settings.audio_.format_set = config.format_set;
  settings.audio_.rate_set = config.rate_set;
  settings.audio_.channel_set = config.channel_set;
  if (config.pa_server) settings.audio_.pa_server = config.pa_server;
  if (config.pa_sink) settings.audio_.pa_sink = config.pa_sink;
  if (config.pa_application_name) settings.audio_.pa_application_name = config.pa_application_name;
  settings.audio_.six_channel_layout = config.six_channel_layout;
  settings.audio_.eight_channel_layout = config.eight_channel_layout;
  settings.audio_.mixdown_enable = config.mixdown_enable;
  settings.audio_.mixdown_channel_layout = config.mixdown_channel_layout;
  settings.audio_.output_channel_mapping_enable = config.output_channel_mapping_enable;
  for (unsigned i = 0; i < config.output_channel_map_size; ++i)
    settings.audio_.output_channel_map.emplace_back(config.output_channel_map[i]);
  settings.volume_.ignore_volume_control = config.ignore_volume_control;
  settings.volume_.volume_max_db_set = config.volume_max_db_set;
  settings.volume_.volume_max_db = config.volume_max_db;
  settings.volume_.volume_range_db = config.volume_range_db;
  settings.volume_.volume_range_hw_priority = config.volume_range_hw_priority;
  settings.volume_.volume_control_profile = config.volume_control_profile;
  settings.volume_.default_airplay_volume = config.default_airplay_volume;
  settings.session_.active_state_timeout = config.active_state_timeout;
  settings.session_.allow_session_interruption = config.allow_session_interruption;
  settings.session_.timeout = config.timeout;
  settings.session_.dont_check_timeout = config.dont_check_timeout;
  if (config.cmd_start) settings.session_.cmd_start = config.cmd_start;
  if (config.cmd_stop) settings.session_.cmd_stop = config.cmd_stop;
  if (config.cmd_set_volume) settings.session_.cmd_set_volume = config.cmd_set_volume;
  if (config.cmd_unfixable) settings.session_.cmd_unfixable = config.cmd_unfixable;
  if (config.cmd_active_start) settings.session_.cmd_active_start = config.cmd_active_start;
  if (config.cmd_active_stop) settings.session_.cmd_active_stop = config.cmd_active_stop;
  settings.session_.cmd_blocking = config.cmd_blocking;
  settings.session_.cmd_start_returns_output = config.cmd_start_returns_output;
  settings.session_.resend_control_first_check_time = config.resend_control_first_check_time;
  settings.session_.resend_control_check_interval_time = config.resend_control_check_interval_time;
  settings.session_.resend_control_last_check_time = config.resend_control_last_check_time;
  settings.diagnostics_.verbosity = config.verbosity;
  settings.network_.endianness = environment.endianness;
  if (config_file_real_path) settings.network_.realPath = config_file_real_path;
  if (default_channel_layouts) settings.audio_.defaultChannelLayouts = default_channel_layouts;
  settings.messages_ = std::move(messages);
  return std::move(settings);
}

  const ConfigurationEnvironment &environment;
  ReceiverSettings settings;
  ConfigurationValues config;
  config_t &config_file_stuff;
  std::deque<std::string> ownedText;
  std::unique_ptr<char, decltype(&free)> resolvedPath{nullptr, &free};
  char *config_file_real_path = nullptr;
  const char *default_channel_layouts = nullptr;
  std::vector<ConfigurationDiagnostic> messages;
};

std::expected<ReceiverSettings, ConfigurationFailure> ConfigurationLoader::load(
    const StartupOptions &options, const ConfigurationEnvironment &environment) {
  ConfigurationReader reader(environment);
  try {
    return reader.read(options);
  } catch (const std::exception &failure) {
    return std::unexpected(ConfigurationFailure{failure.what(), reader.diagnostics()});
  }
}
