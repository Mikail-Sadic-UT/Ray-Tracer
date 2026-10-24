# Hybrid Ray Tracer

ECE 379K project. A ray tracer for spheres and triangles that splits work between CPU and GPU. The CPU builds the acceleration structure (BVH) in parallel. The GPU renders visibility, lighting, and hard shadows. We measure speedup against single-core and multicore CPU rendering, and identify when preparation and CPU-GPU transfers limit that speedup.

| Member | EID | Proposal role | Member file |
|---|---|---|---|
| Jett Vanbrocklin | JWV493 | Member 1 | [JETT.md](docs/members/JETT.md) |
| Mikail Sadic | MFS2447 | Member 2 | [MIKAIL.md](docs/members/MIKAIL.md) |
| Arek Plate | AWP688 | Member 3 | [AREK.md](docs/members/AREK.md) |
| Po Han Ai | PA22435 | Member 4 | [PO.md](docs/members/PO.md) |

**Status:** planning. No code yet. See [ROADMAP.md](docs/ROADMAP.md).

## Goals (from proposal)

These are goals to evaluate, not predictions.

| Comparison | Target | Measured on |
|---|---|---|
| Multicore CPU vs 1 core | ~10× | Largest benchmark scene, up to 72 Grace cores |
| GPU vs fastest multicore CPU | ~2× | Same scene and settings |
| Overall | ~20× | Total time: preparation + transfers + rendering |

- Output: 1920×1080 images (smaller during development).
- Timing: median of 5 runs on the same node, with component breakdown. Cold and warm both reported.
- Quality: no visible differences from the CPU reference in side-by-side review by 2+ reviewers, backed by automated image comparison and known-case tests.

## Platforms

| Machine | CPU | GPU | Notes |
|---|---|---|---|
| TACC Vista GH200 node (primary benchmark) | Grace, 72 Neoverse V2 cores, aarch64 | H100, sm_90, no RT cores | NVLink-C2C CPU-GPU link |
| Developer laptop, WSL2 Ubuntu 24.04 | i9-13980HX, 8P + 16E cores | RTX 4070 Laptop, sm_89, RT cores | PCIe link |
| Fallback: Lonestar6 | AMD EPYC | A100, sm_80 | Only if Vista access fails |

The renderer is an offline CLI program. No native Windows build is needed.

## Quick start

To be filled once Task 1 (repo skeleton and build) lands. See [BUILD.md](docs/BUILD.md) for the planned setup.

## Scope

| Tier | Contents |
|---|---|
| **Core** (proposal) | Parallel CPU BVH build (median, LBVH, SAH), camera, CPU reference (1 and N threads), CUDA megakernel renderer, Lambert + Blinn-Phong lighting, hard shadows, scene generator, image comparison, performance study |
| **Extended** (added in planning) | Triangles alongside spheres, Embree baseline, GPU memory modes (`managed`/`system`), wavefront pipeline, `obj2bin` + Blender export |
| **Stretch** (go/no-go at check-in 3) | OptiX RT-core backend, mirror reflections, GPU BVH build, SIMD packet tracing |

Fallback (reduced scope): one fixed scene of spheres + floor, one light, shadows, 640×480. Keeps parallel CPU prep, GPU rendering, quality checks, and single/multicore comparison.

## Milestones

| Date | Milestone | What exists |
|---|---|---|
| Sept 24–25 | Contract meeting | [INTERFACES.md](docs/INTERFACES.md) signed off |
| Sept 29 | Check-in 1 | Shared inputs agreed; Vista CPU+GPU access verified; repo builds |
| Oct 26–30 | Check-in 2 | CPU reference + first CPU/GPU image; benchmark scenes fixed; initial timings |
| Nov 16–20 | Check-in 3 | Full renderer, multicore comparison, preliminary results; scope frozen; OptiX go/no-go |
| Nov 30 | Ready | Final runs, quality review, slides |
| Dec 1/3 | Presentation | |
| Dec 6 | Release | Report and reproducible release |

## Documentation

| File | Contents |
|---|---|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Modules, data flow, backends, megakernel vs wavefront, CLI, references |
| [INTERFACES.md](docs/INTERFACES.md) | Shared structs, function signatures, file formats, precision rules |
| [BUILD.md](docs/BUILD.md) | WSL2 and Vista setup, CMake options, compilers, dependencies |
| [BENCHMARKING.md](docs/BENCHMARKING.md) | Methodology, timing, sweeps, quality protocol, plots |
| [WORKFLOW.md](docs/WORKFLOW.md) | Git branching, PRs, CI, issues, contribution log |
| [ROADMAP.md](docs/ROADMAP.md) | Tasks, owners, dependencies, milestones, risks |
| [STRETCH.md](docs/STRETCH.md) | OptiX, SIMD, reflections, GPU BVH build, extras |
| [REPO_SKELETON.md](docs/REPO_SKELETON.md) | Recommended directory layout and config file contents |
| [members/](docs/members/) | Per-member scope, deliverables, checklist, contribution log |
