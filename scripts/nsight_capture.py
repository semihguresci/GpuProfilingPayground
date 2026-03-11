#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def run(cmd: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, text=True)
    if check and proc.returncode != 0:
        raise RuntimeError(f"Command failed ({proc.returncode}): {' '.join(cmd)}")
    return proc


def find_nsys_binary(override: str | None) -> str | None:
    if override:
        candidate = Path(override).expanduser().resolve()
        if candidate.exists():
            return str(candidate)
        return None

    from_path = shutil.which("nsys")
    if from_path:
        return from_path

    # Common Windows install locations when Nsight Systems is not on PATH.
    program_files = os.environ.get("ProgramFiles", r"C:\Program Files")
    nsight_root = Path(program_files) / "NVIDIA Corporation"
    patterns = (
        "Nsight Systems */target-windows-x64/nsys.exe",
        "Nsight Systems */host-windows-x64/nsys.exe",
        "Nsight Systems */nsys.exe",
    )
    candidates: list[Path] = []
    for pattern in patterns:
        candidates.extend(nsight_root.glob(pattern))
    if candidates:
        newest = sorted(candidates)[-1]
        return str(newest)

    return None


def find_local_binary(build_dir: Path, config: str, override: str | None) -> Path:
    if override:
        binary = Path(override).resolve()
        if binary.exists():
            return binary
        raise RuntimeError(f"Provided binary path does not exist: {binary}")

    names = ("vk-bench.exe", "vk-bench")
    candidates: list[Path] = []
    for name in names:
        candidates.append(build_dir / name)
        candidates.append(build_dir / config / name)

    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    raise RuntimeError("Could not find vk-bench binary. Build first or pass --binary-path.")


def main() -> int:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Capture local vk-bench run with Nsight Systems.")
    parser.add_argument("out", nargs="?", default=str(root / "results" / "nsight_capture"))
    parser.add_argument("--scene", choices=("triangle", "million-tris", "compute-copy"), default="million-tris")
    parser.add_argument("--warmup", type=int, default=20)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--build-dir", default=str(root / "out" / "build-local"))
    parser.add_argument("--config", default="Release")
    parser.add_argument("--binary-path", default=None)
    parser.add_argument("--nsys-path", default=None)
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()

    nsys_bin = find_nsys_binary(args.nsys_path)
    if not nsys_bin:
        raise RuntimeError(
            "nsys not found on host. Install NVIDIA Nsight Systems and either add nsys to PATH "
            "or pass --nsys-path \"C:\\Program Files\\NVIDIA Corporation\\Nsight Systems <version>\\target-windows-x64\\nsys.exe\"."
        )

    out_base = Path(args.out).resolve()
    out_base.parent.mkdir(parents=True, exist_ok=True)
    json_out = out_base.with_suffix(".json")

    build_dir = Path(args.build_dir).resolve()
    if not args.no_build:
        print(f"Configuring local build in {build_dir}")
        run(
            [
                "cmake",
                "-S",
                str(root),
                "-B",
                str(build_dir),
                "-DVK_BENCH_ENABLE_WINDOW=OFF",
                "-DVK_BENCH_FETCH_GLFW=OFF",
            ]
        )
        print(f"Building vk-bench ({args.config})")
        run(["cmake", "--build", str(build_dir), "--config", args.config])

    binary = find_local_binary(build_dir, args.config, args.binary_path)
    print(f"Using binary: {binary}")
    print(f"Capture output base: {out_base}")
    print(f"Benchmark JSON output: {json_out}")

    # Profile only the benchmark process to avoid Docker/runtime noise.
    run(
        [
            nsys_bin,
            "profile",
            "--trace=vulkan,nvtx,cuda",
            "--output",
            str(out_base),
            str(binary),
            "--headless",
            "--scene",
            args.scene,
            "--warmup",
            str(args.warmup),
            "--frames",
            str(args.frames),
            "--vsync",
            "0",
            "--out",
            str(json_out),
        ]
    )

    print(f"Capture written to {out_base}.qdrep (or .nsys-rep depending on Nsight version).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
