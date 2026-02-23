#!/usr/bin/env bash
set -euo pipefail

IMAGE="${VK_BENCH_IMAGE:-vk-bench}"

# Default results directory is repo-root/results (not scripts/results)
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

RESULT_DIR="${1:-$REPO_ROOT/results}"

mkdir -p "$RESULT_DIR"

echo "Using image: $IMAGE"
echo "Saving results to: $RESULT_DIR"
docker --version

SCENES=(triangle million-tris compute-copy)

for scene in "${SCENES[@]}"; do
  echo "Running scene in Docker: $scene"

  docker run --rm --gpus all \
    -v "$RESULT_DIR:/results" \
    "$IMAGE" \
    --headless \
    --scene "$scene" \
    --warmup 30 \
    --frames 300 \
    --vsync 0 \
    --out "/results/${scene}.json"
done

echo "Saved benchmark outputs to $RESULT_DIR"