#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def run_capture(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return (proc.stdout or "").strip()


def command_exists(name: str) -> bool:
    return shutil.which(name) is not None


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect host/container environment info for vk-bench.")
    parser.add_argument("out", nargs="?", default=str(Path("results") / "system_info.txt"))
    parser.add_argument("--image", default=os.environ.get("VK_BENCH_IMAGE", "vk-bench"))
    args = parser.parse_args()

    if not command_exists("docker"):
        raise RuntimeError("docker is required for scripts/collect_system_info.py")

    out_path = Path(args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    lines: list[str] = []
    lines.append("== Date ==")
    lines.append(dt.datetime.now(dt.timezone.utc).astimezone().isoformat())
    lines.append("")

    lines.append("== Host platform ==")
    lines.append(platform.platform())
    lines.append(f"system={platform.system()} release={platform.release()} version={platform.version()}")
    lines.append("")

    lines.append("== nvidia-smi (host) ==")
    if command_exists("nvidia-smi"):
        lines.append(run_capture(["nvidia-smi"]) or "<no output>")
    else:
        lines.append("nvidia-smi not found")
    lines.append("")

    lines.append("== vulkaninfo --summary (container) ==")
    lines.append(
        run_capture(["docker", "run", "--rm", "--gpus", "all", args.image, "vulkaninfo", "--summary"])
        or "<no output>"
    )
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {out_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
