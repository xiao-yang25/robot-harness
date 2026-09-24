#!/usr/bin/env bash
set -eo pipefail
[[ -f /.dockerenv && ${M4_ISOLATED_SIMULATION:-} == 1 ]] || exit 2
# The host launcher creates an exclusive empty directory. Never append to evidence
# from an earlier session, including when someone invokes the image directly.
mkdir /output/session-started
mkdir -p "$HOME"
cp /simulation/packages.txt /output/packages.txt
if [[ ${M4_VISUAL:-0} == 1 ]]; then
  exec timeout --signal=INT --kill-after=10s 300s xvfb-run -a \
    -s '-screen 0 1600x900x24' bash /simulation/run_owner_handoff.sh
fi
exec timeout --signal=INT --kill-after=10s 300s bash /simulation/run_owner_handoff.sh
