# Build

Planned build setup. Commands become final when task C1 lands (see [ROADMAP.md](ROADMAP.md#c1-skeleton-build-ci-vista-verification)). Items marked **verify on Vista** must be confirmed in C1, by gate G0 (Oct 30).

## Toolchain targets

| | Laptop (WSL2) | Vista GH200 |
|---|---|---|
| OS | Ubuntu 24.04 | Linux, aarch64 (**verify on Vista** distro) |
| Host compiler | GCC 13.3 (Ubuntu default) | `gcc/13.2.0` module |
| CUDA | 13.3 (nvcc 13.3.73), WSL-Ubuntu toolkit | `cuda` module, exact version TBD in C1 (`module spider cuda`) |
| GPU | RTX 4070 Laptop (Ada) | Hopper; record the exact name from `nvidia-smi` (docs list both H100 and H200) |
| CUDA arch | `sm_89` (`RT_CUDA_ARCH=89`) | `sm_90` (`RT_CUDA_ARCH=90`) |
| C++ standard | C++17 | C++17 |
| CMake | ≥ 3.24 (suggested) | `cmake` module (**verify on Vista**) |
| Generator | Ninja | Ninja or Make |

### Pinned toolchain pairs

Supported host compilers depend on the **CUDA minor release**, not just "12.x". For each machine, check the "Host Compiler Support Policy" table in the NVIDIA CUDA installation guide for that exact version, then pin a pair that actually builds and passes tests. GCC 13 is the target because Ubuntu 24.04 ships 13.3 and Vista has `gcc/13.2.0`. Vista's `gcc/15.1.0` is only usable if the loaded CUDA release lists it.

| Machine | GCC | CUDA | Driver | Tested on (date, commit) |
|---|---|---|---|---|
| Laptop (WSL2) | 13.3 | 13.3 (nvcc 13.3.73) | 610.62 | 2026-10-28, `207296c` (`core` headers and `tests/test_math.cpp` only; full CUDA build not run yet) |
| Vista GH node | 13.2.0 | TBD | TBD | TBD |

C1 fills this table. Changing a pair requires a PR that reruns the test suite on that machine.

## Laptop: WSL2 setup

1. Install the latest NVIDIA Windows driver. It provides the GPU driver inside WSL2.
2. In WSL2 Ubuntu 24.04:
   ```bash
   sudo apt update
   sudo apt install build-essential g++-13 cmake ninja-build git python3-venv
   ```
3. Install the **WSL-Ubuntu** CUDA toolkit from NVIDIA's CUDA downloads page (choose "Linux → x86_64 → WSL-Ubuntu"). Do **not** install a Linux NVIDIA driver inside WSL; it breaks the Windows-provided one. With NVIDIA's apt repo:
   ```bash
   wget https://developer.download.nvidia.com/compute/cuda/repos/wsl-ubuntu/x86_64/cuda-keyring_1.1-1_all.deb
   sudo dpkg -i cuda-keyring_1.1-1_all.deb && sudo apt-get update
   sudo apt-get install -y cuda-toolkit-13-3     # never `cuda` or `cuda-drivers`: they pull in a Linux driver
   echo 'export PATH=/usr/local/cuda-13.3/bin:$PATH' >> ~/.bashrc
   ```
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

**Check which OpenMP runtime the binary actually loads.** Vista's default module environment is built around the NVIDIA HPC SDK, whose library paths can end up in `LD_LIBRARY_PATH`. A GCC-built program could then resolve its OpenMP runtime to the wrong library. GCC's `-fopenmp` expects `libgomp`. After every build, and in every batch job:

```bash
ldd build-vista/apps/rt_render/rt_render | grep -Ei 'gomp|omp|nvhpc'
# expected: libgomp.so.1 => .../gcc/13.2.0/.../libgomp.so.1
# wrong:    any libomp / libnvomp / path under an nvidia or nvhpc module
```

If it resolves wrongly, `module unload nvidia` (or `module reset` followed by loading only `gcc` and `cuda`), rebuild, and check again. **Verify on Vista** which module names are involved; TACC's user guide recommends `ldd` for checking library resolution.

Record machine identity on the allocated node every time (the sbatch template does this automatically):

```bash
nvidia-smi --query-gpu=name,uuid,driver_version,memory.total --format=csv
lscpu | head -20
gcc --version; nvcc --version; module list
```

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
| `RT_CUDA_ARCH` | `89` | `89` laptop, `90` Vista Hopper, `80` A100 |
| `RT_ENABLE_EMBREE` | `OFF` | Builds the `embree` backend (Extension E4). Needs Embree 4 installed or fetched |
| `RT_ENABLE_OPTIX` | `OFF` | Builds the `optix` backend (Stretch). Needs `OptiX_INSTALL_DIR` |
| `RT_BUILD_TESTS` | `ON` | GoogleTest unit and regression tests |
| *Deferred:* `RT_COUNTERS` | `OFF` | Counts BVH nodes visited per ray, for explaining traversal cost. Add only if needed; never used for timing |

A CPU-only build must always work. CI uses it.

### Golden images

Goldens are compared with the normal `rt_compare` tolerances, not exact equality, so they work on any x86 machine with GCC 13. Regenerate them deliberately in a PR that says why, never to make a failing test pass.

### Golden-image environment (deferred)

Exact, bit-identical goldens would need a pinned CI container (image digest, exact `g++-13` package, `-O2 -march=x86-64 -ffp-contract=off`, 1 and 4 threads). That's deferred. Revisit only if tolerance-based goldens miss a real regression.
### Compiler flags

| Flags | Where |
|---|---|
| `-O3 -fopenmp` | All Release builds |
| `-march=native` | x86 laptop |
| `-mcpu=native` (or `-mcpu=neoverse-v2`) | Grace (**verify on Vista** which GCC 13.2 accepts) |
| No `-ffast-math`, no `--use_fast_math` | Everywhere (the NaN-safe slab test depends on IEEE comparisons; see [INTERFACES.md](INTERFACES.md#ray-box-slab-test-frozen)) |
| `-ffp-contract=off` | Generator and scene-hash sources only. Reduces variation, but doesn't make x86 and ARM bytes identical (libm differs), so benchmark scenes are generated once and distributed ([INTERFACES.md](INTERFACES.md#generator-determinism-and-scene-distribution)) |
| `-Xcompiler -fopenmp`, `-lineinfo`, `-Xptxas -v` | nvcc (lineinfo for Nsight; `-v` reports registers and local memory) |

Benchmark binaries on Vista must be built on Vista. Don't copy laptop binaries.

## Dependencies

Fetched with CMake `FetchContent`, pinned to **exact tags** (filled in during C1):

| Library | Use | Pin |
|---|---|---|
| nlohmann/json | JSON scenes and results | `vX.Y.Z` |
| stb (stb_image_write) | PNG output | commit hash |
| CLI11 | Command line | `vX.Y.Z` |
| GoogleTest | Tests | `vX.Y.Z` |
| tinyobjloader | `obj2bin` (Extension E6) | `vX.Y.Z` |
| Embree 4 | Embree backend (Extension E4) | `v4.x` (**verify on Vista** ARM64 build or module) |
| OptiX SDK | OptiX backend (Stretch) | `8.x` or newer, manual download |

## OptiX on WSL2 (Stretch)

NVIDIA's recent Windows drivers support OptiX inside WSL2. Verify this early on the 4070 by building and running the `optixSphere` SDK sample in WSL2. If it fails, build only the `optix` backend natively on Windows (MSVC + CUDA). The rest of the project stays WSL2-only. On Vista, OptiX runs on the Hopper GPU without RT-core hardware. See [STRETCH.md](STRETCH.md#optix-backend).

## Profiling tools

- Nsight Systems (`nsys`) and Nsight Compute (`ncu`): ship with the CUDA toolkit. **Verify on Vista** that they're in the `cuda` module or a separate `nvidia` module.
- CPU: `perf` (if allowed), GCC `-fopt-info-vec` for vectorization reports.
