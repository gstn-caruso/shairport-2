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

#include "runtime/common.h"
#include "platform/utilities/network_utilities.h"

#ifdef CONFIG_USE_GIT_VERSION_STRING
#include "cmake-gitversion.h"
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h> // PRIdPTR
#include <libgen.h>
#include <math.h>
#include <memory.h>
#include <poll.h>
#include <popt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <ifaddrs.h>
#include <net/if.h>

#include <netpacket/packet.h>




#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>



#include <syslog.h>


#include "timing/nqptp-shm-structures.h"


unsigned int sps_format_sample_size_array[] = {
    0,       // unknown
    1, 1,    // S8, U8
    2, 2,    // S16_LE, S16_BE,
    4, 4,    // S24_LE, S24_BE,
    3, 3,    // S24_3LE, S24_3BE,
    4, 4,    // S32_LE, S32_BE,
    2, 4, 4, // S16, S24, S32
    0, 0     // Auto, Invalid
};

unsigned int sps_format_sample_size(sps_format_t format) {
  unsigned int response = 0;
  if (format <= SPS_FORMAT_AUTO)
    response = sps_format_sample_size_array[format];
  return response;
}


char sfd[32];
const char *short_format_description(int32_t encoded_format) {
  if (encoded_format < 0)
    snprintf(sfd, sizeof(sfd) - 1, "error %d", encoded_format);
  else
    snprintf(
        sfd, sizeof(sfd) - 1, "%u/%s/%u", RATE_FROM_ENCODED_FORMAT(encoded_format),
        sps_format_description_string((sps_format_t)(FORMAT_FROM_ENCODED_FORMAT(encoded_format))),
        CHANNELS_FROM_ENCODED_FORMAT(encoded_format));
  return (const char *)sfd;
}

// true if shairport-2 is supposed to be sending output to the output device, false otherwise
// static volatile int requested_connection_state_to_output = 1;


/*
// this stuff is to direct logging to syslog via libdaemon or directly
// alternatively you can direct it to stderr using a command line option

#ifdef CONFIG_LIBDAEMON
static void (*sps_log)(int prio, const char *t, ...) = daemon_log;
#else
static void (*sps_log)(int prio, const char *t, ...) = syslog;
#endif

void do_sps_log_to_stderr(__attribute__((unused)) int prio, const char *t, ...) {
  char s[16384];
  va_list args;
  va_start(args, t);
  vsnprintf(s, sizeof(s), t, args);
  va_end(args);
  fprintf(stderr, "%s\n", s);
}

void do_sps_log_to_stdout(__attribute__((unused)) int prio, const char *t, ...) {
  char s[16384];
  va_list args;
  va_start(args, t);
  vsnprintf(s, sizeof(s), t, args);
  va_end(args);
  fprintf(stdout, "%s\n", s);
}

int create_log_file(const char *path) {
  int fd = -1;
  if (path != NULL) {
    char *dirc = strdup(path);
    if (dirc) {
      char *dname = dirname(dirc);
      // create the directory, if necessary
      int result = 0;
      if (dname) {
        char *pdir = realpath(dname, NULL); // will return a NULL if the directory doesn't exist
        if (pdir == NULL) {
          mode_t oldumask = umask(000);
          result = mkpath(dname, 0777);
          umask(oldumask);
        } else {
          free(pdir);
        }
        if ((result == 0) || (result == -EEXIST)) {
          // now open the file
          fd = open(path, O_WRONLY | O_NONBLOCK | O_CREAT | O_EXCL,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
          if ((fd == -1) && (errno == EEXIST))
            fd = open(path, O_WRONLY | O_APPEND | O_NONBLOCK);

          if (fd >= 0) {
            // now we switch to blocking mode
            int flags = fcntl(fd, F_GETFL);
            if (flags == -1) {
              //							strerror_r(errno, (char
              // *)errorstring, sizeof(errorstring));
              // debug(1, "create_log_file -- error %d (\"%s\") getting flags of pipe: \"%s\".",
              // errno,
              // (char *)errorstring, pathname);
            } else {
              flags = fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
              //							if (flags == -1) {
              //								strerror_r(errno,
              //(char *)errorstring, sizeof(errorstring));
              // debug(1, "create_log_file -- error %d
              //(\"%s\") unsetting NONBLOCK of pipe: \"%s\".", errno,
              //(char *)errorstring, pathname);
            }
          }
        }
      }
      free(dirc);
    }
  }
  return fd;
}

void do_sps_log_to_fd(__attribute__((unused)) int prio, const char *t, ...) {
  char s[16384];
  va_list args;
  va_start(args, t);
  vsnprintf(s, sizeof(s), t, args);
  va_end(args);
  if (config.log_fd == -1)
    config.log_fd = create_log_file(config.log_file_path);
  if (config.log_fd >= 0) {
    dprintf(config.log_fd, "%s\n", s);
  } else if (errno != ENXIO) { // maybe there is a pipe there but not hooked up
    fprintf(stderr, "%s\n", s);
  }
}

void log_to_stderr() { sps_log = do_sps_log_to_stderr; }
void log_to_stdout() { sps_log = do_sps_log_to_stdout; }
void log_to_file() { sps_log = do_sps_log_to_fd; }
void log_to_syslog() {
#ifdef CONFIG_LIBDAEMON
  sps_log = daemon_log;
#else
  sps_log = syslog;
#endif

}
*/

