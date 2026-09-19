#!/usr/bin/env bash
set -euo pipefail

# Leave the child channel destinations 64/65 available, but force the second
# socketpair's write endpoint to 64. The launcher must preserve both sources
# before applying its fixed child mappings. All inherited fds are harmless.
if (( $# != 2 )); then
  exit 2
fi
for ((descriptor = 3; descriptor <= 60; ++descriptor)); do
  eval "exec ${descriptor}</dev/null"
done
exec "$1" normal "$2"
