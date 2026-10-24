# Mikail Sadic (Member 2)

## 1. Role

- **Core:** skeleton, CMake, CI, Vista verification; intersection and traversal; backend interface; `DeviceScene` (explicit) and the CUDA megakernel; sweep scripts; GPU profiling; node-hour log.
- **Extensions:** E1 `managed`/`system` memory modes on the GH node; E5 wavefront trace kernels.
- **Stretch:** OptiX (with Arek).

## 2. Owns

`CMakeLists.txt`, `cmake/`, `.github/`, `src/trace/`, `src/backend.hpp`, `src/gpu/` (except wavefront shade, accumulate, and queues), `src/optix/`, `apps/rt_render/`, `apps/hello_cuda/`, `scripts/tacc/`.

## 3. Deliverables

| Gate | Deliverable | Size | Task |
|---|---|---|---|
| G0 (Oct 30) | Repo, protection rules, PR template; CMake with CPU-only fallback; CI per the fixed outline | M | C1 |
| G0 (Oct 30) | `hello_cuda` on a GH node; record the tested GCC + CUDA pair and the GPU name in BUILD.md | S | C1 |
| G0 (Oct 30) | Intersection: watertight triangle, stable sphere quadratic, frozen interval and tie rules; `brute_*` | M | C3 |
| G1 (Nov 6) | `traverse_closest` / `traverse_any` (inclusive culling, near-first, 64-entry stack, empty BVH) | M | C5 |
| G2 (Nov 13) | Backend interface with `RenderSettings`; `DeviceScene` uploading scene + BVH; megakernel calling `render_pixel`; AOV download | M | C10 |
| G2 (Nov 13) | Sweep sbatch scripts with identity recording; node-hour log | S | C12 |
| G3 (Nov 18) | GPU profiling: `-Xptxas -v`, Nsight Compute (occupancy, local memory), Nsight Systems | M | C13 |
| After G2 | E1 memory modes with advice, prefetch, and residency recording (4-day timebox) | M | E1 |
| After G3 | E5 trace kernels with Arek | M | E5 |

## 4. Contracts

- **Owns:** [Intersection and traversal](../INTERFACES.md#5-intersection-and-traversal), [Backend interface](../INTERFACES.md#7-backend-interface), `DeviceScene`.
- **Consumes:** math and slab test (Arek), `SceneHost` (Po), BVH layout (Jett), shading (Arek), timers (Arek).

## 5. Dependencies

- **Blocked by:** math library (C2). Traversal also needs Jett's median builder, but a hand-built 3-node BVH suffices to start.
- **Blocks:** everyone's builds (C1); the brute-force image (C3); the CPU BVH path (C5); G2.
- **Stubs:** hand-built BVH; normal-as-color shading for the first kernel; inline JSON scenes.

## 6. Tests

- Intersection: hit, miss, grazing, inside sphere, behind origin, degenerate triangle, shared edge (exactly one winner by `PrimRef`), vertex hits.
- Arek's double-precision oracle agrees on random rays away from edges.
- BVH vs brute force: identical `prim` and bit-identical `t`.
- Any-hit respects `tmax`. Empty scene returns a miss.
- GPU vs CPU passes `rt_compare` on known scenes and `bench_100k`, on both machines. Repeated GPU runs are bit-identical.
- E1: all modes pass `rt_compare`; unsupported modes fail cleanly on the laptop.

## 7. References

- PBRT v4 Ch. 6 (watertight triangle test, sphere quadratic, ray-box robustness).
- Woop, Benthin, Wald 2013, "Watertight Ray/Triangle Intersection".
- Aila & Laine 2009, "Understanding the Efficiency of Ray Traversal on GPUs".
- CUDA Programming Guide: local memory, unified memory; NVIDIA CUDA installation guide (host compiler table for the exact CUDA version).
- TACC Vista user guide.

## 8. This week (Oct 24–30)

- [ ] Vista: `idev -p gh-dev -N 1 -n 1 -t 1:00:00`; run `nvidia-smi` identity query; build `hello_cuda` with `gcc/13.2.0` + `cuda`.
- [ ] Check the CUDA install guide for the loaded version's supported GCC; fill the pinned-pairs table in [BUILD.md](../BUILD.md#pinned-toolchain-pairs).
- [ ] Create the repo with `main` + `dev` protection and the PR template.
- [ ] Root CMake; CI with the fixed format check.
- [ ] Review INTERFACES v0.3 sections 5 and 7; sign off by Oct 27.
- [ ] Start intersection with tests (branch `mikail/intersect`).

## 9. Risks

| Risk | Mitigation |
|---|---|
| Front-loaded: C1 and C3 in the same week | Keep C1 minimal; intersection is small and well-specified |
| Traversal on the critical path | `brute_*` ships first with the same rules |
| CUDA/GCC pair unsupported on Vista | Check the versioned compatibility table; try module pairs early |
| Megakernel slower than expected | Profile local-memory stack traffic before redesigning |
| `system` mode results drift between runs | Record per-frame times and residency settings (E1 protocol) |

## 10. Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | | | |
| | | | |
