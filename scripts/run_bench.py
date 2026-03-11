#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


EXPECTED_SCENES = ("triangle", "million-tris", "compute-copy")


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def run(cmd: list[str], *, check: bool = True, capture: bool = False) -> subprocess.CompletedProcess[str]:
    kwargs: dict[str, object] = {"text": True}
    if capture:
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.STDOUT
    proc = subprocess.run(cmd, **kwargs)
    if check and proc.returncode != 0:
        joined = " ".join(cmd)
        raise RuntimeError(f"Command failed ({proc.returncode}): {joined}")
    return proc


def parse_scenes(value: str | None) -> list[str]:
    if not value:
        return list(EXPECTED_SCENES)
    parts = [p for p in value.replace(",", " ").split() if p]
    invalid = [p for p in parts if p not in EXPECTED_SCENES]
    if invalid:
        raise ValueError(
            f"Unsupported scene(s): {', '.join(invalid)}. Expected one of: {', '.join(EXPECTED_SCENES)}"
        )
    return parts


def command_exists(name: str) -> bool:
    return shutil.which(name) is not None


def ensure_docker_image(image: str, root: Path) -> None:
    inspect = run(["docker", "image", "inspect", image], check=False)
    if inspect.returncode != 0:
        print(f"Docker image '{image}' not found; building from {root / 'Dockerfile'}")
        run(["docker", "build", "-f", str(root / "Dockerfile"), "-t", image, str(root)])


def rebuild_if_stale(image: str, root: Path) -> None:
    help_out = run(["docker", "run", "--rm", image, "--help"], check=False, capture=True).stdout or ""
    if not all(scene in help_out for scene in EXPECTED_SCENES):
        print(f"Docker image '{image}' is stale or incompatible; rebuilding from {root / 'Dockerfile'}")
        run(["docker", "build", "-f", str(root / "Dockerfile"), "-t", image, str(root)])


def ensure_nvidia_vulkan(image: str) -> None:
    summary = run(
        ["docker", "run", "--rm", "--gpus", "all", "--entrypoint", "vulkaninfo", image, "--summary"],
        check=False,
        capture=True,
    ).stdout or ""
    if "deviceName" not in summary or "NVIDIA" not in summary:
        raise RuntimeError(
            "No NVIDIA Vulkan device detected inside Docker container.\n"
            "Refusing to run benchmarks on software or non-NVIDIA Vulkan.\n"
            f"vulkaninfo summary:\n{summary}"
        )


def find_local_binary(build_dir: Path, config: str, override: str | None) -> Path:
    if override:
        candidate = Path(override).resolve()
        if candidate.exists():
            return candidate
        raise RuntimeError(f"Provided binary path does not exist: {candidate}")

    names = ("vk-bench.exe", "vk-bench")
    candidates: list[Path] = []
    for name in names:
        candidates.append(build_dir / name)
        candidates.append(build_dir / config / name)

    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    raise RuntimeError("Could not find vk-bench binary. Build first or pass --binary-path.")


def run_docker_mode(args: argparse.Namespace) -> None:
    if not command_exists("docker"):
        raise RuntimeError("docker is required for docker mode.")

    root = repo_root()
    result_dir = Path(args.result_dir).resolve()
    result_dir.mkdir(parents=True, exist_ok=True)

    print(f"Using image: {args.image}")
    print(f"Saving results to: {result_dir}")
    run(["docker", "--version"])

    ensure_docker_image(args.image, root)
    rebuild_if_stale(args.image, root)
    ensure_nvidia_vulkan(args.image)

    mount_src = result_dir.as_posix()
    for scene in args.scenes:
        print(f"Running scene in Docker: {scene}")
        run(
            [
                "docker",
                "run",
                "--rm",
                "--gpus",
                "all",
                "-v",
                f"{mount_src}:/results",
                args.image,
                "--headless",
                "--scene",
                scene,
                "--warmup",
                str(args.warmup),
                "--frames",
                str(args.frames),
                "--vsync",
                "0",
                "--out",
                f"/results/{scene}.json",
            ]
        )

    print(f"Saved benchmark outputs to {result_dir}")


def run_local_mode(args: argparse.Namespace) -> None:
    root = repo_root()
    result_dir = Path(args.result_dir).resolve()
    build_dir = Path(args.build_dir).resolve()

    result_dir.mkdir(parents=True, exist_ok=True)

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
    print(f"Saving results to: {result_dir}")

    for scene in args.scenes:
        out_json = result_dir / f"{scene}.json"
        print(f"Running local scene: {scene}")
        run(
            [
                str(binary),
                "--headless",
                "--scene",
                scene,
                "--warmup",
                str(args.warmup),
                "--frames",
                str(args.frames),
                "--vsync",
                "0",
                "--out",
                str(out_json),
            ]
        )

    print(f"Saved benchmark outputs to {result_dir}")


def main() -> int:
    root = repo_root()
    default_result_dir = root / "results"
    default_build_dir = root / "out" / "build-local"

    parser = argparse.ArgumentParser(description="Run vk-bench scenarios in docker or local mode.")
    parser.add_argument("--mode", choices=("docker", "local"), default="local")
    parser.add_argument(
        "result_dir",
        nargs="?",
        default=str(default_result_dir),
        help="Result directory path (default: repo-root/results).",
    )
    parser.add_argument("--image", default=os.environ.get("VK_BENCH_IMAGE", "vk-bench"))
    parser.add_argument("--warmup", type=int, default=int(os.environ.get("VK_BENCH_WARMUP", "30")))
    parser.add_argument("--frames", type=int, default=int(os.environ.get("VK_BENCH_FRAMES", "300")))
    parser.add_argument("--scenes", default=os.environ.get("VK_BENCH_SCENES", " ".join(EXPECTED_SCENES)))

    parser.add_argument("--build-dir", default=str(default_build_dir))
    parser.add_argument("--config", default="Release")
    parser.add_argument("--binary-path", default=None)
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()

    args.scenes = parse_scenes(args.scenes)

    if args.mode == "docker":
        run_docker_mode(args)
    else:
        run_local_mode(args)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
