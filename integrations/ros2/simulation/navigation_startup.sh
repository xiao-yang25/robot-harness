#!/usr/bin/env bash
# Sourced by the scene supervisor; the three PIDs are its own direct children.
navigation_processes_alive() {
  local role pid code
  while (( $# )); do
    role=$1 pid=$2
    shift 2
    if ! kill -0 "$pid" 2>/dev/null; then
      code=0
      wait "$pid" || code=$?
      printf 'Navigation process exited: role=%s pid=%s exit=%s\n' "$role" "$pid" "$code" >&2
      return 1
    fi
  done
}

wait_navigation_endpoint() {
  local endpoint=$1 budget=$2
  shift 2
  local until=$((SECONDS+budget))
  while true; do
    # Check each child even if the endpoint appeared during this poll.
    navigation_processes_alive "$@" || return 1
    [[ ! -f $endpoint ]] || return 0
    if (( SECONDS >= until )); then
      printf 'Navigation endpoint readiness timed out after %s seconds: %s\n' "$budget" "$endpoint" >&2
      return 1
    fi
    sleep .1
  done
}
