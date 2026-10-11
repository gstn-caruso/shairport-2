/*
 * Shairport, an Apple Airplay receiver
 * Copyright (c) James Laird 2013
 * All rights reserved.
 * Modifications and additions (c) Mike Brady 2014--2026
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

#include <errno.h>
#include "volume/volume_runtime.hpp"
#include <fcntl.h>
#include <getopt.h>
#include <libconfig.h>
#include <libgen.h>
#include <memory.h>
#include <net/if.h>
#include "app/startup_options.hpp"
#include "app/configuration_loader.hpp"
#include "app/configuration_validation.hpp"
#include "app/receiver_application.hpp"
#include "app/legacy_config_lease.hpp"
#include <bit>
#include <vector>
#include <stdexcept>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "app/receiver.h"

#include "platform/utilities/ffmpeg_api.h"

#include "timing/ptp-utilities.h"
#include "platform/utilities/generate_device_uuid.h"
#include "platform/utilities/generate_random_uuid.h"
#include <gcrypt.h>
#include <sodium.h>
#include <uuid/uuid.h>




#include <openssl/evp.h>
#include <openssl/md5.h>


#include "monitoring/activity_monitor.h"
#include "audio/output/audio.h"
#include "runtime/common.h"
#include "protocol/rtp/rtp.h"
#include "protocol/rtsp/rtsp.h"
#include "timing/shared_rtp_timing_settings.hpp"
#include "platform/utilities/string_utilities.h"
#include "platform/utilities/exit.h"







#include <syslog.h>



pid_t pid;

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 36
#endif

#define strnull(s) ((s) ? (s) : "(null)")



char *config_file_real_path = NULL;

char first_backend_name[256];

void print_version(void) {
  char *version_string = get_version_string();
  if (version_string) {
    printf("%s\n", version_string);
    free(version_string);
  } else {
    debug(1, "Can't print version string!");
  }
}

int has_fltp_capable_aac_decoder(void) {
  // return 1 if the AAC decoder advertises fltp decoding capability, which
  // is needed for decoding Buffered Audio streams
  debug(3, "checking availability of an  fltp-capable aac decoder");
  int has_capability = 0;
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
  if (codec != NULL) {
    const enum AVSampleFormat *formats = NULL;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    // New API (FFmpeg 7.1+) for getting formats
    debug(3, "getting sample formats the new way");
    int format_count = 0;
    if ((avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                      (const void **)&formats, &format_count) < 0) ||
        (format_count == 0))
      formats = NULL; // not clear if the returned pointer is nulled on error or on zero items
#else
    debug(3, "getting sample formats the old way");
    // older API
    formats = codec->sample_fmts;
#endif
    if (formats != NULL) {
      while ((has_capability == 0) && (*formats != AV_SAMPLE_FMT_NONE)) {
        if (*formats == AV_SAMPLE_FMT_FLTP) {
          has_capability = 1;
        }
        formats++;
      }
    }
  } else {
    debug(3, "no AAC codec found.");
  }
  return has_capability;
}


void exit_function() {
  debug(2, "Stopping the activity monitor.");
      activity_monitor_stop();
      debug(2, "Stopping the activity monitor done.");





      if ((config.output) && (config.output->deinit)) {
        debug(2, "Deinitialise the audio backend.");
        config.output->deinit();
        debug(2, "Deinitialise the audio backend done.");
      }


      free(config.airplay_pi);
      free(config.airplay_pgid);
      free(config.airplay_psi);
      free(config.pk_string);
      free(config.airplay_fex);
      ptp_shm_interface_close();

    mdns_unregister(); // once the dacp handler is done and all player threads are done it should
                       // be safe
    debug(2, "normal exit");
}

// for removing zombie script processes
// see: http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
// used with thanks.

void handle_sigchld(__attribute__((unused)) int sig) {
  int saved_errno = errno;
  while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {
  }
  errno = saved_errno;
}

// for clean exits
void intHandler(__attribute__((unused)) int k) {
  debug(2, "exit on SIGINT");
  exit_request(EXIT_SUCCESS);
}

void termHandler(__attribute__((unused)) int k) {
  debug(2, "exit on SIGTERM");
  exit_request(EXIT_SUCCESS);
}

void _display_config(const char *filename, const int linenumber, __attribute__((unused)) int argc,
                     __attribute__((unused)) char **argv) {

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
#endif

  _inform(filename, linenumber, ">> Display Config Start.");

  // see the man entry on popen
  FILE *fp;
  int status;
  char result[1024];

  fp = popen("uname -a 2>/dev/null", "r");
  if (fp != NULL) {
    if (fgets(result, 1024, fp) != NULL) {
      _inform(filename, linenumber, "");
      _inform(filename, linenumber, "From \"uname -a\":");
      if (result[strlen(result) - 1] <= ' ')
        result[strlen(result) - 1] = '\0'; // remove the last character if it's not printable
      _inform(filename, linenumber, " %s", result);
    }
    status = pclose(fp);
    if (status == -1) {
      debug(1, "Error on pclose");
    }
  }

  fp = popen("(cat /etc/os-release | grep PRETTY_NAME | sed 's/PRETTY_NAME=//' | sed 's/\"//g') "
             "2>/dev/null",
             "r");
  if (fp != NULL) {
    if (fgets(result, 1024, fp) != NULL) {
      _inform(filename, linenumber, "");
      _inform(filename, linenumber, "From /etc/os-release:");
      if (result[strlen(result) - 1] <= ' ')
        result[strlen(result) - 1] = '\0'; // remove the last character if it's not printable
      _inform(filename, linenumber, " %s", result);
    }
    status = pclose(fp);
    if (status == -1) {
      debug(1, "Error on pclose");
    }
  }

  fp = popen("cat /sys/firmware/devicetree/base/model 2>/dev/null", "r");
  if (fp != NULL) {
    if (fgets(result, 1024, fp) != NULL) {
      _inform(filename, linenumber, "");
      _inform(filename, linenumber, "From /sys/firmware/devicetree/base/model:");
      _inform(filename, linenumber, " %s", result);
    }
    status = pclose(fp);
    if (status == -1) {
      debug(1, "Error on pclose");
    }
  }

  char *version_string = get_version_string();
  if (version_string) {
    _inform(filename, linenumber, "");
    _inform(filename, linenumber, "Shairport Sync Version String:");
    _inform(filename, linenumber, " %s", version_string);
    free(version_string);
  } else {
    debug(1, "Can't print version string!\n");
  }

  if (argc != 0) {
    char *obfp = result;
    int i;
    for (i = 0; i < argc - 1; i++) {
      snprintf(obfp, strlen(argv[i]) + 2, "%s ", argv[i]);
      obfp += strlen(argv[i]) + 1;
    }
    snprintf(obfp, strlen(argv[i]) + 1, "%s", argv[i]);
    obfp += strlen(argv[i]);
    *obfp = 0;

    _inform(filename, linenumber, "");
    _inform(filename, linenumber, "Command Line:");
    _inform(filename, linenumber, " %s", result);
  }

  if (config.cfg == NULL)
    _inform(filename, linenumber, "No configuration file.");
  else {
    int configpipe[2];
    if (pipe(configpipe) == 0) {
      FILE *cw;
      cw = fdopen(configpipe[1], "w");
      _inform(filename, linenumber, "");
      _inform(filename, linenumber, "Configuration File:");
      _inform(filename, linenumber, " %s", config_file_real_path);
      _inform(filename, linenumber, "");
      config_write(config.cfg, cw);
      fclose(cw);
      // get back the raw configuration file settings text
      FILE *cr;
      cr = fdopen(configpipe[0], "r");
      int i = 0;
      int ch = 0;
      do {
        ch = fgetc(cr);
        if (ch == EOF) {
          result[i] = '\0';
        } else {
          result[i] = (char)ch;
          i++;
        }
      } while (ch != EOF);
      fclose(cr);
      // debug(1,"result is \"%s\".",result);
      // remove empty stanzas
char *i0 = str_replace(result, "general : \n{\n};\n", "");
char *i1 = str_replace(i0, "sessioncontrol : \n{\n};\n", "");
char *i2 = str_replace(i1, "pulseaudio : \n{\n};\n", "");
char *i14 = str_replace(i2, "diagnostics : \n{\n};\n", "");
free(i2);
free(i1);
free(i0);

      // print it out
      if (strlen(i14) == 0)
        _inform(filename, linenumber, "The Configuration file contains no active settings.");
      else {
        _inform(filename, linenumber, "Configuration File Settings:");
        char *p = i14;
        while (*p != '\0') {
          i = 0;
          while ((*p != '\0') && (*p != '\n')) {
            result[i] = *p;
            p++;
            i++;
          }
          if (i != 0) {
            result[i] = '\0';
            _inform(filename, linenumber, " %s", result);
          }
          if (*p == '\n')
            p++;
        }
      }

      free(i14); // free the cleaned-up configuration string

      /*
            while (fgets(result, 1024, cr) != NULL) {
              // replace funny character at the end, if it's there
              if (result[strlen(result) - 1] <= ' ')
                result[strlen(result) - 1] = '\0'; // remove the last character if it's not
         printable _inform(filename, linenumber, " %s", result);
            }
      */
    } else {
      debug(1, "Error making pipe.\n");
    }
  }
  _inform(filename, linenumber, "");
  _inform(filename, linenumber, ">> Display Config End.");

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

