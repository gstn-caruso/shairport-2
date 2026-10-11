#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-./shairport-2}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cc -shared -fPIC -I"$source_dir/src" "$source_dir/tests/nqptp_fixture.c" -o "$test_dir/fixture.so"
printf 'general = { name = "NQPTP Test"; };\n' > "$test_dir/receiver.conf"
for version in missing permission 0 9 11 truncated inconsistent; do
  if NQPTP_TEST_VERSION=$version LD_PRELOAD="$test_dir/fixture.so" timeout 3 "$binary" --config "$test_dir/receiver.conf" > "$test_dir/output" 2>&1; then
    echo "Unexpected success for NQPTP $version" >&2
    exit 1
  fi
  if ! grep -q 'fatal error:.*NQPTP' "$test_dir/output"; then
    cat "$test_dir/output" >&2
    echo "Missing explicit NQPTP startup failure for $version" >&2
    exit 1
  fi
  if grep -Eq 'ERROR: (AddressSanitizer|LeakSanitizer)|WARNING: ThreadSanitizer|runtime error:' "$test_dir/output"; then
    cat "$test_dir/output" >&2
    exit 1
  fi
  if grep -q 'Startup in Classic' "$test_dir/output"; then
    echo 'Unexpected AirPlay 1 fallback' >&2
    exit 1
  fi
done
echo 'Missing, uninitialised, incompatible and truncated NQPTP rejected.'
