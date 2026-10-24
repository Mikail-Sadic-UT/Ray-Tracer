# Build

Planned build setup. Commands become final when Task 1 lands (see [ROADMAP.md](ROADMAP.md)). Items marked **verify on Vista** need confirmation at check-in 1.

## Toolchain targets

| | Laptop (WSL2) | Vista GH200 |
|---|---|---|
| OS | Ubuntu 24.04 | Linux, aarch64 (**verify on Vista** distro) |
| Host compiler | GCC 13.3 (Ubuntu default) | `gcc/13.2.0` module |
| CUDA | 12.x toolkit for WSL | `cuda` module, 12.x (**verify on Vista**: `module spider cuda`) |
| CUDA arch | `sm_89` (`RT_CUDA_ARCH=89`) | `sm_90` (`RT_CUDA_ARCH=90`) |
| C++ standard | C++17 | C++17 |
| CMake | ≥ 3.24 (suggested) | `cmake` module (**verify on Vista**) |
| Generator | Ninja | Ninja or Make |

Why GCC 13 and not 15: CUDA 12.x supports host GCC versions up to 14. Vista also has `gcc/15.1.0`, but avoid it with nvcc unless the loaded CUDA version supports it. The 13.2 vs 13.3 difference does not matter for C++17.

## Laptop: WSL2 setup

1. Install the latest NVIDIA Windows driver. It provides the GPU driver inside WSL2.
2. In WSL2 Ubuntu 24.04:
   ```bash
   sudo apt update
   sudo apt install build-essential g++-13 cmake ninja-build git python3-venv
   ```
3. Install the **WSL-Ubuntu** CUDA toolkit from NVIDIA's CUDA downloads page (choose "Linux → x86_64 → WSL-Ubuntu"). Do **not** install a Linux NVIDIA driver inside WSL; it breaks the Windows-provided one.
4. Check:
   ```bash
   nvidia-smi
   nvcc --version
   ```
5. Clone the repo inside the WSL filesystem (e.g. `~/src/`), not under `/mnt/c/`. Builds on `/mnt/c` are much slower.

Viewing images: open `\\wsl$\Ubuntu-24.04\home\<user>\...` in Windows Explorer, or use the VS Code WSL extension. No GUI is needed.

Measure the PCIe link once and record it for [BENCHMARKING.md](BENCHMARKING.md): build `bandwidthTest` from NVIDIA's `cuda-samples` repo. The 4070 Laptop is likely PCIe 4.0 ×8.

## Vista setup

```bash
# login node
module reset
module load gcc/13.2.0 cuda cmake     # verify on Vista: exact versions via `module spider`
module list
```

NVIDIA HPC SDK (`nvc`, `nvc++`) is Vista's default compiler suite. Always load `gcc` explicitly before CMake so CMake and nvcc use GCC.

Interactive GPU session for development:

```bash
idev -p gh-dev -N 1 -n 1 -t 1:00:00
```

Batch jobs use `sbatch` on `gh-dev` (testing) or `gh` (production). See the template in [REPO_SKELETON.md](REPO_SKELETON.md#sbatch-template). Build on a compute node, or on a login node if the login architecture matches (**verify on Vista**). Keep build trees and large scenes in `$WORK` or `$SCRATCH`, not `$HOME`.

Fallback if Vista GPU access fails: Lonestar6 A100 nodes, `RT_CUDA_ARCH=80`, x86 host.

## Configure and build

```bash
# laptop, full build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DRT_ENABLE_CUDA=ON -DRT_CUDA_ARCH=89
cmake --build build -j

# Vista
cmake -S . -B build-vista -DCMAKE_BUILD_TYPE=Release \
      -DRT_ENABLE_CUDA=ON -DRT_CUDA_ARCH=90
cmake --build build-vista -j

# CPU-only (CI, machines without NVIDIA GPU)
cmake -S . -B build-cpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DRT_ENABLE_CUDA=OFF

ctest --test-dir build --output-on-failure
```

### CMake options

| Option | Default | Effect |
|---|---|---|
| `RT_ENABLE_CUDA` | `ON` if a CUDA compiler is found | Builds `src/gpu`, the `cuda` backend |
| `RT_CUDA_ARCH` | `89` | `89` laptop, `90` H100, `80` A100 |
| `RT_ENABLE_EMBREE` | `OFF` | Builds the `embree` backend (Extended). Needs Embree 4 installed or fetched |
| `RT_ENABLE_OPTIX` | `OFF` | Builds the `optix` backend (Stretch). Needs `OptiX_INSTALL_DIR` |
| `RT_BUILD_TESTS` | `ON` | GoogleTest unit and regression tests |

A CPU-only build must always work. CI uses it.

### Compiler flags

| Flags | Where |
|---|---|
| `-O3 -fopenmp` | All Release builds |
| `-march=native` | x86 laptop |
| `-mcpu=native` (or `-mcpu=neoverse-v2`) | Grace (**verify on Vista** which GCC 13.2 accepts) |
| No `-ffast-math`, no `--use_fast_math` | Everywhere (see [INTERFACES.md](INTERFACES.md#precision-and-tolerance-rules)) |
| `-Xcompiler -fopenmp`, `-lineinfo` | nvcc (lineinfo for Nsight) |

Benchmark binaries on Vista must be built on Vista. Don't copy laptop binaries.

## Dependencies

Fetched with CMake `FetchContent`, pinned to **exact tags** (filled in during Task 1):

| Library | Use | Pin |
|---|---|---|
| nlohmann/json | JSON scenes and results | `vX.Y.Z` |
| stb (stb_image_write) | PNG output | commit hash |
| CLI11 | Command line | `vX.Y.Z` |
| GoogleTest | Tests | `vX.Y.Z` |
| tinyobjloader | `obj2bin` (Extended) | `vX.Y.Z` |
| Embree 4 | Embree backend (Extended) | `v4.x` (**verify on Vista** ARM64 build or module) |
| OptiX SDK | OptiX backend (Stretch) | `8.x` or newer, manual download |

## OptiX on WSL2 (Stretch)

NVIDIA's recent Windows drivers support OptiX inside WSL2. Verify this early on the 4070 by building and running the `optixSphere` SDK sample in WSL2. If it fails, build only the `optix` backend natively on Windows (MSVC + CUDA). The rest of the project stays WSL2-only. On Vista, OptiX runs on the H100 without RT-core hardware. See [STRETCH.md](STRETCH.md#optix-backend).

## Profiling tools

- Nsight Systems (`nsys`) and Nsight Compute (`ncu`): ship with the CUDA toolkit. **Verify on Vista** that they're in the `cuda` module or a separate `nvidia` module.
- CPU: `perf` (if allowed), GCC `-fopt-info-vec` for vectorization reports.
