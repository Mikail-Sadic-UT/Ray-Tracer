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

The gap to 2× is preparation, not rendering. CPU preparation (bounds, build, layout) is about 47 ms of the GPU's 53.8 ms. The ratio is (P + 29.7) / (P + 6.3) for preparation time P, so P has to fall to about 17 ms. A faster kernel alone cannot get there (a free render still gives 1.65× at the current P). On the laptop, the #68 work cut 1M preparation from about 111 ms to 29 ms at B = 24 and from 306 ms to 156 ms at B = 1, so all three ratios will move once Vista is rerun ([RESULTS.md](RESULTS.md#changes-since-these-runs-laptop-only-vista-rerun-pending)).

## G3 checklist

G3 is an internal checkpoint (target Nov 18, now past; [ROADMAP](ROADMAP.md#gates-and-schedule)). Its exit criteria and where each stands:

| Criterion | Status | Left to do |
|---|---|---|
| Preparation scaling, B = 1 to 72 | Done at 1M ([RESULTS.md](RESULTS.md)) | 10K and 100K have only 1 and 72 threads (#70) |
| CPU render scaling | Done at 1M (69× on 72 threads) | Same gap at 10K and 100K (#70) |
| GPU breakdown | T_br components measured (alloc, upload, render, download, kernel) | Kernel-level view needs profiling |
| Profiling (`-Xptxas -v`, Nsight) | Not started | #70 |
| Preliminary 10× / 2× / 20× ratios, with spread | Done, preliminary | Rerun after any optimization; final numbers come from C14 |
| Scope and extensions reviewed | Open | #71 |

## Work items

Roughly in the order I expect to do them. Issue numbers are the longer write-ups.

### Likely next

| Item | Issue | Why | Done when | Notes |
|---|---|---|---|---|
| Vista rerun of the branch | #68, #67, #73, #70 | The #68 and #67 changes are measured only on the laptop | `verify.sh` passes, `core` sweep rerun, RESULTS updated with the new ratios | Check on Vista: T_br at 1M (target P near 17 ms at B = 72), the device tests skip on the login node (#73), and T_app unattributed time is a few ms (#67). The `core` preset now has 46 points |
| More LBVH preparation, if Vista needs it | #68 | Depends on the Vista numbers | P near 17 ms, or the data says why not | At B = 24 on the laptop the remaining time is emit 9 to 14 ms (it includes the serial zero fill of `Bvh::nodes`), refit 6, freeing scratch 5 to 7, hierarchy 3.5. Ideas: a fused bottom-up (Apetrei-style) hierarchy + refit, a smaller temporary node, reusing scratch |
| Measurement gaps | #70 | Light-count sweep missing; GPU T_app is unstable at small sizes; no profiling | Checklist in the issue | Thread sweep at 10K and 100K is now in the `core` preset. Profiling first checks whether the kernel is worth tuning at all |

### Decisions still open

- **Which extensions to run (#71).** Candidates and what the data says. Sizes are the ROADMAP timeboxes; the owners listed there assumed a four-person team, so treat them as sizes only:
  - **E3 parallel binned SAH (1 week):** addresses the tree-quality tradeoff. The median tree renders 2× faster than LBVH on the CPU and 2.6× on the GPU, but its serial build makes T_br 4.5× to 6× slower. Most directly tied to the findings.
  - **E1 memory modes (4 days):** the proposal's data-movement question. With explicit memory on the GH200, transfer is already about 0.3 ms at 1M, so there may be little to show. The comparison could still be informative.
  - **E2 Blinn-Phong (2 days), E4 Embree (4 days), E5 wavefront (1 week), E6 `obj2bin` + Blender export (3 days):** all independent of the above. E4 would strengthen the "best CPU" tier. E5 and E6 start only after G3, and E5 depends on the profile. ROADMAP expected E1 to E3 to fit and the rest to be cut.
  - **Core optimization only.** A valid choice if time is short.
  - Extensions must not block final runs and the report.
- **Light-count sweep (#70):** scene variants or a `--lights` override in `rt_render`. Decide before starting.

### Later

- **Scenes (#72):** download without `gh`, Vista mirror path, cross-platform hash check.
- **Profiling and tuning for G3:** `-Xptxas -v` (`RT_PTXAS_VERBOSE`), Nsight Compute, Nsight Systems.
- **Final runs (C14):** interleaved reruns of the 1-core baseline, best CPU, best GPU, and fixed-72 configurations, 5 runs each, on the laptop and Vista. Node-hour log rows for the jobs so far (1142824, 1142829, 1142842) through `scripts/tacc/node_hours.sh`.
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
| LBVH preparation: parallel centroids, Morton codes and radix sort, uninitialized scratch, intrinsic leading-zero count, layout counts in the refit; per-stage timing with `RT_BVH_STAGES=1`. Bytes unchanged (laptop) | `mikail/lbvh-scaling` (#68) |
| `validate_scene` builds error paths only on failure; `rt_render` re-checks only camera and light limit; scratch freed inside `layout` | `mikail/lbvh-scaling` (#67) |
| Device tests and `rt_render_cuda_cli` skip when no NVIDIA driver is installed (checked with a stand-in `libcuda`) | `mikail/lbvh-scaling` (#73) |
| IC2 overflow flag in `BvhQuery` (closes W8) and IC3 `AABB::empty()` and constants in `math.hpp` | `mikail/lbvh-scaling` (#69, #4) |
| Thread sweeps at every scene size | `mikail/lbvh-scaling` (#70, first item) |
