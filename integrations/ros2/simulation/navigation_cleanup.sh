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

# Display stays available through all native/scene cleanup. Its node-free group
# gets a separate finite local window only when the optional camera is selected.
stop_navigation_display() {
  local pid=$1 report=$2 helper=$3 deadline code=0 joined=0 failed=0
  [[ -n $pid ]] || return 0
  deadline=$(python3 -c 'import time; print(time.monotonic()+2)')
  python3 "$helper" "$report" "$$" "$deadline" "display=$pid" || failed=1
  if ! kill -0 "$pid" 2>/dev/null; then
    wait "$pid" || code=$?
    joined=1
  else
    failed=1
  fi
  python3 - "$report" "$code" "$joined" <<'DISPLAY_EXIT' || failed=1
import json, sys
from pathlib import Path
path = Path(sys.argv[1])
result = json.loads(path.read_text())
result['groups'][0].update(returncode=int(sys.argv[2]), reaped=sys.argv[3] == '1')
result['clean'] = result['clean'] and sys.argv[3] == '1'
path.write_text(json.dumps(result, indent=2) + '\n')
DISPLAY_EXIT
  return "$failed"
}
