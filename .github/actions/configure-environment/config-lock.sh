#!/usr/bin/env bash
#
# Run one job of a given configuration at a time on a persistent runner host.
# Jobs of the same configuration share a fixed container name and a persistent
# root directory, so a second one starting on another agent of the same host
# would remove the first one's running container and empty its root. Make it
# wait instead.
#
#   config-lock.sh acquire LOCK_FILE STATE_DIR
#   config-lock.sh release STATE_DIR
#
# The lock must outlive the step that takes it, so a detached process holds
# it. That process gives it up when release is called, when the job it belongs
# to ends without calling release (e.g. it was canceled), or after
# CONFIG_LOCK_MAX_MINUTES, whichever comes first.

export LC_ALL=C

set -euo pipefail

MAX_MINUTES="${CONFIG_LOCK_MAX_MINUTES:-260}"

# The nearest buildkite-agent ancestor is the bootstrap process of this job,
# which exits when the job ends. Print nothing when there is none.
job_pid() {
  local pid=$PPID comm
  while [ "$pid" -gt 1 ] && [ -r "/proc/$pid/stat" ]; do
    comm="$(cut -d'(' -f2 "/proc/$pid/stat" | cut -d')' -f1)"
    if [ "$comm" = "buildkite-agent" ]; then
      echo "$pid"
      return
    fi
    pid="$(sed 's/^.*) //' "/proc/$pid/stat" | cut -d' ' -f2)"
  done
}

case "${1:-}" in
  acquire)
    lock="$2"
    state="$3"
    owner="$(job_pid)"
    mkdir -p "$(dirname "$lock")" "$state"
    rm -f "$state/held" "$state/release"
    setsid "$0" hold "$lock" "$state" "$owner" < /dev/null > /dev/null 2>&1 &
    holder=$!
    start=$SECONDS
    next=$((SECONDS + 5))
    while [ ! -e "$state/held" ]; do
      if ! kill -0 "$holder" 2> /dev/null; then
        echo "::error title=Config lock::Lock holder for $lock exited before taking the lock"
        exit 1
      fi
      if [ "$SECONDS" -ge "$next" ]; then
        echo "Waiting for another job using $lock on this host to finish"
        next=$((SECONDS + 300))
      fi
      sleep 0.2
    done
    echo "Took $lock after $((SECONDS - start))s${owner:+ for job process $owner}"
    ;;
  hold)
    lock="$2"
    state="$3"
    owner="${4:-}"
    exec 9>> "$lock"
    flock 9
    echo $$ > "$state/held"
    deadline=$((SECONDS + MAX_MINUTES * 60))
    while [ ! -e "$state/release" ] && [ "$SECONDS" -lt "$deadline" ]; do
      if [ -n "$owner" ] && ! kill -0 "$owner" 2> /dev/null; then
        break
      fi
      sleep 1
    done
    ;;
  release)
    state="$2"
    [ -e "$state/held" ] || exit 0
    holder="$(cat "$state/held")"
    touch "$state/release"
    for _ in $(seq 1 30); do
      kill -0 "$holder" 2> /dev/null || exit 0
      sleep 1
    done
    echo "::warning title=Config lock::Lock holder $holder did not exit after release"
    ;;
  *)
    echo "usage: $0 acquire LOCK_FILE STATE_DIR | release STATE_DIR" >&2
    exit 2
    ;;
esac
