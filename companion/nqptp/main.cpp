#include "runtime.hpp"
#include "debug.h"
#include <cstdio>
#include <exception>
#include <string_view>

int main(int argc, char **argv) {
  int verbosity = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string_view option(argv[i]);
    if (option == "-V" || option == "--version") {
      std::printf("shairport-2-nqptp %s; NQPTP 1.2.8; Shared Memory Interface smi%u\n",
                   VERSION, NQPTP_SHM_STRUCTURES_VERSION);
      return 0;
    }
    if (option == "-h" || option == "--help") {
      std::puts("Usage: shairport-2-nqptp [options]\n"
                "  -V, --version  Print version\n"
                "  -h, --help     Print help\n"
                "  -v/-vv/-vvv    Increase logging verbosity");
      return 0;
    }
    if (option == "-v") verbosity = 1;
    else if (option == "-vv") verbosity = 2;
    else if (option == "-vvv") verbosity = 3;
    else {
      std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      return 1;
    }
  }
  nqptp::debug_init(verbosity, 0, 1, 1);
  try {
    nqptp::Runtime runtime;
    runtime.run();
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "shairport-2-nqptp: %s\n", error.what());
    return 1;
  }
}
