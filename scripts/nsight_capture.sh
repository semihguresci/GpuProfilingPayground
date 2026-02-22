#!/usr/bin/env bash
set -euo pipefail

IMAGE="${VK_BENCH_IMAGE:-vk-bench}"
OUT="${1:-results/nsight_capture}"
mkdir -p "$(dirname "$OUT")"

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required for scripts/nsight_capture.sh" >&2
  exit 1
fi

if ! command -v nsys >/dev/null 2>&1; then
  echo "nsys not found on host. Install NVIDIA Nsight Systems to use this script." >&2
  exit 1
fi

nsys profile \
  --trace=vulkan,nvtx,cuda \
  --output "$OUT" \
  docker run --rm --gpus all \
    -v "$(pwd)/results:/results" \
    "$IMAGE" \
    --headless --scene million-tris --warmup 20 --frames 120 --vsync 0 --out /results/nsight_capture.json

echo "Capture written to ${OUT}.qdrep (or .nsys-rep depending on version)."