shairport_cfg config;

static uint16_t UDPPortIndex = 0;

void resetFreeUDPPort() {
  debug(3, "Resetting UDP Port Suggestion to %u", config.udp_port_base);
  UDPPortIndex = 0;
}

uint16_t nextFreeUDPPort() {
  if (UDPPortIndex == 0)
    UDPPortIndex = config.udp_port_base;
  else if (UDPPortIndex == (config.udp_port_base + config.udp_port_range - 1))
    UDPPortIndex = config.udp_port_base + 3; // avoid wrapping back to the first three, as they can
                                             // be assigned by resetFreeUDPPort without checking
  else
    UDPPortIndex++;
  return UDPPortIndex;
}

// if port is zero, pick any port
// otherwise, try the given port only
int bind_socket_and_port(int type, int ip_family, const char *self_ip_address, uint32_t scope_id,
                         uint16_t *port, int *sock) {
  int ret = 0; // no error
  int local_socket = socket(ip_family, type, 0);
  if (local_socket == -1)
    ret = errno;
  if (ret == 0) {
    SOCKADDR myaddr;
    memset(&myaddr, 0, sizeof(myaddr));
    if (ip_family == AF_INET) {
      struct sockaddr_in *sa = (struct sockaddr_in *)&myaddr;
      sa->sin_family = AF_INET;
      sa->sin_port = ntohs(*port);
      inet_pton(AF_INET, self_ip_address, &(sa->sin_addr));
      ret = bind(local_socket, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
    }
#ifdef AF_INET6
    if (ip_family == AF_INET6) {
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&myaddr;
      sa6->sin6_family = AF_INET6;
      sa6->sin6_port = ntohs(*port);
      inet_pton(AF_INET6, self_ip_address, &(sa6->sin6_addr));
      sa6->sin6_scope_id = scope_id;
      ret = bind(local_socket, (struct sockaddr *)sa6, sizeof(struct sockaddr_in6));
    }
#endif
    if (ret < 0) {
      ret = errno;
      safe_socket_close(&local_socket);
      char errorstring[1024];
      getErrorText((char *)errorstring, sizeof(errorstring));
      warn("error %d: \"%s\". Could not bind a port!", errno, errorstring);
    } else {
      uint16_t sport;
      SOCKADDR local;
      socklen_t local_len = sizeof(local);
      ret = getsockname(local_socket, (struct sockaddr *)&local, &local_len);
      if (ret < 0) {
        ret = errno;
        safe_socket_close(&local_socket);
        char errorstring[1024];
        getErrorText((char *)errorstring, sizeof(errorstring));
        warn("error %d: \"%s\". Could not retrieve socket's port!", errno, errorstring);
      } else {
#ifdef AF_INET6
        if (local.SAFAMILY == AF_INET6) {
          struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&local;
          sport = ntohs(sa6->sin6_port);
        } else
#endif
        {
          struct sockaddr_in *sa = (struct sockaddr_in *)&local;
          sport = ntohs(sa->sin_port);
        }
        *sock = local_socket;
        *port = sport;
      }
    }
  }
  return ret;
}

uint16_t bind_UDP_port(int ip_family, const char *self_ip_address, uint32_t scope_id, int *sock) {
  // look for a port in the range, if any was specified.
  int ret = 0;

  int local_socket = socket(ip_family, SOCK_DGRAM, IPPROTO_UDP);
  if (local_socket == -1)
    die("Could not allocate a socket.");

  /*
    int val = 1;
    ret = setsockopt(local_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    if (ret < 0) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "Error %d: \"%s\". Couldn't set SO_REUSEADDR");
    }
  */

  SOCKADDR myaddr;
  int tryCount = 0;
  uint16_t desired_port;
  do {
    tryCount++;
    desired_port = nextFreeUDPPort();
    memset(&myaddr, 0, sizeof(myaddr));
    if (ip_family == AF_INET) {
      struct sockaddr_in *sa = (struct sockaddr_in *)&myaddr;
      sa->sin_family = AF_INET;
      sa->sin_port = ntohs(desired_port);
      inet_pton(AF_INET, self_ip_address, &(sa->sin_addr));
      ret = bind(local_socket, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
    }
#ifdef AF_INET6
    if (ip_family == AF_INET6) {
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&myaddr;
      sa6->sin6_family = AF_INET6;
      sa6->sin6_port = ntohs(desired_port);
      inet_pton(AF_INET6, self_ip_address, &(sa6->sin6_addr));
      sa6->sin6_scope_id = scope_id;
      ret = bind(local_socket, (struct sockaddr *)sa6, sizeof(struct sockaddr_in6));
    }
#endif

  } while ((ret < 0) && (errno == EADDRINUSE) && (desired_port != 0) &&
           (tryCount < config.udp_port_range));

  // debug(1,"UDP port chosen: %d.",desired_port);

  if (ret < 0) {
    safe_socket_close(&local_socket);
    char errorstring[1024];
    getErrorText((char *)errorstring, sizeof(errorstring));
    die("error %d: \"%s\". Could not bind a UDP port! Check the udp_port_range is large enough -- "
        "it must be "
        "at least 3, and 10 or more is suggested -- or "
        "check for restrictive firewall settings or a bad router! UDP base is %u, range is %u and "
        "current suggestion is %u.",
        errno, errorstring, config.udp_port_base, config.udp_port_range, desired_port);
  }

  uint16_t sport;
  SOCKADDR local;
  socklen_t local_len = sizeof(local);
  getsockname(local_socket, (struct sockaddr *)&local, &local_len);
#ifdef AF_INET6
  if (local.SAFAMILY == AF_INET6) {
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&local;
    sport = ntohs(sa6->sin6_port);
  } else
#endif
  {
    struct sockaddr_in *sa = (struct sockaddr_in *)&local;
    sport = ntohs(sa->sin_port);
  }
  *sock = local_socket;
  return sport;
}

// int get_requested_connection_state_to_output() { return requested_connection_state_to_output; }

// void set_requested_connection_state_to_output(int v) { requested_connection_state_to_output = v; }

void getErrorText(char *destinationString, size_t destinationStringLength) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
  strerror_r(errno, destinationString, destinationStringLength);
#pragma GCC diagnostic pop
}

// The following two functions are adapted slightly and with thanks from Jonathan Leffler's sample
// code at
// https://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux

int do_mkdir(const char *path, mode_t mode) {
  struct stat st;
  int status = 0;

  if (stat(path, &st) != 0) {
    /* Directory does not exist. EEXIST for race condition */
    if (mkdir(path, mode) != 0 && errno != EEXIST)
      status = -1;
  } else if (!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    status = -1;
  }

  return (status);
}

// mkpath - ensure all directories in path exist
// Algorithm takes the pessimistic view and works top-down to ensure
// each directory in path exists, rather than optimistically creating
// the last element and working backwards.

int mkpath(const char *path, mode_t mode) {
  char *pp;
  char *sp;
  int status;
  char *copypath = strdup(path);

  status = 0;
  pp = copypath;
  while (status == 0 && (sp = strchr(pp, '/')) != 0) {
    if (sp != pp) {
      /* Neither root nor double slash in path */
      *sp = '\0';
      status = do_mkdir(copypath, mode);
      *sp = '/';
    }
    pp = sp + 1;
  }
  if (status == 0)
    status = do_mkdir(path, mode);
  free(copypath);
  return (status);
}

// including a simple base64 encoder to minimise malloc/free activity

// From Stack Overflow, with thanks:
// http://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c
// minor mods to make independent of C99.
// more significant changes make it not malloc memory
// needs to initialise the encoding table first

// add _so to end of name to avoid confusion with polarssl's implementation

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
                                'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                                'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                                'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
                                '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

static size_t mod_table[] = {0, 2, 1};

// pass in a pointer to the data, its length, a pointer to the output buffer and
// a pointer to an int
// containing its maximum length
// the actual length will be returned.

char *base64_encode_so(const unsigned char *data, size_t input_length, char *encoded_data,
                       size_t *output_length) {

  size_t calculated_output_length = 4 * ((input_length + 2) / 3);
  if (calculated_output_length > *output_length)
    return (NULL);
  *output_length = calculated_output_length;

  size_t i, j;
  for (i = 0, j = 0; i < input_length;) {

    uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
    uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
    uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

    uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

    encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
  }

  for (i = 0; i < mod_table[input_length % 3]; i++)
    encoded_data[*output_length - 1 - i] = '=';

  return encoded_data;
}

// with thanks!
//



char *base64_enc(uint8_t *input, int length) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  BIO *bmem, *b64;
  BUF_MEM *bptr;
  b64 = BIO_new(BIO_f_base64());
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, input, length);
  (void)BIO_flush(b64);
  BIO_get_mem_ptr(b64, &bptr);

  char *buf = (char *)malloc(bptr->length);
  if (buf == NULL)
    die("could not allocate memory for buf in base64_enc");
  if (bptr->length) {
    memcpy(buf, bptr->data, bptr->length - 1);
    buf[bptr->length - 1] = 0;
  }

  BIO_free_all(b64);

  pthread_setcancelstate(oldState, NULL);
  return buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  BIO *bmem, *b64;
  int inlen = strlen(input);

  b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);

  // Apple cut the padding off their challenges; restore it
  BIO_write(bmem, input, inlen);
  while (inlen++ & 3)
    BIO_write(bmem, "=", 1);
  (void)BIO_flush(bmem);

  int bufsize = strlen(input) * 3 / 4 + 1;
  uint8_t *buf = static_cast<uint8_t *>(malloc(bufsize));
  int nread;

  nread = BIO_read(b64, buf, bufsize);

  BIO_free_all(b64);

  *outlen = nread;
  pthread_setcancelstate(oldState, NULL);
  return buf;
}






