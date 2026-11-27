# Preliminary results

**Status:** three sets of Vista measurements. The current one is the Nov 27 run of `dev` at `94fadfe` ([jump to it](#current-vista-run-of-dev-94fadfe-nov-27)): `verify`, the `core` sweep, the interleaved final runs, the Nsight profile and the extra benchmarks. The same runs on the laptop are [below it](#laptop-run-rtx-4070-laptop-nov-27). Before them come a rerun after the preparation work in PR #75 (Nov 23, commit `13321a5`) and the G2 sweep (Nov 13, commit `14db51d`); both are kept as history. The headline ratios come from the final runs, which rerun the selected configurations interleaved ([BENCHMARKING.md](BENCHMARKING.md#targets-from-proposal)); sweep medians are preliminary. Not measured, by decision on Nov 27 (no time): the `field_clustered` and seed-2 scenes, and the extensions (SAH, memory modes, Embree, wavefront).

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

## Current: Vista run of `dev` (94fadfe, Nov 27)

The first Vista numbers for the code with the materials (Blinn-Phong, mirror, glass, tinted shadows), the sphere-acne fix and `tan_det`; the lit BVH megakernel is at 128 registers (it was 100 on Nov 23). Every row below except the profile comes from one `gh-dev` node, `c642-032` (Grace, 72 cores, GH200, driver 615.71.09), one clean build of `94fadfe`, `gcc/13.2.0`, `cuda/13.3`, `RT_CUDA_ARCH=90`, and the settings in [Setup](#setup). Jobs: `1175084` (`verify`, `core`, `final`, the stills of `extra`), `1175157` (`profile` again, once Nsight was on `PATH`; node `c642-002`, repo at `4330533`, same binary) and `1175169` (the 4K walkthroughs). Data: [vista-core-1175084.csv](data/vista-core-1175084.csv) (sweep), [vista-final-1175084.csv](data/vista-final-1175084.csv) and [the plan it ran](data/vista-final-plan-1175084.json), [vista-profile-1175084.txt](data/vista-profile-1175084.txt), [vista-extra-1175084.csv](data/vista-extra-1175084.csv) and [walks](data/vista-extra-walks-1175084.csv). Regenerate the tables with `python3 scripts/tacc/ratios.py docs/data/<file>.csv`.

### Correctness on Vista

`verify.sh`: 0 failures. ctest passes 358 of 358, the three benchmark scene hashes match the manifest, and CPU and CUDA PFM and AOV files are byte-identical on the 7 known scenes and the 3 benchmark scenes, with repeated CUDA runs identical. The same holds for the extra benchmarks: the CPU and GPU images of every still, and all 240 PNG frames of each house walkthrough, are byte-identical.

### Headline ratios (final runs, T_br, lbvh, 1920 px wide)

CPU\* is the 72-thread CPU at every size, and GPU\* is B = 8 at 10K and B = 72 at 100K and 1M, as the sweep chose them. Each value is the median over 5 interleaved rounds; the spread over the rounds is at most 6% for T_br, so the 10K and 100K rows that needed a rerun on Nov 23 are settled. Inside a single process the 5 runs can still spread more (up to 39% for GPU T_br at 100K, 36% for CPU T_br at 10K); the median over the rounds is what agrees.

| Target | 10K | 100K | 1M | Nov 23 at 1M |
|---|---:|---:|---:|---:|
| ~10×: 1 core / CPU\* | 61.5× | 62.1× | 56.2× | 56.3× |
| ~2×: CPU\* / GPU\* | 5.76× | 3.60× | **2.43×** | 2.40× |
| ~20×: 1 core / GPU\* | 354× | 224× | 137× | 135× |

All three targets are met at every size. With B = 72 fixed instead of the best GPU thread count, CPU / GPU is 4.04×, 3.60× and 2.43× at 10K, 100K and 1M. T_frame and T_app:

| Scene | T_frame: 1 core / CPU\* | T_frame: CPU\* / GPU\* | T_app: 1 core (ms) | T_app: CPU\* (ms) | T_app: GPU\* (ms) |
|---|---:|---:|---:|---:|---:|
| bench_10k_s1 | 70.0× | 7.77× | 522.0 | 12.1 | 250.5 |
| bench_100k_s1 | 68.8× | 5.99× | 1020.6 | 28.0 | 257.4 |
| bench_1m_s1 | 69.2× | 5.17× | 2404.9 | 123.3 | 470.1 |

The CPU still wins T_app at every size, because CUDA context creation costs 130 to 250 ms. GPU T_app is not stable between runs: at 100K and 1M the final runs give 257 and 470 ms, against 146 and 352 ms in the sweep a few minutes earlier (the sweep's own spread was 174% and 41%).

### What the new code cost

The 2× result is unchanged (2.43× against 2.40×), and so is preparation: at B = 72 and 1M it is 10.2 ms (bounds 0.5, build 2.7, layout 7.0), with 0.7 ms of alloc and upload, a 5.9 ms render and 0.1 ms of download. T_frame at 1M rose from 29.4 to 31.4 ms on the CPU (+7%) and from 5.7 to 6.1 ms on the GPU (+7%), on a different node of the same type. Both backends slowed by the same amount, so the shading code accounts for it, and the move from 100 to 128 registers does not show up as a larger GPU slowdown.

### Other sweep dimensions (1M, B = 72, sweep medians)

| | CPU T_br | GPU T_br | CPU / GPU | CPU T_frame | GPU T_frame | CPU / GPU |
|---|---:|---:|---:|---:|---:|---:|
| lbvh, 1920×1080 | 41.3 | 16.9 | 2.44× | 31.3 | 6.1 | 5.16× |
| median, 1920×1080 | 352.0 | 337.6 | 1.04× | 15.4 | 2.3 | 6.73× |
| lbvh, 640×480 | 17.1 | 12.4 | 1.37× | 7.4 | 1.6 | 4.63× |

The median tree still renders 2× as fast as the LBVH on the CPU and 2.6× as fast on the GPU, and its serial build makes T_br 8.5× (CPU) and 20× (GPU) the LBVH values. Light count (copies of the one light on a ring, total power fixed):

| Backend | Quantity | L = 1 | L = 2 | L = 4 |
|---|---|---:|---:|---:|
| CPU | T_frame (ms) | 31.3 | 43.7 | 66.6 |
| GPU | T_frame (ms) | 6.1 | 8.5 | 13.5 |
| CPU | T_br (ms) | 41.3 | 54.7 | 77.5 |
| GPU | T_br (ms) | 16.9 | 19.4 | 24.2 |

Each extra light adds about 12 ms to a CPU frame and 2.5 ms to a GPU frame, so the GPU's lead in T_br grows with the number of lights (2.44×, 2.82×, 3.20×).

### Profiling (GH200)

`scripts/tacc/profile.sh` with Nsight Systems 2026.4.1 and Nsight Compute 2026.2.1 (from the HPC SDK `nvidia/26.9`; see [BUILD.md](BUILD.md#profiling-tools)). Lit LBVH megakernel, 1920×1080, B = 72. Nsight Compute ran `--set full` on one launch with clocks locked to base, so its durations are not benchmark numbers. The laptop profile ([below](#profiling-laptop-nov-26-preliminary)) is the comparison.

| | bench_10k_s1 | bench_100k_s1 | bench_1m_s1 | Laptop at 1M |
|---|---:|---:|---:|---:|
| Registers / thread (cuobjdump) | 128 | 128 | 128 | 128 |
| Stack frame, bytes | 832 | 832 | 832 | 832 |
| Spill requests | 0 | 0 | 0 | 0 |
| Theoretical / achieved occupancy | 25% / 20% | 25% / 19% | 25% / 19% | 33% / 26% |
| Block limit from registers | 2 | 2 | 2 | 2 |
| Compute (SM) throughput | 46% | 43% | 42% | 41% |
| Memory throughput / DRAM throughput | 17% / 0.1% | 25% / 0.1% | 38% / 0.3% | 38% / 14% |
| L1/TEX hit rate / L2 hit rate | 98% / 99% | 99% / 99% | 99% / 97% | 99% / 90% |
| Active threads per warp (of 32) | 20.1 | 13.4 | 9.8 | 9.7 |
| Warp cycles per issued instruction | 6.7 | 6.7 | 7.1 | 7.5 |
| Kernel time, ncu at base clocks (ms) | 0.84 | 2.32 | 5.92 | 21.6 |
| Kernel time, nsys average (ms) | 0.85 | 2.31 | 5.91 | |

1. **The result of the laptop profile holds on the GH200: the kernel waits on latency and divergence, not on bandwidth.** SM throughput is 42 to 46%, the same as the laptop. DRAM throughput is under 0.3% because the GH200's L2 hit rate is 97 to 99% (the laptop's DRAM traffic was 11 to 14%).
2. **Divergence is the same on both GPUs.** 9.8 of 32 threads per warp are active at 1M (9.7 on the laptop), against 20.1 at 10K, so it comes from the scene and the shading, not from the GPU.
3. **Registers set the occupancy.** 128 registers allow 2 blocks of 256 threads per SM, which is 25% theoretical occupancy on Hopper's 64 warps per SM (33% on the laptop's 48), and 19 to 20% achieved. No spills.
4. The kernel takes 100% of GPU kernel time. At 1M the average launch in Nsight Systems is 5.91 ms, which matches the 5.9 ms render in the T_br breakdown. Host-to-device copies are 1.1 ms in total over 36 copies.

A faster kernel would come from less divergence or latency, such as sorting rays or the wavefront pipeline (E5), and not from memory traffic. The 2× target is already met, and the kernel is 5.9 ms of the GPU's 17.0 ms T_br at 1M, so it would save little there.

### Extra benchmarks

CPU at 72 threads against the GPU at B = 72, lbvh, lit, medians of 5 runs for the stills. See [BENCHMARKING.md](BENCHMARKING.md#extra-benchmarks) for what each one is and how it is checked. Spread above 10% on `terrain_10k` (18%) and `map_crew` (22%) comes from GPU T_br.

| Scene | Primitives | Resolution | CPU T_br | GPU T_br | CPU / GPU | CPU T_frame | GPU T_frame | CPU / GPU | CPU = GPU |
|---|---:|---|---:|---:|---:|---:|---:|---:|---|
| terrain_10k | 9,800 | 1920×1080 | 8.3 | 2.1 | 4.0× | 7.4 | 0.9 | 8.3× | yes |
| map_crew | 3,024,680 | 3840×2160 | 130.4 | 37.5 | 3.5× | 106.2 | 13.1 | 8.1× | yes |
| dragon_gold | 249,892 | 3840×2160 | 100.7 | 26.3 | 3.8× | 96.8 | 22.7 | 4.3× | yes |
| balls | 50 | 3840×2160 | 74.2 | 13.3 | 5.6× | 73.5 | 13.0 | 5.6× | yes |

House walkthrough, one preparation then one timed frame per camera (render and download), 240 frames along `house_walk.json` (2,145,083 triangles, 3 point lights):

| Resolution | CPU frame median (min–max) ms | GPU frame median (min–max) ms | CPU / GPU | CPU total (s) | GPU total (s) | Frames identical |
|---|---:|---:|---:|---:|---:|---|
| 1920×1080 | 63.8 (51.7–83.5) | 8.6 (5.6–15.9) | 7.0× | 15.3 | 2.2 | 240 of 240 |
| 3840×2160 | 231.8 (189.0–304.4) | 25.2 (17.8–35.3) | 9.2× | 55.8 | 6.0 | 240 of 240 |

The GPU renders the 8 seconds of 4K video (240 frames at 30 fps) in 6.0 s, which is faster than real time, and the CPU takes 56 s. The T_frame and walkthrough ratios are smaller than on the laptop (6.8× to 17.7× there, from short runs, and 4.3× to 9.2× here), because the 72 Grace cores are a much stronger CPU baseline than the laptop's 32 threads.

The videos (`house_1920x1080.mp4`, `house_3840x2160.mp4`) are not in git. `ffmpeg` on the Vista compute nodes cannot start (`libunwind.so.8` is missing), so the GPU frames were copied to the laptop and encoded there with the settings of `make_video.sh`.

### Limits of the Nov 27 run

- One node (`c642-032`), except the profile (`c642-002`). The final runs are 5 interleaved rounds; the sweep is one job per point.
- GPU T_app is still unstable between runs, as described above. `cuda_init` is the likely cause; the per-process totals are in `points/app_cuda_*/tapp_*.json` on Vista.
- GPU\* for T_frame at 1M is B = 8 only because the three B values differ by noise.
- `field_clustered` and the seed-2 scene are not measured; they were dropped on Nov 27 for lack of time.
- The SAH extension, memory modes, Embree and the wavefront pipeline were not built (cut on Nov 27), so there is nothing to compare them against.

## Laptop run (RTX 4070 Laptop, Nov 27)

The same sweep and final runs on the development laptop, from a clean reboot. Machine: `El-Studiobook`, i9-13980HX (8 P-cores and 16 E-cores, 32 threads), RTX 4070 Laptop GPU (8 GB, `sm_89`, driver 621.40, PCIe), WSL2 Ubuntu 24.04, plugged in with the maximum performance profile. Build: `dev` at `f9bdc3a`, clean, CUDA 13.3, Release, contraction off. Settings as in [Setup](#setup), with the laptop thread counts of [BENCHMARKING.md](BENCHMARKING.md#sweeps) (CPU 1, 2, 4, 8, 16, 24, 32; GPU build threads 1, 8, 32). The `core` sweep has 47 points. The final runs are 24 configurations in interleaved rounds, and were done **twice**: run 1 with 5 rounds and run 2 with 7. Data: [laptop-run1-core.csv](data/laptop-run1-core.csv), [laptop-run1-final.csv](data/laptop-run1-final.csv), [laptop-run2-core.csv](data/laptop-run2-core.csv), [laptop-run2-final.csv](data/laptop-run2-final.csv) and the two plans.

**Which run is reported.** Decided before run 2: report the run with the lower spread on the headline 1M T_br row. That is run 1 (8%, against 18% in run 2, where the 32-thread CPU row drifted). Run 2 is shown beside it as the repeat. The runs agree to within 8% on the 1M T_br ratios. CPU\* is 24 or 32 threads in different rows and runs, and GPU\* is B = 1 or 8 (or 32 for T_frame at 100K in run 1) where those values differ by noise.

### Headline ratios (final runs, lbvh, 1920 px wide)

| Scene | Quantity | CPU 1 core (ms) | CPU\* (ms, threads) | GPU\* (ms, B) | 1 core / CPU\* | CPU\* / GPU\* | 1 core / GPU\* | Spread (max) | Run 2: 1 core / CPU\* | CPU\* / GPU\* | 1 core / GPU\* | Spread (max) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| bench_10k_s1 | T_br | 458.3 | 41.7 (24) | 7.7 (1) | 11.0× | 5.39× | 59× | 10% | 11.5× | 5.10× | 59× | 44% |
| bench_100k_s1 | T_br | 867.5 | 83.8 (32) | 17.1 (8) | 10.4× | 4.89× | 51× | 14% | 11.3× | 4.27× | 48× | 11% |
| bench_1m_s1 | T_br | 1958.3 | 207.3 (24) | 70.4 (8) | 9.4× | **2.94×** | 28× | 8% | 9.6× | 2.74× | 26× | 18% |
| bench_10k_s1 | T_frame | 451.1 | 42.8 (32) | 5.3 (1) | 10.6× | 8.07× | 85× | 12% | 11.4× | 7.27× | 83× | 11% |
| bench_100k_s1 | T_frame | 832.6 | 79.0 (32) | 9.6 (32) | 10.5× | 8.24× | 87× | 7% | 10.9× | 7.50× | 82× | 6% |
| bench_1m_s1 | T_frame | 1789.6 | 164.1 (32) | 23.1 (1) | 10.9× | 7.11× | 78× | 9% | 11.1× | 7.10× | 79× | 8% |
| bench_10k_s1 | T_app | 474.4 | 48.6 (32) | 209.3 (32) | 9.8× | 0.23× | 2× | 105% | 9.3× | 0.26× | 2× | 116% |
| bench_100k_s1 | T_app | 875.2 | 85.8 (32) | 212.0 (32) | 10.2× | 0.40× | 4× | 58% | 10.0× | 0.42× | 4× | 66% |
| bench_1m_s1 | T_app | 2083.9 | 297.7 (32) | 359.3 (32) | 7.0× | 0.83× | 6× | 17% | 8.0× | 0.74× | 6× | 27% |

- **The three targets** hold on the laptop too: CPU / GPU is 2.94× at 1M (4.89× at 100K and 5.39× at 10K), and 1 core / GPU is 28× at 1M. The multicore speedup is 9.4× at 1M and 10.4× to 11.0× at the smaller sizes, a little under 10× at 1M.
- **Multicore speedup on this CPU** is capped by its design. Past 8 P-cores (16 threads) the E-cores add less, and at 1M the sweep's best T_br is at 24 threads (213.6 ms), with 32 threads slower (246.3 ms, the build alone rises from 15.2 to 36.4 ms). So CPU\* is not the highest thread count here, which is what the "fastest tested CPU" rule is for.
- **Noise.** Rows measured in milliseconds jitter on a laptop: at 10K and 100K the GPU B = 32 T_br rows reach 56% to 119% spread, and GPU T_app is again unstable. The 1M T_br and T_frame rows used for the targets are within 18% in both runs and within 10% in run 1. Treat the 10K and 100K T_br ratios as indicative only.
- The CPU wins T_app at every size, for the same reason as on Vista: CUDA initialization is most of the GPU's 209 ms at 10K.

### What changes on a PCIe laptop

At 1M with B = 8 (sweep run 1) the GPU's T_br is 68.3 ms, made of bounds 3.9, build 17.6, layout 16.4, **alloc and upload 8.7**, render 19.4 and **download 2.3**. On the GH200 the same two transfer stages take 0.7 and 0.1 ms. The laptop's render stage is also 3.3× longer (19.4 against 5.9 ms), and its preparation 38 ms against 10 ms on Grace's 72 cores. This is the data-movement question of the proposal, answered without the memory-mode extension: over PCIe the transfers cost about 11 ms of a 68 ms T_br, over NVLink-C2C about 0.8 ms of 17 ms.

### Light count and other dimensions (1M, B = 32, sweep run 1)

| | L = 1 | L = 2 | L = 4 |
|---|---:|---:|---:|
| CPU T_frame (ms) | 174.3 | 232.2 | 361.2 |
| GPU T_frame (ms) | 22.1 | 29.6 | 44.8 |

Each extra light adds about 62 ms to a CPU frame and 7.6 ms to a GPU frame. The median tree renders at T_frame 76.9 ms (CPU) and 10.4 ms (GPU), against 174.3 and 22.1 ms for LBVH, so it is 2.3× and 2.1× faster at render time here. The 640×480 T_frame is 35.9 ms on the CPU and 5.5 ms on the GPU. T_br for these dimensions has spreads of 15% to 60% on the laptop and is in the CSVs but not quoted.

## Nov 23 rerun, before the materials (commit 13321a5)

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

## Limits of the Nov 23 and G2 numbers

Tracked in #70 (measurement gaps), #67 (unattributed time) and #68 (build scaling); the extension choice is #71.

- The G2 CPU\* at 10K and 100K came from two thread counts; the Nov 23 rerun has all eight at every size.
- GPU T_app is still unstable (spread 207% at 10K, 103% at 100K, 39% at 1M in the rerun; 205%, 37%, 13% at G2). Run-to-run variation in `cuda_init` is the likely cause. The CSV keeps only the median run, so the per-process totals are in `points/app_cuda_*/tapp_*.json` on Vista.
- GPU\* for T_frame at 1M is B = 8 only because the three B values (1, 8, 72) differ by noise; T_frame does not depend on B.
- Clustered scenes and seed 2 are not measured (dropped Nov 27). The light count and the Vista profile came with the [Nov 27 run](#current-vista-run-of-dev-94fadfe-nov-27).
- One node, one job per point. The spread column is the only noise estimate.
