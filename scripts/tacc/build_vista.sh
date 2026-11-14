#!/usr/bin/env bash
# Configure and build the CUDA build on a Vista GH compute node (not a login node: -mcpu=native
# targets the build machine, and the first configure fetches dependencies).
#   source scripts/tacc/env.sh && bash scripts/tacc/build_vista.sh   # RT_JOBS=N sets -j (default 8)
# Fails if the build prints a warning or rt_render would load the wrong OpenMP runtime.
set -euo pipefail
: "${RT_REPO:?source scripts/tacc/env.sh first}"
cd "$RT_REPO"
cmake -S . -B "$RT_BUILD" -DCMAKE_BUILD_TYPE=Release -DRT_ENABLE_CUDA=ON -DRT_CUDA_ARCH=90 \
  -DCMAKE_CXX_COMPILER=g++
log="$RT_BUILD/build.log"
# Capped: nvcc starts several processes per file, and 72 parallel jobs exhaust the per-user
# process limit on a login node ("resource temporarily unavailable"). RT_JOBS overrides it.
jobs="${RT_JOBS:-8}"
cmake --build "$RT_BUILD" -j "$jobs" 2>&1 | tee "$log"
if grep -Eq 'warning|error' "$log"; then
  echo "build_vista: the build printed warnings or errors; see $log" >&2
  exit 1
fi
ldd "$RT_BUILD/apps/rt_render/rt_render" | grep -Ei 'gomp|omp|nvhpc' | tee "$RT_BUILD/ldd-omp.txt"
if ! grep -q libgomp "$RT_BUILD/ldd-omp.txt" || grep -Eqi 'libomp|nvomp|nvhpc' "$RT_BUILD/ldd-omp.txt"; then
  echo "build_vista: rt_render loads the wrong OpenMP runtime (BUILD.md \"Vista setup\")" >&2
  exit 1
fi
echo "build_vista: OK, $(git rev-parse --short HEAD) in $RT_BUILD" >&2
