#!/usr/bin/env bash
set -euo pipefail

RESULT_DIR="${1:-results}"
mkdir -p "$RESULT_DIR"

for scene in triangle million-tris compute-copy; do
  echo "Running scene: $scene"
  vk-bench --headless --scene "$scene" --warmup 30 --frames 300 --vsync 0 --out "$RESULT_DIR/${scene}.json"
done

echo "Saved benchmark outputs to $RESULT_DIR"
