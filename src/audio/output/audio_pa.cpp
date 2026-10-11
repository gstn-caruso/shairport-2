/*
 * PulseAudio Backend. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017--2025
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

// Based (distantly, with thanks) on
// http://stackoverflow.com/questions/29977651/how-can-the-pulseaudio-asynchronous-library-be-used-to-play-raw-pcm-data

#include "audio/output/audio.h"
#include "runtime/common.h"
#include <algorithm>
#include <errno.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

import receiver.audio.output.queue;

typedef struct {
  pa_sample_format_t pa_format;
  sps_format_t sps_format;
  unsigned int bytes_per_sample;
} pa_sps_t;

// these are the only formats that audio_pw will ever allow itself to be configured with
static pa_sps_t format_lookup[] = {{PA_SAMPLE_S16LE, SPS_FORMAT_S16_LE, 2},
                                   {PA_SAMPLE_S16BE, SPS_FORMAT_S16_BE, 2},
                                   {PA_SAMPLE_S32LE, SPS_FORMAT_S32_LE, 4},
                                   {PA_SAMPLE_S32BE, SPS_FORMAT_S32_BE, 4}};

#define CHANNEL_MAP_SIZE 1024
static char channel_map[CHANNEL_MAP_SIZE + 1];

pa_threaded_mainloop *mainloop;
pa_mainloop_api *mainloop_api;
pa_context *context;
pa_stream *stream;
static bool mainloop_started = false;

static int32_t current_encoded_output_format = 0;
const char *default_channel_layouts = NULL;
static PcmOutputQueue output_queue;

// use an SPS_FORMAT_... to find an entry in the format_lookup table or return NULL
static pa_sps_t *sps_format_lookup(sps_format_t to_find) {
  pa_sps_t *response = NULL;
  unsigned int i = 0;
  while ((response == NULL) && (i < sizeof(format_lookup) / sizeof(pa_sps_t))) {
    if (format_lookup[i].sps_format == to_find)
      response = &format_lookup[i];
    else
      i++;
  }
  return response;
}

void context_state_cb(pa_context *context, void *mainloop);
void stream_state_cb(pa_stream *s, void *mainloop);
void stream_success_cb(pa_stream *stream, int success, void *userdata);
void stream_write_cb(pa_stream *stream, size_t requested_bytes, void *userdata);

int status_error_notifications = 0;
static void check_pa_stream_status(pa_stream *p, const char *message) {
  if (status_error_notifications < 10) {
    if (p == NULL) {
      warn("%s No pulseaudio stream!", message);
      status_error_notifications++;
    } else {
      status_error_notifications++; // assume an error
      switch (pa_stream_get_state(p)) {
      case PA_STREAM_UNCONNECTED:
        warn("%s Pulseaudio stream unconnected!", message);
        break;
      case PA_STREAM_CREATING:
        warn("%s Pulseaudio stream being created!", message);
        break;
      case PA_STREAM_READY:
        status_error_notifications--; // no error
        break;
      case PA_STREAM_FAILED:
        warn("%s Pulseaudio stream failed!", message);
        break;
      case PA_STREAM_TERMINATED:
        warn("%s Pulseaudio stream unexpectedly terminated!", message);
        break;
      default:
        warn("%s Pulseaudio stream in unexpected state %d!", message, pa_stream_get_state(p));
        break;
      }
    }
  }
}

static int check_settings(sps_format_t sample_format, unsigned int sample_rate,
                          unsigned int channel_count) {

  // debug(1, "pa check_settings: configuration: %u/%s/%u.", sample_rate,
  //       sps_format_description_string(sample_format), channel_count);

  int response = EINVAL;

  pa_sps_t *format_info = sps_format_lookup(sample_format);
  if (format_info != NULL) {
    // here, try to create a stream of the given format.

    pa_threaded_mainloop_lock(mainloop);
    // Create a playback stream
    pa_sample_spec sample_specifications;
    sample_specifications.format = format_info->pa_format;
    sample_specifications.rate = sample_rate;
    sample_specifications.channels = channel_count;

    pa_channel_map map;
    pa_channel_map_init_auto(&map, sample_specifications.channels, PA_CHANNEL_MAP_DEFAULT);

    pa_stream *check_settings_stream =
        pa_stream_new(context, "Playback", &sample_specifications, &map);

    if (check_settings_stream != NULL) {
      response = 0; // success
      pa_stream_unref(check_settings_stream);
    }
    pa_threaded_mainloop_unlock(mainloop);

    // response = 0;
  }

  // debug(1, "pa check_settings: configuration: %u/%s/%u %s.", sample_rate,
  //       sps_format_description_string(sample_format), channel_count,
  //       response == 0 ? "is okay" : "can not be configured");
  return response;
}

static int check_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return check_settings(static_cast<sps_format_t>(format), rate, channels);
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  uint64_t start_time = get_absolute_time_in_ns();
  int32_t response =
      search_for_suitable_configuration(channels, rate, format, &check_configuration);
  int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
  debug(3, "pa: get_configuration took %0.3f mS.", elapsed_time * 0.000001);
  return response;
}

static int configure(int32_t requested_encoded_format, char **resulting_channel_map) {
  debug(3, "pa: configure %s.", short_format_description(requested_encoded_format));
  int response = 0;
  pa_threaded_mainloop_lock(mainloop);
  if (current_encoded_output_format != requested_encoded_format) {
    uint64_t start_time = get_absolute_time_in_ns();
    if (current_encoded_output_format == 0)
      debug(3, "pa: setting output configuration to %s.",
            short_format_description(requested_encoded_format));
    else
      // note -- can't use short_format_description twice in one call because it returns the same
      // string buffer each time
      debug(3, "pa: changing output configuration to %s.",
            short_format_description(requested_encoded_format));
    current_encoded_output_format = requested_encoded_format;
    pa_sps_t *format_info =
        sps_format_lookup(static_cast<sps_format_t>(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format)));

    if (format_info == NULL)
      die("pa: can't find format information!");

    if (stream != NULL) {
      // debug(1, "pa: stopping and releasing the current stream...");
      if (pa_stream_is_corked(stream) == 0) {
        // debug(1,"Flush and cork for flush.");
        pa_stream_flush(stream, stream_success_cb, NULL);
        pa_stream_cork(stream, 1, stream_success_cb, mainloop);
      }
      pa_stream_disconnect(stream);
      pa_stream_unref(stream);
      stream = NULL;
    }

    output_queue = PcmOutputQueue(RATE_FROM_ENCODED_FORMAT(current_encoded_output_format),
        format_info->bytes_per_sample * CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format));
    // Create a playback stream
    pa_sample_spec sample_specifications;
    sample_specifications.format = format_info->pa_format;
    sample_specifications.rate = RATE_FROM_ENCODED_FORMAT(current_encoded_output_format);
    sample_specifications.channels = CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);

    uint32_t buffer_size_in_bytes =
        (uint32_t)CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format) *
        format_info->bytes_per_sample * RATE_FROM_ENCODED_FORMAT(current_encoded_output_format) /
        10;

    pa_channel_map map;

    pa_channel_map *pacm = NULL;

    // if we've not asked specifically for native formats, ask for the alsa format...
    if ((default_channel_layouts == NULL) || (strcasecmp(default_channel_layouts, "alsa") == 0)) {
      pacm = pa_channel_map_init_auto(&map, sample_specifications.channels, PA_CHANNEL_MAP_ALSA);
    }

    // ask for the native format...
    if (pacm == NULL) {
      pacm = pa_channel_map_init_auto(&map, sample_specifications.channels, PA_CHANNEL_MAP_DEFAULT);
    }

    // ask for some format...
    if (pacm == NULL) {
      pacm =
          pa_channel_map_init_extend(&map, sample_specifications.channels, PA_CHANNEL_MAP_DEFAULT);
    }

    if (resulting_channel_map != NULL) { // if needed...
      // PA_CHANNEL_MAP_ALSA gives default channel maps that correspond to the FFmpeg defaults.
      // make up a channel map
      channel_map[0] = '\0';
      int c;
      for (c = 0; c < map.channels; c++) {
        switch (map.map[c]) {

        case PA_CHANNEL_POSITION_MONO:
          strncat(channel_map, "FC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_LEFT:
          strncat(channel_map, "FL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_RIGHT:
          strncat(channel_map, "FR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_CENTER:
          strncat(channel_map, "FC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_REAR_CENTER:
          strncat(channel_map, "BC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_REAR_LEFT:
          strncat(channel_map, "BL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_REAR_RIGHT:
          strncat(channel_map, "BR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_LFE:
          strncat(channel_map, "LFE", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER:
          strncat(channel_map, "FLC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER:
          strncat(channel_map, "FRC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_SIDE_LEFT:
          strncat(channel_map, "SL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_SIDE_RIGHT:
          strncat(channel_map, "SR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX0:
          strncat(channel_map, "AUX0", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX1:
          strncat(channel_map, "AUX1", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX2:
          strncat(channel_map, "AUX2", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX3:
          strncat(channel_map, "AUX3", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX4:
          strncat(channel_map, "AUX4", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX5:
          strncat(channel_map, "AUX5", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX6:
          strncat(channel_map, "AUX6", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX7:
          strncat(channel_map, "AUX7", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX8:
          strncat(channel_map, "AUX8", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX9:
          strncat(channel_map, "AUX9", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX10:
          strncat(channel_map, "AUX10", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX11:
          strncat(channel_map, "AUX11", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX12:
          strncat(channel_map, "AUX12", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX13:
          strncat(channel_map, "AUX13", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX14:
          strncat(channel_map, "AUX14", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX15:
          strncat(channel_map, "AUX15", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX16:
          strncat(channel_map, "AUX16", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX17:
          strncat(channel_map, "AUX17", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX18:
          strncat(channel_map, "AUX18", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX19:
          strncat(channel_map, "AUX19", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX20:
          strncat(channel_map, "AUX20", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX21:
          strncat(channel_map, "AUX21", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX22:
          strncat(channel_map, "AUX22", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX23:
          strncat(channel_map, "AUX23", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX24:
          strncat(channel_map, "AUX24", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX25:
          strncat(channel_map, "AUX25", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX26:
          strncat(channel_map, "AUX26", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX27:
          strncat(channel_map, "AUX27", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX28:
          strncat(channel_map, "AUX28", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX29:
          strncat(channel_map, "AUX29", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX30:
          strncat(channel_map, "AUX30", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX31:
          strncat(channel_map, "AUX31", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_CENTER:
          strncat(channel_map, "TC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_FRONT_LEFT:
          strncat(channel_map, "TFL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_FRONT_RIGHT:
          strncat(channel_map, "TFR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_FRONT_CENTER:
          strncat(channel_map, "TFC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_REAR_LEFT:
          strncat(channel_map, "TBL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_REAR_RIGHT:
          strncat(channel_map, "TBR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_REAR_CENTER:
          strncat(channel_map, "TBC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        default:
          break;
        }
        if (c != (map.channels - 1))
          strncat(channel_map, " ", sizeof(channel_map) - 1 - strlen(channel_map));
      }

      debug(1, "audio_pa: channel map for %d channels is \"%s\".", sample_specifications.channels,
            channel_map);
    }

    stream = pa_stream_new(context, "Playback", &sample_specifications, &map);
    pa_stream_set_state_callback(stream, stream_state_cb, mainloop);
    pa_stream_set_write_callback(stream, stream_write_cb, mainloop);

    // recommended settings, i.e. server uses sensible values
    pa_buffer_attr buffer_attr;
    buffer_attr.maxlength = (uint32_t)-1;
    buffer_attr.tlength = buffer_size_in_bytes;
    buffer_attr.prebuf = (uint32_t)0;
    buffer_attr.minreq = (uint32_t)-1;

    pa_stream_flags_t stream_flags;
    stream_flags = static_cast<pa_stream_flags_t>(
        PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_NOT_MONOTONIC |
        PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_ADJUST_LATENCY);

    int connect_result;

    if (config.pa_sink) {
      // Connect stream to the sink specified in the config
      connect_result = pa_stream_connect_playback(stream, config.pa_sink, &buffer_attr,
                                                  stream_flags, NULL, NULL);
    } else {
      // Connect stream to the default audio output sink
      connect_result =
          pa_stream_connect_playback(stream, NULL, &buffer_attr, stream_flags, NULL, NULL);
    }

    if (connect_result != 0)
      die("could not connect to the pulseaudio playback stream -- the error message is \"%s\".",
          pa_strerror(pa_context_errno(context)));

    // Wait for the stream to be ready
    for (;;) {
      pa_stream_state_t stream_state = pa_stream_get_state(stream);
      if (!PA_STREAM_IS_GOOD(stream_state))
        die("stream state is no longer good while waiting for stream to become ready -- the error "
            "message is \"%s\".",
            pa_strerror(pa_context_errno(context)));
      if (stream_state == PA_STREAM_READY)
        break;
      pa_threaded_mainloop_wait(mainloop);
    }

    // to here

    int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
    debug(3, "pa: configuration took %0.3f mS.", elapsed_time * 0.000001);
  } else {
    debug(3, "pa: setting output configuration  -- configuration unchanged, so nothing done.");
  }
  if ((response == 0) && (resulting_channel_map != NULL)) {
    *resulting_channel_map = channel_map;
  }
  pa_threaded_mainloop_unlock(mainloop);
  return response;
}


static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {

  stream = NULL;    // no stream
  output_queue = PcmOutputQueue{};

  // Get a mainloop and its context

  mainloop = pa_threaded_mainloop_new();
  if (mainloop == NULL)
    die("could not create a pa_threaded_mainloop.");
  mainloop_api = pa_threaded_mainloop_get_api(mainloop);
  if (config.pa_application_name)
    context = pa_context_new(mainloop_api, config.pa_application_name);
  else
    context = pa_context_new(mainloop_api, "shairport-2");
  if (context == NULL)
    die("could not create a new context for pulseaudio.");
  // Set a callback so we can wait for the context to be ready
  pa_context_set_state_callback(context, &context_state_cb, mainloop);

  // Lock the mainloop so that it does not run and crash before the context is ready
  pa_threaded_mainloop_lock(mainloop);

  // Start the mainloop
  if (pa_threaded_mainloop_start(mainloop) != 0) {
    pa_threaded_mainloop_unlock(mainloop);
    die("could not start the pulseaudio threaded mainloop");
  }
  mainloop_started = true;

  if (pa_context_connect(context, config.pa_server, PA_CONTEXT_NOFLAGS, NULL) != 0) {
    pa_threaded_mainloop_unlock(mainloop);
    die("failed to connect to the pulseaudio context -- the error message is \"%s\".",
        pa_strerror(pa_context_errno(context)));
  }

  // Wait for the context to be ready
  for (;;) {
    pa_context_state_t context_state = pa_context_get_state(context);
    if (!PA_CONTEXT_IS_GOOD(context_state)) {
      pa_threaded_mainloop_unlock(mainloop);
      die("pa context is not good -- the error message \"%s\".",
          pa_strerror(pa_context_errno(context)));
    }
    if (context_state == PA_CONTEXT_READY)
      break;
    pa_threaded_mainloop_wait(mainloop);
  }
  pa_threaded_mainloop_unlock(mainloop);
  return 0;
}

static void deinit(void) {
  if (mainloop_started) {
    pa_threaded_mainloop_stop(mainloop);
    mainloop_started = false;
  }
  if (stream != NULL) {
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    stream = nullptr;
  }
  if (context != nullptr) {
    pa_context_disconnect(context);
    pa_context_unref(context);
    context = nullptr;
  }
  if (mainloop != nullptr) {
    pa_threaded_mainloop_free(mainloop);
    mainloop = nullptr;
  }
  output_queue = PcmOutputQueue{};
  current_encoded_output_format = 0;
}

/*
static void start(__attribute__((unused)) int sample_rate,
                  __attribute__((unused)) int sample_format) {
  check_pa_stream_status(stream, "audio_pa start.");
}
*/

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  // debug(1,"pa_play of %d samples.",samples);
  // copy the samples into the queue
  pa_threaded_mainloop_lock(mainloop);
  check_pa_stream_status(stream, "audio_pa play.");

  pa_sps_t *format_info =
      sps_format_lookup(static_cast<sps_format_t>(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format)));
  size_t bytes_to_transfer = samples * format_info->bytes_per_sample *
                             CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);

  const auto accepted = output_queue.enqueue({static_cast<const std::byte *>(buf), bytes_to_transfer});
  if (accepted > 0) {
    const auto writable = pa_stream_writable_size(stream);
    if (writable != static_cast<size_t>(-1) && writable > 0)
      stream_write_cb(stream, writable, mainloop);
    if (pa_stream_is_corked(stream) > 0)
      pa_stream_cork(stream, 0, stream_success_cb, mainloop);
  }
  pa_threaded_mainloop_unlock(mainloop);
  return 0;
}

