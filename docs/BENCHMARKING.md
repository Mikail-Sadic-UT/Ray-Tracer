# Benchmarking

How we measure, what we compare, and how we check quality. Timing fields are defined in [INTERFACES.md](INTERFACES.md#6-timing-and-results-schema).

## Two comparison tiers

| Tier | CPU side | GPU side | Same BVH? | Answers |
|---|---|---|---|---|
| **Controlled** (Core) | Our CPU renderer, 1..N threads | Our CUDA renderer (mega and wavefront) | Yes, same builder output | How much does the hardware alone gain, algorithm held fixed? |
| **Best-possible** (Extended) | Embree 4, N threads | Best CUDA variant; OptiX if built (Stretch) | No, each uses its best structure | Best achievable on each platform ("best X vs best Y") |

The proposal's ~10× / ~2× / ~20× targets are evaluated in the controlled tier. The best-possible tier is reported alongside it.

## Fairness rules

Within a comparison, these are identical:

- Scene file and seed, camera, resolution, light count and types.
- Shading model and material flags (`shade()` is shared by every backend, including Embree and OptiX).
- Controlled tier: the same BVH method and parameters.
- Machine (same node for CPU and GPU runs on Vista), compiler, and build flags.
- Thread pinning settings.

The reference is never weakened to make GPU numbers look better. Missed targets are reported with explanations.

## Timing protocol

| Measurement | Definition |
|---|---|
| **Cold** | First render in a fresh process. Timestamps start at process start. Includes CUDA context init (`cuda_init`, forced by a `cudaFree(0)` at start and timed separately). |
| **Warm** | Same process, after 1 discarded warm-up run. 5 timed runs; report the **median**. Raw runs are kept in the results file. |
| **Total** | `build + upload + render + download` |
| **Excluded** | File `load` and image `write`. Reported separately. |

- Warm runs redo the BVH build and upload each time, so "total" stays end-to-end. A render-only variant (`--reuse-prep`) can be added for render-focused plots.
- CPU timers use `std::chrono::steady_clock`. GPU stages use `cudaEvent` pairs. Total wall time uses `steady_clock` with `cudaDeviceSynchronize` at stage boundaries.
- Embree `build` = Embree's internal BVH build time.

## Scene sizes

Provisional, fixed at check-in 2:

| Name | Objects | Mix | Seed |
|---|---|---|---|
| `known_*` | 1–20 | Hand-written inline JSON (tests) | n/a |
| `bench_10k` | 10,000 | Spheres + triangle terrain + floor | 1 |
| `bench_100k` | 100,000 | Same | 1 |
| `bench_1m` | 1,000,000 | Same | 1 |

Record the exact `rt_gen` command for each in the results and the report. `.bin` files are regenerated, not committed.

## Sweeps

| Dimension | Values | Machines |
|---|---|---|
| CPU threads | 1, 2, 4, 8, 16, 32, 64, 72 | Vista |
| CPU threads | 1, 2, 4, 8, 16, 24, 32 | Laptop (P/E-core caveat) |
| BVH method | `median`, `lbvh`, `sah` | Both |
| Pipeline | `mega`, `wavefront` | Both |
| Memory mode | `explicit` | Both |
| Memory mode | `managed`, `system` | GH200 only |
| Light count L | 1, 2, 4, 8 | Both |
| Resolution | 640×480, 1920×1080 | Both (1080p is the headline) |
| Scene size | 10K, 100K, 1M | Both |

Full cross-products are too expensive. Vary one dimension at a time around a baseline config: `bench_1m`, 1920×1080, L=2, `sah`, `mega`, `explicit`, max threads.

## Thread pinning

```bash
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export OMP_NUM_THREADS=<N>
```

- GH200 nodes have one 72-core Grace CPU. Run CPU and GPU comparisons on the same GH200 node.
- Optional extra CPU data point: Vista Grace-Grace nodes (144 cores). See [STRETCH.md](STRETCH.md#extra-experiments).
- Laptop: the i9-13980HX has 8 P-cores (16 threads) and 16 E-cores. Scaling curves will bend past 8–16 threads. Report as-is with a note. Laptop power and thermal limits add noise: plug in and use the maximum performance profile.

## Link bandwidth

Measure once per machine with `bandwidthTest` (CUDA samples), pinned and pageable, host→device and device→host. Record in the report.

| Machine | Link | Expected | Measured |
|---|---|---|---|
| Laptop 4070 | PCIe (likely 4.0 ×8) | ~12–14 GB/s | TBD |
| GH200 | NVLink-C2C | Several hundred GB/s | TBD |

Compare memory modes as **ratios within a machine** (e.g. `system` vs `explicit` upload + render on GH200) plus measured bandwidth. Raw cross-machine times also reflect the H100 vs 4070 compute difference.

## Node-hour budget

About 50 node-hours total. Develop on `gh-dev`. Save `gh` production runs for final sweeps. Log every job:

| Date | Who | Queue | Job ID | Nodes × hours | Purpose | Running total |
|---|---|---|---|---|---|---|
| | | | | | | |

The log lives in `docs/nodehours.md` once the repo exists (or a shared sheet). Mikail owns it.

## Quality protocol

1. **Known-case tests** (CI): analytic checks for visibility (correct closest object), lighting (Lambert value at known points, 1/d² falloff), and shadows (shadow boundary of a sphere on a plane, no acne).
2. **Image comparison** (`rt_compare`): every backend vs the CPU reference on known-case scenes and `bench_100k`.
   - Pass: ≥ 99.9% of pixels within 2/255 per channel (tune at check-in 2). Report max error and write a diff heatmap.
3. **Determinism**: same-backend runs are bit-identical (1 vs N threads, repeated runs, repeated builds).
4. **Side-by-side review**: at least 2 reviewers, native resolution, same display, per proposal. Focus on edges and shadow boundaries. Record who reviewed what.
5. **Approximate modes** (if any are explored) are reported separately with their own quality and speed numbers. The verified mode stays the default.

## Planned plots

| Plot | Owner | Data |
|---|---|---|
| CPU render scaling (speedup vs threads) | Po | Vista + laptop |
| BVH build scaling per method | Jett | Vista, 1M |
| Preparation vs render share of total, by scene size | Jett, Arek | Controlled tier, GPU |
| Build time vs render time per BVH method | Jett | 3 sizes |
| Component breakdown (stacked bars) | Arek | Cold and warm |
| Light-count sweep | Arek | GPU + CPU |
| Megakernel vs wavefront (+ wavefront stage breakdown) | Arek, Mikail | Both GPUs |
| Memory modes | Mikail | GH200 + laptop explicit |
| Tier comparison (our CPU, Embree, our GPU, OptiX) | All | Vista |
| 2×2 {H100, 4070} × {CUDA mega, OptiX} | Mikail, Arek | Stretch only |

Plotting: Python + matplotlib scripts in `scripts/plot/`, reading `results.json` / CSV.

## Profiling

- Nsight Systems: timeline, transfers, kernel overlap.
- Nsight Compute: occupancy, register count, memory throughput for traversal kernels.
- **Verify on Vista** that both are available.
