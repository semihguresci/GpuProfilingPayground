#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-results/nsight_capture}"
mkdir -p "$(dirname "$OUT")"

if ! command -v nsys >/dev/null 2>&1; then
  echo "nsys not found. Install NVIDIA Nsight Systems to use this script." >&2
  exit 1
fi

nsys profile \
  --trace=vulkan,nvtx,cuda \
  --output "$OUT" \
  vk-bench --headless --scene million-tris --warmup 20 --frames 120 --vsync 0 --out "${OUT}.json"

echo "Capture written to ${OUT}.qdrep (or .nsys-rep depending on version)."