#define display_config(argc, argv) _display_config(__FILE__, __LINE__, argc, argv)


/*
typedef struct {           // channel layout names and equates -- see
                           // https://www.ffmpeg.org/doxygen/2.4/group__channel__mask__c.html
  const char *name;        // e.g. "AV_CH_LAYOUT_5POINT1"
  uint64_t channel_layout; // e.g. AV_CH_LAYOUT_5POINT1
} channel_layout_t;
channel_layout_t channel_layouts[] = {
    {"AV_CH_LAYOUT_MONO", AV_CH_LAYOUT_MONO},
    {"AV_CH_LAYOUT_STEREO", AV_CH_LAYOUT_STEREO},
    {"AV_CH_LAYOUT_2POINT1", AV_CH_LAYOUT_2POINT1},
    {"AV_CH_LAYOUT_2_1", AV_CH_LAYOUT_2_1},
    {"AV_CH_LAYOUT_SURROUND", AV_CH_LAYOUT_SURROUND},
    {"AV_CH_LAYOUT_3POINT1", AV_CH_LAYOUT_3POINT1},
    {"AV_CH_LAYOUT_4POINT0", AV_CH_LAYOUT_4POINT0},
    {"AV_CH_LAYOUT_4POINT1", AV_CH_LAYOUT_4POINT1},
    {"AV_CH_LAYOUT_2_2", AV_CH_LAYOUT_2_2},
    {"AV_CH_LAYOUT_QUAD", AV_CH_LAYOUT_QUAD},
    {"AV_CH_LAYOUT_5POINT0", AV_CH_LAYOUT_5POINT0},
    {"AV_CH_LAYOUT_5POINT1", AV_CH_LAYOUT_5POINT1},
    {"AV_CH_LAYOUT_5POINT0_BACK", AV_CH_LAYOUT_5POINT0_BACK},
    {"AV_CH_LAYOUT_5POINT1_BACK", AV_CH_LAYOUT_5POINT1_BACK},
    {"AV_CH_LAYOUT_6POINT0", AV_CH_LAYOUT_6POINT0},
    {"AV_CH_LAYOUT_6POINT0_FRONT", AV_CH_LAYOUT_6POINT0_FRONT},
    {"AV_CH_LAYOUT_HEXAGONAL", AV_CH_LAYOUT_HEXAGONAL},
    {"AV_CH_LAYOUT_6POINT1", AV_CH_LAYOUT_6POINT1},
    {"AV_CH_LAYOUT_6POINT1_BACK", AV_CH_LAYOUT_6POINT1_BACK},
    {"AV_CH_LAYOUT_6POINT1_FRONT", AV_CH_LAYOUT_6POINT1_FRONT},
    {"AV_CH_LAYOUT_7POINT0", AV_CH_LAYOUT_7POINT0},
    {"AV_CH_LAYOUT_7POINT0_FRONT", AV_CH_LAYOUT_7POINT0_FRONT},
    {"AV_CH_LAYOUT_7POINT1", AV_CH_LAYOUT_7POINT1},
    {"AV_CH_LAYOUT_7POINT1_WIDE", AV_CH_LAYOUT_7POINT1_WIDE},
    {"AV_CH_LAYOUT_7POINT1_WIDE_BACK", AV_CH_LAYOUT_7POINT1_WIDE_BACK},
    {"AV_CH_LAYOUT_OCTAGONAL", AV_CH_LAYOUT_OCTAGONAL},
    //        {"AV_CH_LAYOUT_STEREO_DOWNMIX",	AV_CH_LAYOUT_STEREO_DOWNMIX},
};

uint64_t av_channel_layout_lookup(const char *str) {
  unsigned int m;
  uint64_t response = 0;
  for (m = 0; (m < sizeof(channel_layouts) / sizeof(channel_layout_t)) && (response == 0); m++) {
    if (strcasecmp(str, channel_layouts[m].name) == 0) {
      response = channel_layouts[m].channel_layout;
    }
  }
  return response;
}

const char *av_channel_layout_name(uint64_t channel_layout) {
  const char *response = NULL;
  unsigned int m;
  for (m = 0; (m < sizeof(channel_layouts) / sizeof(channel_layout_t)) && (response == NULL); m++) {
    if (channel_layouts[m].channel_layout == channel_layout) {
      response = channel_layouts[m].name;
    }
  }
  return response;
}
*/