// Look for the item in the setting which could be either a string or an array or list or group of
// strings. Result: 0 means there is a setting but no match, 1 means there's no setting, 2 means
// "auto" was found, 3 means a match.
int check_string_or_list_setting(config_setting_t *setting, const char *item) {
  int result = 1; // means there is no setting at all (so the caller should implement the default)
  if (setting != NULL) { // definitely a setting
    const char *str = config_setting_get_string(setting);
    debug(3, "check \"%s\" against \"%s\"", str, item);
    if (str != NULL) { // definitely a string
      if (strcasecmp(str, item) == 0) {
        result = 3; // an exact match
      } else if (strcasecmp(str, "auto") == 0) {
        result = 2; // auto
      } else {
        result = 0; // a string that is not a match
      }
    } else { // it might be a list, an array or a group
      int i = 0;
      result = 0; // presume there is no match
      // keep looking, even if "auto" has been found, to see if the exact match (preferred) is there
      // too.
      while (((result == 0) || (result == 2)) && (i < config_setting_length(setting))) {
        const char *str2 = config_setting_get_string_elem(setting, i);
        if (str2 != NULL) { // definitely a string
          if (strcasecmp(str2, "auto") == 0) {
            result = 2; // auto
          } else if (strcasecmp(str2, item) == 0) {
            result = 3; // an exact match
          }
        }
        i++; // will point to 1 past the found item or last item.
      }
    }
  }
  return result;
}

