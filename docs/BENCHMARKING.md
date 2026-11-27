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

- Report median, min, max, and `spread = (max − min) / median`. If spread exceeds ~10%, rerun and look into it.
- Plots show the components of the run whose total is the median (`median_run_index`). Don't add up component medians.
- `unattributed = total − Σ components` is reported. If it's large, some stage isn't instrumented.
- T_app on Vista is "cold process, warm file cache" (we can't drop OS caches); the report says so.
- *Deferred:* an uninstrumented `--no-components` run to measure timer overhead. Add it if instrumentation overhead looks significant on small scenes.

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

**For the final report only (C14):** pick CPU\* and GPU\* from the sweep, then rerun the 1-core baseline, CPU\*, GPU\*, and fixed-72 configurations in one job, interleaved, 5 runs each. Report the headline ratios from those reruns; picking the minimum of noisy medians biases the picked value low. Preliminary G3 numbers can come straight from the sweep.

`sweep.py final` does this ([Sweeps](#sweeps)). It reads CPU\* and GPU\* per scene and quantity from a `sweep.csv` by the rule above, then runs 5 rounds. Each round measures every selected configuration once, with the order rotated from round to round, so drift during the job affects every configuration alike. In a round, a T_br/T_frame configuration is one process (1 warm-up and 5 runs, the in-process median as usual), and a T_app configuration is one fresh process. The reported value is the median over the 5 rounds, and spread is computed over the rounds. `ratios.py final.csv` reads each configuration's role (`one_core`, `cpu_star`, `gpu_star`, `cpu_fixed`, `gpu_fixed`) from the file. It does not pick again, because that would bring back the bias. The T_app sweep measures the CPU at 1 and 72 threads only, so T_app's CPU\* is the better of those two.

| Target | Headline ratio | Also reported |
|---|---|---|
| ~10× multicore | T_br(CPU, B=1, R=1) / T_br(CPU\*) | Fixed: T_br(CPU, 1, 1) / T_br(CPU, 72, 72) |
| ~2× GPU | T_br(CPU\*) / T_br(GPU\*) | Fixed: T_br(CPU, 72, 72) / T_br(GPU, B=72) |
| ~20× overall | T_br(CPU, B=1, R=1) / T_br(GPU\*) | Fixed: T_br(CPU, 1, 1) / T_br(GPU, B=72) |

- The fixed-72 rows are labeled as such and never stand in for the headline "fastest CPU" claim.
- The same tables are reported for T_frame and T_app, labeled separately. These are hypotheses to evaluate, not predictions.

## Comparison tiers

| Tier | CPU side | GPU side | Same BVH? | Status |
|---|---|---|---|---|
| **Controlled** | Our CPU renderer | Our CUDA megakernel | Yes, identical `Bvh` | Core |
| **Best-possible** | Embree 4 | Best CUDA variant; OptiX if built | No | Extension / Stretch |

The proposal's targets are evaluated in the controlled tier.

## Fairness rules

Within a comparison, these are identical: scene hash, camera, resolution, lights, shading flags, BVH method and parameters (controlled tier), node, compiler and flags, thread pinning. The CPU reference is never weakened, and missed targets are reported with explanations.

## Scene sizes

Measured in **primitives**. Fixed at gate G1 (Nov 6), and presented at check-in 2 (Nov 12):

| Name | Spheres | Triangles | Primitives | Seed |
|---|---|---|---|---|
| `known_*` | 1–10 | 2–20 | ≤ 30 | n/a (hand-written) |
| `bench_10k` | 5,000 | ~5,000 (terrain, which is also the floor) | 10,000 | 1 |
| `bench_100k` | 50,000 | ~50,000 | 100,000 | 1 |
| `bench_1m` | 500,000 | ~500,000 | 1,000,000 | 1 |

The terrain grid gives `2·g²` triangles, slightly below the target, so the generator tops it up with sphere count until the total is exactly N. Results record the actual counts. In the published set `scenes-v1` ([manifest](../scenes/manifest.json)) these are 5,000 + 5,000, 50,072 + 49,928, and 500,000 + 500,000 (spheres + triangles).

Every results file records the scene hash and the exact `rt_gen` command. Icospheres, if used, are counted by their triangles (20·4^k each).

### Scene families

Primitive count alone doesn't define the workload: most of a million primitives could be off screen, hidden, or trivially rejected. Each benchmark scene is therefore fully specified by a layout, a size, and a seed. `bench_*` above is shorthand for `field_uniform_*_s1`.

Shared definition, for N total primitives (half spheres, half terrain triangles):

| Element | Rule |
|---|---|
| Extent | Square ground of side `S = 400 · sqrt(N / 1,000,000)`, centered at the origin. Primitive density per unit area is the same at every size. |
| Terrain | Height field on a `g × g` grid with `g = floor(sqrt(N_tri / 2))`, two triangles per cell, spanning the extent. Height `h(x,z) = 2·sin(2πx/40)·cos(2πz/40) + 0.5·sin(2πx/9 + 1)`: fixed, so terrain shape doesn't vary with seed. |
| Sphere radius | Log-uniform in [0.1, 1.0]. The distribution is independent of N. |
| Sphere height | Resting on the terrain: `y = h(x,z) + r + U[0, 1)` |
| Materials | 8 fixed diffuse colors, assigned by index mod 8 |
| Camera | **Fit to the scene bounds**, computed by `rt_gen` and written into the JSON. vfov 45°, 1920×1080, elevation 35° looking along −z, `look_at` = center of the scene AABB (terrain plus spheres, including sphere tops). Distance D is the smallest value at which all 8 AABB corners project inside 95% of the image half-width and half-height (bisection; deterministic). The whole field is in view at every size, with margin. |
| Light | One point light at `(0.3·S, 0.8·S, 0.2·S)`, intensity `∝ S²` so irradiance at the field center is the same at every size |

| Layout | Sphere placement | Purpose |
|---|---|---|
| **uniform** (principal) | `x, z` uniform over the extent | Main scaling family |
| **clustered** (contrast) | 64 cluster centers uniform over the extent; each sphere is `center + N(0, S/40)` in `x, z`, clamped to the extent | Uneven density and heavy overlap, a harder BVH case |

Benchmark matrix (kept small):

| Scene | Sizes | Seed | Use | When |
|---|---|---|---|---|
| `field_uniform` | 10K, 100K, 1M | 1 | All Core sweeps | Core (G1) |
| `field_clustered` | 100K, 1M | 1 | Contrast case: uneven density, harder BVH | After G2, if time |
| `field_uniform` | 1M | 2 | Sensitivity to scene generation | After G2, if time |

Camera fitting matters: a fixed camera position cropped the field in an earlier draft. With terrain heights in [−2.5, 2.5] and sphere tops ≤ 5.5, the fit gives about `D ≈ 1.29·S` at 10K and `1.15·S` at 1M. `rt_gen` tests that all 8 AABB corners land inside the 95% frame.

Workload diagnostics, computed from the AOVs at no extra cost: **visible fraction** (distinct hit `prim_id` values ÷ N) and **hit fraction** (pixels with a hit ÷ all pixels). They show whether a size step actually added visible work. *Deferred:* BVH node-visit counts via an `RT_COUNTERS` build, if traversal cost needs explaining.

If the seed-2 scene runs (after G2), its ratios are reported as "sensitivity to scene generation". They aren't expected to match seed 1 within spread, since the geometry differs.

## Sweeps

Core sweeps, varying one dimension at a time around a baseline. Baseline: `bench_1m`, 1920×1080, 1 point light, `lbvh`, `mega`, `explicit`, B = R = max.

| Dimension | Values | Machines |
|---|---|---|
| B and R together | 1, 2, 4, 8, 16, 32, 64, 72 | Vista |
| B and R together | 1, 2, 4, 8, 16, 24, 32 | Laptop (P/E-core caveat) |
| B alone (GPU runs) | 1, 8, 72 | Vista |
| Scene size | 10K, 100K, 1M (`field_uniform`) | Both |
| Layout / seed (after G2, if time) | `field_clustered` 100K and 1M; `field_uniform` 1M seed 2 | Vista |
| BVH method | `median`, `lbvh` (+ `sah` if built) | Both |
| Light count L | 1, 2, 4 | Both |
| Resolution | 640×480, 1920×1080 | Both |

Extensions add their own dimensions: memory modes, pipeline, Embree.

**Running them (C12):** `scripts/tacc/sweep.py` turns each row into a preset (`cpu-threads`, `gpu-build-threads`, `size`, `bvh`, `resolution`, `lights`, `tapp`). `g2` is the first T_app / T_br / T_frame at 3 sizes. `core` is every Core row. `smoke` is the known scene, for checking a node. The size preset also measures the 1-core CPU baseline at each size, which the target ratios need. Light count L uses `rt_render --lights N`, which replaces the scene's light with N copies on a ring about the Y axis, each with 1/N of the intensity, so total power is fixed and only the shadow-ray count changes; the L = 1 points are the `size` points. `ratios.py` counts only one-light rows toward the targets and prints a separate light-count table. On Vista, from the repo root in `$WORK`:

```bash
mkdir -p logs
sbatch --export=ALL,PRESET=verify scripts/tacc/sweep.sbatch        # G2 correctness checks only
sbatch scripts/tacc/sweep.sbatch                                   # verify, then the g2 preset
sbatch -p gh -t 01:00:00 --export=ALL,PRESET=core scripts/tacc/sweep.sbatch
sbatch -p gh -t 02:00:00 --export=ALL,PRESET="lights core" scripts/tacc/sweep.sbatch   # several presets, one allocation
```

Each job writes `$SCRATCH/rt-results/<presets>-<job id>/` (a several-step `PRESET` gets one subdirectory per step): `identity/` (nvidia-smi, lscpu, gcc, nvcc, modules, ldd, OpenMP environment), `points/<point>/` (results files and the exact command), `verify/summary.txt`, `sweep.csv`, and `ratios.md` (the tables of `ratios.py`). The CSV has one row per point and quantity, with the components of the median run. Finished points are skipped on resubmission.

Two more steps can go in `PRESET`. `final` runs the interleaved final measurement ([Targets](#targets-from-proposal)) from `FROM=<sweep.csv>`, or else from the sweep earlier in the same job. It writes `final.csv`, `final_plan.json` (the selection and the sweep values it came from) and `ratios.md`. `profile` runs `scripts/tacc/profile.sh` ([Profiling](#profiling)). For the report, the whole sequence fits in one allocation:

```bash
sbatch -p gh -t 02:00:00 --export=ALL,PRESET="core final profile" scripts/tacc/sweep.sbatch
sbatch --export=ALL,PRESET=final,FROM=$SCRATCH/rt-results/core-<job id>/sweep.csv scripts/tacc/sweep.sbatch   # final runs alone, from an earlier sweep
python3 scripts/tacc/sweep.py final --sweep docs/data/vista-core-1164735.csv --machine vista --scenes scenes/generated --dry-run   # selection and time estimate, no measuring
```

The final step reruns 21 configurations in 5 rounds. That is about 5 minutes of measuring on Vista, estimated from the Nov 23 sweep. It warns if the sweep it chose from was measured at a different commit. Finished rounds are skipped on resubmission. The laptop runs `g2` in about 1.5 minutes (`--machine laptop`); copy `sweep.csv` and `identity/` out of `$SCRATCH` before it is purged. `python3 scripts/tacc/ratios.py sweep.csv` prints the target ratios (CPU\* and GPU\* chosen by the rule above, with the number of thread counts CPU\* came from) and the T_br breakdown; results so far are in [RESULTS.md](RESULTS.md).

## Extra benchmarks

These are CPU-against-GPU comparisons on scenes outside the Core matrix. They are not swept: each scene runs once on the CPU at B = R = the top thread count (72 on Vista) and once on the GPU at B = the top count, with lbvh and lit shading. They don't count toward the targets. `scripts/tacc/extra.py` runs them, and `PRESET=extra` does so in a Vista job.

| Benchmark | Scene | Primitives | Resolution | Measures |
|---|---|---:|---|---|
| `terrain_10k` | `bench_10k`'s terrain without spheres (`rt_gen --layout terrain --primitives 10000`, IC8), small enough to see the individual triangles | 9,800 triangles | 1920×1080 | T_br, T_frame |
| `map_crew` | Among Us map with crew models (`scenes/meshes`) | 3,024,680 triangles | 3840×2160 | T_br, T_frame |
| `dragon_gold` | XYZ RGB dragon, gold mirror, on the mesh stage (`scenes/meshes`) | 249,884 triangles + 8 spheres | 3840×2160 | T_br, T_frame |
| `balls` | 48 scattered spheres: matte, glossy, mirror and glass; 4 point lights (`scenes/extra/balls.json`, from `scripts/bench/make_balls.py`) | 48 spheres + 2 triangles | 3840×2160 | T_br, T_frame |
| `house` walkthrough | `house.json` along `house_walk.json` (`scenes/meshes`) | 2,145,083 triangles | 1920×1080 and 3840×2160 | Per-frame time over 240 frames, and the total |

Stills are measured like sweep points, with 1 warm-up, then the median of 5 runs. The walkthrough uses `rt_render --path`, which prepares once and then times one render and download per camera; PNG writing is not included. Frame time varies along the path because the view changes, so its min and max describe the path rather than noise. The headline walkthrough number is the total render time of the 240 frames (8 s of video at 30 fps).

`extra.py` also checks correctness as it runs:

- Every still's CPU and GPU PFM and AOV files are compared byte for byte. The files are deleted when they match and kept when they differ.
- Every walkthrough frame's PNG is compared between the two backends.
- With ffmpeg available, the GPU frames are encoded into `videos/house_<W>x<H>.mp4`, with the same settings as `make_video.sh`. If ffmpeg is missing, cannot start, or fails, the run only warns and keeps the GPU frames for later encoding. On a Vista `gh-dev` compute node (Nov 27), `/usr/bin/ffmpeg` exists but fails with `libunwind.so.8: cannot open shared object file`, so on Vista the frames are kept: copy them to the laptop and encode them there with the command below.

It writes:

- `sweep.csv`: the stills, in the sweep format;
- `walks.csv`;
- `extra.md`: the CPU-against-GPU tables.

On the laptop at `5e6ea0a`, and on Vista at `94fadfe` (Nov 27, [RESULTS.md](RESULTS.md#extra-benchmarks)), all four stills are byte-identical between CPU and GPU, and every walkthrough frame is identical (240 of 240 at both resolutions on Vista).

```bash
python3 scripts/tacc/extra.py plan --machine vista                         # commands, and which scenes are missing
python3 scripts/tacc/extra.py run --machine laptop --rt-render build/apps/rt_render/rt_render --out out/extra
sbatch -p gh -t 01:00:00 --export=ALL,PRESET=extra scripts/tacc/sweep.sbatch
```

**Scene files.** `scenes/meshes` is not in git, and generated scenes are never regenerated on another machine (INTERFACES §2), so the scenes are copied from the laptop to the same paths in the Vista clone. That is 9 files and about 218 MB. `<$WORK>` stands for the output of `echo $WORK` on Vista:

```bash
rsync -av scenes/generated/terrain_10k.{json,bin} <user>@vista.tacc.utexas.edu:<$WORK>/Ray-Tracer/scenes/generated/
rsync -av scenes/meshes/{house,map_crew,dragon_gold}.{json,bin} scenes/meshes/house_walk.json \
  <user>@vista.tacc.utexas.edu:<$WORK>/Ray-Tracer/scenes/meshes/
```

Check each copy with `scene_dump --verify` against these hashes (laptop, Nov 26):

| Scene | content_hash | scene_hash |
|---|---|---|
| `terrain_10k` | `0x032dec4a8b57a0cf` | `0x71221c8fec1d09e8` |
| `map_crew` | `0x3d90535998d7c173` | `0x5777c336bd354655` |
| `dragon_gold` | `0xcad8a1eb6ed7215f` | `0xecf2b1bcbdba53d6` |
| `house` | `0xb5fef591984e6c7b` | `0xf0812e6b01d51b0f` |
| `balls` (in git) | `0x4db24d7645a3739b` | `0x8842f81161e3c4d2` |

`extra.py` fails before measuring if a scene is missing. `EXTRA_ONLY=terrain_10k,balls` in the job, or `--only` on the command line, runs a subset.

## Machine identity

Record from the allocated node, not from documentation:

- GPU: `nvidia-smi --query-gpu=name,uuid,driver_version,memory.total --format=csv`, plus `cudaGetDeviceProperties` name and SM count. Vista's GH nodes are documented as both H100 and H200; `nvidia-smi` on node `c642-061` (Oct 28) reports "NVIDIA GH200 120GB", 132 SMs, driver 590.48.01 in different places; the docs say "Hopper (sm_90)", and the report uses the recorded name.
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
| Placement / first touch | `explicit`: device memory from `cudaMalloc`, filled by a host-to-device `cudaMemcpy` (DMA). No CPU-touched pages involved. `managed`: fresh `cudaMallocManaged` buffer, filled by a CPU copy (CPU first touch), then an optional prefetch. `system`: fresh `mmap` pages filled by a CPU copy (CPU first touch). `system-reuse`: touched once at load time; later placement depends on migration. |
| `prefetch` | `managed` only: `cudaMemPrefetchAsync` to the GPU, timed as `upload` |
| `advice` | `none`, `preferred-gpu`, `read-mostly` (`cudaMemAdvise`) |
| Residency note | Under `system` and `system-reuse`, pages may migrate to GPU memory over repeated accesses, depending on driver configuration. T_frame runs record frame-by-frame times to expose that drift. **Verify on Vista** how to query the migration settings. |

Comparisons:

- `system` vs `explicit` (both fresh per preparation) compares CPU-resident data accessed by the GPU against an explicit copy into GPU memory.
- `system-reuse` is reported as its own experiment. It shows what happens when geometry stays resident across preparations, which is a different question, and it's never presented as a T_br result for `system`.
- The `system` copy into fresh pages is timed as `upload`, so `system` T_br includes it. It's a host-side memcpy, not a transfer. The report states this.

## Node-hour budget

About 50 node-hours. Develop on `gh-dev`. Production sweeps on `gh`. Mikail keeps the log; `bash scripts/tacc/node_hours.sh <job id> --total <running total>` prints a row from `sacct`:

| Date | Who | Queue | Job ID | Nodes × hours | Purpose | Running total |
|---|---|---|---|---|---|---|
| | | | | | | |

## Quality protocol

Shared CPU/GPU code proves the backends are **consistent**, not that they're **correct**. A shared intersection or shading bug makes both backends agree perfectly. Correctness therefore comes from checks that don't reuse the code under test.

### 1. Independent correctness checks (CI, as each feature lands)

- **Analytic known cases**, with expected values computed by hand or in Python by someone other than the function's author:
  - visibility: expected `prim_id` at chosen pixels;
  - Lambert value at a sphere's pole and at a floor point;
  - 1/d² falloff;
  - shadow boundary location of a sphere on the floor;
  - no shadow acne on an unoccluded plane.
  - Tolerance: relative 1e-5 on linear values.
- **Double-precision oracle** (test-only code, written by Arek, not Mikail; `tests/oracle/`): ray-sphere and ray-triangle in `double`, compared with the float versions on seeded random rays. Hits must agree except within a band around edges, silhouettes and interval ends, derived from the float code's roundoff (eps = 2^-24; derivation in the comment of `tests/oracle/test_oracle.cpp`):
  - sphere: hit or miss is undetermined when `|R² − dist²| ≤ 2R·ε_L`, with `ε_L = 32·eps·(|origin − center| + R)`; a root is wrong by at most `min(√D, D/h)/|dir| + 32·eps·scale`, where `D = 2R·ε_L` and `h` is the half chord;
  - triangle: inside or outside is undetermined when `|margin| ≤ 8·eps·reach²/A`, with `reach` the farthest vertex from the origin and `A` the area projected perpendicular to the ray;
  - inside the band any decision is accepted, but a reported hit must still match the oracle's `t` and barycentrics within the tolerance. Rays in a triangle's own plane are not judged.
- **BVH vs brute force**: identical `Hit` (same `prim`, bit-identical `t`) on random rays and scenes.
- **Determinism**: CPU 1 thread vs N threads, repeated BVH builds, and repeated GPU runs are bit-identical (linear PFM and AOVs).

### 2. Image comparison (`rt_compare`)

Inputs: linear PFM plus the `prim_id` and `light_mask` AOVs from both images. All checks run on **linear floats before clamping**. `rt_compare` prints a pass/fail line per check and exits non-zero if any check fails.

Float differences legitimately flip a few pixels along silhouettes and shadow boundaries, so those get a small, bounded allowance. The allowance requires the alternative to be locally plausible. "A neighbor is different" alone isn't enough, because on a dense mesh nearly every pixel has a different neighbor.

| Check | Rule | Fails if |
|---|---|---|
| Nonfinite | No NaN/Inf in either image | Any |
| Explained hit mismatch | A pixel with a different `prim_id` is explained only if the test ID appears among the reference IDs in its 3×3 neighborhood (background counts as an ID), and vice versa | Any unexplained mismatch |
| Explained shadow mismatch | Same `prim_id`, different `light_mask`: the test mask must appear among the reference masks in the 3×3 neighborhood | Any unexplained mismatch |
| Mismatch budget | Explained hit + shadow mismatches ≤ `B_px` of all pixels | Over budget |
| Shading agreement | Same `prim_id` and `light_mask`: `|a − b| ≤ τ_abs + τ_rel·|ref|` per channel | Any pixel |
| Broad summary | % of pixels within 2/255 in 8-bit sRGB | Reported only |

Starting values: `τ_abs = 1e-5`, `τ_rel = 1e-4`, `B_px = 0.05%` of pixels (~154 at 640×480, ~1,000 at 1080p). These started as guesses, to be set from real CPU-vs-GPU pairs at G2. If something exceeds tolerance, find the cause (often FMA contraction) before loosening anything.

**G2 calibration (Nov 13, laptop, RTX 4070 Laptop, C10):** the starting values are kept. With the default FMA contraction, GCC and nvcc fused differently, and `rt_compare` failed `bench_1m` at 1080p: 1,407 shading-agreement pixels (at most 1/255 in 8-bit sRGB) and 46 unexplained shadow mismatches (isolated shadow-edge flips). `bench_100k` failed 28 and 9. The cause was fixed instead of loosening the tolerances: with contraction off everywhere ([BUILD.md](BUILD.md#compiler-flags)), CPU and GPU images and AOVs are byte-identical on `bench_10k`, `bench_100k` and `bench_1m` (LBVH, lit) and on the known scenes. The same holds on a Vista GH node (Grace + GH200, Nov 13, `verify.sh`): byte-identical on the known scenes and `bench_10k/100k/1m`, so no Vista-specific tolerance is needed ([RESULTS.md](RESULTS.md)).

**Core corruption tests** (`ctest`, with C8). Each corrupted copy of a reference image must fail, by the named check:

| # | Scene (640×480) | Corruption | Rejected by |
|---|---|---|---|
| 1 | `known_two_spheres` | One pixel's RGB set to NaN | Nonfinite |
| 2 | `known_two_spheres` | Sphere 0's pixels (RGB + AOVs) copied 3 px to the right | Explained hit |
| 3 | `known_two_spheres` | 10×10 block inside sphere 0, ≥ 10 px from its edge: RGB += 0.1 | Shading agreement |
| 4 | `known_two_spheres` | 20×20 block of lit floor, ≥ 10 px from shadow edges: light bit cleared, RGB set to ambient-only | Explained shadow |

Fixtures check their own setup before corrupting (the sphere is ≥ 20 px in radius, blocks lie inside a single region). A fixture that can't satisfy its setup fails, rather than passing silently.

### Deferred comparison hardening

Designed during review and **not required now**. Add these if a visibly wrong image ever passes the checks above, or if time allows after G2.

- **Explained color:** a mismatched pixel must also match the color of a neighbor that has its claimed ID (start tolerance `0.02 + 0.05·|c|`).
- **All-pixel backstop:** caps on visibly different pixels (0.1%) and mean error (1e-3) over all pixels, mismatches included.
- **Localization warning:** flags 16×16 tiles that contain clustered mismatches.
- **`known_dense_mesh`:** a level-6 icosphere (81,920 triangles) plus a terrain patch, where most triangles cover 1–4 px. It stress-tests the edge allowance.
- **Dense-mesh corruption tests:** non-adjacent ID swaps (must fail explained hit), 308 plausible swaps (must fail the budget), and swaps with wrong colors (must fail explained color). When building them, pick pixels that satisfy each test's assumptions: channel values ≥ 0.2 for color scaling, and an original ID still visible next to each swapped pixel. Also assert the intended corruption count.
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

At G3, enough to explain the results:

- `-Xptxas -v` register and local-memory (spill and stack) usage for each kernel. The traversal stack is dynamically indexed, so expect it in local memory.
- Nsight Compute: occupancy, local-memory traffic, achieved memory throughput for the megakernel.
- Nsight Systems: the timeline of preparation, transfers, and kernels.

`scripts/tacc/profile.sh` does all three, for the lit LBVH megakernel at 1920×1080 with B = all cores, on each benchmark scene:

```bash
bash scripts/tacc/profile.sh --build build --scenes scenes/generated --out out/profile            # laptop
sbatch --export=ALL,PRESET=profile scripts/tacc/sweep.sbatch                                      # Vista, after verify
```

It writes `summary.txt` with the numbers worth quoting:

- registers, stack and local memory per kernel, from `cuobjdump --dump-resource-usage` of the built binary, so no `RT_PTXAS_VERBOSE` rebuild is needed;
- the Nsight Systems kernel and memcpy totals;
- the main Nsight Compute metrics: occupancy, throughputs, hit rates and active threads per warp.

The full reports (`.nsys-rep`, `.ncu-rep`) are next to it, for `nsys-ui` and `ncu-ui`. Times measured under a profiler are not benchmark numbers. Nsight Compute replays the kernel and locks clocks to base, and Nsight Systems adds tracing overhead. On the laptop both tools work under WSL2 (nsys 2026.1.3, ncu 2026.2.1, from the CUDA 13.3 toolkit). The 1M run takes about 15 s. **On Vista:** the `cuda/13.3` module has no Nsight, so the first profile job (Nov 27) wrote only the `cuobjdump` resources (128 registers for the lit BVH kernel on `sm_90`, as on the laptop). The tools are in the HPC SDK that `cuda/13.3` comes from, `/home1/apps/nvidia/Linux_aarch64/26.9/compilers/bin` (`nsys` 2026.4.1, `ncu` 2026.2.1), and `env.sh` now appends that directory to `PATH`. The older SDK under `/opt/apps/nvidia/24.5` has wrappers that fail. Whether users can read the GPU performance counters is still to check: `profile.sh` names `ERR_NVGPUCTRPERM` in its summary if they can't.

Which machine to use:

- **The laptop** is for finding out why the kernel performs as it does, and for trying changes without spending node hours. Registers, occupancy limits, divergence, and whether the kernel is bound by latency or by bandwidth mostly carry over from the 4070 (`sm_89`) to the GH200 (`sm_90`).
- **Vista** gives the numbers for the report. It is the machine the ratios come from, and it differs from the laptop in ways that change the profile:
  - the GPU: 132 SMs against 36, HBM3, a larger L2;
  - the host link: NVLink-C2C against PCIe;
  - the Grace host, where preparation runs.

  Rerun `profile.sh` there and quote those numbers, with the register count checked against the `sm_90` build.
