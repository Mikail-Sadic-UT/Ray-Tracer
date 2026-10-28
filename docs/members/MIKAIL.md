# Mikail Sadic (Member 2)

## Start here

**You build:** the build system and CI, ray-primitive intersection and BVH traversal (GPU visibility), the backend interface, and the CUDA renderer (`DeviceScene` + megakernel). You also handle Vista: verification, batch scripts, and the node-hour log.

**Read first, in order:**
1. [INTERFACES §5 Intersection and traversal](../INTERFACES.md#5-intersection-and-traversal) (your contract)
2. [INTERFACES §7 Backend interface](../INTERFACES.md#7-backend-interface) and [owning storage vs views](../INTERFACES.md#owning-storage-vs-views) (`DeviceScene` is yours)
3. [INTERFACES §6 tracing queries and pixel result](../INTERFACES.md#tracing-queries) (what your kernel calls)
4. [INTERFACES §3 node layout](../INTERFACES.md#node-layout-frozen) (what you traverse)
5. [BUILD.md](../BUILD.md) and [REPO_SKELETON.md](../REPO_SKELETON.md) (you create these files)

**First three tasks:**
1. Repo skeleton files: root CMake with a CPU-only fallback, CI, PR template, `hello_cuda` running on the laptop and a Vista GH node. Record the working GCC + CUDA pair (by Oct 30).
2. Intersection (watertight triangle, stable sphere) + `brute_closest` / `brute_any` (by Oct 30).
3. GPU smoke image: `render_pixel<BruteQuery, NormalShading>` in a CUDA kernel (~Nov 3).

**You hand off:**

| What | To | When | Contract |
|---|---|---|---|
| Building repo + CI | Everyone | Oct 30 | [BUILD.md](../BUILD.md) |
| `intersect_*`, `brute_*`, `Hit` | Po (first CPU image), Arek (shadow tests) | Oct 30 | §5 |
| `traverse_closest` / `traverse_any`, `BvhQuery` | Po (CPU backend), Arek | Nov 6 | §5, §6 |
| `Backend` interface, `rt_render` CLI | Everyone | Nov 6 | §7 |
| CUDA megakernel backend | Everyone (GPU results) | Nov 13 | §7 |
| sbatch sweep scripts | Everyone (measurements) | Nov 13 | [BENCHMARKING](../BENCHMARKING.md#sweeps) |

**You depend on:**

| What | From | Until it lands, use |
|---|---|---|
| `vec3`, `Ray`, `AABB`, slab test | Arek (Oct 27) | Nothing to stub; it's small, so pair with Arek if it's late |
| `Scene` view | Po (Oct 30) | A hand-filled `Scene` struct in tests |
| `Bvh` | Jett (Nov 6) | A hand-built 3-node BVH |
| `shade_hit` | Arek (Nov 6) | `NormalShading` |

**Done means:** BVH traversal gives identical hits to brute force (same `prim`, bit-identical `t`) on random rays. GPU vs CPU passes `rt_compare` on the known scenes and `bench_100k`, on both machines. Repeated GPU runs are bit-identical.

### Agent brief

Paste this into your coding agent along with `docs/INTERFACES.md`:

```text
You are implementing intersection, traversal, the backend interface, and the CUDA backend for a
C++17/CUDA ray tracer. Repo layout: docs/REPO_SKELETON.md. Build details: docs/BUILD.md.
Contract: docs/INTERFACES.md sections 5 and 7, plus "Owning storage vs views", "Tracing queries",
and "Pixel result". Follow them exactly.

Scope (only edit these): CMakeLists.txt, cmake/, .github/, src/trace/, src/backend.hpp, src/gpu/
(not wavefront/), apps/rt_render/, apps/hello_cuda/, scripts/tacc/, and tests for these.
Toolchain: GCC 13 host compiler, CUDA 12.x, sm_89 (laptop) / sm_90 (Vista). A CPU-only build must always work.

Rules:
- Follow INTERFACES section 0 (code conventions): namespace rt, includes from src/ like
  "core/math.hpp", host errors as rt::Error, CUDA calls in RT_CUDA_CHECK, one CMake target per module.
- All RT_HOSTDEV code is inline in headers. No -rdc, no device linking.
- Hit acceptance: open interval (tmin, tmax); equal t -> lowest PrimRef wins; box culling far bound inclusive.
- Watertight triangle test (PBRT v4 6.5.3) including the double fallback when an edge function is exactly 0.
- Traversal: descend into nearer child, push farther; 64-entry stack; set an overflow flag on overflow.
- DeviceScene: one cudaMalloc + cudaMemcpy per array (scene arrays + BVH nodes + prim refs).
- prepare() allocates all per-frame buffers; render() never allocates. A resolution change means prepare() again.
- float only (except the watertight fallback); no -ffast-math / --use_fast_math.
- Comments are C++ Doxygen (`///`, `///<`, `//`), per INTERFACES section 0. Commit messages are
  kernel style, signed off with `git commit -s`, per WORKFLOW.md "Commit messages".
- At the start of a session, run the environment check in WORKFLOW.md "Development environment".
  If it is not WSL Ubuntu 24.04 or Vista with the BUILD.md toolchain, warn the user once (never
  block), and add the `Developed-on:` / `Needs-check:` trailers to your commits.
- If the interface looks wrong or incomplete, do not change it locally. Stop and write a short
  note proposing the change.

Deliverables, in order:
1. Root CMake (options in BUILD.md), CI workflow, hello_cuda.
2. intersect_sphere, intersect_triangle, brute_closest, brute_any + tests (hit, miss, grazing,
   inside sphere, degenerate triangle, shared edge tie, vertex hit).
3. GPU smoke kernel: render_pixel<BruteQuery, NormalShading>; rt_render --backend cuda --bvh none --shade normal.
4. traverse_closest / traverse_any + BVH-vs-brute-force tests.
5. Backend interface, CPU/CUDA dispatch in rt_render, CUDA megakernel with render_pixel<BvhQuery>.
```

## Deliverables

| When | Deliverable | Size |
|---|---|---|
| Oct 27 | Review INTERFACES §5, §7 and sign off | S |
| Oct 30 | Repo skeleton files, CMake, CI, `hello_cuda` on laptop + GH node; toolchain pair recorded in BUILD.md | M |
| Oct 30 | Intersection + brute force | M |
| ~Nov 3 | GPU smoke image | S |
| Nov 6 | Traversal (`BvhQuery`) | M |
| Nov 13 | `DeviceScene` (explicit) + megakernel; sweep sbatch scripts; node-hour log | M |
| Nov 18 | GPU profiling (`-Xptxas -v`, Nsight) | M |
| Later | E1 memory modes (after G2); E5 trace kernels (after G3); OptiX (stretch) | |

## Risks

| Risk | Mitigation |
|---|---|
| Front-loaded first week | Keep the CMake minimal; intersection is small and fully specified |
| Traversal is on the critical path | `brute_*` ships first with the same rules, so others aren't blocked |
| CUDA/GCC pair unsupported on Vista | Check NVIDIA's host-compiler table for the exact CUDA version; try module pairs early |
| Wrong OpenMP runtime on Vista | `ldd` check in [BUILD.md](../BUILD.md#vista-setup) |

## References

PBRT v4 Ch. 6 (watertight test); Woop et al. 2013; Aila & Laine 2009 (GPU traversal); CUDA Programming Guide (local memory, unified memory); TACC Vista user guide.

## Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | | | |
| | | | |
