#!/usr/bin/env bash
#
# Empty a persistent CI root directory except for the caches kept across jobs.
# BASE_ROOT_DIR is the persistent root, so every job copies the source tree
# there and the CI script writes its scratch files (test directories of failed
# tests, qa-assets, binaries) there too. None of that is meant to outlive the
# job, and left in place it fills the disk of the host over time.

export LC_ALL=C

set -uo pipefail

root="$1"
[ -d "$root" ] || exit 0

find "$root" -mindepth 1 -maxdepth 1 \
  ! -name ccache_dir ! -name depends ! -name previous_releases -print0 |
  while IFS= read -r -d '' path; do
    "$(dirname "$0")/remove-dirs.sh" "$path"
  done
