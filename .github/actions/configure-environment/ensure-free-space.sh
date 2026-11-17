#!/usr/bin/env bash
#
# Make room on a persistent runner host before a job starts.
#
#   ensure-free-space.sh CACHE_ROOT
#
# The persistent root keeps depends and ccache of every job configuration
# across jobs. depends keys its built packages by recipe hash and never
# removes outdated ones, so the disk fills over time, and jobs then fail at a
# random step with "No space left on device". When the filesystem of
# CACHE_ROOT has less than CI_MIN_FREE_GB free, remove what can be rebuilt,
# cheapest first, until it has enough:
#
#   1. Docker build cache and dangling images.
#   2. Intermediate and installed depends trees (depends/work, depends/<host>),
#      which depends recreates from its built packages.
#   3. ccache directories.
#   4. Built depends packages, oldest first.
#
# Steps 2-4 only touch configurations whose lock (see config-lock.sh) is free,
# and hold it while they do, so a job running elsewhere on the host keeps its
# files. The calling job holds its own lock already, so its caches are kept.
#
# If the filesystem still has less than CI_HARD_MIN_FREE_GB free afterwards,
# fail now with a clear message instead of later at a random step.

export LC_ALL=C

set -uo pipefail

cache_root="$1"
min_kb=$(( ${CI_MIN_FREE_GB:-20} * 1024 * 1024 ))
hard_min_kb=$(( ${CI_HARD_MIN_FREE_GB:-8} * 1024 * 1024 ))
remove_dirs="$(dirname "$0")/remove-dirs.sh"

free_kb() {
  df -Pk "$cache_root" | awk 'NR == 2 { print $4 }'
}

report() {
  echo "$1: $(( $(free_kb) / 1024 / 1024 )) GiB free on the filesystem of $cache_root"
}

enough() {
  [ "$(free_kb)" -ge "$min_kb" ]
}

# Run "$@" while holding the lock of the configuration whose root is $1,
# or return 1 at once if another job holds it.
with_idle_config() {
  local name lock
  name="$(basename "$1")"
  shift
  lock="$cache_root/locks/$name.lock"
  mkdir -p "$(dirname "$lock")"
  (
    exec 8>> "$lock"
    flock -n 8 || exit 1
    "$@"
  )
}

remove_depends_trees() {
  local root="$1" dir
  for dir in "$root"/depends/work "$root"/depends/*/; do
    dir="${dir%/}"
    [ -d "$dir" ] || continue
    case "$(basename "$dir")" in
      built|sources) continue ;;
    esac
    [ -L "$dir" ] && continue
    "$remove_dirs" "$dir"
  done
}

remove_ccache() {
  "$remove_dirs" "$1/ccache_dir"
}

remove_package() {
  echo "Removing built depends package $1"
  rm -f -- "$1" "$1.hash"
}

report "Before cleanup"
enough && exit 0

if command -v docker > /dev/null 2>&1; then
  echo "Pruning Docker build cache and dangling images"
  docker builder prune --force > /dev/null 2>&1 || true
  docker image prune --force > /dev/null 2>&1 || true
  report "After pruning Docker"
  if ! enough; then
    docker builder prune --all --force > /dev/null 2>&1 || true
    report "After pruning all unused Docker build cache"
  fi
fi

roots=()
for root in "$cache_root"/work/*/; do
  [ -d "$root" ] && roots+=("${root%/}")
done

for step in remove_depends_trees remove_ccache; do
  enough && break
  for root in "${roots[@]}"; do
    enough && break
    with_idle_config "$root" "$step" "$root" || echo "Skipping $(basename "$root"): in use by another job"
  done
  report "After $step"
done

if ! enough; then
  # Oldest packages first, across all configurations.
  while IFS= read -r -d '' entry; do
    enough && break
    package="${entry#* }"
    root="${package%%/depends/built/*}"
    with_idle_config "$root" remove_package "$package" || true
  done < <(find "$cache_root"/work/*/depends/built -type f -name '*.tar.gz' -printf '%T@ %p\0' 2> /dev/null | sort -zn)
  report "After removing built depends packages"
fi

if [ "$(free_kb)" -lt "$hard_min_kb" ]; then
  echo "::error title=Disk space::Only $(( $(free_kb) / 1024 / 1024 )) GiB free on the filesystem of $cache_root after cleanup; free space on this runner host (at least ${CI_HARD_MIN_FREE_GB:-8} GiB needed)"
  exit 1
fi
if ! enough; then
  echo "::warning title=Disk space::Only $(( $(free_kb) / 1024 / 1024 )) GiB free on the filesystem of $cache_root after cleanup (target ${CI_MIN_FREE_GB:-20} GiB)"
fi
exit 0
