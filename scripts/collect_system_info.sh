#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-results/system_info.txt}"
mkdir -p "$(dirname "$OUT")"

{
  echo "== Date =="
  date -Iseconds
  echo
  echo "== uname =="
  uname -a
  echo
  echo "== nvidia-smi =="
  nvidia-smi || true
  echo
  echo "== vulkaninfo --summary =="
  vulkaninfo --summary || true
} > "$OUT"

echo "Wrote $OUT"