// Look for the item in the setting which could be either an int or an array or list or group of
// ints. Result: 0 means there is a setting but no match, 1 means there's no setting, 2 means "auto"
// was found, 3 means a match.
int check_int_or_list_setting(config_setting_t *setting, const int item) {
  int result = 1;        // means there is no setting at all
  if (setting != NULL) { // definitely a setting
    int setting_type = config_setting_type(setting);
    if (setting_type == CONFIG_TYPE_STRING) {
      if (strcasecmp(config_setting_get_string(setting), "auto") == 0) {
        result = 2; // auto
      } else {
        result = 0; // a string that can not be a match
      }
    } else if (setting_type == CONFIG_TYPE_INT) {
      if (item == config_setting_get_int(setting))
        result = 3; // an exact match
      else
        result = 0; // a setting but not a match
    } else {        // it might be a list, an array or a group
      int i = 0;
      result = 0; // presume there is no match (there is a setting)
      // keep looking, even if "auto" has been found, to see if the exact match (preferred) is there
      // too.
      while (((result == 0) || (result == 2)) && (i < config_setting_length(setting))) {
        config_setting_t *sub_setting = config_setting_get_elem(setting, i);
        int sub_setting_type = config_setting_type(sub_setting);
        if (sub_setting_type == CONFIG_TYPE_STRING) {
          if (strcasecmp(config_setting_get_string_elem(sub_setting, i), "auto") == 0) {
            result = 2; // auto
          }
        } else if (sub_setting_type == CONFIG_TYPE_INT) {
          if (item == config_setting_get_int(sub_setting))
            result = 3; // an exact match
        }
        i++; // will point to 1 past the found item or last item.
      }
    }
  }
  return result;
}



void command_set_volume(double volume) {
  // this has a cancellation point if waiting is enabled
  if (config.cmd_set_volume) {
    /*Spawn a child to run the program.*/
    pid_t pid = fork();
    if (pid == 0) { /* child process */
      size_t command_buffer_size = strlen(config.cmd_set_volume) + 32;
      char *command_buffer = (char *)malloc(command_buffer_size);
      if (command_buffer == NULL) {
        inform("Couldn't allocate memory for set_volume argument string");
      } else {
        memset(command_buffer, 0, command_buffer_size);
        snprintf(command_buffer, command_buffer_size, "%s %f", config.cmd_set_volume, volume);
        // debug(1,"command_buffer is \"%s\".",command_buffer);
        int argC;
        char **argV;
        // debug(1,"set_volume command found.");
        if (poptParseArgvString(command_buffer, &argC, (const char ***)&argV) != 0) {
          // note that argV should be free()'d after use, but we expect this fork to exit
          // eventually.
          warn("Can't decipher on-set-volume command arguments \"%s\".", command_buffer);
          free(argV);
          free(command_buffer);
        } else {
          free(command_buffer);
          // debug(1,"Executing on-set-volume command %s with %d arguments.",argV[0],argC);
          execv(argV[0], argV);
          warn("Execution of on-set-volume command \"%s\" failed to start", config.cmd_set_volume);
          // debug(1, "Error executing on-set-volume command %s", config.cmd_set_volume);
          _exit(EXIT_FAILURE); /* only if execv fails */
        }
      }
      _exit(EXIT_SUCCESS);
    } else {
      if (config.cmd_blocking) { /* pid!=0 means parent process and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0); /* wait for child to exit */
        if (rc != pid) {
          warn("Execution of on-set-volume command returned an error.");
          debug(1, "on-set-volume command %s finished with error %d", config.cmd_set_volume, errno);
        }
      }
      // debug(1,"Continue after on-set-volume command");
    }
  }
}

