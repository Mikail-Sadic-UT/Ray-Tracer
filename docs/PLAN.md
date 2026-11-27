# Plan (living)

What is known about the remaining work after G2, as of **Nov 27**. This is a working list, not a commitment: items get added, reordered, dropped, and expanded as results come in. The fixed parts are the deadlines in [ROADMAP.md](ROADMAP.md#gates-and-schedule) and the Core deliverable in the [README](../README.md#required-deliverable). Everything else here can change.

Where things stand: Core is built and verified. The CPU renderer, median and LBVH builders, and the CUDA megakernel are in, CPU and GPU images are byte-identical on the known and benchmark scenes, and the Nov 27 Vista run of `dev` has the final ratios, the GH200 profile and the extra benchmarks ([RESULTS.md](RESULTS.md#current-vista-run-of-dev-94fadfe-nov-27)). What is left is the extension decision (#71), the remaining measurement gaps, the quality review, and the report and slides.

Rules for this file: edit it freely, keep it short, and move finished items to the "Done" list with the PR. Interface changes go in [INTERFACE_CHANGES.md](INTERFACE_CHANGES.md); [INTERFACES.md](INTERFACES.md) is frozen.

## Where the headline ratios stand

Vista GH200, 1M primitives, 1080p, LBVH (T_br, median of 5 interleaved rounds; `dev` at `94fadfe`, job 1175084, Nov 27; [details](RESULTS.md#current-vista-run-of-dev-94fadfe-nov-27)):

| Target | Now | Note |
|---|---:|---|
| ~10×: 1 core / best CPU | 56.2× | Met (was 29.8× at G2) |
| ~2×: best CPU / GPU | 2.43× | **Met at 1M** (was 1.46× at G2); 5.76× at 10K and 3.60× at 100K, with spread at most 6% |
| ~20×: 1 core / GPU | 137× | Met (was 43.4× at G2) |

The 2× target was missed at G2 because of preparation, not rendering (47 ms of the GPU's 53.8 ms). PR #75 (#68, #67) cut preparation to 10.0 ms at B = 72, and the Nov 23 and Nov 27 Vista runs measure 2.40× and 2.43× at 1M; the materials and the 128-register kernel changed T_frame by 7% on both backends and did not move the ratio. Layout (7.0 ms at B = 72, grows past 16 threads) is now the largest preparation stage; at about 2 ms the ratio would be near 3×.

## G3 checklist

G3 is an internal checkpoint (target Nov 18, now past; [ROADMAP](ROADMAP.md#gates-and-schedule)). Its exit criteria and where each stands:

| Criterion | Status | Left to do |
|---|---|---|
| Preparation scaling, B = 1 to 72 | Done at all three sizes ([RESULTS.md](RESULTS.md)) | |
| CPU render scaling | Done at all three sizes (70× on 72 threads at 1M) | |
| GPU breakdown | Done: T_br components, and the kernel-level view on the laptop and the GH200 | |
| Profiling (`-Xptxas -v`, Nsight) | Done on both machines: 128 registers, no spills, latency- and divergence-bound ([RESULTS.md](RESULTS.md#profiling-gh200)); Nsight and the GPU counters work on a GH node | |
| Preliminary 10× / 2× / 20× ratios, with spread | Done: all three met at every size, from the final runs (Nov 27) | |
| Scope and extensions reviewed | Done (Nov 27): no further extensions, no clustered or seed-2 scenes | |

## Vista runs

Done Nov 27 on `gh-dev` (jobs 1175084, 1175157, 1175169): `verify` (ctest 358/358, CPU = GPU byte-identical), the 50-point `core` sweep, the final runs, the profile and the extra benchmarks. Data is in `docs/data/vista-*-1175084.*`. To rerun after a Core code change:

1. On a compute node: `git pull`, then `source scripts/tacc/env.sh && bash scripts/tacc/build_vista.sh` (the build takes about a minute when only a few files changed).
2. `sbatch -p gh-dev -t 02:00:00 --export=ALL,PRESET="core final profile extra" scripts/tacc/sweep.sbatch`. The whole job took about 15 minutes on Nov 27. Use `gh-dev`: on Nov 27 `gh` had about 500 jobs pending and no start estimate.
3. The extra step needs `terrain_10k` (`bash scripts/fetch_scenes.sh scenes-extra-v1`) and the mesh scenes (`house`, `map_crew` and `dragon_green`, copied from the laptop) in place (BENCHMARKING.md "Extra benchmarks"). The Vista numbers for `dragon_green` and `showcase` were measured on Nov 27 (job 1175611, 12 s): export `PRESET=extra VERIFY=0 EXTRA_ONLY=dragon_green,showcase` and run `sbatch -p gh-dev -t 00:30:00 scripts/tacc/sweep.sbatch`, because `--export=` splits on commas. `ffmpeg` on the compute nodes cannot start, so copy the GPU frames of the house walkthroughs to the laptop and encode them there.
4. Copy the results out of `$SCRATCH` and update RESULTS.md from them.

## Work items

Roughly in the order I expect to do them. Issue numbers are the longer write-ups.

### Likely next

| Item | Issue | Why | Done when | Notes |
|---|---|---|---|---|
| Layout stage of the LBVH build | #68 | Now the largest preparation stage: 6.8 ms at B = 72, up from 3.6 ms at B = 16 | Optional: P near 5.6 ms (ratio near 3×) | Mostly emit and freeing the scratch. Emit is probably bound by the serial zero fill of `Bvh::nodes` (review of #75: emit 7.0 ms at B = 3, 9.3 at 8, 9.3 to 10.8 at 24 on the laptop); avoiding it means `Bvh::nodes` stops being a value-initialized `std::vector`, which is an interface change (an IC entry). Other ideas: a fused bottom-up (Apetrei-style) hierarchy + refit, a smaller temporary node, reusing scratch. The 2× target is already met, so this is only worth doing if time is left |
| Measurement gaps | #70 | GPU T_app is unstable between runs (spread 41% to 174% in the Nov 27 sweep; 146 ms in the sweep and 257 ms in the final runs at 100K) | Checklist in the issue | The thread sweeps at 10K and 100K are done. Profiling first checks whether the kernel is worth tuning at all (5.6 ms of the GPU's 16.4 ms) |

### Decisions

- **Extensions (#71): decided Nov 27 to run none beyond those already done, for lack of time.** E1, E3, E4 and E5 are cut. The plan on Nov 23 (working solo) was: camera-path video first (done, see below), then E2 Blinn-Phong (done), then mirror reflections (done), then colored glass with tinted shadows (done), and a showcase scene and orbit video for the slides (done). All behind material flags, so the benchmark scenes, goldens and CPU/GPU byte equality stay unchanged; mirror images turned out byte-identical between CPU and GPU, so they need no separate comparison category. Candidates and what the data says, kept for the report's future-work section. Sizes are the ROADMAP timeboxes; the owners listed there assumed a four-person team, so treat them as sizes only:
  - **E3 parallel binned SAH (1 week):** addresses the tree-quality tradeoff. The median tree renders 2× faster than LBVH on the CPU and 2.6× on the GPU, but its serial build makes T_br 8.9× (CPU) and 20× (GPU) slower after #68. The most a better tree can save is about 3.5 ms of GPU render, so SAH helps GPU T_br only if its build costs less than that on top of LBVH; the case rests on T_frame and the CPU render.
  - **E1 memory modes (4 days):** the proposal's data-movement question. With explicit memory on the GH200, transfer is already about 0.3 ms at 1M, so there may be little to show. The comparison could still be informative.
  - **E2 Blinn-Phong (2 days), E4 Embree (4 days), E5 wavefront (1 week), E6 `obj2bin` + Blender export (3 days):** all independent of the above. E4 would strengthen the "best CPU" tier. E5 and E6 start only after G3, and E5 depends on the profile. ROADMAP expected E1 to E3 to fit and the rest to be cut.
  - **Core optimization only.** This is the choice.
  - Extensions must not block final runs and the report.

### Later

- **Scenes (#72):** download without `gh`, Vista mirror path, cross-platform hash check.
- **Kernel tuning (optional):** the profile shows the lit BVH kernel is latency- and divergence-bound at 128 registers (25% theoretical occupancy on the GH200, no spills; [RESULTS.md](RESULTS.md#profiling-gh200)). The 2× target is met and the kernel is 5.9 of the GPU's 17.0 ms T_br at 1M, so there is little to gain in T_br.
- **Final runs (C14):** interleaved reruns of the 1-core baseline, best CPU, best GPU, and fixed-72 configurations, 5 rounds, on the laptop and Vista. Done on Vista and on the laptop on Nov 27 (`sweep.py final`, `PRESET=final`; [BENCHMARKING.md](BENCHMARKING.md#targets-from-proposal); [RESULTS.md](RESULTS.md#laptop-run-rtx-4070-laptop-nov-27)). Node-hour log rows for the jobs so far (1142824, 1142829, 1142842, 1164735) through `scripts/tacc/node_hours.sh`.
- **Quality review:** done Nov 27 (Mikail reviewed the pictures; one reviewer, where [BENCHMARKING.md](BENCHMARKING.md#quality-protocol) asks for two or more, so the report should say so). Still to do: the report's validation section.
- **Report, slides, release:** reproducible release with the scene archive, commit hash, and exact commands. Dates in the ROADMAP schedule. The `final` tag (PR `dev` to `main`, [WORKFLOW.md](WORKFLOW.md#checkpoints-and-check-ins)) waits for the report: the numbers in RESULTS.md come from `94fadfe` (Vista) and `f9bdc3a` (laptop), and the tag should sit on the commit the report cites.

## Parked

Not planned. Listed so they are not lost; each can be pulled into the plan if its trigger happens.

- **Stretch** ([STRETCH.md](STRETCH.md)): OptiX backend, SIMD packet tracing, GPU-side LBVH build, extra experiments. The GPU-side build would attack the 47 ms preparation directly, but it is large. Look at it only if #68 finds the CPU build cannot get near 17 ms.
- **Deferred items** ([ROADMAP](ROADMAP.md#deferred-designed-not-required)), each with its own trigger:
  - extra `rt_compare` checks and dense-mesh corruption tests: a visibly wrong image passes `rt_compare`;
  - exact goldens in a pinned CI container: tolerance goldens miss a regression;
  - `RT_COUNTERS` traversal counters: traversal cost needs explaining in the report;
  - `--no-components` overhead check: instrumentation seems to distort small scenes;
  - `field_clustered` and seed-2 scenes: dropped Nov 27 for lack of time; the report lists them as not measured;
  - heavier review and CI process: the problem it solves actually happens.

## Things to watch

- **Numbers before changes.** Optimizations to Core code must keep CPU and GPU byte-identical and the goldens unchanged. `scripts/tacc/verify.sh` is the quick check on a laptop or Vista node.
- **Noise.** The sweep records spread. GPU T_app has 13% to 205% spread depending on size. Don't quote a ratio whose spread is above 10% without a rerun.
- **Contraction is off project-wide.** Any new numeric code is compiled with `-ffp-contract=off` and `--fmad=false`. Write `fmaf()` where a fused result is wanted.
- **No libm in shared code.** `tanf`, `sinf`, `powf` and friends round differently in glibc and CUDA, so a call in code that runs on both backends breaks byte-identical images for some inputs. `camera_ray` used `tanf` and did so for about a third of all fields of view (70 degrees among them) until `tan_det`; every scene we ship uses 45 degrees, which happens to agree, so it went unseen. Shared code uses `spec_pow` and `tan_det` (basic IEEE operations only); `test_camera_device` and the shading device tests compare host and device bit for bit.
- **Megakernel registers.** The lit BVH megakernel is at 128 registers on sm_89 and sm_90 (no spills), the most that still fits two 256-thread blocks per SM. Anything more in `shading.hpp` likely halves occupancy and slows every GPU number; check `cuobjdump --dump-resource-usage` (or `RT_PTXAS_VERBOSE`) before adding to it. The Nov 27 Vista run at 128 registers has GPU T_frame 7% above the 100-register run, the same as the CPU's rise, so the registers have not cost anything visible so far.
- **Node hours.** Vista jobs go in the node-hour log ([BENCHMARKING.md](BENCHMARKING.md#node-hour-budget)).

## Done since G2

| Item | PR |
|---|---|
| Extra benchmarks on the replaced scenes: `dragon_green` and the six-ball `showcase` at 4K on the laptop and Vista (CPU = GPU byte-identical; Vista CPU / GPU 6.9× to 8.6×, laptop 7.8× to 16.8×) | #98 and `mikail/vista-extra-new` |
| Release assets: scene set `scenes-extra-v1` (`terrain_10k`, with a manifest entry, verified through `fetch_scenes.sh`) and the release `results-nov27` with 6 benchmark images and their provenance. Map_crew and house renders held back (third-party models) | `mikail/release-assets` |
| Laptop sweep and final runs at `f9bdc3a`, run twice (the run with the lower 1M spread is reported): 2.94× CPU / GPU at 1M, PCIe transfers cost about 11 ms of the GPU's 68 ms T_br | `mikail/laptop-results` |
| Nov 27 Vista run of `dev`: final ratios (all three targets met at every size, 2.43× at 1M), GH200 profile, light-count and extra benchmarks, walkthrough videos; `env.sh` puts Nsight on `PATH`; `extra.py` survives a broken ffmpeg | #93 and this branch |
| Extra benchmarks: `scripts/tacc/extra.py` (CPU against GPU at the top thread count on `terrain_10k`, `map_crew`, `dragon_gold` and `balls` (replaced on Nov 27 by `dragon_green` and the six-ball `showcase`), and the house walkthrough at 1080p and 4K, with CPU = GPU byte checks and the video), `PRESET=extra`; `rt_gen --layout terrain` (IC8); `scenes/extra/balls.json` from `scripts/bench/make_balls.py`; `rt_render --path` prints the total frame time. Laptop: every still and walkthrough frame CPU = GPU byte-identical | `mikail/extra-benchmarks` |
| Final-run and profiling tooling: `sweep.py final` (CPU\* and GPU\* from a sweep, 5 interleaved rounds, `final.csv` with roles), `ratios.py` reads final runs by role, `scripts/tacc/profile.sh` (cuobjdump, Nsight Systems, Nsight Compute), `sweep.sbatch` steps `final` and `profile` and a `ratios.md` per step; laptop profile in RESULTS.md | `mikail/final-runs` |
| CUDA megakernel backend, `DeviceScene`, contraction off project-wide | #65 |
| Vista sweep, verification and node-hour scripts; first Vista results | #66 |
| Vista rerun of the branch: `verify.sh` 0 failures, ctest 307/307, `core` sweep at 46 points (job 1164735, data in `docs/data/`) | #75 |
| LBVH preparation: parallel centroids, Morton codes and radix sort, uninitialized scratch, intrinsic leading-zero count, layout counts in the refit; per-stage timing with `RT_BVH_STAGES=1`. Bytes unchanged (laptop) | `mikail/lbvh-scaling` (#68) |
| `validate_scene` builds error paths only on failure; `rt_render` re-checks only camera and light limit; scratch freed inside `layout` | `mikail/lbvh-scaling` (#67) |
| Device tests and `rt_render_cuda_cli` skip when no NVIDIA driver is installed (checked with a stand-in `libcuda`) | `mikail/lbvh-scaling` (#73) |
| IC2 overflow flag in `BvhQuery` (closes W8) and IC3 `AABB::empty()` and constants in `math.hpp` | `mikail/lbvh-scaling` (#69, #4) |
| Thread sweeps at every scene size | `mikail/lbvh-scaling` (#70, first item) |
| Light-count sweep: `rt_render --lights N` (N copies of light 0 on a ring, 1/N intensity), `lights` sweep preset, `num_lights` CSV column, light table in `ratios.py`. Measured on Vista on Nov 27 | `mikail/lights-orbit` (#70) |
| Misc tools: `scripts/bake_glb.py` bakes a glb's base-color textures into flat per-piece palette materials (per-material subdivision, alpha cut-outs, scene setup; Pillow and numpy; test skipped without them), `glb2obj.py --group-by material`, and three more materials in the mesh stage template. No renderer change | `mikail/misc-tools` |
| Camera paths: `rt_render --path FILE --path-frames N --frames DIR` renders along waypoints (centripetal Catmull-Rom through position, look-at and field of view; constant speed or equal time per segment; optional ease), `make_video.sh --path`. Host-only, first and last frames byte-identical to plain renders at the end waypoints; unit and CLI tests | `mikail/camera-path` |
| `camera_ray` no longer uses `tanf`: CPU and GPU rounded it differently for about a third of all fields of view (found with a 70-degree camera on a 2.3M-triangle map; CPU and GPU images differed in 3 pixels). New `tan_det` (double precision, basic operations only) is the correctly rounded float at every angle tried and equals the old value at 45 degrees, so benchmark and golden images are byte-identical; lit BVH kernel still 128 registers; `bench_1m` T_frame within noise on the laptop. Tests: host accuracy, and host vs device bit for bit | `mikail/camera-tan` |
| E6 `obj2bin`: OBJ meshes placed in a template scene (`scenes/templates/mesh_stage.json`), own parser in `obj_io`, CLI test; Stanford dragon (250K triangles) byte-identical on CPU and CUDA, gold dragon orbit video; per-group materials (`--group-material`) and `scripts/glb2obj.py` for glTF models. No renderer change | `mikail/obj2bin` |
| Self-shadow speckles on distant sphere tops: sphere hit points projected onto the sphere so shadow rays clear it (63 false-shadow pixels at `bench_10k`, none left); goldens regenerated. The lit benchmark images change in the last bits; timing should not | `mikail/sphere-acne` |
| Tinted shadows through glass (IC7), absorption measured between surface points, `known_showcase` scene and golden (every material) and its orbit video | `mikail/glass-shadows` |
| Colored glass: `MAT_GLASS` with Fresnel reflection and refraction and color absorption, `"glass"` / `"ior"` JSON keys, shared bounce stack, `known_glass` scene and golden, oracle case. IC6 | `mikail/glass` |
| Mirror reflections: `MAT_MIRROR` with reflectance `ks`, up to 4 bounces, `"mirror"` JSON key, `known_mirror` scene and golden, oracle case. IC5 | `mikail/mirror` |
| E2: Blinn-Phong highlights for `MAT_SPECULAR` and directional lights, deterministic `spec_pow` (CPU and GPU byte-identical), directional lights in scene JSON, `known_specular` scene and golden, oracle case. IC4, closes W3 | `mikail/blinn-phong` |
| Camera orbit: `rt_render --orbit N --frames DIR` (one preparation, N frames, T_frame over the path) and `scripts/make_video.sh` (MP4 via ffmpeg) | `mikail/lights-orbit` |
