#!/usr/bin/env bash
#
# Remove directories on a persistent runner. CI containers run as root, so
# files they wrote into bind-mounted directories may not be removable by the
# runner user. Fall back to sudo, and then to Docker, which the runner user
# can use to act as root anyway.

set -uo pipefail

for dir in "$@"; do
  [ -e "$dir" ] || continue
  echo "Removing $dir ($(du -sh "$dir" 2>/dev/null | cut -f1))"
  rm -rf -- "$dir" 2>/dev/null && continue
  sudo -n rm -rf -- "$dir" 2>/dev/null && continue
  parent="$(dirname "$dir")"
  name="$(basename "$dir")"
  docker run --rm --network none --volume "$parent:/parent" mirror.gcr.io/ubuntu:24.04 \
    rm -rf -- "/parent/$name" && continue
  echo "::warning title=Cleanup::Could not remove $dir"
done