void command_start(void) {
  // this has a cancellation point if waiting is enabled or a response is awaited
  if (config.cmd_start) {
    pid_t pid;
    int pipes[2];

    if (config.cmd_start_returns_output && pipe(pipes) != 0) {
      warn("Unable to allocate pipe for popen of start command.");
      debug(1, "pipe finished with error %d", errno);
      return;
    }
    /*Spawn a child to run the program.*/
    pid = fork();
    if (pid == 0) { /* child process */
      int argC;
      char **argV;

      if (config.cmd_start_returns_output) {
        safe_socket_close(&pipes[0]);
        if (dup2(pipes[1], 1) < 0) {
          warn("Unable to reopen pipe as stdout for popen of start command");
          debug(1, "dup2 finished with error %d", errno);
          safe_socket_close(&pipes[1]);
          return;
        }
      }

      // debug(1,"on-start command found.");
      if (poptParseArgvString(config.cmd_start, &argC, (const char ***)&argV) !=
          0) // note that argV should be free()'d after use, but we expect this fork to exit
             // eventually.
        debug(1, "Can't decipher on-start command arguments");
      else {
        // debug(1,"Executing on-start command %s with %d arguments.",argV[0],argC);
        execv(argV[0], argV);
        warn("Execution of on-start command failed to start");
        debug(1, "Error executing on-start command %s", config.cmd_start);
        _exit(EXIT_FAILURE); /* only if execv fails */
      }
    } else {
      if (config.cmd_blocking || config.cmd_start_returns_output) { /* pid!=0 means parent process
                                    and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0);                              /* wait for child to exit */
        if ((rc != pid) && (errno != ECHILD)) {
          // In this context, ECHILD means that the child process has already completed, I think!
          warn("Execution of on-start command returned an error.");
          debug(1, "on-start command %s finished with error %d", config.cmd_start, errno);
        }
        if (config.cmd_start_returns_output) {
          static char buffer[256];
          int len;
          safe_socket_close(&pipes[1]);
          len = read(pipes[0], buffer, 255);
          safe_socket_close(&pipes[0]);
          buffer[len] = '\0';
          if (buffer[len - 1] == '\n')
            buffer[len - 1] = '\0'; // strip trailing newlines
          debug(1, "received '%s' as the device to use from the on-start command", buffer);
        }
      }
      // debug(1,"Continue after on-start command");
    }
  }
}
void command_execute(const char *command, const char *extra_argument, const int block) {
  // this has a cancellation point if waiting is enabled
  if (command) {
    char new_command_buffer[2048];
    char *full_command = (char *)command;
    if (extra_argument != NULL) {
      memset(new_command_buffer, 0, sizeof(new_command_buffer));
      snprintf(new_command_buffer, sizeof(new_command_buffer), "%s %s", command, extra_argument);
      full_command = new_command_buffer;
    }

    /*Spawn a child to run the program.*/
    pid_t pid = fork();
    if (pid == 0) { /* child process */
      int argC;
      char **argV;
      if (poptParseArgvString(full_command, &argC, (const char ***)&argV) !=
          0) // note that argV should be free()'d after use, but we expect this fork to exit
             // eventually.
        debug(1, "Can't decipher command arguments in \"%s\".", full_command);
      else {
        // debug(1,"Executing command %s",full_command);
        execv(argV[0], argV);
        warn("Execution of command \"%s\" failed to start", full_command);
        debug(1, "Error executing command \"%s\".", full_command);
        _exit(EXIT_FAILURE); /* only if execv fails */
      }
    } else {
      if (block) { /* pid!=0 means parent process and if blocking is true, wait for
                                    process to finish */
        pid_t rc = waitpid(pid, 0, 0); /* wait for child to exit */
        if ((rc != pid) && (errno != ECHILD)) {
          // In this context, ECHILD means that the child process has already completed, I think!
          warn("Execution of command \"%s\" returned an error.", full_command);
          debug(1, "Command \"%s\" finished with error %d", full_command, errno);
        }
      }
      // debug(1,"Continue after on-unfixable command");
    }
  }
}

void command_stop(void) {
  // this has a cancellation point if waiting is enabled
  if (config.cmd_stop)
    command_execute(config.cmd_stop, "", config.cmd_blocking);
}

// this is for reading an unsigned 32 bit number, such as an RTP timestamp

uint32_t uatoi(const char *nptr) {
  uint64_t llint = atoll(nptr);
  uint32_t r = llint;
  return r;
}

uint64_t get_monotonic_time_in_ns() {
  uint64_t time_now_ns;

  struct timespec tn;
  clock_gettime(CLOCK_MONOTONIC, &tn);
  uint64_t tnnsec = tn.tv_sec;
  tnnsec = tnnsec * 1000000000;
  uint64_t tnjnsec = tn.tv_nsec;
  time_now_ns = tnnsec + tnjnsec;


  return time_now_ns;
}

