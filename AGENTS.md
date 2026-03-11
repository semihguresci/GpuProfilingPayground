# AGENTS.md

## Purpose

Guidance for coding agents working in this repository.

## Project summary

- Project: `vk-bench` Vulkan micro-benchmark.
- Language: C++ (single primary source file: `src/main.cpp`).
- Build: CMake.
- Runtime: Linux Docker workflow with NVIDIA GPU (`--gpus all`) is the default path.

## What to optimize for

- Keep benchmark behavior deterministic and easy to compare run-to-run.
- Prefer small, reviewable changes over broad refactors.
- Preserve JSON output compatibility unless a change is explicitly requested.

## Editing rules

- Avoid changing benchmark semantics (scene workloads, frame loops, timing region boundaries) unless requested.
- Add comments only where logic is non-obvious (timing, synchronization, resource lifetime).
- Keep code style consistent with existing file formatting and naming.
- Do not add dependencies unless they are necessary and justified.

## Verification checklist

When changing code, run what is feasible in the environment:

1. Configure/build with CMake.
2. Run a short headless benchmark (`--frames` small) and verify JSON output is written.
3. Confirm no obvious regressions in CLI arguments or scene dispatch.

If GPU execution is not available, state what could not be validated.

## Key files

- `src/main.cpp`: Vulkan setup, scene execution, timing collection, JSON output.
- `CMakeLists.txt`: targets, shader compilation/install, window mode options.
- `scripts/run_bench.sh`: scripted benchmark runs in Docker.
- `scripts/nsight_capture.sh`: Nsight Systems capture helper.
- `scripts/collect_system_info.sh`: host/container environment capture.
- `README.md`: user-facing usage and run instructions.

## Common pitfalls

- Do not assume window mode is available; respect `VK_BENCH_HAS_WINDOW`.
- Keep shader lookup behavior intact (`VK_BENCH_SHADER_DIR`, executable-local shaders, fallback paths).
- Ensure Vulkan objects are destroyed in reverse-lifetime order on every exit path.
