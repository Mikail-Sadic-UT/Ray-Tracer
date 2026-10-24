# Hybrid Ray Tracer

ECE 379K project. A ray tracer for spheres and triangles that splits work between CPU and GPU. The CPU builds the acceleration structure (BVH) in parallel. The GPU renders visibility, lighting, and hard shadows. We measure the speedup over single-core and multicore CPU rendering, and identify when preparation and CPU-GPU data movement limit it.

| Member | EID | Proposal role | Member file |
|---|---|---|---|
| Jett Vanbrocklin | JWV493 | Member 1 | [JETT.md](docs/members/JETT.md) |
| Mikail Sadic | MFS2447 | Member 2 | [MIKAIL.md](docs/members/MIKAIL.md) |
| Arek Plate | AWP688 | Member 3 | [AREK.md](docs/members/AREK.md) |
| Po Han Ai | PA22435 | Member 4 | [PO.md](docs/members/PO.md) |

**Status (Oct 24):** planning docs only; no code merged in this repository. The schedule was re-baselined to start from today. See [ROADMAP.md](docs/ROADMAP.md#status).

## Required deliverable

1. Spheres + triangles, Lambert lighting with point lights, hard shadows.
2. Serial median BVH (correctness baseline) and one scalable parallel builder (LBVH).
3. CPU rendering on one core and on many cores.
4. A CUDA megakernel that matches the CPU under the [quality protocol](docs/BENCHMARKING.md#quality-protocol).
5. Validation: analytic tests, independent oracle, BVH vs brute force, image/AOV comparison.
6. Measurements: application, build-and-render, and prepared-frame times, with scaling and breakdowns.

Extensions (memory modes, Blinn-Phong, SAH, Embree, wavefront, `obj2bin`) start only after the Core gates they depend on pass, in [priority order](docs/ROADMAP.md#extensions-in-priority-order-each-timeboxed). OptiX and the other [stretch items](docs/STRETCH.md) are future work.

## Goals (from proposal)

Hypotheses to evaluate, all measured with the same workload and the same timing definition (T_br, [defined here](docs/BENCHMARKING.md#timing-quantities)):

| Comparison | Target |
|---|---|
| Fastest tested multicore CPU vs 1 core (build and render threads both 1) | ~10× |
| GPU vs fastest tested multicore CPU (best thread count, not assumed 72) | ~2× |
| GPU vs 1 core, measured directly | ~20× |

Fixed 72-thread comparisons are reported alongside, labeled separately ([details](docs/BENCHMARKING.md#targets-from-proposal)).

Output 1920×1080. Median of 5 runs, with spread reported. Quality: no visible differences in side-by-side review by 2+ reviewers, backed by automated checks.

## Platforms

| Machine | CPU | GPU | Notes |
|---|---|---|---|
| TACC Vista GH node (primary) | Grace, aarch64 (record core count via `lscpu`) | Hopper, sm_90, no RT cores (record exact name via `nvidia-smi`) | NVLink-C2C |
| Laptop, WSL2 Ubuntu 24.04 | i9-13980HX, 8P + 16E | RTX 4070 Laptop, sm_89 | PCIe |
| Fallback: Lonestar6 | AMD EPYC | A100, sm_80 | Only if Vista fails |

Offline CLI renderer. No native Windows build.

## Schedule

| Date | Gate / event | Exit criteria (summary) |
|---|---|---|
| Oct 27 | Contract sign-off | [INTERFACES.md](docs/INTERFACES.md) v0.3 accepted |
| Oct 30 | **G0** | Builds on WSL2 + Vista, CI green, brute-force CPU image |
| ~Oct 31–Nov 7 | Check-in 2 (postponed, date TBD) | Latest passed gate (G0 or G1) plus the re-baselined plan; see [ROADMAP.md](docs/ROADMAP.md#gates-and-schedule) |
| Nov 6 | **G1** | Correct CPU renderer, median BVH, multicore, generator, `rt_compare`, CPU timings, benchmark scenes fixed |
| Nov 13 | **G2** | CUDA matches CPU; LBVH; first Vista timings. Reduced scope if missed |
| Nov 16–20 | Check-in 3; **G3** Nov 18 | Scaling, breakdowns, profiling, preliminary ratios; extensions chosen |
| Nov 30 | Ready | Final runs, review, slides |
| Dec 1/3 | Presentation | |
| Dec 6 | Release | Report, reproducible release |

## Quick start

To be filled once task C1 (skeleton and build) lands. Planned setup: [BUILD.md](docs/BUILD.md).

## Documentation

| File | Contents |
|---|---|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Data flow, ownership vs views, backend lifecycle, GPU pipelines, memory modes, CLI |
| [INTERFACES.md](docs/INTERFACES.md) | Types, frozen numeric rules, BVH pipeline, file formats, results schema |
| [BUILD.md](docs/BUILD.md) | WSL2 and Vista setup, pinned toolchains, CMake options, flags |
| [BENCHMARKING.md](docs/BENCHMARKING.md) | Timing quantities, threads, targets, sweeps, memory-mode protocol, quality protocol |
| [WORKFLOW.md](docs/WORKFLOW.md) | Branching, PRs, CI, issues, gates |
| [ROADMAP.md](docs/ROADMAP.md) | Scope, gates, owners, tasks, risks, status |
| [STRETCH.md](docs/STRETCH.md) | OptiX, SIMD, reflections, GPU BVH build |
| [REPO_SKELETON.md](docs/REPO_SKELETON.md) | Recommended layout and config file contents |
| [members/](docs/members/) | Per-member scope, deliverables, checklist, contribution log |