// Not defined for macOS
uint64_t get_realtime_in_ns() {
  uint64_t time_now_ns;
  struct timespec tn;
  clock_gettime(CLOCK_REALTIME, &tn);
  uint64_t tnnsec = tn.tv_sec;
  tnnsec = tnnsec * 1000000000;
  uint64_t tnjnsec = tn.tv_nsec;
  time_now_ns = tnnsec + tnjnsec;
  return time_now_ns;
}

uint64_t get_absolute_time_in_ns() {
  // CLOCK_MONOTONIC_RAW/CLOCK_MONOTONIC in Linux/FreeBSD etc, monotonic in MacOSX
  uint64_t time_now_ns;

  struct timespec tn;
#ifdef CLOCK_MONOTONIC_RAW
  clock_gettime(CLOCK_MONOTONIC_RAW, &tn);
#else
  clock_gettime(CLOCK_MONOTONIC, &tn);
#endif
  uint64_t tnnsec = tn.tv_sec;
  tnnsec = tnnsec * 1000000000;
  uint64_t tnjnsec = tn.tv_nsec;
  time_now_ns = tnnsec + tnjnsec;


  return time_now_ns;
}

int try_to_open_pipe_for_writing(const char *pathname) {
  // tries to open the pipe in non-blocking mode first.
  // if it succeeds, it sets it to blocking.
  // if not, it returns -1.

  int fdis = open(pathname, O_WRONLY | O_NONBLOCK); // open it in non blocking mode first

  // we check that it's not a "real" error. From the "man 2 open" page:
  // "ENXIO  O_NONBLOCK | O_WRONLY is set, the named file is a FIFO, and no process has the FIFO
  // open for reading." Which is okay.
  // This is checked by the caller.

  if (fdis >= 0) {
    // now we switch to blocking mode
    int flags = fcntl(fdis, F_GETFL);
    if (flags == -1) {
      char errorstring[1024];
      getErrorText((char *)errorstring, sizeof(errorstring));
      debug(1, "try_to_open_pipe -- error %d (\"%s\") getting flags of pipe: \"%s\".", errno,
            (char *)errorstring, pathname);
    } else {
      flags = fcntl(fdis, F_SETFL, flags & ~O_NONBLOCK);
      if (flags == -1) {
        char errorstring[1024];
        getErrorText((char *)errorstring, sizeof(errorstring));
        debug(1, "try_to_open_pipe -- error %d (\"%s\") unsetting NONBLOCK of pipe: \"%s\".", errno,
              (char *)errorstring, pathname);
      }
    }
  }
  return fdis;
}

/* from http://burtleburtle.net/bob/rand/smallprng.html */

// this is not thread-safe, so we need a mutex on it to use it properly.
// always lock use this when accessing the fp_time_at_last_debug_message

pthread_mutex_t r64_mutex = PTHREAD_MUTEX_INITIALIZER;

// typedef uint64_t u8;
typedef struct ranctx {
  uint64_t a;
  uint64_t b;
  uint64_t c;
  uint64_t d;
} ranctx;

static struct ranctx rx;

#define rot(x, k) (((x) << (k)) | ((x) >> (64 - (k))))
uint64_t ranval(ranctx *x) {
  uint64_t e = x->a - rot(x->b, 7);
  x->a = x->b ^ rot(x->c, 13);
  x->b = x->c + rot(x->d, 37);
  x->c = x->d + e;
  x->d = e + x->a;
  return x->d;
}

void raninit(ranctx *x, uint64_t seed) {
  uint64_t i;
  x->a = 0xf1ea5eed, x->b = x->c = x->d = seed;
  for (i = 0; i < 20; ++i) {
    (void)ranval(x);
  }
}

void r64init(uint64_t seed) { raninit(&rx, seed); }

uint64_t r64u() { return (ranval(&rx)); }

int64_t r64i() { return (ranval(&rx) >> 1); }

uint32_t nctohl(const uint8_t *p) { // read 4 characters from *p and do ntohl on them
  // this is to avoid possible aliasing violations
  uint32_t holder;
  memcpy(&holder, p, sizeof(holder));
  return ntohl(holder);
}

uint16_t nctohs(const uint8_t *p) { // read 2 characters from *p and do ntohs on them
  // this is to avoid possible aliasing violations
  uint16_t holder;
  memcpy(&holder, p, sizeof(holder));
  return ntohs(holder);
}

uint64_t nctoh64(const uint8_t *p) {
  uint32_t landing = nctohl(p); // get the high order 32 bits
  uint64_t vl = landing;
  vl = vl << 32;                          // shift them into the correct location
  landing = nctohl(p + sizeof(uint32_t)); // and the low order 32 bits
  uint64_t ul = landing;
  vl = vl + ul;
  return vl;
}

void sps_nanosleep(const time_t sec, const long nanosec) {
  struct timespec req, rem;
  int result;
  req.tv_sec = sec;
  req.tv_nsec = nanosec;
  do {
    result = nanosleep(&req, &rem);
    rem = req;
  } while ((result == -1) && (errno == EINTR));
  if (result == -1)
    debug(1, "Error in sps_nanosleep of %" PRIdMAX " sec and %ld nanoseconds: %d.", (intmax_t)sec,
          nanosec, errno);
}