static ConfigurationEnvironment configurationEnvironment(const StartupOptions &options) {
  ConfigurationEnvironment environment;
  environment.defaultPath = std::string(SYSCONFDIR) + "/shairport-sync.conf";
  char hostname[256]{};
  gethostname(hostname, sizeof(hostname) - 1);
  environment.hostname = hostname;
  environment.packageVersion = PACKAGE_VERSION;
  std::unique_ptr<char, decltype(&free)> version(get_version_string(), &free);
  environment.detailedVersion = version ? version.get() : PACKAGE_VERSION;
  environment.firmwareVersion = PACKAGE_VERSION;
#ifdef CONFIG_USE_GIT_VERSION_STRING
  if (git_version_string[0] != '\0')
    environment.firmwareVersion = git_version_string;
#endif
  if (options.operation() == StartupOptions::Operation::receive)
    get_device_id(environment.hardwareAddress.data(), environment.hardwareAddress.size());
  environment.endianness = std::endian::native == std::endian::little ? SS_LITTLE_ENDIAN :
                           std::endian::native == std::endian::big ? SS_BIG_ENDIAN : SS_PDP_ENDIAN;
  environment.interfaceIndex = [](std::string_view name) {
    return if_nametoindex(std::string(name).c_str());
  };
  return environment;
}

