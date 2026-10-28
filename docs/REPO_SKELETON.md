# Recommended Repo Skeleton

**Partly built in task C1** ([ROADMAP.md](ROADMAP.md#c1-skeleton-build-ci-vista-verification)): the root and `cmake/` build files, `src/CMakeLists.txt` (`rt_core`), `tests/CMakeLists.txt`, `apps/hello_cuda/`, `.clang-format`, `.gitignore`, and `.github/`. The rest of the tree is a recommendation; file names are suggestions, and owners can adjust within their directories. Each module owner adds `src/<module>/CMakeLists.txt` defining only their target; `src/CMakeLists.txt` picks it up automatically.

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
│   ├── core/       (Arek)      hostdev.hpp  math.hpp  ray.hpp  aabb.hpp  color.hpp  error.hpp
│   ├── scene/      (Po)        scene.hpp (Scene view)  scene_host.hpp  scene_io.cpp  binary_format.hpp  hash.hpp
│   │                           generator.cpp  pcg32.hpp      # generator.cpp + hash: -ffp-contract=off
│   ├── camera/     (Jett)      camera.hpp (inline camera_ray, device-usable)  camera.cpp (host only: parsing, overrides)
│   ├── bvh/        (Jett)      bvh_types.hpp  bvh.hpp  bounds.cpp  build_median.cpp  build_lbvh.cpp  layout.cpp  build_sah.cpp  validate.cpp
│   │                           morton.hpp  radix_sort.hpp
│   ├── trace/      (Mikail)    intersect.hpp  traverse.hpp  brute.hpp
│   ├── shade/      (Arek)      shade.hpp  lights.hpp  offset.hpp
│   ├── cpu/        (Po)        cpu_backend.hpp  cpu_backend.cpp
│   ├── gpu/        (Mikail, Arek)
│   │   ├── device_scene.hpp   device_scene.cu      # DeviceScene, memory modes
│   │   ├── cuda_backend.hpp   cuda_backend.cu
│   │   ├── megakernel.cu
│   │   └── wavefront/          queues.hpp  trace_kernels.cu (Mikail)  shade_kernels.cu  accumulate.cu (Arek)
│   ├── embree/     (Po)        embree_backend.hpp  embree_backend.cpp
│   ├── optix/      (Mikail)    optix_backend.hpp  optix_backend.cpp  programs.cu
│   ├── bench/      (Arek)      timings.hpp  timer.hpp  gpu_timer.hpp  results.hpp  results.cpp  machine_info.cpp
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
│   └── oracle/                 # independent checks: double-precision intersection (Arek), shading expected values (Jett)
├── scenes/
│   ├── known/                  # Small hand-written inline JSON scenes
│   ├── golden/                 # Small golden PFM + .u32img files, compared with normal tolerances
│   └── manifest.json           # Benchmark scenes: rt_gen command, generator environment, hashes (files themselves not committed)
├── scripts/
│   ├── ci/                     # golden_check.sh, regen_goldens.sh
│   ├── fetch_scenes.sh         # download a scene set from the release archive and verify hashes
│   ├── tacc/                   # modules.sh, sbatch templates, sweep generators
│   ├── plot/                   # matplotlib scripts
│   └── blender/                # export_scene.py
└── docs/
```

`src/image/` is owned by Po (PNG/PFM IO sits next to `rt_compare`).

## Root CMakeLists outline

```cmake
cmake_minimum_required(VERSION 3.28)
project(HybridRayTracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RT_ENABLE_CUDA   "Build CUDA backend"           ON)
option(RT_ENABLE_EMBREE "Build Embree backend (ext)"   OFF)
option(RT_ENABLE_OPTIX  "Build OptiX backend (stretch)" OFF)
option(RT_BUILD_TESTS   "Build tests"                  ON)
set(RT_CUDA_ARCH "89" CACHE STRING "CUDA arch: 89 laptop, 90 Vista Hopper, 80 A100")

find_package(OpenMP REQUIRED)

# CPU flags: no fast-math anywhere
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(RT_CPU_FLAGS -O3 -mcpu=native)   # GCC 13.2.0 on Vista accepts it
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

Target names, sources, owners, and link dependencies are fixed in [INTERFACES §0 CMake targets](INTERFACES.md#cmake-targets). Each optional target defines `RT_HAS_CUDA` / `RT_HAS_EMBREE` / `RT_HAS_OPTIX` so `rt_render` can list available backends.

## .gitignore

```gitignore
# Build
build*/
out/
.cache/
compile_commands.json

# Generated data
*.bin
*.bin.gz
scenes/generated/          # filled by scripts/fetch_scenes.sh from the release archive
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

## Tests
<!-- Tests added or updated -->

- [ ] GPU checked (only if render code changed): CUDA build compiles; if a GPU path exists,
      paste the rt_compare summary for a known scene: ______
- [ ] Changes docs/INTERFACES.md: approved by each affected owner, version line bumped
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
    defaults:
      run:
        shell: bash      # GitHub runs bash steps with -e -o pipefail
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0   # full history so the base branch can be diffed
      - name: Install
        run: sudo apt-get update && sudo apt-get install -y g++-13 cmake ninja-build clang-format
      - name: Configure
        run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRT_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++-13
      - name: Build
        run: cmake --build build -j
      - name: Tests (unit, golden, rt_compare corruption)
        run: ctest --test-dir build --output-on-failure
      - name: Format check
        env:
          BASE: ${{ github.base_ref || 'dev' }}
        run: |
          set -euo pipefail
          git fetch --no-tags origin "$BASE"
          # Capture the list first so a failing git diff fails the step instead of
          # silently producing an empty list for xargs.
          files="$(git diff --name-only --diff-filter=ACMR "origin/$BASE...HEAD" -- '*.cpp' '*.hpp' '*.cu' '*.cuh')"
          if [ -n "$files" ]; then
            printf '%s\n' "$files" | xargs clang-format --dry-run --Werror
          fi

```

- On push events (no `base_ref`), the step diffs against `dev`. A push to `dev` itself then diffs against itself and checks nothing, which is fine because every change reached `dev` through a checked PR.
- Pin action versions and the `clang-format` version (different versions format differently). Add FetchContent caching once builds get slow.

## sbatch template

`scripts/tacc/run_gh.sbatch` (queue names `gh-dev` and `gh` and the module versions were checked on Oct 28; **verify on Vista** the time limits and the allocation name):

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
module load gcc/13.2.0 cuda/13.3

export OMP_PROC_BIND=close
export OMP_PLACES=cores

BIN=$WORK/ray-tracer/build-vista/apps/rt_render/rt_render
SCENE=$SCRATCH/scenes/bench_1m_seed1.json
OUT=$SCRATCH/results/$SLURM_JOB_ID
mkdir -p "$OUT"

# Machine identity, recorded on the allocated node
nvidia-smi --query-gpu=name,uuid,driver_version,memory.total --format=csv > "$OUT/gpu.csv"
lscpu > "$OUT/lscpu.txt"
{ gcc --version; nvcc --version; module list 2>&1; } > "$OUT/toolchain.txt"

# Fail fast if the binary would load the wrong OpenMP runtime (see BUILD.md)
ldd "$BIN" > "$OUT/ldd.txt"
if ! grep -q 'libgomp' "$OUT/ldd.txt" || grep -Eqi 'libomp|nvomp|nvhpc' "$OUT/ldd.txt"; then
  echo "Wrong OpenMP runtime; see $OUT/ldd.txt" >&2; exit 1
fi

COMMON="--scene $SCENE --bvh lbvh --width 1920 --height 1080 --runs 5"

# CPU scaling: build and render threads set explicitly; T = 1 is the 1-core baseline
for T in 1 2 4 8 16 32 64 72; do
  $BIN $COMMON --backend cpu --build-threads $T --render-threads $T \
       --mode prepared --results "$OUT/cpu_t$T.json" --pfm "$OUT/cpu_t$T.pfm"
done

# GPU, explicit memory (Core)
$BIN $COMMON --backend cuda --pipeline mega --mem explicit --build-threads 72 \
     --mode prepared --results "$OUT/cuda_explicit.json" --pfm "$OUT/cuda_explicit.pfm"

# T_app: 5 fresh processes
for i in 1 2 3 4 5; do
  $BIN $COMMON --backend cuda --pipeline mega --mem explicit --build-threads 72 \
       --mode app --results "$OUT/cuda_app_$i.json"
done
```

Extension E1 adds a loop over `--mem managed|system` (plus a separate `system-reuse` experiment) with the `--mem-advice` and `--prefetch` settings recorded in each results file.

Log each job in the node-hour log ([BENCHMARKING.md](BENCHMARKING.md#node-hour-budget)).