void malloc_cleanup(void *arg) {
  // the address of the malloc variable is passed in case a realloc is done as some time
  // debug(1, "malloc cleanup called.");
  void **allocation = static_cast<void **>(arg);
  void *ref = *allocation;
  if (ref != NULL)
    free(ref);
}

void plist_cleanup(void *arg) {
  // debug(1, "plist cleanup called.");
  plist_free((plist_t)arg);
}

void socket_cleanup(void *arg) {
  int *p = (int *)arg;
  debug(3, "socket_cleanup called for socket: %d.", *p);
  safe_socket_close(p);
}

void cv_cleanup(void *arg) {
  // debug(1, "cv_cleanup called.");
  pthread_cond_t *cv = (pthread_cond_t *)arg;
  pthread_cond_destroy(cv);
}

void mutex_cleanup(void *arg) {
  // debug(1, "mutex_cleanup called.");
  pthread_mutex_t *mutex = (pthread_mutex_t *)arg;
  pthread_mutex_destroy(mutex);
}

void rwlock_unlock(void *arg) { pthread_rwlock_unlock((pthread_rwlock_t *)arg); }

void mutex_unlock(void *arg) { pthread_mutex_unlock((pthread_mutex_t *)arg); }

void thread_cleanup(void *arg) {
  debug(3, "thread_cleanup called.");
  pthread_t *thread = (pthread_t *)arg;
  pthread_cancel(*thread);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  pthread_join(*thread, NULL);
  pthread_setcancelstate(oldState, NULL);
  debug(3, "thread_cleanup done.");
}

void pthread_cleanup_debug_mutex_unlock(void *arg) { pthread_mutex_unlock((pthread_mutex_t *)arg); }

char *get_version_string() {
  char *version_string = static_cast<char *>(malloc(1024));
  if (version_string) {
#ifdef CONFIG_USE_GIT_VERSION_STRING
    if (git_version_string[0] != '\0')
      strcpy(version_string, git_version_string);
    else
#endif
      strcpy(version_string, PACKAGE_VERSION);
    strcat(version_string, "-AirPlay2");
    char smiv[1024];
    snprintf(smiv, 1024, "-smi%u", NQPTP_SHM_STRUCTURES_VERSION);
    strcat(version_string, smiv);
    strcat(version_string, "-OpenSSL");
    strcat(version_string, "-Avahi");
    strcat(version_string, "-PulseAudio");
    strcat(version_string, "-sysconfdir:");
    strcat(version_string, SYSCONFDIR);
  }
  return version_string;
}

// This will check the incoming string "s" of length "len" with the existing NUL-terminated string
// "str" and update "flag" accordingly.

// Note: if the incoming string length is zero, then the a NULL is used; i.e. no zero-length strings
// are stored.

// If the strings are different, the str is free'd and replaced by a pointer
// to a newly strdup'd string and the flag is set
// If they are the same, the flag is cleared

int string_update_with_size(char **str, int *flag, char *s, size_t len) {
  if (*str) {
    if ((s) && (len)) {
      if ((len != strlen(*str)) || (strncmp(*str, s, len) != 0)) {
        free(*str);
        //*str = strndup(s, len); // it seems that OpenWrt 12 doesn't have this
        char *p = static_cast<char *>(malloc(len + 1));
        memcpy(p, s, len);
        p[len] = '\0';
        *str = p;
        *flag = 1;
      } else {
        *flag = 0;
      }
    } else {
      // old string is non-NULL, new string is NULL or length 0
      free(*str);
      *str = NULL;
      *flag = 1;
    }
  } else { // old string is NULL
    if ((s) && (len)) {
      //*str = strndup(s, len); // it seems that OpenWrt 12 doesn't have this
      char *p = static_cast<char *>(malloc(len + 1));
      memcpy(p, s, len);
      p[len] = '\0';
      *str = p;
      *flag = 1;
    } else {
      // old string is NULL and new string is NULL or length 0
      *flag = 0; // so no change
    }
  }
  return *flag;
}

// from https://stackoverflow.com/questions/13663617/memdup-function-in-c, with thanks
void *memdup(const void *mem, size_t size) {
  void *out = malloc(size);

  if (out != NULL)
    memcpy(out, mem, size);

  return out;
}

// This will allocate memory and place the NUL-terminated hex character equivalent of
// the bytearray passed in whose length is given.
char *debug_malloc_hex_cstring(void *packet, size_t nread) {
  char *response = static_cast<char *>(malloc(nread * 3 + 1));
  unsigned char *q = static_cast<unsigned char *>(packet);
  char *obfp = response;
  size_t obfc;
  for (obfc = 0; obfc < nread; obfc++) {
    snprintf(obfp, 4, "%02x ", *q);
    obfp += 3; // two digit characters and a space
    q++;
  };
  obfp--; // overwrite the last space with a NUL
  *obfp = 0;
  return response;
}

