#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$1
CC=$2
CXX=$3
export CC CXX
linkage_binary=$4
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"$CC" -std=c17 -x c -fsyntax-only "$source_dir/pair_ap/pair.h" "$source_dir/pair_ap/pair-tlv.h"
"$linkage_binary" --version > "$test_dir/version"
grep -q 'AirPlay2-smi10-OpenSSL-Avahi-PulseAudio' "$test_dir/version"
for feature in AIRPLAY_2 PULSEAUDIO AVAHI OPENSSL FFMPEG; do
  grep -q "^#define CONFIG_$feature 1" "$build_dir/config.h"
done
meson setup "$test_dir/outside-repository" "$source_dir" -Dtests=false > "$test_dir/output" 2>&1 || {
  cat "$test_dir/output" >&2
  exit 1
}
if PKG_CONFIG_LIBDIR="$test_dir/no-packages" PKG_CONFIG_PATH= \
    meson setup "$test_dir/missing-dependencies" "$source_dir" -Dtests=false \
      > "$test_dir/output" 2>&1; then
  echo 'Accepted missing required dependencies' >&2
  exit 1
fi
grep -q 'Dependency.*not found' "$test_dir/output"
for option in WITH_ALSA=true CONFIG_AIRPLAY_2=false WITH_SSL=mbedtls CONFIG_LIBDAEMON=true; do
  if meson setup "$test_dir/$option" "$source_dir" "-D$option" > "$test_dir/output" 2>&1; then
    echo "Accepted removed option $option" >&2
    exit 1
  fi
  grep -q 'Unknown option' "$test_dir/output"
done
for option in cpp_std=c++23 cpp_std=gnu++26 cpp_args=-std=c++23 cpp_args=-std=gnu++26; do
  if meson setup "$test_dir/$option" "$source_dir" "-D$option" > "$test_dir/output" 2>&1; then
    echo "Accepted incompatible C++ dialect $option" >&2
    exit 1
  fi
  grep -q 'C++26 without extensions is required' "$test_dir/output"
done
if CC=gcc CXX=g++ meson setup "$test_dir/gcc" "$source_dir" > "$test_dir/output" 2>&1; then
  echo 'Accepted unpinned compiler' >&2
  exit 1
fi
grep -q 'Clang 23.1.3 is required' "$test_dir/output"
cat > "$test_dir/non-linux.ini" <<EOF
[binaries]
c = '$CC'
cpp = '$CXX'
[host_machine]
system = 'generic'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF
if meson setup "$test_dir/platform" "$source_dir" --cross-file "$test_dir/non-linux.ini" \
    > "$test_dir/output" 2>&1; then
  echo 'Accepted unsupported platform' >&2
  exit 1
fi
grep -q 'Linux is the only supported platform' "$test_dir/output"
test -f "$build_dir/plists/get_info_response.cpp"
echo 'Pinned C++26 Linux AirPlay 2 configuration contract passed.'
