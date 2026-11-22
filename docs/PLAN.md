# Plan (living)

What is known about the remaining work after G2, as of **Nov 22**. This is a working list, not a commitment: items get added, reordered, dropped, and expanded as results come in. The fixed parts are the deadlines in [ROADMAP.md](ROADMAP.md#gates-and-schedule) and the Core deliverable in the [README](../README.md#required-deliverable). Everything else here can change.

Where things stand: Core is built and verified. The CPU renderer, median and LBVH builders, and the CUDA megakernel are in, CPU and GPU images are byte-identical on the known and benchmark scenes, and there are first Vista measurements ([RESULTS.md](RESULTS.md)). What is left is making the numbers good, filling measurement gaps, and the final runs and report.

Rules for this file: edit it freely, keep it short, and move finished items to the "Done" list with the PR. Interface changes go in [INTERFACE_CHANGES.md](INTERFACE_CHANGES.md); [INTERFACES.md](INTERFACES.md) is frozen.

## Where the headline ratios stand

Preliminary, Vista GH200, 1M primitives, 1080p, LBVH (T_br, median of 5; [details](RESULTS.md)):

| Target | Now | Note |
|---|---:|---|
| ~10×: 1 core / best CPU | 29.8× | Met |
| ~2×: best CPU / GPU | 1.46× | Not met at 1M; met at 10K and 100K, from two thread counts only |
| ~20×: 1 core / GPU | 43.4× | Met |

The gap to 2× is preparation, not rendering. CPU preparation (bounds, build, layout) is about 47 ms of the GPU's 53.8 ms. The ratio is (P + 29.7) / (P + 6.3) for preparation time P, so P has to fall to about 17 ms. A faster kernel alone cannot get there (a free render still gives 1.65× at the current P).

## Work items

Roughly in the order I expect to do them. Issue numbers are the longer write-ups.

### Likely next

| Item | Issue | Why | Done when | Notes |
|---|---|---|---|---|
| LBVH build and layout scaling | #68 | Biggest lever on the 2× target. Build speeds up 6.7× from 1 to 72 threads; about 30 ms does not parallelize. Layout is fastest at 16 threads (9.1 ms) and slower at 72 (12.5 ms) | T_br at 1M drops toward ~17 ms of preparation, or the data says why it can't | First add per-stage timing inside the builder to name the 30 ms. Output must stay byte-identical at B = 1, 8, 72 and BVH = brute force must hold |
| T_app unattributed time | #67 | About 0.4 s of T_app at 1M is in no component; likely a second `validate_scene` per primitive path string | `unattributed_ms` at 1M is a few ms; images, hashes and error messages unchanged | Time the call first to confirm. Touches `scene_io` |
| Device tests without a driver | #73 | Toolkit-only machines (such as the Vista login node) fail instead of skipping | Device tests skip with no driver; a too-old driver still fails | Small |
| Measurement gaps | #70 | Ratios at 10K and 100K rest on two thread counts; no profiling; light-count sweep missing; GPU T_app is unstable at small sizes | Checklist in the issue | Profiling first checks whether the 5.6 ms kernel is worth tuning at all |

### Decisions still open

- **Which extensions to run (#71).** Candidates and what the data says:
  - **E3 parallel binned SAH:** addresses the tree-quality tradeoff. The median tree renders 2× faster than LBVH on the CPU and 2.6× on the GPU, but its serial build makes T_br 4.5× to 6× slower. Most directly tied to the findings.
  - **E1 memory modes:** the proposal's data-movement question. With explicit memory on the GH200, transfer is already about 0.3 ms at 1M, so there may be little to show. The comparison could still be informative.
  - **E2 Blinn-Phong, E4 Embree, E5 wavefront, E6 `obj2bin`:** all independent of the above. E4 would strengthen the "best CPU" tier. E5 depends on G3 and the profile.
  - **Core optimization only.** A valid choice if time is short.
  - Extensions must not block final runs and the report.
- **Light-count sweep (#70):** scene variants or a `--lights` override in `rt_render`. Decide before starting.
- **Interface additions** ([INTERFACE_CHANGES.md](INTERFACE_CHANGES.md)): IC2 (overflow flag in `BvhQuery`, #69) and IC3 (`AABB::empty()`, #4) are decided but not done. Do them when a PR touches the same code, or as one small PR.

### Later

- **Scenes (#72):** download without `gh`, Vista mirror path, cross-platform hash check.
- **Profiling and tuning for G3:** `-Xptxas -v` (`RT_PTXAS_VERBOSE`), Nsight Compute, Nsight Systems.
- **Final runs (C14):** interleaved reruns of the 1-core baseline, best CPU, best GPU, and fixed-72 configurations, 5 runs each, on the laptop and Vista. Node-hour log rows for the jobs so far (1142824, 1142829, 1142842) through `scripts/tacc/node_hours.sh`.
- **Quality review:** side-by-side review by 2 or more people ([BENCHMARKING.md](BENCHMARKING.md#quality-protocol)) and the report's validation section.
- **Report, slides, release:** reproducible release with the scene archive, commit hash, and exact commands. Dates in the ROADMAP schedule.

## Things to watch

- **Numbers before changes.** Optimizations to Core code must keep CPU and GPU byte-identical and the goldens unchanged. `scripts/tacc/verify.sh` is the quick check on a laptop or Vista node.
- **Noise.** The sweep records spread. GPU T_app has 13% to 205% spread depending on size. Don't quote a ratio whose spread is above 10% without a rerun.
- **Contraction is off project-wide.** Any new numeric code is compiled with `-ffp-contract=off` and `--fmad=false`. Write `fmaf()` where a fused result is wanted.
- **Node hours.** Vista jobs go in the node-hour log ([BENCHMARKING.md](BENCHMARKING.md#node-hour-budget)).

## Done since G2

| Item | PR |
|---|---|
| CUDA megakernel backend, `DeviceScene`, contraction off project-wide | #65 |
| Vista sweep, verification and node-hour scripts; first Vista results | #66 |
