#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-./shairport-2}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
check_rejection() {
  if ! grep -Eq 'removed option|Invalid or repeated argument' "$test_dir/output" ||
      grep -Eq 'ERROR: (AddressSanitizer|LeakSanitizer)|WARNING: ThreadSanitizer|runtime error:' "$test_dir/output"; then
    cat "$test_dir/output" >&2
    exit 1
  fi
}
cc -shared -fPIC -I"$source_dir/src" "$source_dir/tests/nqptp_fixture.c" -o "$test_dir/fixture.so"
for group in alsa jack sndio ao soundio pipewire pipe stdout dummy dsp metadata dbus mpris mqtt; do
  printf '%s = {};\n' "$group" > "$test_dir/receiver.conf"
  if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" --config "$test_dir/receiver.conf" > "$test_dir/output" 2>&1; then
    echo "Accepted removed group $group" >&2; exit 1
  fi
  check_rejection
done
for setting in sessioncontrol.daemonize_with_pid_file sessioncontrol.daemonize_without_pid_file sessioncontrol.daemon_pid_dir general.soxr_delay_threshold general.dbus_service_bus general.mpris_service_bus diagnostics.retain_cover_art; do
  case "$setting" in
    *.daemonize_*|*.retain_cover_art) values='false true' ;;
    *.soxr_delay_threshold) values='0 30' ;;
    *) values='"" "session"' ;;
  esac
  for value in $values; do
    printf '%s = { %s = %s; };\n' "${setting%%.*}" "${setting#*.}" "$value" > "$test_dir/receiver.conf"
    if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" --config "$test_dir/receiver.conf" > "$test_dir/output" 2>&1; then
      echo "Accepted removed setting $setting=$value" >&2; exit 1
    fi
    check_rejection
  done
done
for setting in service_type output_backend mdns_backend alac_decoder; do
  printf 'general = { %s = "auto"; };\n' "$setting" > "$test_dir/receiver.conf"
  if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" --config "$test_dir/receiver.conf" > "$test_dir/output" 2>&1; then
    echo "Accepted removed setting $setting" >&2; exit 1
  fi
  check_rejection
done
printf 'general = { interpolation = "soxr"; };\n' > "$test_dir/receiver.conf"
if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" --config "$test_dir/receiver.conf" > "$test_dir/output" 2>&1; then
  echo 'Accepted removed soxr interpolation' >&2; exit 1
fi
check_rejection
printf 'general = {};\n' > "$test_dir/receiver.conf"
for option in --service-type=auto --output=pulseaudio --mdns=avahi --daemon --kill --justDaemoniseNoPIDFile --stuffing=soxr; do
  status=0
  if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" --config "$test_dir/receiver.conf" "$option" > "$test_dir/output" 2>&1; then
    echo "Accepted removed CLI option $option" >&2; exit 1
  else
    status=$?
  fi
  test "$status" = 2
  check_rejection
done
echo 'Removed runtime configuration and CLI options rejected.'
