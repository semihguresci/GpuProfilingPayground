#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
  if command -v python >/dev/null 2>&1; then
    PYTHON_BIN=python
  else
    echo "python3 (or python) is required for scripts/collect_system_info.sh" >&2
    exit 1
  fi
fi

"$PYTHON_BIN" "$SCRIPT_DIR/collect_system_info.py" "$@"