int pa_delay(long *the_delay) {
  // debug(1, "pa delay");
  pa_threaded_mainloop_lock(mainloop);
  check_pa_stream_status(stream, "audio_pa delay.");
  // debug(1,"pa_delay");
  long result = 0;
  int reply = 0;
  pa_usec_t latency;
  int negative;
  int gl = pa_stream_get_latency(stream, &latency, &negative);
  if (gl == -PA_ERR_NODATA) {
    reply = -ENODEV;
  } else if (gl != 0) {
    reply = -EIO;
  } else {
    pa_sps_t *format_info =
        sps_format_lookup(static_cast<sps_format_t>(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format)));
    // convert audio_occupancy bytes to frames and latency microseconds into frames
    result = (output_queue.occupiedBytes() / (format_info->bytes_per_sample *
                                 CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format))) +
             (latency * RATE_FROM_ENCODED_FORMAT(current_encoded_output_format)) / 1000000;
    reply = 0;
  }
  pa_threaded_mainloop_unlock(mainloop);
  *the_delay = result;
  return reply;
}

static void flush(void) {
  // debug(1, "pa flush");
  if (stream != NULL) {
    check_pa_stream_status(stream, "audio_pa flush.");
    pa_threaded_mainloop_lock(mainloop);
    if (pa_stream_is_corked(stream) == 0) {
      // debug(1,"Flush and cork for flush.");
      pa_stream_flush(stream, stream_success_cb, NULL);
      pa_stream_cork(stream, 1, stream_success_cb, mainloop);
    }
    output_queue.clear();
    pa_threaded_mainloop_unlock(mainloop);
  }
}

