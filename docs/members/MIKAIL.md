# Mikail Sadic (Member 2)

## 1. Role

- **Original:** GPU visibility (intersection + traversal), TACC access and builds, batch scripts.
- **Added:** top-level CMake/CUDA build, GitHub Actions CI, GPU memory modes (Extended), OptiX backend (Stretch), node-hour log.

## 2. Owns

`CMakeLists.txt`, `cmake/`, `.github/`, `src/trace/`, `src/gpu/` (except wavefront shade/accumulate/queues), `src/optix/`, `apps/rt_render/`, `apps/hello_cuda/`, `scripts/tacc/`.

## 3. Deliverables

| When | Deliverable | Size | Task | Unblocks |
|---|---|---|---|---|
| Sept 22–29 | Vista CPU+GPU access test, hello_cuda on GH200, module list | S | 1 | Check-in 1 |
| Sept 22–29 | Repo skeleton, CMake options, CI, sbatch template, branch protection | M | 1 | Everyone's builds |
| By ~Oct 6 | Sphere/triangle intersection + `traverse_closest`/`traverse_any`, CPU-tested first | M | 3, 5 | Po's reference, Arek's integration (**critical path**) |
| By Oct 26 | `DeviceMemory` (explicit), megakernel, first GPU image | M | 10 | Check-in 2 |
| By Nov 20 | `managed` + `system` modes on GH200 | M | 15 | NVLink-C2C study |
| By Nov 20 | Wavefront trace kernels (with Arek); traversal tuning: nearer-child-first, node layout, occupancy | M | 16, 18 | Pipeline comparison |
| By Nov 20 | Sweep sbatch scripts, results collection | S | 13 | Final measurements |
| Check-in 3 | OptiX go/no-go; backend in late Nov if go | L | 21 | RT-core comparison |

## 4. Contracts

- **Owns:** traversal + intersection API (`Hit`, `intersect_*`, `traverse_*`, `brute_*`) in [INTERFACES.md](../INTERFACES.md#4-traversal-and-intersection). Backend interface in [ARCHITECTURE.md](../ARCHITECTURE.md#backend-interface).
- **Consumes:** math (Arek), Scene (Po), BVH layout (Jett), shading (Arek), timers (Arek).

## 5. Dependencies

- **Blocked by:** BVH layout contract and Scene struct (week 1).
- **Blocks:** Po's BVH reference, Arek's GPU integration, everyone's builds (Task 1).
- **Stubs while waiting:**
  - Hand-built 3-node BVH for traversal tests before Jett's builder lands.
  - Constant-color `shade()` for the first megakernel.
  - Inline JSON scenes.

## 6. Tests

- Intersection: hit, miss, grazing, origin inside sphere, behind origin, degenerate triangle, edge/vertex hits.
- Traversal vs brute force: identical `Hit` on 10K random rays over random scenes (CPU).
- Any-hit respects `tmax` (occluder past the light is ignored).
- GPU vs CPU image within tolerance on known-case and `bench_100k`, both machines.
- All `--mem` modes produce matching images on GH200; unsupported mode fails cleanly on laptop.

## 7. References

- Aila & Laine 2009, "Understanding the Efficiency of Ray Traversal on GPUs".
- PBRT v4 Ch. 6 (triangle intersection, watertight test).
- Jacco Bikker BVH series (GPU traversal part).
- Laine, Karras & Aila 2013, "Megakernels Considered Harmful".
- TACC Vista user guide; NVIDIA Grace Hopper unified memory blog posts.
- OptiX SDK samples `optixSphere`, `optixWhitted`.

## 8. First-week checklist (before Sept 29)

- [ ] Vista: `idev -p gh-dev -N 1 -n 1 -t 1:00:00`, `nvidia-smi`, build and run hello_cuda with `gcc/13.2.0` + `cuda`.
- [ ] Record `module spider cuda`, `gcc`, `cmake` versions in [BUILD.md](../BUILD.md). Confirm CUDA supports GCC 13.2.
- [ ] Create the GitHub repo, `main` + `dev` with protection rules, PR template.
- [ ] Root CMakeLists with options, pinned FetchContent, CPU-only fallback.
- [ ] CI workflow running a placeholder test.
- [ ] sbatch template + node-hour log started.
- [ ] Draft traversal/intersection section of INTERFACES.md for the Sept 24–25 meeting.
- [ ] Optional: check OptiX `optixSphere` sample on the 4070 in WSL2.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Heavy, front-loaded September | Keep Task 1 minimal; skeleton first, polish later |
| Traversal delays block Po and Arek | Brute-force functions ship first with the same signatures |
| CUDA/GCC mismatch on Vista | Try module pairs early; document the working pair |
| `system` mode behaves unexpectedly | Start with `explicit`; treat `system` as an experiment and report what happens |
| OptiX on WSL2 fails | Native Windows build for that backend only, or no-go |

## 10. Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Sept 22–28 | | | |
| | | | |
