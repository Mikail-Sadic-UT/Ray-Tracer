# Preliminary results

**Status:** two sets of Vista measurements: the G2 sweep (Nov 13, 2026, commit `14db51d`) and a rerun after the preparation work in PR #75 (Nov 23, commit `13321a5`), which is the current one ([jump to it](#after-the-preparation-work-commit-13321a5-nov-23)). The G2 sections below are kept as the baseline. These are sweep medians, not the final report numbers: the final report reruns the selected configurations interleaved ([BENCHMARKING.md](BENCHMARKING.md#targets-from-proposal)). They are **not** G3 results. The light-count and clustered-scene rows are still to measure, and so is the Vista profile. A first laptop profile is [below](#profiling-laptop-nov-26-preliminary).

**These numbers predate `dev` as of Nov 26 (`5e6ea0a`).** Since `13321a5`, the materials (Blinn-Phong, mirror, glass, tinted shadows) have landed in `shading.hpp`. They raised the lit BVH megakernel from 100 to 128 registers. The sphere-acne fix and `tan_det` also changed the lit benchmark images in their last bits. On the laptop at `5e6ea0a`, `verify.sh` has 0 failures, with CPU and CUDA byte-identical on all 7 known scenes and all 3 benchmark scenes, and ctest passes 356 of 356. Neither has been rerun on Vista. The next Vista job is `verify`, then the `core` sweep, the [final runs](BENCHMARKING.md#targets-from-proposal) and the [profile](BENCHMARKING.md#profiling), and it replaces the tables below.

## Setup

| | |
|---|---|
| Machine | TACC Vista `gh-dev` node c642-001: Grace (Neoverse-V2, 72 cores), NVIDIA GH200 120GB, driver 615.71.09 |
| Toolchain | `gcc/13.2.0`, `cuda/13.3`, `RT_CUDA_ARCH=90`, Release, contraction off (`-ffp-contract=off`, `--fmad=false`) |
| Code | commit `14db51d` (branch `mikail/sweeps`: PRs #65 and #66) |
| Jobs | `1142829` (`g2` preset, `verify.sh`), `1142842` (`core` preset) |
| Scenes | `scenes-v1`: `bench_10k_s1`, `bench_100k_s1`, `bench_1m_s1`; hashes match `scenes/manifest.json` |
| Settings | 1920×1080, lit, one point light, `lbvh` (unless noted), `mega`, `explicit`, `OMP_PROC_BIND=close OMP_PLACES=cores` |
| Runs | T_br and T_frame: 1 warm-up, then 5 runs, median. T_app: 5 fresh processes, median |
| Rerun (Nov 23) | `gh-dev` node c642-062 (same GPU, driver 615.71.09), commit `13321a5`, job `1164735` (interactive `idev`), `core` preset with 46 points |
| Data | [data/vista-core-1142842.csv](data/vista-core-1142842.csv) (G2) and [data/vista-core-1164735.csv](data/vista-core-1164735.csv) (rerun): one row per point and quantity; the components are those of the median run. Regenerate the tables with `python3 scripts/tacc/ratios.py docs/data/<file>.csv` |

## Correctness on Vista

`scripts/tacc/verify.sh` on the GPU node: ctest 306/306, scene hashes match the manifest, and CPU vs CUDA passes all five `rt_compare` checks on the three known scenes and the three benchmark scenes. The CPU and CUDA PFM and AOV files are **byte-identical** on every one of them, and repeated CUDA runs are byte-identical. The starting `rt_compare` tolerances are unchanged (see [BENCHMARKING.md](BENCHMARKING.md#2-image-comparison-rt_compare)).

## After the preparation work (commit 13321a5, Nov 23)

PR #75 parallelized the serial stages of the LBVH build (#68), removed the second `validate_scene` pass and its string building (#67), and made the thread sweeps run at every size (#70). Output bytes are unchanged. `verify.sh` on node c642-062: ctest 307/307, scene hashes match the manifest, and CPU and CUDA PFM and AOV files are byte-identical on the three known scenes and all three benchmark scenes, with repeated CUDA runs identical. Same machine type, settings and run counts as the G2 table below. Every size now has all eight thread counts for CPU\* and the three B values for GPU\*.

### Target ratios (lbvh, 1920 px wide)

| Scene | Quantity | CPU 1 core (ms) | CPU\* (ms, threads, tested) | GPU\* (ms, B) | 1 core / CPU\* | CPU\* / GPU\* | 1 core / GPU\* |
|---|---|---:|---:|---:|---:|---:|---:|
| bench_10k_s1 | T_br | 505.8 | 8.2 (72, 8) | 1.4 (8) | 61.8x | 5.88x | 363.5x |
| bench_10k_s1 | T_frame | 504.6 | 7.1 (72, 8) | 0.9 (1) | 70.7x | 8.12x | 573.7x |
| bench_10k_s1 | T_app | 507.5 | 11.7 (72, 2) | 134.0 (72) | 43.2x | 0.09x | 3.8x |
| bench_100k_s1 | T_br | 966.7 | 15.4 (72, 8) | 4.2 (72) | 62.6x | 3.70x | 231.5x |
| bench_100k_s1 | T_frame | 955.5 | 13.6 (72, 8) | 2.2 (1) | 70.0x | 6.20x | 434.1x |
| bench_100k_s1 | T_app | 973.7 | 27.5 (72, 2) | 265.6 (72) | 35.5x | 0.10x | 3.7x |
| bench_1m_s1 | T_br | 2208.1 | 39.3 (72, 8) | 16.4 (72) | 56.3x | 2.40x | 135.0x |
| bench_1m_s1 | T_frame | 2055.7 | 29.4 (72, 8) | 5.7 (72) | 70.0x | 5.19x | 363.3x |
| bench_1m_s1 | T_app | 2289.3 | 122.0 (72, 2) | 367.7 (72) | 18.8x | 0.33x | 6.2x |

### T_br breakdown, bench_1m_s1 (ms, median run)

| Backend | B (R) | Total | bounds | build | layout | alloc+upload | render | download |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| cpu | 1 (1) | 2208.1 | 14.9 | 97.3 | 14.3 | 0.0 | 2081.6 | 0.0 |
| cpu | 2 (2) | 1118.0 | 11.7 | 49.0 | 7.7 | 0.0 | 1049.6 | 0.0 |
| cpu | 4 (4) | 563.6 | 5.9 | 25.4 | 5.8 | 0.0 | 526.5 | 0.0 |
| cpu | 8 (8) | 283.7 | 3.0 | 13.1 | 4.4 | 0.0 | 263.1 | 0.0 |
| cpu | 16 (16) | 144.0 | 1.4 | 6.9 | 3.6 | 0.0 | 132.1 | 0.0 |
| cpu | 32 (32) | 75.0 | 0.8 | 3.8 | 4.2 | 0.0 | 66.2 | 0.0 |
| cpu | 64 (64) | 43.0 | 0.6 | 2.3 | 6.6 | 0.0 | 33.4 | 0.0 |
| cpu | 72 (72) | 39.3 | 0.5 | 2.5 | 6.5 | 0.0 | 29.7 | 0.0 |
| cuda | 1 | 131.0 | 15.0 | 94.9 | 14.6 | 0.7 | 5.6 | 0.1 |
| cuda | 8 | 26.3 | 3.0 | 13.1 | 3.8 | 0.7 | 5.6 | 0.1 |
| cuda | 72 | 16.4 | 0.5 | 2.7 | 6.8 | 0.6 | 5.6 | 0.1 |

### Spread above 10%

- bench_10k_s1 T_app cuda B72 lbvh 1920: spread 207%
- bench_100k_s1 T_app cuda B72 lbvh 1920: spread 103%
- bench_1m_s1 T_app cuda B72 lbvh 1920: spread 39%
- bench_100k_s1 T_br cuda B72 lbvh 1920: spread 15%
- bench_10k_s1 T_br cuda B72 lbvh 1920: spread 12%
- bench_10k_s1 T_frame cuda B72 lbvh 1920: spread 11%
- bench_1m_s1 T_br cuda B72 lbvh 640: spread 18%

The 10K and 100K GPU T_br ratios (5.88x, 3.70x) rest on rows with spread above 10%, so they need a rerun before they are quoted. The 1M 1080p rows are within 10%.

### Against the proposal's targets (T_br)

| Target | 10K | 100K | 1M | Result |
|---|---:|---:|---:|---|
| ~10×: 1 core / CPU\* | 62× | 63× | 56.3× | Met at every size |
| ~2×: CPU\* / GPU\* | 5.9× | 3.7× | **2.40×** | **Met at every size** (10K and 100K pending a rerun for spread) |
| ~20×: 1 core / GPU\* | 364× | 232× | 135× | Met at every size |

### What changed

1. **The 2× target is met at 1M (1.46× → 2.40×).** The GPU's T_br fell from 53.8 to 16.4 ms. Preparation at B = 72 is 10.0 ms (bounds 0.5, build 2.7, layout 6.8), against 46.8 ms before and the 17 ms the ratio needed. The ratio formula (P + 29.7) / (P + 6.3) gives 2.43 for P = 10.0, close to the measured 2.40.
2. **The LBVH build now scales.** Build time at 1M falls from 97.3 ms on one thread to 2.5 ms on 72 (39×; it was 6.7×). The serial parts were the centroid bounds, the Morton codes, the radix sort, the zero fill of the temporary nodes and a bit-by-bit leading-zero count (#68). The 1-core build also got faster (222 → 97 ms), and the CPU at 72 threads improved more than the 1-core run, so the 1-core / CPU\* ratio rose from 29.8× to 56.3×.
3. **Layout is now the largest preparation stage and grows with threads.** It is 3.6 ms at B = 16 and 6.5 ms at B = 72 (it includes freeing the builder's scratch). If it were about 2 ms, P would be near 5.6 ms and the ratio near 3.0×. See PLAN.md.
4. **T_app has no unattributed time.** `unattributed_ms` is 0.8 to 1.1 ms in every T_app row (it was 419 ms at 1M). The scene `load` at 1M fell from 492 to 79 ms. CPU T_app at 1M with 72 threads is 122 ms (was 991 ms). GPU T_app is 368 ms (was 1212 ms): load 92 ms plus `cuda_init` 255 ms. The CPU still wins T_app at every size because of `cuda_init` (128 to 255 ms), and the GPU T_app spread is still large.
5. **The median tree still renders about 2× faster, but its build now costs far more by comparison.** At 1M, B = R = 72: T_frame is 14.7 ms for median against 29.4 ms for LBVH on the CPU, and 2.2 ms against 5.7 ms on the GPU. T_br is 349.7 ms (CPU) and 336.6 ms (GPU) for median, which is 8.9× and 20.5× the LBVH values. A better tree can save at most about 3.5 ms of GPU render here (5.7 → 2.2 ms), so a parallel SAH build (E3) would have to add less than that to the LBVH build to improve GPU T_br; its case rests on T_frame and on the CPU render (#71).
6. **Resolution.** At 640×480 (1M, B = R = 72) T_br is 16.7 ms on the CPU and 12.0 ms on the GPU (1.39×; the GPU row has 18% spread), and T_frame is 7.0 ms and 1.5 ms (4.7×).
7. **CPU rendering still scales almost linearly** (70× on 72 threads at 1M). Transfer is still negligible on the GH200 (alloc + upload 0.6 ms, download 0.1 ms).

## Target ratios at G2 (commit 14db51d)

CPU\* is the lowest median among the tested thread counts (B = R). **Only the 1M scene was measured at all eight thread counts (1 to 72).** At 10K and 100K only 1 and 72 threads were run, so CPU\* is the better of two and the GPU ratios there are upper bounds. The 1M rows are the ones to quote.

### Target ratios (lbvh, 1920 px wide)

| Scene | Quantity | CPU 1 core (ms) | CPU\* (ms, threads, tested) | GPU\* (ms, B) | 1 core / CPU\* | CPU\* / GPU\* | 1 core / GPU\* |
|---|---|---:|---:|---:|---:|---:|---:|
| bench_10k_s1 | T_br | 502.8 | 7.7 (72, 2) | 1.7 (72) | 65.1x | 4.63x | 301.6x |
| bench_10k_s1 | T_frame | 500.9 | 7.2 (72, 2) | 0.9 (72) | 70.0x | 8.10x | 567.1x |
| bench_10k_s1 | T_app | 512.5 | 19.8 (72, 2) | 142.9 (72) | 25.9x | 0.14x | 3.6x |
| bench_100k_s1 | T_br | 964.0 | 17.5 (72, 2) | 6.5 (72) | 55.1x | 2.71x | 149.0x |
| bench_100k_s1 | T_frame | 942.2 | 13.7 (72, 2) | 2.2 (72) | 68.9x | 6.18x | 425.5x |
| bench_100k_s1 | T_app | 1057.5 | 112.7 (72, 2) | 358.9 (72) | 9.4x | 0.31x | 2.9x |
| bench_1m_s1 | T_br | 2332.8 | 78.4 (72, 8) | 53.8 (72) | 29.8x | 1.46x | 43.4x |
| bench_1m_s1 | T_frame | 2034.2 | 29.1 (72, 8) | 5.7 (8) | 69.8x | 5.13x | 358.0x |
| bench_1m_s1 | T_app | 3238.2 | 991.0 (72, 2) | 1212.1 (72) | 3.3x | 0.82x | 2.7x |

### T_br breakdown, bench_1m_s1 (ms, median run)

| Backend | B (R) | Total | bounds | build | layout | alloc+upload | render | download |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| cpu | 1 (1) | 2332.8 | 15.4 | 222.3 | 37.2 | 0.0 | 2057.5 | 0.0 |
| cpu | 2 (2) | 1200.6 | 13.0 | 127.2 | 21.9 | 0.0 | 1038.1 | 0.0 |
| cpu | 4 (4) | 626.9 | 7.4 | 80.0 | 17.0 | 0.0 | 522.0 | 0.0 |
| cpu | 8 (8) | 332.0 | 4.8 | 55.4 | 9.8 | 0.0 | 261.6 | 0.0 |
| cpu | 16 (16) | 187.2 | 3.3 | 43.0 | 9.1 | 0.0 | 131.2 | 0.0 |
| cpu | 32 (32) | 114.9 | 2.6 | 36.7 | 9.3 | 0.0 | 65.9 | 0.0 |
| cpu | 64 (64) | 81.1 | 2.3 | 34.0 | 10.9 | 0.0 | 33.3 | 0.0 |
| cpu | 72 (72) | 78.4 | 2.2 | 33.3 | 12.5 | 0.0 | 29.7 | 0.0 |
| cuda | 1 | 277.5 | 15.4 | 220.5 | 34.5 | 0.7 | 5.6 | 0.1 |
| cuda | 8 | 76.9 | 4.6 | 55.3 | 9.9 | 0.7 | 5.6 | 0.1 |
| cuda | 72 | 53.8 | 2.3 | 32.9 | 11.6 | 0.7 | 5.6 | 0.1 |

### Spread above 10%

- bench_100k_s1 T_app cuda B72 lbvh 1920: spread 37%
- bench_10k_s1 T_app cuda B72 lbvh 1920: spread 205%
- bench_1m_s1 T_app cuda B72 lbvh 1920: spread 13%
- bench_100k_s1 T_br cuda B72 lbvh 1920: spread 11%
- bench_10k_s1 T_br cuda B72 lbvh 1920: spread 20%

## Against the proposal's targets at G2 (T_br)

| Target | 10K | 100K | 1M | Result |
|---|---:|---:|---:|---|
| ~10×: 1 core / CPU\* | 65× | 55× | 29.8× | Met at every size |
| ~2×: CPU\* / GPU\* | 4.6× | 2.7× | 1.46× | Met at 10K and 100K (see the caveat above); **not met at 1M** |
| ~20×: 1 core / GPU\* | 302× | 149× | 43.4× | Met at every size |

For a loaded, prepared scene (T_frame) the GPU is 5.1× faster than the best CPU at 1M, and 6.2× and 8.1× at 100K and 10K. For a cold process (T_app) the CPU wins at every size: CUDA context creation costs 130 to 240 ms and the scene load dominates both backends.

## What the G2 data said

These findings describe the G2 runs; findings 2, 3 and 8 were acted on (see "What changed" above).

1. **Preparation, not rendering, limits the GPU's T_br at 1M.** The GPU's 53.8 ms is bounds 2.3 + build 32.9 + layout 11.6 (CPU work, 46.8 ms or 87%) plus 6.4 ms for alloc, upload, render and download. The CPU's 78.4 ms is about 48 ms of preparation (2.2 + 33.3 + 12.5) plus a 29.7 ms render.
2. **The 2× target needs preparation near 17 ms** (tracked in #68). The ratio is (P + 29.7) / (P + 6.3) for preparation time P, and P is about 47 ms now. Faster preparation helps the CPU run too, so P has to fall about 2.7× to reach 2×. Speeding up the GPU kernel cannot do it: a zero-time render would still give only 1.65× at the current P.
3. **The LBVH build stops scaling around 16 threads.** At 1M it speeds up 6.7× from 1 to 72 threads (222 → 33 ms). The curve fits about 30 ms that does not parallelize plus a part that scales (predicted 55 ms at 8 threads, 43 ms at 16, 33 ms at 72, against 55.4, 43.0 and 33.3 measured). These data do not show which stage holds that time; per-stage timing inside the builder would (#68). The layout step is fastest at 16 threads (9.1 ms) and slower at 72 (12.5 ms).
4. **CPU rendering scales almost linearly** (69× on 72 threads at 1M), so the CPU's weak spot is the build.
5. **Host-device transfer is negligible on the GH200.** At 1M, upload is 0.2 ms, allocation 0.4 ms and download 0.1 ms (NVLink-C2C). The laptop's PCIe upload is about 8 ms at the same size.
6. **Tree quality matters as much as build cost.** At 1M with B = R = 72 the serial `median` tree renders about twice as fast as the LBVH on the CPU (T_frame 14.7 ms vs 29.1 ms) and 2.6× as fast on the GPU (2.2 ms vs 5.7 ms), but its serial build makes T_br 4.5× (CPU) and 6.3× (GPU) slower (349.6 ms and 337.5 ms). This is the build-cost against tree-quality tradeoff that the SAH extension (E3) targets (decision in #71).
7. **Resolution shifts the balance.** At 640×480 (1M, B = R = 72) T_br is 54.6 ms on the CPU and 49.6 ms on the GPU (1.10×), and T_frame is 6.9 ms and 1.5 ms (4.6×). Less rendering work leaves preparation as almost the whole T_br.
8. **T_app is mostly scene load and unattributed time, not rendering.** At 1M on the CPU with 72 threads, T_app is 991 ms: load 492 ms, unattributed 419 ms, and preparation and render about 80 ms. The unattributed time matches `validate_scene` running a second time in `rt_render` outside the `load` timer (it is also called inside `load_scene`); each call builds a path string per primitive. I read this from the code and have not timed that call by itself (#67). On the laptop at 10M primitives the unattributed time was about 5 s. The GPU adds 239 ms of `cuda_init` at 1M (1212 ms total).

## Profiling (laptop, Nov 26, preliminary)

`scripts/tacc/profile.sh` on the laptop (RTX 4070 Laptop, `sm_89`, driver 621.40, CUDA 13.3). The renderer is at `5e6ea0a`; the "dirty" build flag only reflects the uncommitted profiling script. Settings: lit LBVH megakernel, 1920×1080, B = 32. Nsight Compute ran `--set full` on one launch with clocks locked to base, so its durations are not benchmark numbers. This shows what limits the kernel. The GH200 numbers for the report still have to be measured.

| | bench_10k_s1 | bench_100k_s1 | bench_1m_s1 |
|---|---:|---:|---:|
| Registers / thread (cuobjdump) | 128 | 128 | 128 |
| Stack frame, bytes (traversal stack) | 832 | 832 | 832 |
| Spill requests | 0 | 0 | 0 |
| Theoretical / achieved occupancy | 33% / 28% | 33% / 26% | 33% / 26% |
| Block limit from registers | 2 | 2 | 2 |
| Compute (SM) throughput | 41% | 43% | 41% |
| Memory throughput / DRAM throughput | 23% / 11% | 25% / 11% | 38% / 14% |
| L1/TEX hit rate / L2 hit rate | 98% / 87% | 99% / 89% | 99% / 90% |
| Active threads per warp (of 32) | 19.9 | 13.3 | 9.7 |
| Warp cycles per issued instruction | 8.3 | 7.2 | 7.5 |
| Kernel time, ncu (ms) | 3.1 | 8.3 | 21.6 |

What it shows:

1. **The kernel is limited by latency and divergence, not by bandwidth.** Compute is at about 41% of peak and DRAM at 11 to 14%. The working set is served from cache: L1 hit rate is 98 to 99% and L2 is 87 to 90%. This includes the traversal stack, which is in local memory.
2. **Divergence grows with scene size.** On average 9.7 of 32 threads in a warp are active at 1M, against 19.9 at 10K. The likely cause, not yet checked, is that neighboring pixels take different paths through the deeper tree and that only some of them shoot shadow rays.
3. **Occupancy is set by registers.** At 128 registers only 2 blocks of 256 threads fit per SM, which is 33% theoretical occupancy and 26 to 28% achieved. There are no spills. This is the limit PLAN.md describes: going over 128 registers would halve occupancy.
4. Nsight Systems shows the kernel takes 100% of GPU kernel time. The transfers are PCIe copies, which on the GH200 are much shorter (alloc + upload 0.6 ms at 1M, above).

The likeliest kernel improvements would therefore come from reducing divergence or latency rather than memory traffic, for example by sorting rays or using a wavefront pipeline (E5). PLAN.md leaves those out of scope. On the GH200 the kernel is 5.6 of the GPU's 16.4 ms T_br at 1M, and preparation is the rest.

## Limits of these numbers

Tracked in #70 (measurement gaps), #67 (unattributed time) and #68 (build scaling); the extension choice is #71.

- The G2 CPU\* at 10K and 100K came from two thread counts; the Nov 23 rerun has all eight at every size.
- GPU T_app is still unstable (spread 207% at 10K, 103% at 100K, 39% at 1M in the rerun; 205%, 37%, 13% at G2). Run-to-run variation in `cuda_init` is the likely cause. The CSV keeps only the median run, so the per-process totals are in `points/app_cuda_*/tapp_*.json` on Vista.
- GPU\* for T_frame at 1M is B = 8 only because the three B values (1, 8, 72) differ by noise; T_frame does not depend on B.
- The light-count dimension, clustered scenes and seed 2 are not measured. Profiling exists for the laptop only (above); the Vista profile is still to do.
- One node, one job per point. The spread column is the only noise estimate.