static void initializeRuntimeIdentity() {
  // Create an airplay psi UUID based on the ap1_prefix.

  // a uuid_t and an md5 hash are both 128 bits, 16 bytes
  uuid_t result;
  memset(result, 0, sizeof(result));
  if (sizeof(config.ap1_prefix) < sizeof(result))
    memcpy(result, config.ap1_prefix, sizeof(config.ap1_prefix));
  else
    memcpy(result, config.ap1_prefix, sizeof(result));

  // OpenSSL is mandatory for AirPlay 2
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);
  EVP_DigestUpdate(mdctx, config.ap1_prefix, sizeof(config.ap1_prefix));
  unsigned int md5_digest_len = EVP_MD_size(EVP_md5());
  EVP_DigestFinal_ex(mdctx, result, &md5_digest_len);
  EVP_MD_CTX_free(mdctx);

  // now, convert it into a type 4 UUID
  // see https://stackoverflow.com/questions/10867405/generating-v5-uuid-what-is-name-and-namespace
  // //set high-nibble to 5 to indicate type 5

  result[6] &= 0x0F;
  result[6] |= 0x40;

  // set upper two bits to "10"
  result[8] &= 0x3F;
  result[8] |= 0x80;

  char *psi_uuid = static_cast<char *>(malloc(UUID_STR_LEN + 1));
  // Produces a UUID string at uuid consisting of lower-case letters
  uuid_unparse_lower(result, psi_uuid);
  config.airplay_psi = psi_uuid;

  debug(3, "size of pk is %zu.", sizeof(config.airplay_pk));
  pair_public_key_get(PAIR_SERVER_HOMEKIT, config.airplay_pk, config.airplay_device_id);
  char buf[128];
  char *ptr = buf;
  size_t pk_index;
  for (pk_index = 0; pk_index < sizeof(config.airplay_pk); pk_index++)
    ptr += sprintf(ptr, "%02x", config.airplay_pk[pk_index]);
  *ptr = '\0';
  config.pk_string = strdup(buf);

  // the features code is a 64-bit number, but in the mDNS advertisement, the least significant 32
  // bit are given first for example, if the features number is 0x1C340405F4A00, it will be given as
  // features=0x405F4A00,0x1C340 in the mDNS string, and in a signed decimal number in the plist:
  // 496155702020608 this setting here is the source of both the plist features response and the
  // mDNS string.

  config.airplay_features = 0x00018340405C4A00; // no AP2 metadata (b50), no AP1 text (b17), no AP1
                                                // progress (b16), no AP1 artwork (b15)


  // now generate the fex field
  uint8_t fexbytes[8];
  uint64_t temp = config.airplay_features;
  debug(4, "airplay_features are %" PRIx64 ".", temp);
  for (int i = 0; i < 8; i++) {
    fexbytes[i] = temp & 0xff;
    temp = temp >> 8;
  }

  config.airplay_fex = base64_enc(fexbytes, 8);
  if (config.airplay_fex == NULL)
    die("could not allocate memory for \"airplay_fex\"");
  // strip the padding.
  char *padding = strchr(config.airplay_fex, '=');
  if (padding)
    *padding = 0;
  debug(2, "airplay_fex is \"%s\"", config.airplay_fex);

  // now the status flags
  // Advertised with mDNS and returned with GET /info, see
  // https://openairplay.github.io/airplay-spec/status_flags.html

  config.airplay_statusflags = 0;
  config.airplay_statusflags |= 1 << 2; // Audio cable is attached
  if (config.password != NULL) {
    config.airplay_statusflags |= 1 << 7; // Password required
  }
  // config.airplay_statusflags |= 1 << 10; // DeviceWasSetupForHKAccessControl
  // config.airplay_statusflags |= 1 << 11; // DeviceSupportsRelay
  // config.airplay_statusflags |= 1 << 19; // Unknown. Seems to control whether individual volume
  // controls are shown and whether the SPS devices shows when its active.

  config.airplay_pi = generate_device_uuid(config.airplay_device_id);
  config.airplay_pgid = generate_random_uuid();


}

