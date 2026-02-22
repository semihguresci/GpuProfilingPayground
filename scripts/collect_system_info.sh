#!/usr/bin/env bash
set -euo pipefail

IMAGE="${VK_BENCH_IMAGE:-vk-bench}"
OUT="${1:-results/system_info.txt}"
mkdir -p "$(dirname "$OUT")"

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required for scripts/collect_system_info.sh" >&2
  exit 1
fi

{
  echo "== Date =="
  date -Iseconds
  echo
  echo "== uname =="
  uname -a
  echo
  echo "== nvidia-smi (host) =="
  nvidia-smi || true
  echo
  echo "== vulkaninfo --summary (container) =="
  docker run --rm --gpus all "$IMAGE" vulkaninfo --summary || true
} > "$OUT"

echo "Wrote $OUT"
