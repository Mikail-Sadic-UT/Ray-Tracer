# Benchmarking

What we measure, how we compare, and how we check that images are correct. Field names are defined in [INTERFACES.md](INTERFACES.md#8-timing-and-results-schema).

## Timing quantities

Three quantities, each measured directly as elapsed wall time. Components explain each total; they are not assumed to add up to it.

| Quantity | Starts | Ends | Includes | How measured |
|---|---|---|---|---|
| **T_app**: cold application time | First statement of `main()` | Final image (and AOVs) in host memory | Argument parsing, file load, CUDA context init, all preparation, render, download | One per fresh process; 5 processes, median |
| **T_br**: build-and-render time | Start of preparation (scene loaded as `SceneHost`, CUDA context initialized, backend constructed) | Image in host memory | `bounds`, `build`, `layout`, `convert`, `alloc`, `upload`, `render`, `download` | In process: 1 discarded warm-up, then 5 runs, median. Each run releases and fully re-prepares. |
| **T_frame**: prepared-scene frame time | Start of `render()` | Image in host memory | `render`, `download` | In process after preparation: 1 warm-up, then 5 runs, median |

Excluded from all three: image `write`, OS process launch, and dynamic loading before `main()`. T_app also includes file load, which T_br excludes.

**T_frame is required.** A static scene does not need its BVH rebuilt when only the camera changes, so this measures what an interactive user would see after the first image.

Rules:

- Report median, min, max, and `spread = (max − min) / median` for every quantity. Spread > 10% → rerun and investigate. Don't just average it away.
- The run whose total is the median supplies the component breakdown shown in plots (`median_run_index`). Never sum component medians.
- `unattributed = total − Σ components` is reported. More than 5% of total means instrumentation is missing something; investigate it.
- Stage-boundary `cudaDeviceSynchronize` calls are part of the instrumented run. Once per configuration, compare against an uninstrumented total (`--no-components`) to confirm the overhead is small.
- File cache: we can't drop OS caches on Vista, so T_app is "cold process, warm file cache". The report states this.

## Threads

Preparation and rendering thread counts are set separately:

```
--build-threads B --render-threads R
```

| Configuration | B | R | Meaning |
|---|---|---|---|
| CPU 1-core baseline | 1 | 1 | Everything on one core. No parallel preparation. |
| CPU multicore | N | N | Parallel preparation and rendering |
| GPU | N | n/a (GPU) | Parallel CPU preparation, GPU render |

Each phase uses OpenMP `num_threads(B)` or `num_threads(R)` explicitly, with `OMP_PROC_BIND=close OMP_PLACES=cores` so k threads use the first k cores. Embree uses one internal thread count (set from R) for both its build and its traversal; this is noted wherever Embree appears.

## Targets (from proposal)

All three targets use **T_br**, the same scene, and the same image settings. Each ratio is measured directly. Ratios are never multiplied together.

The proposal compares the GPU against the **fastest tested** multicore CPU, which isn't necessarily the 72-thread run. If 32 or 64 threads is faster, using 72 would overstate the GPU speedup.

- **CPU\***: the CPU configuration with the **lowest median of the quantity being reported** among all tested thread counts (B = R across the sweep). It's chosen separately for each scene size and each quantity, so CPU\* for T_br, T_frame, and T_app can be different thread counts.
- **GPU\***: the GPU configuration with the lowest median of the quantity being reported among the tested build-thread counts B, with the same per-size, per-quantity selection.
- Results record which configuration was selected for each quantity, and its spread.

**Selection and reporting use different runs.** Picking the minimum of several noisy medians biases the picked value low (the winner's curse). Both CPU\* and GPU\* are now selected this way, so the bias doesn't consistently favor either side. To keep it out of the reported numbers:

1. **Selection:** use the sweep data only to pick CPU\* and GPU\*.
2. **Confirmation:** in a separate job on one node, rerun the 1-core baseline, CPU\*, GPU\*, and the fixed-72 configurations. Use 5 runs each, interleaved in round-robin order so drift affects all of them equally.
3. **Report:** all headline and fixed-72 ratios come from the confirmation runs. The sweep numbers appear only in scaling plots.

| Target | Headline ratio | Also reported |
|---|---|---|
| ~10× multicore | T_br(CPU, B=1, R=1) / T_br(CPU\*) | Fixed: T_br(CPU, 1, 1) / T_br(CPU, 72, 72) |
| ~2× GPU | T_br(CPU\*) / T_br(GPU\*) | Fixed: T_br(CPU, 72, 72) / T_br(GPU, B=72) |
| ~20× overall | T_br(CPU, B=1, R=1) / T_br(GPU\*) | Fixed: T_br(CPU, 1, 1) / T_br(GPU, B=72) |

- The fixed-72 rows are labeled as such and never stand in for the headline "fastest CPU" claim.
- If confirmation reruns change which configuration is fastest, keep the selected one, report the discrepancy, and use the extra data to judge noise. Don't reselect from the confirmation data.
- B ≠ R combinations may be tested as extra points. If one beats every B = R point, it becomes CPU\* and the report says so.
- The same tables are reported for T_frame (render-focused) and T_app (end-to-end), labeled separately. These are hypotheses to evaluate, not predictions.

## Comparison tiers

| Tier | CPU side | GPU side | Same BVH? | Status |
|---|---|---|---|---|
| **Controlled** | Our CPU renderer | Our CUDA megakernel | Yes, identical `Bvh` | Core |
| **Best-possible** | Embree 4 | Best CUDA variant; OptiX if built | No | Extension / Stretch |

The proposal's targets are evaluated in the controlled tier.

## Fairness rules

Within a comparison, these are identical: scene hash, camera, resolution, lights, shading flags, BVH method and parameters (controlled tier), node, compiler and flags, thread pinning. The CPU reference is never weakened, and missed targets are reported with explanations.

## Scene sizes

Measured in **primitives**. Fixed at gate G1 (Nov 6), and presented at check-in 2:

| Name | Spheres | Triangles | Primitives | Seed |
|---|---|---|---|---|
| `known_*` | 1–10 | 2–20 | ≤ 30 | n/a (hand-written) |
| `bench_10k` | 5,000 | 5,000 (terrain + floor) | 10,000 | 1 |
| `bench_100k` | 50,000 | 50,000 | 100,000 | 1 |
| `bench_1m` | 500,000 | 500,000 | 1,000,000 | 1 |

Every results file records the scene hash and the exact `rt_gen` command. Icospheres, if used, are counted by their triangles (20·4^k each).

## Sweeps

Core sweeps, varying one dimension at a time around a baseline. Baseline: `bench_1m`, 1920×1080, 1 point light, `lbvh`, `mega`, `explicit`, B = R = max.

| Dimension | Values | Machines |
|---|---|---|
| B and R together | 1, 2, 4, 8, 16, 32, 64, 72 | Vista |
| B and R together | 1, 2, 4, 8, 16, 24, 32 | Laptop (P/E-core caveat) |
| B alone (GPU runs) | 1, 8, 72 | Vista |
| Scene size | 10K, 100K, 1M | Both |
| BVH method | `median`, `lbvh` (+ `sah` if built) | Both |
| Light count L | 1, 2, 4 | Both |
| Resolution | 640×480, 1920×1080 | Both |

Extensions add their own dimensions: memory modes, pipeline, Embree.

## Machine identity

Record from the allocated node, not from documentation:

- GPU: `nvidia-smi --query-gpu=name,uuid,driver_version,memory.total --format=csv`, plus `cudaGetDeviceProperties` name and SM count. Vista's GH nodes are documented as both H100 and H200 in different places; the docs say "Hopper (sm_90)", and the report uses the recorded name.
- CPU: `lscpu` model and core count.
- Toolchain: `gcc --version`, `nvcc --version`, loaded modules (`module list`).

## Thread pinning

```bash
export OMP_PROC_BIND=close
export OMP_PLACES=cores
```

- Run CPU and GPU comparisons on the same GH node.
- Laptop: the i9-13980HX has 8 P-cores (16 threads) and 16 E-cores, so scaling bends past 8–16 threads. Report as-is. Plug in and use the maximum performance profile.

## Link bandwidth

Measure once per machine with `bandwidthTest` (CUDA samples), pinned and pageable, both directions:

| Machine | Link | Measured |
|---|---|---|
| Laptop 4070 | PCIe (likely 4.0 ×8) | TBD |
| Vista GH node | NVLink-C2C | TBD |

Memory modes are compared as ratios within one machine. Cross-machine differences also include GPU architecture, memory bandwidth, and cache differences.

## Memory-mode protocol (Extension E1, GH node only)

Unified memory placement depends on who first touched the pages, prefetches, advice, and driver migration policy. Without recording these, warm and cold runs can silently measure different data placement.

| Recorded field | Values |
|---|---|
| `mode` | `explicit`, `managed`, `system`, `system-reuse` |
| `realloc_per_prepare` | `true` for T_br in `explicit`, `managed`, `system`; `false` for T_frame (buffers from the last preparation are reused) |
| `geometry_reused` | `false` for `explicit`, `managed`, `system`: every preparation allocates fresh memory and copies the scene and BVH into it. `true` for `system-reuse`: views into the persistent `SceneHost`/`Bvh` vectors, no copy. |
| `fresh_pages` | `system` only: `mmap`/`munmap` per preparation, so the C library allocator can't hand back pages a previous run may have migrated to the GPU |
| First touch | CPU, for all fresh modes: the copy into the new buffer is a CPU write. Under `system-reuse`, first touch happened once at load time, and later placement depends on migration. |
| `prefetch` | `managed` only: `cudaMemPrefetchAsync` to the GPU, timed as `upload` |
| `advice` | `none`, `preferred-gpu`, `read-mostly` (`cudaMemAdvise`) |
| Residency note | Under `system` and `system-reuse`, pages may migrate to GPU memory over repeated accesses, depending on driver configuration. T_frame runs record frame-by-frame times to expose that drift. **Verify on Vista** how to query the migration settings. |

Comparisons:

- `system` vs `explicit` (both fresh per preparation) compares CPU-resident data accessed by the GPU against an explicit copy into GPU memory.
- `system-reuse` is reported as its own experiment. It shows what happens when geometry stays resident across preparations, which is a different question, and it's never presented as a T_br result for `system`.
- The `system` copy into fresh pages is timed as `upload`, so `system` T_br includes it. It's a host-side memcpy, not a transfer. The report states this.

## Node-hour budget

About 50 node-hours. Develop on `gh-dev`. Production sweeps on `gh`. Mikail keeps the log:

| Date | Who | Queue | Job ID | Nodes × hours | Purpose | Running total |
|---|---|---|---|---|---|---|
| | | | | | | |

## Quality protocol

Shared CPU/GPU code proves the backends are **consistent**, not that they're **correct**. A shared intersection or shading bug makes both backends agree perfectly. Correctness therefore comes from checks that don't reuse the code under test.

### 1. Independent correctness checks (CI)

- **Analytic known cases**, with expected values computed by hand or in Python by someone other than the function's author:
  - visibility: expected `prim_id` at chosen pixels;
  - Lambert value at a sphere's pole and at a floor point;
  - 1/d² falloff;
  - shadow boundary location of a sphere on the floor;
  - no shadow acne on an unoccluded plane.
  - Tolerance: relative 1e-5 on linear values.
- **Double-precision oracle** (test-only code, written by Arek, not Mikail): ray-sphere and ray-triangle in `double`, compared with the float versions on random rays. Hits must agree except within a documented distance of edges and silhouettes.
- **BVH vs brute force**: identical `Hit` (same `prim`, bit-identical `t`) on random rays and scenes.
- **Determinism**: CPU 1 thread vs N threads, repeated BVH builds, and repeated GPU runs are bit-identical (linear PFM and AOVs).

### 2. Image comparison (`rt_compare`)

Inputs: linear PFM plus the `prim_id` and `light_mask` AOVs from both images. All checks run on **linear floats before clamping**.

| Check | Rule | Failure means |
|---|---|---|
| Nonfinite | Zero NaN/Inf pixels in either image | Hard fail |
| Primary-hit agreement | Pixels with different `prim_id` must be reference **edge pixels** (a 4-neighbor has a different `prim_id`) | Non-edge mismatch → fail |
| Shadow agreement | Pixels with same `prim_id` but different `light_mask` must be reference shadow-edge pixels (a 4-neighbor has a different mask) | Non-edge mismatch → fail |
| Shading agreement | Pixels with same `prim_id` and `light_mask`: `|a − b| ≤ τ_abs + τ_rel·|ref|` per channel | Any failure → fail |
| Localization (**provisional**) | Per 16×16 tile, count of edge mismatches; flag tiles above a limit (starting guess: 8) | **Warning only until calibrated** |
| Broad summary | ≥ 99.9% of pixels within 2/255 in 8-bit sRGB | Reported; regression signal, not the main gate |

- Start values: `τ_abs = 1e-5`, `τ_rel = 1e-4`. They get set at gate G2 from measured CPU-vs-GPU data, and the chosen values and evidence are documented.
- **The localization rule is an unvalidated heuristic.** A legitimate thin boundary, such as a near-grazing silhouette or a long shadow edge crossing a tile, can exceed any fixed per-tile count. Until G2 it's reported as a warning, with the flagged tiles in the heatmap. At G2, look at the distribution of per-tile counts from real CPU/GPU pairs on known scenes and `bench_100k`. Then either set a limit above the observed legitimate maximum (and make it a failure), or keep it warning-only. Document the decision and its data. Every flagged tile is inspected by eye regardless.
- **No per-feature loosening.** If highlights or any other feature exceed tolerance, find the cause (for example, an FMA contraction in a `pow` argument) before changing thresholds.
- Output: counts per category, max errors, and a heatmap that marks each category.

### 3. Side-by-side review

At least 2 reviewers, native resolution, same display, per the proposal. Focus on edges and shadow boundaries. Record who reviewed what.

### 4. Approximate modes

Anything approximate is reported separately with its own speed and quality. The verified mode stays the default.

## Planned plots

| Plot | Owner | Quantity |
|---|---|---|
| CPU scaling: preparation and render, speedup vs threads | Jett (prep), Po (render) | T_br components |
| Target ratios (10× / 2× / 20×) | Arek | T_br; T_frame and T_app alongside |
| Preparation vs frame share, by scene size | Jett, Arek | T_br components |
| Cold vs prepared: T_app, T_br, T_frame per backend | Arek | All three |
| Build vs render time per BVH method | Jett | T_br components |
| Light-count sweep | Arek | T_frame |
| Extensions: memory modes, mega vs wavefront, tiers, OptiX | Owners | Per extension |

Every plot caption states the quantity, scene hash, machine name, and thread counts.

## Profiling

Profiling is required at gate G3, not optional:

- `-Xptxas -v` register and local-memory (spill and stack) usage for each kernel. The traversal stack is dynamically indexed, so expect it in local memory.
- Nsight Compute: occupancy, local-memory traffic, achieved memory throughput for the megakernel.
- Nsight Systems: the timeline of preparation, transfers, and kernels.
- **Verify on Vista** that both Nsight tools are available.