int ReceiverApplication::run(ReceiverSettings settings) {
  static std::optional<LegacyConfigLease> processSettings;
  processSettings.emplace(std::move(settings));
  config_file_real_path = processSettings->configurationRealPath();
  atexit(exit_function);
  exit_init();
  const auto &diagnostics = processSettings->settings().diagnostics();
  debug_init(diagnostics.verbosity, 0, 1, 1, exit_request);
  pid = getpid();
  setlogmask(LOG_UPTO(LOG_DEBUG));
  openlog(NULL, 0, LOG_DAEMON);
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 10, 0)
  avcodec_init();
#endif
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
  avcodec_register_all();
#endif
  av_log_set_level(AV_LOG_ERROR);
  r64init(0);
  const auto *firstBackend = audio_get_output(nullptr);
  if (!firstBackend)
    die("No audio backend found! Check your build of Shairport Sync.");
  strncpy(first_backend_name, firstBackend->name, sizeof(first_backend_name) - 1);
  config.output_name = first_backend_name;
  sharedVolumeLevel.remember(AirPlayVolume{config.default_airplay_volume});
  initializeRuntimeIdentity();
  ptp_send_control_message_string("T");
  if (ptp_shm_interface_open() != 0) {
    die("NQPTP is required for AirPlay 2 and must be running with readable, complete shared memory: %s.", strerror(errno));
  }
  int ptp_clock_version = ptp_get_clock_version();
  if (ptp_clock_version == 0)
    die("NQPTP shared memory is not initialised or its clock data is inconsistent.");
  if (ptp_clock_version != NQPTP_SHM_STRUCTURES_VERSION)
    die("NQPTP shared memory version %d is incompatible; version %d is required.",
        ptp_clock_version, NQPTP_SHM_STRUCTURES_VERSION);


    if (has_fltp_capable_aac_decoder() == 0) {
      die("Shairport Sync can not run on this system. Run \"shairport-sync -h\" for more "
          "information.");
    }

  uint64_t apf = config.airplay_features;
  uint64_t apfh = config.airplay_features;
  apfh = apfh >> 32;
  uint32_t apf32 = apf;
  uint32_t apfh32 = apfh;

    debug(1,
          "Startup in AirPlay 2 mode, with features 0x%" PRIx32 ",0x%" PRIx32 " on device \"%s\".",
          apf32, apfh32, config.airplay_device_id);


  // control-c (SIGINT) cleanly
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_handler = intHandler;
  sigaction(SIGINT, &act, NULL);

  // terminate (SIGTERM)
  struct sigaction act2;
  memset(&act2, 0, sizeof(struct sigaction));
  act2.sa_handler = termHandler;
  sigaction(SIGTERM, &act2, NULL);

  // stop a pipe signal from killing the program
  signal(SIGPIPE, SIG_IGN);

  // install a zombie process reaper
  // see: http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
  struct sigaction sa;
  sa.sa_handler = &handle_sigchld;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa, 0) == -1) {
    perror(0);
    exit(1);
  }

  // make sure the program can create files that group and world can read
  umask(S_IWGRP | S_IWOTH);

  /* print out version */

  char *version_dbs = get_version_string();
  if (version_dbs) {
    debug(1, "Version String: \"%s\"", version_dbs);
    free(version_dbs);
  } else {
    debug(1, "Can't print the version information!");
  }

  if (sodium_init() < 0) {
    debug(1, "Can't initialise libsodium!");
  } else {
    debug(2, "libsodium initialised.");
  }

  // this code is based on
  // https://www.gnupg.org/documentation/manuals/gcrypt/Initializing-the-library.html

  /* Version check should be the very first call because it
    makes sure that important subsystems are initialized.
    #define NEED_LIBGCRYPT_VERSION to the minimum required version. */

