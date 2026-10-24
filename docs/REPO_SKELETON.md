# Recommended Repo Skeleton

**Recommendation only. Nothing here exists yet.** Mikail creates the skeleton in Task 1 ([ROADMAP.md](ROADMAP.md#task-1-repo-skeleton-build-ci)). File names below are suggestions; owners can adjust within their directories.

## Directory tree

```
.
├── CMakeLists.txt
├── cmake/                      # Find modules, compiler flag helpers
├── .clang-format
├── .gitignore
├── .github/
│   ├── workflows/ci.yml
│   └── pull_request_template.md
├── src/
│   ├── core/       (Arek)      hostdev.hpp  math.hpp  ray.hpp  aabb.hpp  color.hpp
│   ├── scene/      (Po)        scene.hpp  scene_io.hpp  scene_io.cpp  binary_format.hpp  generator.hpp  generator.cpp  pcg32.hpp
│   ├── camera/     (Jett)      camera.hpp  camera.cpp
│   ├── bvh/        (Jett)      bvh.hpp  build_median.cpp  build_lbvh.cpp  build_sah.cpp  validate.cpp  morton.hpp  radix_sort.hpp
│   ├── trace/      (Mikail)    intersect.hpp  traverse.hpp  brute.hpp
│   ├── shade/      (Arek)      shade.hpp  lights.hpp  offset.hpp
│   ├── cpu/        (Po)        cpu_backend.hpp  cpu_backend.cpp
│   ├── gpu/        (Mikail, Arek)
│   │   ├── device_memory.hpp  device_memory.cu
│   │   ├── cuda_backend.hpp   cuda_backend.cu
│   │   ├── megakernel.cu
│   │   └── wavefront/          queues.hpp  trace_kernels.cu (Mikail)  shade_kernels.cu  accumulate.cu (Arek)
│   ├── embree/     (Po)        embree_backend.hpp  embree_backend.cpp
│   ├── optix/      (Mikail)    optix_backend.hpp  optix_backend.cpp  programs.cu
│   ├── bench/      (Arek)      timer.hpp  gpu_timer.hpp  results.hpp  results.cpp  machine_info.cpp
│   ├── image/      (Po)        image.hpp  write_png.cpp  write_pfm.cpp  read_pfm.cpp
│   └── backend.hpp             # Backend interface
├── apps/
│   ├── rt_render/  main.cpp
│   ├── rt_gen/     main.cpp
│   ├── rt_compare/ main.cpp
│   ├── scene_dump/ main.cpp
│   ├── obj2bin/    main.cpp
│   └── hello_cuda/ main.cu
├── tests/                      # GoogleTest: test_math.cpp, test_intersect.cpp, test_bvh.cpp, ...
├── scenes/
│   ├── known/                  # Small hand-written inline JSON scenes
│   └── golden/                 # Small golden PNG/PFM images for regression tests
├── scripts/
│   ├── ci/                     # golden_check.sh
│   ├── tacc/                   # modules.sh, sbatch templates, sweep generators
│   ├── plot/                   # matplotlib scripts
│   └── blender/                # export_scene.py
└── docs/
```

`src/image/` is owned by Po (PNG/PFM IO sits next to `rt_compare`).

## Root CMakeLists outline

```cmake
cmake_minimum_required(VERSION 3.24)
project(HybridRayTracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RT_ENABLE_CUDA   "Build CUDA backend"           ON)
option(RT_ENABLE_EMBREE "Build Embree backend"         OFF)
option(RT_ENABLE_OPTIX  "Build OptiX backend (stretch)" OFF)
option(RT_BUILD_TESTS   "Build tests"                  ON)
set(RT_CUDA_ARCH "89" CACHE STRING "CUDA arch: 89 laptop, 90 H100, 80 A100")

find_package(OpenMP REQUIRED)

# CPU flags: no fast-math anywhere
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(RT_CPU_FLAGS -O3 -mcpu=native)   # verify on Vista
else()
  set(RT_CPU_FLAGS -O3 -march=native)
endif()

# CUDA is optional: CPU-only build must always work
if(RT_ENABLE_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_ARCHITECTURES ${RT_CUDA_ARCH})
    find_package(CUDAToolkit REQUIRED)
  else()
    message(WARNING "No CUDA compiler found, building CPU-only")
    set(RT_ENABLE_CUDA OFF)
  endif()
endif()

include(FetchContent)
FetchContent_Declare(json        GIT_REPOSITORY https://github.com/nlohmann/json.git           GIT_TAG <pinned-tag>)
FetchContent_Declare(cli11       GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git          GIT_TAG <pinned-tag>)
FetchContent_Declare(stb         GIT_REPOSITORY https://github.com/nothings/stb.git            GIT_TAG <pinned-commit>)
FetchContent_Declare(googletest  GIT_REPOSITORY https://github.com/google/googletest.git       GIT_TAG <pinned-tag>)
FetchContent_Declare(tinyobj     GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git GIT_TAG <pinned-tag>)
FetchContent_MakeAvailable(json cli11 googletest tinyobj)   # stb: FetchContent_Populate + include dir

add_subdirectory(src)
add_subdirectory(apps)
if(RT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
```

Suggested target structure: `rt_core` (INTERFACE, header-only core + trace + shade), `rt_scene`, `rt_bvh`, `rt_cpu`, `rt_gpu` (if CUDA), `rt_embree` / `rt_optix` (if enabled), `rt_bench`, `rt_image`. Apps link what they need. Each optional target defines `RT_HAS_CUDA` / `RT_HAS_EMBREE` / `RT_HAS_OPTIX` so `rt_render` can list available backends.

## .gitignore

```gitignore
# Build
build*/
out/
.cache/
compile_commands.json

# Generated data
*.bin
scenes/generated/
results/
*.nsys-rep
*.ncu-rep

# Images (except small goldens)
*.png
*.pfm
!scenes/golden/*.png
!scenes/golden/*.pfm
!docs/**/*.png

# Editors / OS
.vscode/
.idea/
*.swp
.DS_Store

# Python
__pycache__/
.venv/
```

## .clang-format

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
PointerAlignment: Left
AllowShortFunctionsOnASingleLine: Inline
SortIncludes: true
```

The team can change the base style at the contract meeting. Pick once and don't revisit.

## PR template

`.github/pull_request_template.md`:

```markdown
## Summary
<!-- What changed and why. Link the issue: Closes #N -->

## Area
- [ ] bvh  - [ ] gpu  - [ ] shading  - [ ] cpu  - [ ] scene  - [ ] infra  - [ ] bench  - [ ] docs

## Tests
<!-- Tests added or updated -->

## Checks
- [ ] CPU CI green
- [ ] GPU-checked locally and/or on TACC (required if rendering code changed)
      Machine(s):
      rt_compare vs CPU reference (max err, % over threshold):

## Performance impact
<!-- Before/after numbers, or "none expected" -->

## Contract change?
- [ ] No
- [ ] Yes: edits docs/INTERFACES.md, needs approval from all four members
```

## CI workflow outline

`.github/workflows/ci.yml`:

```yaml
name: ci
on:
  pull_request:
    branches: [dev, main]
  push:
    branches: [dev, main]
jobs:
  cpu:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: Install
        run: sudo apt-get update && sudo apt-get install -y g++-13 cmake ninja-build clang-format
      - name: Configure
        run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRT_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++-13
      - name: Build
        run: cmake --build build -j
      - name: Unit tests
        run: ctest --test-dir build --output-on-failure
      - name: Golden-image regression
        run: ./scripts/ci/golden_check.sh build    # renders scenes/known/*, compares to scenes/golden/*
      - name: Format check
        run: git diff --name-only origin/${{ github.base_ref || 'dev' }} -- '*.cpp' '*.hpp' '*.cu' | xargs -r clang-format --dry-run --Werror
```

Pin action versions. Add FetchContent caching once builds get slow.

## sbatch template

`scripts/tacc/run_gh.sbatch` (queue names, limits, and module versions: **verify on Vista**):

```bash
#!/bin/bash
#SBATCH -J rt-bench
#SBATCH -p gh-dev               # gh for production runs
#SBATCH -N 1
#SBATCH -n 1
#SBATCH -t 00:30:00
#SBATCH -o logs/%x.%j.out
#SBATCH -A <allocation>

module reset
module load gcc/13.2.0 cuda

export OMP_PROC_BIND=close
export OMP_PLACES=cores

BIN=$WORK/ray-tracer/build-vista/apps/rt_render/rt_render
SCENE=$SCRATCH/scenes/bench_1m_seed1.json
OUT=$SCRATCH/results/$SLURM_JOB_ID
mkdir -p "$OUT"

nvidia-smi > "$OUT/nvidia-smi.txt"

for T in 1 2 4 8 16 32 64 72; do
  OMP_NUM_THREADS=$T $BIN --scene "$SCENE" --backend cpu --bvh sah --threads $T \
      --width 1920 --height 1080 --runs 5 --results "$OUT/cpu_t$T.json" --out "$OUT/cpu_t$T.png"
done

for MEM in explicit managed system; do
  OMP_NUM_THREADS=72 $BIN --scene "$SCENE" --backend cuda --pipeline mega --mem $MEM --bvh sah \
      --threads 72 --width 1920 --height 1080 --runs 5 --results "$OUT/cuda_$MEM.json" --out "$OUT/cuda_$MEM.png"
done
```

Log each job in the node-hour log ([BENCHMARKING.md](BENCHMARKING.md#node-hour-budget)).
