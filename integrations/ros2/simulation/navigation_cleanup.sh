#!/usr/bin/env bash
# Only the scene supervisor's own child PIDs are passed here.
stop_navigation_owner() {
  local pid=$1 until=$((SECONDS+$2)) forced=0 code=0
  [[ -n $pid ]] || return 0
  kill -INT "$pid" 2>/dev/null || true
  while kill -0 "$pid" 2>/dev/null && (( SECONDS < until )); do sleep .05; done
  if kill -0 "$pid" 2>/dev/null; then
    forced=1
    kill -KILL "$pid" 2>/dev/null || true
  fi
  wait "$pid" || code=$?
  python3 - "$pid" "$code" "$forced" "${3:-/output/owner-process.json}" <<'OWNER_EXIT' || true
import json, sys
from pathlib import Path
Path(sys.argv[4]).write_text(json.dumps(dict(
    pid=int(sys.argv[1]), returncode=int(sys.argv[2]), forced=bool(int(sys.argv[3])), reaped=True)))
OWNER_EXIT
}
