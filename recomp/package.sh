#!/usr/bin/env bash
# Always build and validate the public profile; never copy the development build.
set -euo pipefail
exec python "$(dirname "$0")/package.py" "$@"
