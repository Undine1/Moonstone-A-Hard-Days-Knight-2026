#!/usr/bin/env bash
# Same recipe on Windows and Bash; profile flags have one source of truth.
set -euo pipefail
exec python "$(dirname "$0")/build.py" "$@"
