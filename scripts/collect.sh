#!/usr/bin/env bash
# Aggregate remote and working-tree worker results; --no-fetch uses cached refs.
set -euo pipefail
cd "$(dirname "$0")/.."
exec "${PYTHON:-python3}" scripts/collect_results.py "$@"
