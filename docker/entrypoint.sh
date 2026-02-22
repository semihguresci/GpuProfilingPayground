#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "" ]]; then
  exec /usr/local/bin/vk-bench --headless --frames 300 --out /workspace/results.json
fi

exec /usr/local/bin/vk-bench "$@"