#define NEED_LIBGCRYPT_VERSION "1.5.4"

  if (!gcry_check_version(NEED_LIBGCRYPT_VERSION)) {
    die("libgcrypt is too old (need %s, have %s).", NEED_LIBGCRYPT_VERSION,
        gcry_check_version(NULL));
  }

  /* Disable secure memory.  */
  gcry_control(GCRYCTL_DISABLE_SECMEM, 0);

  /* ... If required, other initialization goes here.  */

  /* Tell Libgcrypt that initialization has completed. */
  gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

  debug(2, "libgcrypt initialised.");


  debug(2, "Log Verbosity is %d.", debug_level());

  config.output = audio_get_output(config.output_name);
  if (!config.output) {
    die("the audio backend selected: \"%s\" is not supported. Either it is invalid, or support has "
        "not been included in this build of Shairport Sync.",
        config.output_name == NULL ? "<unspecified>" : config.output_name);
  }
  debug(1, "audio backend is \"%s\".", config.output_name);
  config.output->init(0, nullptr);

  if (debug_level() <= 1) // keep FFmpeg stuff quiet unless verbosity is 2 or more
    av_log_set_level(AV_LOG_QUIET);

  switch (config.endianness) {
  case SS_LITTLE_ENDIAN:
    debug(2, "The processor is running little-endian.");
    break;
  case SS_BIG_ENDIAN:
    debug(2, "The processor is running big-endian.");
    break;
  case SS_PDP_ENDIAN:
    debug(2, "The processor is running pdp-endian.");
    break;
  }

  /* Mess around with the latency options */
  // Basically, we expect the source to set the latency and add a fixed offset of 11025 frames to
  // it, which sounds right
  // If this latency is outside the max and min latensies that may be set by the source, clamp it to
  // fit.

  // If they specify a non-standard latency, we suggest the user to use the
  // audio_backend_latency_offset instead.

  if (config.userSuppliedLatency) {
    inform("The fixed latency setting is deprecated, as Shairport Sync gets the correct "
           "latency automatically from the source.");
    inform("Use the audio_backend_latency_offset_in_seconds setting "
           "instead to compensate for timing issues.");
  }
  const int option_print_level = 1;
  /* Print out options */
  debug(option_print_level, "disable_resend_requests is %s.",
        config.disable_resend_requests ? "on" : "off");
  debug(option_print_level,
        "diagnostic_drop_packet_fraction is %f. A value of 0.0 means no packets will be dropped "
        "deliberately.",
        config.diagnostic_drop_packet_fraction);
  debug(option_print_level, "statistics_requester status is %d.", config.statistics_requested);
  debug(option_print_level, "rtsp listening port is %d.", config.port);
  debug(option_print_level, "udp base port is %d.", config.udp_port_base);
  debug(option_print_level, "udp port range is %d.", config.udp_port_range);
  debug(option_print_level, "player name is \"%s\".", config.service_name);
  debug(option_print_level, "run_this_before_play_begins action is \"%s\".",
        strnull(config.cmd_start));
  debug(option_print_level, "run_this_after_play_ends action is \"%s\".", strnull(config.cmd_stop));
  debug(option_print_level, "wait-cmd status is %d.", config.cmd_blocking);
  debug(option_print_level, "run_this_before_play_begins may return output is %d.",
        config.cmd_start_returns_output);
  debug(option_print_level, "run_this_if_an_unfixable_error_is_detected action is \"%s\".",
        strnull(config.cmd_unfixable));
  debug(option_print_level, "run_this_before_entering_active_state action is  \"%s\".",
        strnull(config.cmd_active_start));
  debug(option_print_level, "run_this_after_exiting_active_state action is  \"%s\".",
        strnull(config.cmd_active_stop));
  debug(option_print_level, "active_state_timeout is  %f seconds.", config.active_state_timeout);
  debug(2, "userSuppliedLatency is %d.", config.userSuppliedLatency);
  debug(option_print_level, "interpolation setting is \"%s\".",
        config.packet_stuffing == ST_basic     ? "basic"
        : config.packet_stuffing == ST_vernier ? "vernier"
                                               : "auto");
  debug(option_print_level, "resync time is %f seconds.", config.resync_threshold);
  debug(option_print_level, "allow session interruption: \"%s\".",
        config.allow_session_interruption == 0 ? "no" : "yes");
  debug(option_print_level, "busy timeout time is %d.", config.timeout);
  debug(option_print_level, "drift tolerance is %f seconds.", config.tolerance);
  debug(option_print_level, "password is \"%s\".", strnull(config.password));
  debug(option_print_level, "default airplay volume is: %.6f.", config.default_airplay_volume);
  debug(option_print_level, "ignore_volume_control is %d.", config.ignore_volume_control);
  if (config.volume_max_db_set)
    debug(option_print_level, "volume_max_db is %d.", config.volume_max_db);
  else
    debug(option_print_level, "volume_max_db is not set");
  debug(option_print_level,
        "volume range in dB (zero means use the range specified by the mixer): %u.",
        config.volume_range_db);
  debug(option_print_level,
        "volume_range_combined_hardware_priority (1 means hardware mixer attenuation is used "
        "first) is %d.",
        config.volume_range_hw_priority);
  debug(option_print_level,
        "playback_mode is %d (0-stereo, 1-mono, 1-reverse_stereo, 2-both_left, 3-both_right).",
        config.playback_mode);
  debug(option_print_level, "disable_synchronization is %d.", config.no_sync);
  debug(option_print_level, "use_mmap_if_available is %d.", config.no_mmap ? 0 : 1);
  debug(option_print_level, "output_format automatic selection is %sabled.",
        config.output_format_auto_requested ? "en" : "dis");
  // if (config.output_format_auto_requested == 0)
  //   debug(option_print_level, "output_format is \"%s\".",
  //         sps_format_description_string(config.current_output_configuration->format));
  debug(option_print_level, "output_rate automatic selection is %sabled.",
        config.output_rate_auto_requested ? "en" : "dis");
  // if (config.output_rate_auto_requested == 0)
  //   debug(option_print_level, "output_rate is %d.", config.current_output_configuration->rate);
  debug(option_print_level, "audio backend desired buffer length is %f seconds.",
        config.audio_backend_buffer_desired_length);
  debug(option_print_level,
        "audio_backend_buffer_interpolation_threshold_in_seconds is %f seconds.",
        config.audio_backend_buffer_interpolation_threshold_in_seconds);
  debug(option_print_level, "audio backend latency offset is %f seconds.",
        processSettings->settings().audio().audio_backend_latency_offset);
  if (config.audio_backend_silent_lead_in_time_auto == 1)
    debug(option_print_level, "audio backend silence lead-in time is \"auto\".");
  else
    debug(option_print_level, "audio backend silence lead-in time is %f seconds.",
          config.audio_backend_silent_lead_in_time);
  debug(option_print_level, "zeroconf regtype is \"%s\".", config.regtype);
  if (config.interface)
    debug(option_print_level, "mdns service interface \"%s\" requested.", config.interface);
  else
    debug(option_print_level, "no special mdns service interface was requested.");
  char *realConfigPath = realpath(config.configfile, NULL);
  if (realConfigPath) {
    debug(option_print_level, "configuration file name \"%s\" resolves to \"%s\".",
          config.configfile, realConfigPath);
    free(realConfigPath);
  } else {
    debug(option_print_level, "configuration file name \"%s\" can not be resolved.",
          config.configfile);
  }



  debug(2, "LIBAVUTIL_VERSION_MAJOR is %d", LIBAVUTIL_VERSION_MAJOR);

