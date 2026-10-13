#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$1
c_compiler=$2
cxx_compiler=$3
linkage_binary=$4
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"$linkage_binary" --version > "$test_dir/version"
grep -q 'AirPlay2-smi10-OpenSSL-Avahi-PulseAudio' "$test_dir/version"
for feature in AIRPLAY_2 PULSEAUDIO AVAHI OPENSSL FFMPEG; do
  grep -q "^#define CONFIG_$feature 1" "$build_dir/config.h"
done
cmake -S "$source_dir" -B "$test_dir/outside-repository" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$source_dir/cmake/clang-toolchain.cmake" \
  -DBUILD_TESTING=OFF > "$test_dir/output" 2>&1 || {
    cat "$test_dir/output" >&2
    exit 1
  }
if PKG_CONFIG_LIBDIR="$test_dir/no-packages" PKG_CONFIG_PATH= \
    cmake -S "$source_dir" -B "$test_dir/missing-dependencies" -G Ninja \
      -DCMAKE_C_COMPILER="$c_compiler" -DCMAKE_CXX_COMPILER="$cxx_compiler" \
      > "$test_dir/output" 2>&1; then
  echo 'Accepted missing required dependencies' >&2
  exit 1
fi
grep -q 'required packages were not found' "$test_dir/output"
for option in WITH_ALSA=ON CONFIG_AIRPLAY_2=OFF WITH_SSL=mbedtls CONFIG_LIBDAEMON=ON; do
  if cmake -S "$source_dir" -B "$test_dir/$option" -G Ninja \
      -DCMAKE_C_COMPILER="$c_compiler" -DCMAKE_CXX_COMPILER="$cxx_compiler" \
      "-D$option" > "$test_dir/output" 2>&1; then
    echo "Accepted removed option $option" >&2
    exit 1
  fi
  grep -q 'removed option' "$test_dir/output"
done
for option in CMAKE_CXX_STANDARD=23 CMAKE_CXX_EXTENSIONS=ON; do
  if cmake -S "$source_dir" -B "$test_dir/$option" -G Ninja \
      -DCMAKE_C_COMPILER="$c_compiler" -DCMAKE_CXX_COMPILER="$cxx_compiler" \
      "-D$option" > "$test_dir/output" 2>&1; then
    echo "Accepted incompatible C++ dialect $option" >&2
    exit 1
  fi
  grep -q 'C++26 without extensions is required' "$test_dir/output"
done
if cmake -S "$source_dir" -B "$test_dir/gcc" -G Ninja \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ > "$test_dir/output" 2>&1; then
  echo 'Accepted unpinned compiler' >&2
  exit 1
fi
grep -q 'Clang 23.1.3 is required' "$test_dir/output"
if cmake -S "$source_dir" -B "$test_dir/platform" -G Ninja \
    -DCMAKE_C_COMPILER="$c_compiler" -DCMAKE_CXX_COMPILER="$cxx_compiler" \
    -DCMAKE_SYSTEM_NAME=Generic > "$test_dir/output" 2>&1; then
  echo 'Accepted unsupported platform' >&2
  exit 1
fi
grep -q 'Linux is the only supported platform' "$test_dir/output"
for production_source in src/app/main.cpp pair_ap/pair.cpp pair_ap/pair_fruit.cpp pair_ap/pair_homekit.cpp pair_ap/pair-tlv.cpp; do
  printf 'set_source_files_properties("%s/%s" PROPERTIES LANGUAGE C)\n' "$source_dir" "$production_source" \
    > "$test_dir/c-production.cmake"
  if cmake -S "$source_dir" -B "$test_dir/c-production-$production_source" -G Ninja \
      -DCMAKE_C_COMPILER="$c_compiler" -DCMAKE_CXX_COMPILER="$cxx_compiler" \
      -DCMAKE_USER_MAKE_RULES_OVERRIDE="$test_dir/c-production.cmake" \
      > "$test_dir/output" 2>&1; then
    echo "Accepted own production source compiled as C: $production_source" >&2
    exit 1
  fi
  grep -q 'Own production source must compile as C++26' "$test_dir/output"
done
test -f "$build_dir/plists/get_info_response.cpp"
echo 'Pinned C++26 Linux AirPlay 2 configuration contract passed.'
