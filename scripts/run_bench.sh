#!/usr/bin/env bash
set -euo pipefail

IMAGE="${VK_BENCH_IMAGE:-vk-bench}"
<<<<<<< ours
<<<<<<< ours

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
=======
=======
>>>>>>> theirs
RESULT_DIR="${1:-results}"
mkdir -p "$RESULT_DIR"

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required for scripts/run_bench.sh" >&2
  exit 1
fi

for scene in triangle million-tris compute-copy; do
  echo "Running scene in Docker: $scene"
  docker run --rm --gpus all \
    -v "$(pwd)/$RESULT_DIR:/results" \
    "$IMAGE" \
    --headless --scene "$scene" --warmup 30 --frames 300 --vsync 0 --out "/results/${scene}.json"
done

echo "Saved benchmark outputs to $RESULT_DIR"
<<<<<<< ours
>>>>>>> theirs
=======
>>>>>>> theirs
