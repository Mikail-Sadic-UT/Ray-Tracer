# Plan (living)

What is known about the remaining work after G2, as of **Nov 23**. This is a working list, not a commitment: items get added, reordered, dropped, and expanded as results come in. The fixed parts are the deadlines in [ROADMAP.md](ROADMAP.md#gates-and-schedule) and the Core deliverable in the [README](../README.md#required-deliverable). Everything else here can change.

Where things stand: Core is built and verified. The CPU renderer, median and LBVH builders, and the CUDA megakernel are in, CPU and GPU images are byte-identical on the known and benchmark scenes, and there are first Vista measurements ([RESULTS.md](RESULTS.md)). What is left is making the numbers good, filling measurement gaps, and the final runs and report.

Rules for this file: edit it freely, keep it short, and move finished items to the "Done" list with the PR. Interface changes go in [INTERFACE_CHANGES.md](INTERFACE_CHANGES.md); [INTERFACES.md](INTERFACES.md) is frozen.

## Where the headline ratios stand

Preliminary, Vista GH200, 1M primitives, 1080p, LBVH (T_br, median of 5; commit `13321a5`, job 1164735; [details](RESULTS.md)):

| Target | Now | Note |
|---|---:|---|
| ~10×: 1 core / best CPU | 56.3× | Met (was 29.8× at G2) |
| ~2×: best CPU / GPU | 2.40× | **Met at 1M** (was 1.46×); 5.9× at 10K and 3.7× at 100K, whose GPU rows have spread above 10% |
| ~20×: 1 core / GPU | 135× | Met (was 43.4×) |

The 2× target was missed at G2 because of preparation, not rendering (47 ms of the GPU's 53.8 ms). PR #75 (#68, #67) cut preparation to 10.0 ms at B = 72, and the Nov 23 Vista rerun measures 2.40× at 1M ([RESULTS.md](RESULTS.md#after-the-preparation-work-commit-13321a5-nov-23)). Layout (6.8 ms, grows past 16 threads) is now the largest preparation stage; at about 2 ms the ratio would be near 3×.

## G3 checklist

G3 is an internal checkpoint (target Nov 18, now past; [ROADMAP](ROADMAP.md#gates-and-schedule)). Its exit criteria and where each stands:

| Criterion | Status | Left to do |
|---|---|---|
| Preparation scaling, B = 1 to 72 | Done at all three sizes ([RESULTS.md](RESULTS.md)) | |
| CPU render scaling | Done at all three sizes (70× on 72 threads at 1M) | |
| GPU breakdown | T_br components measured (alloc, upload, render, download, kernel) | Kernel-level view needs profiling |
| Profiling (`-Xptxas -v`, Nsight) | Not started | #70 |
| Preliminary 10× / 2× / 20× ratios, with spread | Done, all three met at 1M (Nov 23) | 10K and 100K GPU T_br rows need a rerun (spread above 10%); final numbers come from C14 |
| Scope and extensions reviewed | Open | #71 |

## Work items

Roughly in the order I expect to do them. Issue numbers are the longer write-ups.

### Likely next

| Item | Issue | Why | Done when | Notes |
|---|---|---|---|---|
| Layout stage of the LBVH build | #68 | Now the largest preparation stage: 6.8 ms at B = 72, up from 3.6 ms at B = 16 | Optional: P near 5.6 ms (ratio near 3×) | Mostly emit and freeing the scratch. Emit is probably bound by the serial zero fill of `Bvh::nodes` (review of #75: emit 7.0 ms at B = 3, 9.3 at 8, 9.3 to 10.8 at 24 on the laptop); avoiding it means `Bvh::nodes` stops being a value-initialized `std::vector`, which is an interface change (an IC entry). Other ideas: a fused bottom-up (Apetrei-style) hierarchy + refit, a smaller temporary node, reusing scratch. The 2× target is already met, so this is only worth doing if time is left |
| Measurement gaps | #70 | Light-count sweep missing; GPU T_app is unstable (spread 39% to 207%); no profiling; 10K and 100K GPU T_br rows have spread above 10% | Checklist in the issue | The thread sweeps at 10K and 100K are done. Profiling first checks whether the kernel is worth tuning at all (5.6 ms of the GPU's 16.4 ms) |

### Decisions still open

- **Which extensions to run (#71).** Candidates and what the data says. Sizes are the ROADMAP timeboxes; the owners listed there assumed a four-person team, so treat them as sizes only:
  - **E3 parallel binned SAH (1 week):** addresses the tree-quality tradeoff. The median tree renders 2× faster than LBVH on the CPU and 2.6× on the GPU, but its serial build makes T_br 8.9× (CPU) and 20× (GPU) slower after #68. The most a better tree can save is about 3.5 ms of GPU render, so SAH helps GPU T_br only if its build costs less than that on top of LBVH; the case rests on T_frame and the CPU render.
  - **E1 memory modes (4 days):** the proposal's data-movement question. With explicit memory on the GH200, transfer is already about 0.3 ms at 1M, so there may be little to show. The comparison could still be informative.
  - **E2 Blinn-Phong (2 days), E4 Embree (4 days), E5 wavefront (1 week), E6 `obj2bin` + Blender export (3 days):** all independent of the above. E4 would strengthen the "best CPU" tier. E5 and E6 start only after G3, and E5 depends on the profile. ROADMAP expected E1 to E3 to fit and the rest to be cut.
  - **Core optimization only.** A valid choice if time is short.
  - Extensions must not block final runs and the report.
- **Light-count sweep (#70):** scene variants or a `--lights` override in `rt_render`. Decide before starting.

### Later

- **Scenes (#72):** download without `gh`, Vista mirror path, cross-platform hash check.
- **Profiling and tuning for G3:** `-Xptxas -v` (`RT_PTXAS_VERBOSE`), Nsight Compute, Nsight Systems.
- **Final runs (C14):** interleaved reruns of the 1-core baseline, best CPU, best GPU, and fixed-72 configurations, 5 runs each, on the laptop and Vista. Node-hour log rows for the jobs so far (1142824, 1142829, 1142842, 1164735) through `scripts/tacc/node_hours.sh`.
- **Quality review:** side-by-side review by 2 or more people ([BENCHMARKING.md](BENCHMARKING.md#quality-protocol)) and the report's validation section.
- **Report, slides, release:** reproducible release with the scene archive, commit hash, and exact commands. Dates in the ROADMAP schedule.

## Parked

Not planned. Listed so they are not lost; each can be pulled into the plan if its trigger happens.

- **Stretch** ([STRETCH.md](STRETCH.md)): OptiX backend, SIMD packet tracing, mirror reflections, GPU-side LBVH build, extra experiments. The GPU-side build would attack the 47 ms preparation directly, but it is large. Look at it only if #68 finds the CPU build cannot get near 17 ms.
- **Deferred items** ([ROADMAP](ROADMAP.md#deferred-designed-not-required)), each with its own trigger:
  - extra `rt_compare` checks and dense-mesh corruption tests: a visibly wrong image passes `rt_compare`;
  - exact goldens in a pinned CI container: tolerance goldens miss a regression;
  - `RT_COUNTERS` traversal counters: traversal cost needs explaining in the report;
  - `--no-components` overhead check: instrumentation seems to distort small scenes;
  - `field_clustered` and seed-2 scenes: time remains (also in #70);
  - heavier review and CI process: the problem it solves actually happens.

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
| Vista rerun of the branch: `verify.sh` 0 failures, ctest 307/307, `core` sweep at 46 points (job 1164735, data in `docs/data/`) | #75 |
| LBVH preparation: parallel centroids, Morton codes and radix sort, uninitialized scratch, intrinsic leading-zero count, layout counts in the refit; per-stage timing with `RT_BVH_STAGES=1`. Bytes unchanged (laptop) | `mikail/lbvh-scaling` (#68) |
| `validate_scene` builds error paths only on failure; `rt_render` re-checks only camera and light limit; scratch freed inside `layout` | `mikail/lbvh-scaling` (#67) |
| Device tests and `rt_render_cuda_cli` skip when no NVIDIA driver is installed (checked with a stand-in `libcuda`) | `mikail/lbvh-scaling` (#73) |
| IC2 overflow flag in `BvhQuery` (closes W8) and IC3 `AABB::empty()` and constants in `math.hpp` | `mikail/lbvh-scaling` (#69, #4) |
| Thread sweeps at every scene size | `mikail/lbvh-scaling` (#70, first item) |