int get_device_id(uint8_t *id, int int_length) {

  uint64_t wait_time = 10000000000L; // wait up to this (ns) long to get a MAC address

  int response = -1;
  struct ifaddrs *ifaddr = NULL;
  struct ifaddrs *ifa = NULL;

  int i = 0;
  uint8_t *t = id;
  for (i = 0; i < int_length; i++) {
    *t++ = 0;
  }

  uint64_t wait_until = get_absolute_time_in_ns();
  wait_until = wait_until + wait_time;

  int64_t time_to_wait;
  do {
    int oldState;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
    if (getifaddrs(&ifaddr) == 0) {
      t = id;
      int found = 0;

      for (ifa = ifaddr; ((ifa != NULL) && (found == 0)); ifa = ifa->ifa_next) {
#ifdef AF_PACKET
        if ((ifa->ifa_addr) && (ifa->ifa_addr->sa_family == AF_PACKET)) {
          struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
          if (((ifa->ifa_flags & IFF_UP) != 0) && ((ifa->ifa_flags & IFF_RUNNING) != 0) &&
              ((ifa->ifa_flags & IFF_LOOPBACK) == 0) && (ifa->ifa_addr != 0)) {
            found = 1;
            response = 0;
            for (i = 0; ((i < s->sll_halen) && (i < int_length)); i++) {
              *t++ = s->sll_addr[i];
            }
          }
        }
#else
#ifdef AF_LINK
        struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
        if ((sdl) && (sdl->sdl_family == AF_LINK)) {
          if (sdl->sdl_type == IFT_ETHER) {
            found = 1;
            response = 0;
            uint8_t *s = (uint8_t *)LLADDR(sdl);
            for (i = 0; ((i < sdl->sdl_alen) && (i < int_length)); i++) {
              *t++ = *s++;
            }
          }
        }
#endif
#endif
      }
      freeifaddrs(ifaddr);
    }
    pthread_setcancelstate(oldState, NULL);
    // wait a little time if we haven't got a response
    if (response != 0) {
      usleep(100000);
    }
    time_to_wait = wait_until - get_absolute_time_in_ns();
  } while ((response != 0) && (time_to_wait > 0));
  if (response != 0)
    warn("Can't create a device ID -- no valid MAC address can be found.");
  return response;
}

char *bnprintf(char *buffer, ssize_t max_bytes, const char *format, ...) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, max_bytes, format, args);
  va_end(args);
  pthread_setcancelstate(oldState, NULL);
  // debug(1,"bnprintf string is: \"%s\"", buffer);
  return buffer;
}

int do_pthread_setname(pthread_t *thread, const char *format, ...) {
  // pthread_setname_np/2 not defined in macOS
  char actual_name[16];
  va_list args;
  va_start(args, format);
  vsnprintf(actual_name, sizeof(actual_name), format, args);
  va_end(args);
  return pthread_setname_np(*thread, actual_name);
}

int named_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg, const char *format, ...) {
  char actual_name[16];
  va_list args;
  va_start(args, format);
  vsnprintf(actual_name, sizeof(actual_name), format, args);
  va_end(args);
  int response = pthread_create(thread, attr, start_routine, arg);
  if (response != 0) {
    debug(1, "error creating thread \"%s\"", actual_name);
  }
  if (response == 0) {
    pthread_setname_np(*thread, actual_name);
  }
  return response;
}

int named_pthread_create_with_priority(pthread_t *thread, int priority,
                                       void *(*start_routine)(void *), void *arg,
                                       const char *format, ...) {

  // if this gets a permissions error, it'll try to create a thread without any special
  // priority or scheduling

  static int failed_to_set_rt = 0;

  struct sched_param param;
  pthread_attr_t attr;
  int ret = 0;

  char actual_name[16];
  va_list args;
  va_start(args, format);
  vsnprintf(actual_name, sizeof(actual_name), format, args);
  va_end(args);

  /* Initialize pthread attributes (default values) */
  ret = pthread_attr_init(&attr);
  if (ret == 0) {
    /* Set scheduler policy and priority of pthread */
    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    if (ret == 0) {
      param.sched_priority = priority;
      ret = pthread_attr_setschedparam(&attr, &param);
      if (ret == 0) {
        /* Use scheduling parameters of attr */
        ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        if (ret != 0) {
          debug(1, "pthread setinheritsched failed");
        }
      } else {
        debug(1, "pthread setschedparam failed");
      }
    } else {
      debug(1, "pthread setschedpolicy failed");
    }
  } else {
    debug(1, "init pthread attributes failed");
  }
  // ret == 0 if creating and setting up the attributes was successful
  if (ret == 0) {

    ret = pthread_create(thread, &attr, start_routine, arg);
    pthread_attr_destroy(&attr);
  }
  // ret will be non-zero if there was a problem creating the attribute or creating the prioritized
  // thread
  if (ret != 0) {
    ret = pthread_create(thread, NULL, start_routine, arg);
    if (failed_to_set_rt == 0) {
      inform("Can not set realtime properties of thread \"%s\".", actual_name);
      failed_to_set_rt = 1;
    }
  }
  if (ret == 0) {
    pthread_setname_np(*thread, actual_name);
  } else {
    die("named_pthread_create_with_priority failed with error %d", ret);
  }
  return ret;
}
