# Source this on a Vista node (interactive idev or inside an sbatch job):
#   source scripts/tacc/env.sh
# Loads the pinned toolchain (BUILD.md "Vista setup"), sets the thread pinning every run needs
# (BENCHMARKING.md "Thread pinning"), and records `module list` for sweep.py's identity files.
module reset
module load gcc/13.2.0 cuda/13.3 cmake
# Nsight is not in the cuda/13.3 module. The HPC SDK that cuda/13.3 comes from (nvidia/26.9) ships
# it; nsys 2026.4.1 and ncu 2026.2.1 start from this directory on a GH node (Nov 27). The
# directory is appended, never prepended: it also holds that SDK's compilers, which must not
# shadow the gcc and CUDA tools loaded above. (The old SDK under /opt/apps/nvidia/24.5 has
# wrappers that fail with "Version 12_4 ... is not available".) RT_NSIGHT_DIR overrides the path.
RT_NSIGHT_DIR="${RT_NSIGHT_DIR:-/home1/apps/nvidia/Linux_aarch64/26.9/compilers/bin}"
if ! command -v nsys >/dev/null 2>&1 && [[ -d "$RT_NSIGHT_DIR" ]]; then
  export PATH="$PATH:$RT_NSIGHT_DIR"
fi
export OMP_PROC_BIND=close
export OMP_PLACES=cores
# TACC shells set OMP_NUM_THREADS=1. The project never reads it (every parallel region passes
# num_threads), but nproc does, so scripts use `nproc --all`.
RT_MODULE_LIST="$(module list 2>&1)"
export RT_MODULE_LIST
# Repo root, from this file's location; build tree and default scene directory under it.
RT_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export RT_REPO
export RT_BUILD="${RT_BUILD:-$RT_REPO/build-vista}"
export RT_SCENE_DEST="${RT_SCENE_DEST:-$RT_REPO/scenes/generated}"
