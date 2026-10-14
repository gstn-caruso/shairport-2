#!/bin/sh
set -eu
binary=$1
version=$2
case "$("$binary" --version)" in
  *"$version; NQPTP 1.2.8;"*smi10*) ;;
  *) echo 'Unexpected companion version identity' >&2; exit 1 ;;
esac
"$binary" --help | grep -q 'Usage:'