static void stop(void) {
  // debug(1, "pa stop");
  if (stream != NULL) {
    check_pa_stream_status(stream, "audio_pa stop.");
    // Cork the stream so it will stop playing
    pa_threaded_mainloop_lock(mainloop);
    if (pa_stream_is_corked(stream) == 0) {
      // debug(1,"Flush and cork for stop.");
      pa_stream_flush(stream, stream_success_cb, NULL);
      pa_stream_cork(stream, 1, stream_success_cb, mainloop);
    }
    output_queue.clear();
    pa_threaded_mainloop_unlock(mainloop);
  }
}

void context_state_cb(__attribute__((unused)) pa_context *local_context, void *local_mainloop) {
  // debug(1,"context_state_cb called.");
  pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(local_mainloop), 0);
}

void stream_state_cb(__attribute__((unused)) pa_stream *s, void *local_mainloop) {
  // debug(1,"stream_state_cb called.");
  pa_threaded_mainloop_signal(static_cast<pa_threaded_mainloop *>(local_mainloop), 0);
}

void stream_write_cb(pa_stream *local_stream, size_t requested_bytes,
                     __attribute__((unused)) void *userdata) {
  check_pa_stream_status(local_stream, "audio_pa stream_write_cb.");
  size_t bytes_to_transfer = requested_bytes;
  uint8_t *buffer = NULL;
  int ret = 0;
  while ((bytes_to_transfer > 0) && (output_queue.occupiedBytes() > 0) && (ret == 0)) {
    if (pa_stream_is_suspended(local_stream))
      debug(1, "local_stream is suspended");
    size_t bytes_we_can_transfer = std::min(bytes_to_transfer, output_queue.occupiedBytes());
    ret = pa_stream_begin_write(local_stream, (void **)&buffer, &bytes_we_can_transfer);
    if (ret != 0) break;
    if (buffer == NULL || bytes_we_can_transfer == 0) {
      pa_stream_cancel_write(local_stream);
      break;
    }
    bytes_we_can_transfer = output_queue.copyTo({reinterpret_cast<std::byte *>(buffer), bytes_we_can_transfer});
    if (bytes_we_can_transfer == 0) {
      pa_stream_cancel_write(local_stream);
      break;
    }
    ret = pa_stream_write(local_stream, buffer, bytes_we_can_transfer, NULL, 0LL,
                          PA_SEEK_RELATIVE);
    if (ret != 0) {
      pa_stream_cancel_write(local_stream);
      break;
    }
    output_queue.consume(bytes_we_can_transfer);
    bytes_to_transfer -= bytes_we_can_transfer;
  }
  if (ret != 0)
    debug(1, "error writing to pa buffer");
}

void stream_success_cb(__attribute__((unused)) pa_stream *local_stream,
                       __attribute__((unused)) int success,
                       __attribute__((unused)) void *userdata) {
  return;
}

audio_output audio_pa = {.name = "pulseaudio",
                         .init = &init,
                         .deinit = &deinit,
                         .prepare = NULL,
                         .get_configuration = &get_configuration,
                         .configure = &configure,
                         .start = NULL,
                         .play = &play,
                         .stop = &stop,
                         .is_running = NULL,
                         .flush = &flush,
                         .delay = &pa_delay,
                         .stats = NULL,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};