#if LIBAVUTIL_VERSION_MAJOR >= 57
  unsigned int cc;
  for (cc = 1; cc <= 8; cc++) {
    AVChannelLayout output_channel_layout;
    av_channel_layout_default(&output_channel_layout, cc);
    char chLayoutDescription[1024];
    int sts = av_channel_layout_describe(&output_channel_layout, chLayoutDescription,
                                         sizeof(chLayoutDescription));
    if (sts >= 0) {
      int idx;
      char channel_map[1024];
      channel_map[0] = '\0';
      for (idx = 0; idx < output_channel_layout.nb_channels; idx++) {
        enum AVChannel av = av_channel_layout_channel_from_index(&output_channel_layout, idx);
        char chName[64];
        int cts = av_channel_name(chName, sizeof(chName), av);
        if (cts >= 0) {
          if (idx != 0)
            strncat(channel_map, " ", sizeof(channel_map) - 1);
          strncat(channel_map, chName, sizeof(channel_map) - 1 - strlen(channel_map));
          debug(4, "Channel %d: \"%s\"", idx, chName);
        } else {
          debug(1, "Insufficient space for the name of channel %d.", idx);
        }
      }
      if (cc == 1) {
        debug(2, "Default layout for one channel: \"%s\", channel map: \"%s\".",
              chLayoutDescription, channel_map);
      } else {
        debug(2, "Default layout for %u channels: \"%s\", channel map: \"%s\".", cc,
              chLayoutDescription, channel_map);
      }
    } else {
      debug(1, "Insufficient space for the description of the default layout for %u channels.", cc);
    }
    av_channel_layout_uninit(&output_channel_layout);
  }
