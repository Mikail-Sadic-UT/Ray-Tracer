# Build

Build setup from task C1 (see [ROADMAP.md](ROADMAP.md#c1-skeleton-build-ci-vista-verification)). The laptop build is verified. The Vista details below come from a login node (Oct 28); the compute-node build and the driver version are still to confirm. Items marked **verify on Vista** must be confirmed in C1, by gate G0 (Oct 30).

## Toolchain targets

| | Laptop (WSL2) | Vista GH200 |
|---|---|---|
| OS | Ubuntu 24.04 | Rocky Linux 9.7, aarch64 (Grace) |
| Host compiler | GCC 13.3 (Ubuntu default) | `gcc/13.2.0` module |
| CUDA | 13.3 (nvcc 13.3.73), WSL-Ubuntu toolkit | `cuda/13.3` module. **Load it by version**: plain `cuda` gives 12.5 |
| GPU | RTX 4070 Laptop (Ada) | Hopper; record the exact name from `nvidia-smi` (docs list both H100 and H200) |
| CUDA arch | `sm_89` (`RT_CUDA_ARCH=89`) | `sm_90` (`RT_CUDA_ARCH=90`) |
| C++ standard | C++17 | C++17 |
| CMake | ≥ 3.28 (Ubuntu 24.04 apt ships 3.28) | `cmake` module: 3.29.5, 3.31.5, or 4.1.1 (default) |
| Generator | Ninja | Unix Makefiles (no `ninja` module on Vista) |

### Pinned toolchain pairs

Both machines use **CUDA 13.3**, so the laptop and Vista run the same nvcc minor release. The configure step prints a warning if it finds a different CUDA release (`RT_CUDA_PINNED`, default `13.3`). Supported host compilers depend on the CUDA minor release: CUDA 13.3 supports GCC 6 through 16, so GCC 13 is fine on both. GCC 13 is the target because Ubuntu 24.04 ships 13.3 and Vista has `gcc/13.2.0`.

| Machine | GCC | CUDA | Driver | Tested on (date, commit) |
|---|---|---|---|---|
| Laptop (WSL2) | 13.3 | 13.3 (nvcc 13.3.73) | 610.62 | 2026-10-28, C1 skeleton: CMake 3.28 CPU-only and CUDA `sm_89` builds, `ctest` 4/4, `hello_cuda` on the RTX 4070 |
| Vista GH node | 13.2.0 | 13.3 (`cuda/13.3` module) | TBD (check `nvidia-smi` on a GH node) | TBD |

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
# login or compute node
module reset
module load gcc/13.2.0 cuda/13.3 cmake
module list                           # expect gcc/13.2.0, cuda/13.3, cmake/4.1.1
```

- **`module reset` loads the NVIDIA HPC SDK (`nvidia/24.7`)** as the default compiler suite. Loading `gcc/13.2.0` replaces it (Lmod prints "replacing nvidia/24.7 with gcc/13.2.0"). Always load `gcc` explicitly before CMake so CMake and nvcc use GCC.
- **Load CUDA by version.** Plain `module load cuda` gives `cuda/12.5`, which doesn't match the laptop. Available: 11.8, 12.4–12.9, 13.0, 13.1, 13.3.
- **CMake** defaults to 4.1.1. `cmake/3.29.5` and `cmake/3.31.5` also work with the project's 3.28 minimum.
- **No Ninja:** use the default Unix Makefiles generator, i.e. leave out `-G Ninja`.
- **Storage:** clone and build in `$WORK` (1 TB quota). `$SCRATCH` is purged, and `$HOME` is small (23 GB).
- **Hostnames** end in `.vista.tacc.utexas.edu` (for example `login1.vista.tacc.utexas.edu`).

**Check which OpenMP runtime the binary actually loads.** Vista's default module environment is built around the NVIDIA HPC SDK, whose library paths can end up in `LD_LIBRARY_PATH`. A GCC-built program could then resolve its OpenMP runtime to the wrong library. GCC's `-fopenmp` expects `libgomp`. After every build, and in every batch job:

```bash
ldd build-vista/apps/rt_render/rt_render | grep -Ei 'gomp|omp|nvhpc'
# expected: libgomp.so.1 => .../gcc/13.2.0/.../libgomp.so.1
# wrong:    any libomp / libnvomp / path under an nvidia or nvhpc module
```

If it resolves wrongly, `module unload nvidia` (or `module reset` followed by loading only `gcc` and `cuda/13.3`), rebuild, and check again. The NVIDIA module is `nvidia/<version>` (24.5 through 26.9 are installed). Even with `gcc/13.2.0` loaded, `LD_LIBRARY_PATH` still contains NVIDIA paths from `nvpl` (math libraries) and the CUDA module, so the `ldd` check matters. TACC's user guide recommends `ldd` for checking library resolution.

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

Batch jobs use `sbatch` on `gh-dev` (testing, 20 nodes) or `gh` (production, 576 nodes). See the template in [REPO_SKELETON.md](REPO_SKELETON.md#sbatch-template). Allocation: `TRA25006` (check the balance with `/usr/local/etc/taccinfo`). The login node is also aarch64 and accepts `-mcpu=native`, but build on a compute node: `-march`/`-mcpu=native` targets the CPU the build runs on, and only compute nodes have the GPU for `hello_cuda` and tests. Keep build trees and large scenes in `$WORK` or `$SCRATCH`, not `$HOME`.

Fallback if Vista GPU access fails: Lonestar6 A100 nodes, `RT_CUDA_ARCH=80`, x86 host.

## Configure and build

```bash
# laptop, full build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DRT_ENABLE_CUDA=ON -DRT_CUDA_ARCH=89
cmake --build build -j

# Vista (after `module load gcc/13.2.0 cuda/13.3 cmake`; Makefiles, since there's no ninja)
cmake -S . -B build-vista -DCMAKE_BUILD_TYPE=Release \
      -DRT_ENABLE_CUDA=ON -DRT_CUDA_ARCH=90 -DCMAKE_CXX_COMPILER=g++
cmake --build build-vista -j

# CPU-only (CI, machines without NVIDIA GPU)
cmake -S . -B build-cpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DRT_ENABLE_CUDA=OFF

ctest --test-dir build --output-on-failure
./build/apps/hello_cuda/hello_cuda      # prints GPU name and SM count, runs one kernel (CUDA builds)
```

- **nvcc must be findable.** If the configure step prints `No CUDA compiler found, building CPU-only`, put `/usr/local/cuda-13.3/bin` on `PATH`, or pass `-DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.3/bin/nvcc`.
- **Host compiler:** nvcc uses the same compiler as the C++ code (`CMAKE_CUDA_HOST_COMPILER` defaults to `CMAKE_CXX_COMPILER`), so pass `-DCMAKE_CXX_COMPILER=g++-13` when the default `g++` isn't GCC 13.
- **No fast-math:** configure fails if `CMAKE_CXX_FLAGS` or `CMAKE_CUDA_FLAGS` contains fast-math or `-Ofast`.
- **Flags:** every target that links `rt_core` gets `-Wall -Wextra -march=native` (`-mcpu=native` on aarch64) and, for CUDA, `-lineinfo`.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `RT_ENABLE_CUDA` | `ON` if a CUDA compiler is found | Builds `src/gpu`, the `cuda` backend |
| `RT_CUDA_ARCH` | `89` | `89` laptop, `90` Vista Hopper, `80` A100 |
| `RT_ENABLE_EMBREE` | `OFF` | Builds the `embree` backend (Extension E4). Needs Embree 4 installed or fetched. Not implemented yet: `ON` stops the configure step |
| `RT_ENABLE_OPTIX` | `OFF` | Builds the `optix` backend (Stretch). Needs `OptiX_INSTALL_DIR`. Not implemented yet: `ON` stops the configure step |
| `RT_BUILD_TESTS` | `ON` | GoogleTest unit and regression tests |
| `RT_PTXAS_VERBOSE` | `OFF` | Adds `-Xptxas -v` to nvcc: per-kernel register and local-memory report (G3 profiling) |
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
| `-mcpu=native` (or `-mcpu=neoverse-v2`) | Grace. GCC 13.2.0 accepts both; CMake uses `-mcpu=native` on aarch64 |
| No `-ffast-math`, no `--use_fast_math` | Everywhere (the NaN-safe slab test depends on IEEE comparisons; see [INTERFACES.md](INTERFACES.md#ray-box-slab-test-frozen)) |
| `-ffp-contract=off` | Generator and scene-hash sources only. Reduces variation, but doesn't make x86 and ARM bytes identical (libm differs), so benchmark scenes are generated once and distributed ([INTERFACES.md](INTERFACES.md#generator-determinism-and-scene-distribution)) |
| `-Xcompiler -fopenmp`, `-lineinfo`, `-Xptxas -v` | nvcc (lineinfo for Nsight; `-v` reports registers and local memory) |

Benchmark binaries on Vista must be built on Vista. Don't copy laptop binaries.

## Dependencies

Fetched with CMake `FetchContent` as release archives, pinned by version and SHA-256 in [`cmake/RtDependencies.cmake`](../cmake/RtDependencies.cmake). Changing a pin means updating both the URL and the hash:

| Library | Use | Pin |
|---|---|---|
| nlohmann/json | JSON scenes and results | `v3.12.0` (target `nlohmann_json::nlohmann_json`) |
| stb (stb_image_write) | PNG output | commit `003501f6` (target `stb`, headers only) |
| CLI11 | Command line | `v2.7.2` (target `CLI11::CLI11`) |
| GoogleTest | Tests | `v1.18.0` (target `GTest::gtest_main`; only with `RT_BUILD_TESTS`) |
| tinyobjloader | `obj2bin` (Extension E6) | Not added yet; pinned when E6 starts |
| Embree 4 | Embree backend (Extension E4) | `v4.x` (**verify on Vista** ARM64 build or module) |
| OptiX SDK | OptiX backend (Stretch) | `8.x` or newer, manual download |

## OptiX on WSL2 (Stretch)

NVIDIA's recent Windows drivers support OptiX inside WSL2. Verify this early on the 4070 by building and running the `optixSphere` SDK sample in WSL2. If it fails, build only the `optix` backend natively on Windows (MSVC + CUDA). The rest of the project stays WSL2-only. On Vista, OptiX runs on the Hopper GPU without RT-core hardware. See [STRETCH.md](STRETCH.md#optix-backend).

## Profiling tools

- Nsight Systems (`nsys`) and Nsight Compute (`ncu`): ship with the CUDA toolkit. **Verify on Vista** that they're in the `cuda` module or a separate `nvidia` module.
- CPU: `perf` (if allowed), GCC `-fopt-info-vec` for vectorization reports.
