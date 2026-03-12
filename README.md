# vk-bench

`vk-bench` is a focused Vulkan benchmark app that runs one controlled workload per frame and emits JSON timing summaries suitable for regression tracking.

## Scope

This repository contains one executable (`vk-bench`) with three scenes:

- `triangle`: graphics path drawing one triangle (or `--triangles N` instances)
- `million-tris`: raster stress path drawing 1,000,000 instances
- `compute-copy`: compute-only path using a storage-buffer dispatch

No engine systems, textures, or model assets are included.

## Quick start (Docker)

```bash
docker build -f Dockerfile -t vk-bench .
docker run --rm --gpus all vk-bench --headless --frames 300 --out /tmp/results.json
```

Scripts default to image `vk-bench`. Set `VK_BENCH_IMAGE=<name>` to override.
Cross-platform script entrypoints are Python-based (`scripts/*.py`) with `.sh` and `.ps1` wrappers.

## Build locally (CMake)

```bash
cmake -S . -B out/build -DVK_BENCH_ENABLE_WINDOW=ON -DVK_BENCH_FETCH_GLFW=ON
cmake --build out/build --config Release
```

- `VK_BENCH_ENABLE_WINDOW=ON|OFF`: enable or disable GLFW-backed window mode.
- `VK_BENCH_FETCH_GLFW=ON|OFF`: fetch GLFW if no system package is found.

## Run scenarios

Run a single benchmark:

```bash
docker run --rm --gpus all vk-bench \
  --headless --scene million-tris --warmup 30 --frames 300 --out /tmp/million-tris.json
```

Run a local windowed rendering test:

```bash
cmake -S . -B out/build-window -DVK_BENCH_ENABLE_WINDOW=ON -DVK_BENCH_FETCH_GLFW=ON
cmake --build out/build-window --config Release
./out/build-window/Release/vk-bench --scene triangle --frames 300 --vsync 1 --out results/windowed-triangle.json
```

Windowed mode is supported for `triangle` and `million-tris`. Do not pass `--headless` for this path.

Run a compute-only benchmark:

```bash
docker run --rm --gpus all vk-bench \
  --headless --scene compute-copy --warmup 30 --frames 300 --out /tmp/compute-copy.json
```

`compute-copy` is a headless-only path. It does not create a window or present a swapchain.

Run scripted benchmark(s):

```bash
scripts/run_bench.sh results
```

Docker mode from Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_bench.ps1 results
```

Run scripted benchmark(s) in local mode:

```bash
python3 scripts/run_bench.py --mode local results
```

Run local mode from Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_bench_local.ps1 -ResultDir results
```

The local runner uses `--headless` and the same default scenes (`triangle`, `million-tris`, `compute-copy`). It configures/builds `out/build-local` by default, then writes JSON outputs to `results/`.

Note: the current script default scene list is defined in `scripts/run_bench.py`.
The script refuses to run unless `vulkaninfo --summary` inside the container reports an NVIDIA Vulkan device, which prevents accidental fallback to software Vulkan such as `llvmpipe`.

## Output format

Each run writes JSON with metadata + summary stats:

- `scene`, `headless`, `frames`, `warmup`, `vsync`
- `resolution`, `device_name`, `driver_version`
- `cpu_frame_time_ms` (`avg`, `p50`, `p95`)
- `gpu_frame_time_ms` (`avg`, `p50`, `p95`)

Headless graphics runs also write a screenshot bitmap next to the JSON output using the same basename, for example `results.json` and `results.bmp`. Compute-only runs do not emit an image.

Example:

```json
{
  "scene": "million-tris",
  "cpu_frame_time_ms": {"avg": 0.0731, "p50": 0.0613, "p95": 0.1188},
  "gpu_frame_time_ms": {"avg": 2.4182, "p50": 2.3395, "p95": 2.7560}
}
```

## Timing model

- GPU timing uses two Vulkan timestamps around the recorded workload.
- CPU timing measures submit-to-complete per frame.
- Warmup frames are not recorded in final statistics.

## Nsight capture

```bash
scripts/nsight_capture.sh results/nsight_capture
```

Capture a specific scene:

```bash
scripts/nsight_capture.sh results/nsight_triangle --scene triangle
```

Windows PowerShell wrapper:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/nsight_capture.ps1 results/nsight_capture
```

If `nsys` is not in `PATH` on Windows, pass the full executable path:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/nsight_capture.ps1 results/nsight_capture --nsys-path "C:\Program Files\NVIDIA Corporation\Nsight Systems 2025.4.1\target-windows-x64\nsys.exe"
```

Direct local equivalent:

```bash
nsys profile --trace=vulkan,nvtx,cuda --output results/nsight_capture \
  ./out/build-local/Release/vk-bench \
  --headless --scene million-tris --warmup 20 --frames 120 --vsync 0 --out results/nsight_capture.json
```

## Troubleshooting GPU access

Recommended (Linux + NVIDIA):

```bash
docker run --rm --gpus all vk-bench vulkaninfo --summary
```

`scripts/run_bench.sh` performs this check automatically and exits early if the container does not expose an NVIDIA Vulkan device.

Collect host/container system info:

```bash
scripts/collect_system_info.sh results/system_info.txt
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts/collect_system_info.ps1 results/system_info.txt
```

Fallback ICD override (headless):

```bash
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json
vk-bench --headless --frames 300 --out results.json
```

## Repository layout

- `src/`: Vulkan benchmark implementation
- `shaders/`: Slang shader sources compiled to SPIR-V at build time
- `scripts/`: benchmark and profiling helper scripts

## Limitations

- Window mode requires build-time GLFW support (`VK_BENCH_HAS_WINDOW=1`).
- CI can validate build/lint flow, but meaningful performance validation needs GPU-backed runners.
