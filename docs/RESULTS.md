# Preliminary results

**Status:** first Vista measurements, from the G2 sweep (Nov 13, 2026). These are sweep medians, not the final report numbers: the final report reruns the selected configurations interleaved ([BENCHMARKING.md](BENCHMARKING.md#targets-from-proposal)). They are **not** G3 results: the profiling, the light-count and clustered-scene rows, and the full thread sweep at the two smaller sizes are still to do.

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
| Data | [data/vista-core-1142842.csv](data/vista-core-1142842.csv) (one row per point and quantity; the components are those of the median run). Regenerate the tables below with `python3 scripts/tacc/ratios.py docs/data/vista-core-1142842.csv` |

## Correctness on Vista

`scripts/tacc/verify.sh` on the GPU node: ctest 306/306, scene hashes match the manifest, and CPU vs CUDA passes all five `rt_compare` checks on the three known scenes and the three benchmark scenes. The CPU and CUDA PFM and AOV files are **byte-identical** on every one of them, and repeated CUDA runs are byte-identical. The starting `rt_compare` tolerances are unchanged (see [BENCHMARKING.md](BENCHMARKING.md#2-image-comparison-rt_compare)).

## Target ratios

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

## Against the proposal's targets (T_br)

| Target | 10K | 100K | 1M | Result |
|---|---:|---:|---:|---|
| ~10×: 1 core / CPU\* | 65× | 55× | 29.8× | Met at every size |
| ~2×: CPU\* / GPU\* | 4.6× | 2.7× | 1.46× | Met at 10K and 100K (see the caveat above); **not met at 1M** |
| ~20×: 1 core / GPU\* | 302× | 149× | 43.4× | Met at every size |

For a loaded, prepared scene (T_frame) the GPU is 5.1× faster than the best CPU at 1M, and 6.2× and 8.1× at 100K and 10K. For a cold process (T_app) the CPU wins at every size: CUDA context creation costs 130 to 240 ms and the scene load dominates both backends.

## What the data says

1. **Preparation, not rendering, limits the GPU's T_br at 1M.** The GPU's 53.8 ms is bounds 2.3 + build 32.9 + layout 11.6 (CPU work, 46.8 ms or 87%) plus 6.4 ms for alloc, upload, render and download. The CPU's 78.4 ms is about 48 ms of preparation (2.2 + 33.3 + 12.5) plus a 29.7 ms render.
2. **The 2× target needs preparation near 17 ms.** The ratio is (P + 29.7) / (P + 6.3) for preparation time P, and P is about 47 ms now. Faster preparation helps the CPU run too, so P has to fall about 2.7× to reach 2×. Speeding up the GPU kernel cannot do it: a zero-time render would still give only 1.65× at the current P.
3. **The LBVH build stops scaling around 16 threads.** At 1M it speeds up 6.7× from 1 to 72 threads (222 → 33 ms). The curve fits about 30 ms that does not parallelize plus a part that scales (predicted 55 ms at 8 threads, 43 ms at 16, 33 ms at 72, against 55.4, 43.0 and 33.3 measured). These data do not show which stage holds that time; per-stage timing inside the builder would. The layout step is fastest at 16 threads (9.1 ms) and slower at 72 (12.5 ms).
4. **CPU rendering scales almost linearly** (69× on 72 threads at 1M), so the CPU's weak spot is the build.
5. **Host-device transfer is negligible on the GH200.** At 1M, upload is 0.2 ms, allocation 0.4 ms and download 0.1 ms (NVLink-C2C). The laptop's PCIe upload is about 8 ms at the same size.
6. **Tree quality matters as much as build cost.** At 1M with B = R = 72 the serial `median` tree renders about twice as fast as the LBVH on the CPU (T_frame 14.7 ms vs 29.1 ms) and 2.6× as fast on the GPU (2.2 ms vs 5.7 ms), but its serial build makes T_br 4.5× (CPU) and 6.3× (GPU) slower (349.6 ms and 337.5 ms). This is the build-cost against tree-quality tradeoff that the SAH extension (E3) targets.
7. **Resolution shifts the balance.** At 640×480 (1M, B = R = 72) T_br is 54.6 ms on the CPU and 49.6 ms on the GPU (1.10×), and T_frame is 6.9 ms and 1.5 ms (4.6×). Less rendering work leaves preparation as almost the whole T_br.
8. **T_app is mostly scene load and unattributed time, not rendering.** At 1M on the CPU with 72 threads, T_app is 991 ms: load 492 ms, unattributed 419 ms, and preparation and render about 80 ms. The unattributed time matches `validate_scene` running a second time in `rt_render` outside the `load` timer (it is also called inside `load_scene`); each call builds a path string per primitive. I read this from the code and have not timed that call by itself. On the laptop at 10M primitives the unattributed time was about 5 s. The GPU adds 239 ms of `cuda_init` at 1M (1212 ms total).

## Limits of these numbers

- CPU\* at 10K and 100K comes from two thread counts (above). The `core` preset's thread sweep covers only 1M.
- GPU T_app is unstable at small sizes (spread 205% at 10K, 37% at 100K; 13% at 1M). Run-to-run variation in `cuda_init` is the likely cause. The CSV keeps only the median run, so the per-process totals are in `points/app_cuda_*/tapp_*.json` on Vista.
- GPU\* for T_frame at 1M is B = 8 only because the three B values (1, 8, 72) differ by noise; T_frame does not depend on B.
- The light-count dimension, clustered scenes and seed 2 are not measured. There is no profiling data yet (`-Xptxas -v`, Nsight).
- One node, one job per point. The spread column is the only noise estimate.
