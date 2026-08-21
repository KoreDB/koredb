#!/usr/bin/env bash
#
# Launch an Electron entry point under CI and hold it to a deadline.
#
# A shutdown that never finishes is a real failure mode for a native addon, and
# it is the one hardest to read from a log: the job simply stops producing
# output until the whole runner times out. So the process is bounded here, and
# when it overruns its thread stacks are dumped first - a hung teardown is only
# actionable if you can see which call is blocking.
#
# Usage: ci-launch.sh <command> [args...]
#   LAUNCH_TIMEOUT  seconds to wait before declaring the launch hung (default 120)
set -euo pipefail

TIMEOUT="${LAUNCH_TIMEOUT:-120}"
ELECTRON_MATCH="electron/dist/electron"

dump_stacks() {
  if ! command -v gdb >/dev/null 2>&1; then
    echo "gdb is not installed; skipping the stack dump."
    return 0
  fi
  local pids
  pids="$(pgrep -f "$ELECTRON_MATCH" || true)"
  if [ -z "$pids" ]; then
    echo "No surviving Electron process to dump."
    return 0
  fi
  for pid in $pids; do
    echo "===== thread stacks for pid $pid ====="
    # sudo because Ubuntu's default ptrace_scope only lets a process trace its
    # own descendants, and gdb is a sibling of the Electron tree here.
    sudo gdb -p "$pid" -batch -ex "thread apply all bt" 2>&1 | head -300 || true
  done
}

if [ "${RUNNER_OS:-}" = "Linux" ]; then
  # Headless runners have no X server; Electron needs one even for an app that
  # never opens a window.
  xvfb-run -a "$@" &
  launch_pid=$!

  waited=0
  while kill -0 "$launch_pid" 2>/dev/null; do
    if [ "$waited" -ge "$TIMEOUT" ]; then
      echo "::error::Electron did not exit within ${TIMEOUT}s"
      dump_stacks
      kill -9 "$launch_pid" 2>/dev/null || true
      pkill -9 -f "$ELECTRON_MATCH" 2>/dev/null || true
      wait "$launch_pid" 2>/dev/null || true
      exit 124
    fi
    sleep 1
    waited=$((waited + 1))
  done

  set +e
  wait "$launch_pid"
  status=$?
  set -e
  exit "$status"
fi

# macOS and Windows: no display plumbing needed, and no stack dumping wired up.
# `timeout` is not part of a stock macOS, so it is used only when present.
if command -v timeout >/dev/null 2>&1; then
  set +e
  timeout -k 10 "$TIMEOUT" "$@"
  status=$?
  set -e
  if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
    echo "::error::Electron did not exit within ${TIMEOUT}s"
  fi
  exit "$status"
fi

exec "$@"