#endif











  activity_monitor_start();
  debug(4, "create an RTSP listener");
  auto timingSettings = std::make_shared<SharedRtpTimingSettings>(
      processSettings->settings().audio().audio_backend_latency_offset);
  int listenerError = rtsp_listener_start(timingSettings);
  if (listenerError)
    die("Cannot create RTSP listener: %s", strerror(listenerError));

  // wait forever...
  while (1) {
    usleep(1000000);
  }
  return 0;
}

int shairport_receiver_main(int argc, char **argv) {
  std::vector<std::string_view> arguments(argv + 1, argv + argc);
  const auto options = StartupOptions::parse(arguments);
  if (!options) {
    fprintf(stderr, "%s\n", options.error().c_str());
    return 2;
  }
  if (options->operation() == StartupOptions::Operation::version) {
    print_version();
    return 0;
  }


  auto settings = ConfigurationLoader::load(*options, configurationEnvironment(*options));
  if (!settings) {
    for (const auto &diagnostic : settings.error().precedingDiagnostics)
      fprintf(stderr, "%s\n", diagnostic.message.c_str());
    fprintf(stderr, "%s\n", settings.error().message.c_str());
    return 1;
  }
  for (const auto &diagnostic : settings->diagnosticsLog())
    fprintf(stderr, "%s\n", diagnostic.message.c_str());
  if (options->operation() == StartupOptions::Operation::checkConfiguration)
    return 0;
  ReceiverApplication application;
  return application.run(std::move(*settings));
}
