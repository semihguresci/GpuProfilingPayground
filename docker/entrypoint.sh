#!/usr/bin/env bash
set -euo pipefail

cd /workspace/build

# if your code supports it, export shader dir explicitly
export VK_BENCH_SHADER_DIR="${VK_BENCH_SHADER_DIR:-/workspace/build/shaders}"

if [[ $# -eq 0 ]]; then
  exec ./vk-bench --headless --frames 300 --vsync 0 --out /results/results.json
fi

exec ./vk-bench "$@"