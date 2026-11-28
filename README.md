# Hybrid Ray Tracer

ECE 379K project. A ray tracer for spheres and triangles that splits work between CPU and GPU. The CPU builds the acceleration structure (BVH) in parallel. The GPU renders visibility, lighting, and hard shadows. We measure the speedup over single-core and multicore CPU rendering, and identify when preparation and CPU-GPU data movement limit it.

| Member | EID | Proposal role | Member file |
|---|---|---|---|
| Jett Vanbrocklin | JWV493 | Member 1 | [JETT.md](docs/members/JETT.md) |
| Mikail Sadic | MFS2447 | Member 2 | [MIKAIL.md](docs/members/MIKAIL.md) |
| Arek Plate | AWP688 | Member 3 | [AREK.md](docs/members/AREK.md) |
| Po Han Ai | PA22435 | Member 4 | [PO.md](docs/members/PO.md) |

**Status (Nov 27):** Core is built, verified and measured. The CPU and CUDA renderers produce byte-identical images on every benchmark and extra scene, on the laptop and on a Vista GH node. On Vista at 1M primitives the best multicore CPU is 56× faster than one core for build-and-render, the GPU is 2.43× faster than the best CPU and 137× faster than one core, and all three targets of the proposal hold at 10K, 100K and 1M ([RESULTS.md](docs/RESULTS.md); the laptop run is in the same file). The benchmark scenes are published as `scenes-v1` (`bash scripts/fetch_scenes.sh scenes-v1`). Interfaces are frozen and the workflow rules are relaxed (see [WORKFLOW.md](docs/WORKFLOW.md), "Post-G2 note"). The remaining work is the report, the slides and the release, listed in [PLAN.md](docs/PLAN.md#for-the-report-and-slides). See [ROADMAP.md](docs/ROADMAP.md#status).

## Required deliverable

1. Spheres + triangles, Lambert lighting with point lights, hard shadows.
2. Serial median BVH (correctness baseline) and one correct parallel builder. LBVH is preferred; a task-parallel median builder is the accepted alternative (decided Nov 10).
3. CPU rendering on one core and on many cores.
4. A CUDA megakernel that matches the CPU under the [quality protocol](docs/BENCHMARKING.md#quality-protocol).
5. Validation: analytic tests, independent oracle, BVH vs brute force, image/AOV comparison.
6. Measurements: application, build-and-render, and prepared-frame times, with scaling and breakdowns.

Extensions: Blinn-Phong highlights and directional lights (E2), mirror reflections, colored glass and `obj2bin` (E6) landed. Memory modes (E1), SAH (E3), Embree (E4) and the wavefront pipeline (E5) were cut on Nov 27 for lack of time ([PLAN.md](docs/PLAN.md#decisions)). The landed ones: see [`known_specular`](scenes/known/known_specular.json), [`known_mirror`](scenes/known/known_mirror.json), [`known_glass`](scenes/known/known_glass.json) (with tinted shadows) and [`known_showcase`](scenes/known/known_showcase.json), which has all of them. `obj2bin` puts OBJ meshes such as the Stanford dragon into a scene ([user guide](docs/USER_GUIDE.md#render-an-obj-mesh)). OptiX and the other [stretch items](docs/STRETCH.md) are future work.

## Goals (from proposal)

Hypotheses to evaluate, all measured with the same workload and the same timing definition (T_br, [defined here](docs/BENCHMARKING.md#timing-quantities)):

| Comparison | Target |
|---|---|
| Fastest tested multicore CPU vs 1 core (build and render threads both 1) | ~10× |
| GPU vs fastest tested multicore CPU (best thread count, not assumed 72) | ~2× |
| GPU vs 1 core, measured directly | ~20× |

Fixed 72-thread comparisons are reported alongside, labeled separately ([details](docs/BENCHMARKING.md#targets-from-proposal)).

Output 1920×1080. Median of 5 runs, with spread reported. Quality: no visible differences in side-by-side review by 2+ reviewers (the target of the proposal; the review done on Nov 27 had one reviewer, see [BENCHMARKING.md](docs/BENCHMARKING.md#3-side-by-side-review)), backed by automated checks.

## Platforms

| Machine | CPU | GPU | Notes |
|---|---|---|---|
| TACC Vista GH node (primary) | Grace, aarch64, 72 Neoverse-V2 cores | NVIDIA GH200 120GB (Hopper, sm_90, 132 SMs), no RT cores | NVLink-C2C |
| Laptop (Mikail's), WSL2 Ubuntu 24.04 | i9-13980HX, 8P + 16E | RTX 4070 Laptop, sm_89 | PCIe |
| Fallback: Lonestar6 | AMD EPYC | A100, sm_80 | Only if Vista fails |

Offline CLI renderer. No native Windows build.

## Schedule

| Date | Gate / event | Exit criteria (summary) |
|---|---|---|
| Oct 27 | Contract sign-off | [INTERFACES.md](docs/INTERFACES.md) v1.0 accepted |
| Oct 30 | **G0** | Builds on WSL2 + Vista, CI green, brute-force CPU image |
| Nov 6 | **G1** | Correct CPU renderer, median BVH, multicore, generator, `rt_compare`, CPU timings, benchmark scenes fixed |
| Nov 12 | Check-in 2 (postponed) | G1 complete, builder decision (Nov 10), G2 progress; see [ROADMAP.md](docs/ROADMAP.md#gates-and-schedule) |
| Nov 13 | **G2** | CUDA matches CPU; correct parallel builder (LBVH or task-parallel median); first Vista timings. Progress target; scope reviewed by the team |
| Nov 18 | **G3** (internal target) | Scaling, breakdowns, profiling, preliminary ratios; extensions chosen |
| Nov 30 | Ready | Final runs, review, slides |
| Dec 1/3 | Presentation | |
| Dec 6 | Release | Report, reproducible release |

There is no check-in 3. G2/G3 are internal progress targets, and Nov 13 does not automatically trigger scope reduction. The team will review progress and available time, including work during the break, while keeping the final deadlines in view.

## Quick start

For a full walkthrough of rendering options, scene generation, image comparison, and timing, see the **[User guide](docs/USER_GUIDE.md)**.

On WSL2 Ubuntu 24.04, install `g++-13`, CMake 3.28+, and `ninja-build`. Run the commands below from the repository root. Full setup, including CUDA and Vista: [BUILD.md](docs/BUILD.md).

### Compile the CPU renderer

Configure the build, then compile. The first configure downloads the pinned dependencies, so it needs internet access. No GPU or CUDA toolkit is needed for this build.

```bash
cmake -S . -B build-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DRT_ENABLE_CUDA=OFF
cmake --build build-cpu -j 8
```

After editing code, rerun `cmake --build build-cpu -j 8` to rebuild the affected files. To run the tests:

```bash
ctest --test-dir build-cpu --output-on-failure
```

### Render the two-sphere scene

This uses the CPU renderer, median BVH, lighting and hard shadows, with four render threads:

```bash
mkdir -p out
./build-cpu/apps/rt_render/rt_render \
  --scene scenes/known/known_two_spheres.json \
  --backend cpu --bvh median --shade lit \
  --render-threads 4 \
  --out out/two_spheres.png
```

Open `out/two_spheres.png` to see the red and blue spheres on a floor with shadows. From WSL, `explorer.exe out` opens the output folder in Windows Explorer. The camera and resolution come from the scene JSON.

Use `--shade normal` to visualize surface directions, or `--width 1920 --height 1080` to override the resolution. `--lights N` renders with N copies of the scene's light, and `--orbit N --frames DIR` renders N frames around the scene; `scripts/make_video.sh` turns an orbit into an MP4 ([user guide](docs/USER_GUIDE.md#camera-orbit-and-video)). For a point-and-click version, `python3 scripts/gui.py` opens a small window that sets these options, renders, and shows the image and timings ([user guide](docs/USER_GUIDE.md#render-from-a-gui)). See all options with:

```bash
./build-cpu/apps/rt_render/rt_render --help
```

## How to use these docs

Each member works on their own branch, often through a coding agent. The parts fit together because everyone codes against the same contract.

1. **Start with your member file** ([members/](docs/members/)). Its "Start here" lists what you build, what to read, what you hand off to whom, and what to use until a dependency lands. Its **agent brief** can be pasted straight into your agent, together with `docs/INTERFACES.md`.
2. **[INTERFACES.md](docs/INTERFACES.md) is the contract.** Types, signatures, and numeric rules there are what make the parts slot together. If one looks wrong during implementation, change it through a small interface PR approved by the affected owners. Don't work around it locally.
3. **Everything else is reference.** Read the other docs when your task touches them. Items marked *deferred* were designed during review but aren't required; pick one up only if its trigger happens ([list](docs/ROADMAP.md#deferred-designed-not-required)).

Expect to find gaps during implementation. Fix them where they show up, and keep INTERFACES.md current when they affect someone else.

## Documentation

| File | Contents |
|---|---|
| [USER_GUIDE.md](docs/USER_GUIDE.md) | Compile, render, change settings, inspect/generate scenes, compare images, collect timings, and troubleshoot |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Data flow, ownership vs views, backend lifecycle, GPU pipelines, memory modes, CLI |
| [INTERFACES.md](docs/INTERFACES.md) | Types, frozen numeric rules, BVH pipeline, file formats, results schema. Frozen at v1.0 (Nov 22) |
| [PLAN.md](docs/PLAN.md) | Living list: decisions, what is left for the report and slides, where every result and image lives, what to watch |
| [INTERFACE_CHANGES.md](docs/INTERFACE_CHANGES.md) | Interface additions and changes since the v1.0 freeze, and the rules for recording them |
| [CONTRACT_NOTES.md](docs/CONTRACT_NOTES.md) | Known gaps in INTERFACES that don't need a change yet, with triggers and owners |
| [BUILD.md](docs/BUILD.md) | WSL2 and Vista setup, pinned toolchains, CMake options, flags |
| [RESULTS.md](docs/RESULTS.md) | Vista and laptop measurements (Nov 27), ratios against the proposal's targets, the GH200 profile, the extra benchmarks, and the earlier Nov 23 and G2 runs |
| [BENCHMARKING.md](docs/BENCHMARKING.md) | Timing quantities, threads, targets, sweeps, memory-mode protocol, quality protocol |
| [WORKFLOW.md](docs/WORKFLOW.md) | Branching, commit messages, development environment check, PRs, CI, issues, gates |
| [ROADMAP.md](docs/ROADMAP.md) | Scope, gates, owners, tasks, risks, status |
| [STRETCH.md](docs/STRETCH.md) | OptiX, SIMD, reflections, GPU BVH build |
| [REPO_SKELETON.md](docs/REPO_SKELETON.md) | Recommended layout and config file contents |
| [members/](docs/members/) | Per-member scope, deliverables, checklist, contribution log |
