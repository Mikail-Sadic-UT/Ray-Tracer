# User guide

A practical guide to compiling and using the tools currently in this repo. All commands run from the **repository root** in a Bash terminal. Examples use WSL2 Ubuntu 24.04 and write output under `out/`.

**Current scope:** CPU rendering works with spheres, triangles, Lambert point-light shading, hard shadows, and median or parallel LBVH acceleration. CUDA builds include GPU tests and `hello_cuda`; the production `rt_render --backend cuda` path is still pending in the Nov 12 snapshot. Some CLI choices describe future features and currently return an error.

## Find what you need

- [Compile and test](#compile-and-test)
- [Render your first image](#render-your-first-image)
- [Change the view and rendering settings](#change-the-view-and-rendering-settings)
- [Save colors and diagnostic outputs](#save-colors-and-diagnostic-outputs)
- [Compare two renders](#compare-two-renders)
- [Inspect and edit scenes](#inspect-and-edit-scenes)
- [Generate larger scenes](#generate-larger-scenes)
- [Download the fixed benchmark scenes](#download-the-fixed-benchmark-scenes)
- [Measure performance](#measure-performance)
- [Build CUDA tests](#build-cuda-tests)
- [Troubleshooting](#troubleshooting)
- [Command reference](#command-reference)

## Compile and test

### Requirements

For the CPU build, use GCC 13, CMake 3.28+, Ninja, OpenMP, and Python 3. Python is needed by the retrieval/benchmark scripts and CLI integration tests. On WSL Ubuntu 24.04:

```bash
sudo apt update
sudo apt install g++-13 cmake ninja-build python3
```

CMake downloads pinned versions of JSON, CLI, image, and testing dependencies during the first configuration. That step needs internet access. For full platform setup, see [BUILD.md](BUILD.md), including its macOS CPU instructions.

### Configure, then compile

```bash
cmake -S . -B build-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DRT_ENABLE_CUDA=OFF
cmake --build build-cpu -j 8
```

`-S .` selects the source directory; `-B build-cpu` selects the build directory. `Release` enables optimizations. `-j 8` allows eight compilation jobs at once; it does not set the renderer's thread count.

After editing code, rerun the build command. CMake/Ninja rebuild the affected files. Rerun configuration when changing build options. To make a separate debug build, use another directory with `-DCMAKE_BUILD_TYPE=Debug`.

### Run tests

```bash
ctest --test-dir build-cpu --output-on-failure
```

List available tests or run a subset:

```bash
ctest --test-dir build-cpu -N
ctest --test-dir build-cpu -R 'Trace|Traverse' --output-on-failure
```

`-R` selects tests by a regular expression over their names. Use the listing to find names for other modules.

### Executables

| Tool | CPU build path | Purpose |
|---|---|---|
| `rt_render` | `build-cpu/apps/rt_render/rt_render` | Render images and collect timings |
| `scene_dump` | `build-cpu/apps/scene_dump/scene_dump` | Inspect and validate scenes |
| `rt_gen` | `build-cpu/apps/rt_gen/rt_gen` | Generate spheres and terrain |
| `rt_compare` | `build-cpu/apps/rt_compare/rt_compare` | Compare linear images and diagnostics |

Each executable accepts `--help`.

## Render your first image

```bash
mkdir -p out/practice
./build-cpu/apps/rt_render/rt_render \
  --scene scenes/known/known_two_spheres.json \
  --backend cpu --bvh median --shade lit \
  --render-threads 4 \
  --out out/practice/two_spheres.png
```

Open the PNG to see red and blue spheres on a floor, with lighting and cast shadows. The floor is two triangles. On WSL, open the folder in Windows Explorer:

```bash
explorer.exe out/practice
```

The scene JSON supplies the camera and resolution (640×480 for this scene). The renderer loads the scene, builds the BVH, renders pixels, and saves the image. Create output directories before running.

## Change the view and rendering settings

### Resolution and threads

Add these options to a render command:

```bash
--width 1920 --height 1080 --render-threads 8
```

Width and height must each be in `1..16384`. More render threads distribute pixel tiles across more CPU threads. The CPU loop uses dynamic 16×16 tiles.

Preparation and rendering have separate counts: `--build-threads B` and `--render-threads R`. The median builder is serial. Use `--bvh lbvh --build-threads 8` for parallel preparation; rendering still uses `--render-threads R`.

### Camera overrides

You can change the view without editing the JSON:

```bash
./build-cpu/apps/rt_render/rt_render \
  --scene scenes/known/known_two_spheres.json \
  --cam-pos 3 2 6 --cam-look 0 0.5 0 --cam-vfov 40 \
  --out out/practice/new_view.png
```

`--cam-pos` sets the camera position; `--cam-look` sets its target. Each takes three coordinates. `--cam-vfov` sets the vertical field of view in degrees. Coordinates use Y-up. The camera position and target must differ, and the field of view must lie strictly between 0 and 180 degrees.

### Normal visualization

Use `--shade normal` instead of `--shade lit` to show surface normals as colors. This skips lights and shadows, making it useful for inspecting geometry and tracing. Use lit shading for performance comparisons.

### BVH versus brute force

`--bvh median` builds the serial baseline tree. `--bvh lbvh` builds a parallel Morton-code tree with deterministic output across build thread counts. `--bvh none` tests every primitive directly. Brute force is useful for checking small scenes, but becomes expensive on large ones. `--max-leaf-size 4` controls how many primitives a BVH leaf may contain; 4 is the default.

## Save colors and diagnostic outputs

PNG is convenient to view. For numerical comparisons, save **PFM**, which preserves linear, unclamped RGB floats. PNG clamps colors and converts them to sRGB.

```bash
./build-cpu/apps/rt_render/rt_render \
  --scene scenes/known/known_two_spheres.json \
  --bvh median --shade lit --render-threads 4 \
  --out out/practice/reference.png \
  --pfm out/practice/reference.pfm \
  --aov primid,lightmask
```

This produces:

| File | Contents |
|---|---|
| `reference.png` | Viewable image |
| `reference.pfm` | Linear RGB values |
| `reference.primid.u32img` | Which primitive each pixel hit; miss ID is `0xFFFFFFFF` |
| `reference.lightmask.u32img` | Bits identifying which lights contributed at each pixel |

The extra outputs are called AOVs. A light contributes when it faces the surface and its shadow ray is unblocked. `--aov` requires `--pfm`; the light-mask output supports at most 32 lights. `.u32img` files are binary diagnostics, not ordinary viewable images.

## Compare two renders

First create the reference above. Then render the same view with brute force:

```bash
./build-cpu/apps/rt_render/rt_render \
  --scene scenes/known/known_two_spheres.json \
  --bvh none --shade lit --render-threads 4 \
  --pfm out/practice/brute.pfm --aov primid,lightmask

./build-cpu/apps/rt_compare/rt_compare \
  out/practice/reference.pfm out/practice/brute.pfm
```

The first positional argument is the reference; the second is the test image. Both need their matching primitive-ID and light-mask sidecars. The checks should pass for this comparison.

The tool checks nonfinite colors, unexplained geometry/shadow differences, a mismatch budget, and shading agreement. Small locally plausible edge differences have a bounded allowance. The final sRGB summary is informational. Exit status is zero on success and nonzero on failure.

The default linear-color tolerance is `1e-5 + 1e-4 × |reference|`, with an explained-mismatch budget of 0.05% of pixels. Options are `--tau-abs`, `--tau-rel`, and `--budget-percent`. Investigate mismatches before changing tolerances; see [BENCHMARKING.md](BENCHMARKING.md#quality-protocol).

## Inspect and edit scenes

```bash
./build-cpu/apps/scene_dump/scene_dump \
  --verify scenes/known/known_two_spheres.json
```

This loads and validates the scene, then prints counts, camera settings, geometry/content hash, and scene hash. Loading binary geometry also checks its header and content hash. `--verify` does not by itself verify the release manifest's SHA-256 values.

Use `--json` to print a normalized inline representation. Large binary scenes can produce a lot of text. Material names and generator metadata are omitted from normalized output because they are not retained in the loaded scene type.

For a small custom scene, copy a known scene:

```bash
cp scenes/known/known_two_spheres.json out/practice/custom.json
```

Edit its camera, material `albedo` colors, point-light position/intensity, sphere centers/radii, or triangle vertices, then validate and render the copy. Material references are zero-based indices into `materials`. Core shading uses diffuse materials and point lights. Coordinates and colors must be finite; radii must be positive; colors/intensities must be nonnegative. Degenerate triangles are allowed but never hit.

Version-2 scenes may store geometry inline or reference a `.bin` file. Binary paths resolve relative to the scene JSON, so keep the JSON and referenced geometry together. Full format and validation rules: [INTERFACES.md §2](INTERFACES.md#2-scene-image-and-file-formats).

## Generate larger scenes

```bash
mkdir -p out/generated
./build-cpu/apps/rt_gen/rt_gen \
  --layout uniform --primitives 10000 --seed 1 \
  --out out/generated/dev_10k

./build-cpu/apps/scene_dump/scene_dump \
  --verify out/generated/dev_10k.json

./build-cpu/apps/rt_render/rt_render \
  --scene out/generated/dev_10k.json \
  --bvh median --shade lit --render-threads 8 \
  --out out/generated/dev_10k.png
```

The generator writes JSON metadata and binary geometry. `--out` accepts a prefix or a `.json` path. `--primitives` is the exact total number of spheres plus triangles and must be at least 4 and below `2^31`. Only `uniform` is currently supported.

Scenes contain colored spheres over a height-field terrain, a point light, and a fitted 1920×1080 camera. A fixed seed controls sphere placement; terrain shape is fixed. Generation is repeatable on the same recorded environment, but math-library differences can change bytes across machines. Use local generation for development; use the published files for comparable benchmarks.

## Download the fixed benchmark scenes

```bash
RT_SCENE_DUMP="$PWD/build-cpu/apps/scene_dump/scene_dump" \
  bash scripts/fetch_scenes.sh scenes-v1
```

The explicit executable path ensures the script uses your CPU build. Python 3 is required. Retrieval uses `gh` when available, otherwise the manifest URLs. It verifies sizes/hashes, decompresses geometry, validates scenes, and installs them in `scenes/generated/`. Valid cached files can be reused.

| Scene JSON | Spheres | Triangles | Total |
|---|---:|---:|---:|
| `bench_10k_s1.json` | 5,000 | 5,000 | 10,000 |
| `bench_100k_s1.json` | 50,072 | 49,928 | 100,000 |
| `bench_1m_s1.json` | 500,000 | 500,000 | 1,000,000 |

Pass, for example, `--scene scenes/generated/bench_100k_s1.json` to `rt_render`. Use `--bvh median` for the serial baseline or `--bvh lbvh --build-threads 8` for parallel preparation.

Optional environment overrides: `RT_SCENE_MANIFEST` selects the manifest and `RT_SCENE_DEST` selects the installation directory. Release retrieval through `gh` was verified at publication; direct-URL retrieval and loading on Vista remained open checks in the Nov 11 notes. See [ROADMAP C6](ROADMAP.md#c6-generator-bin-hash).

## Measure performance

### Preparation plus rendering, and prepared frames

```bash
./build-cpu/apps/rt_render/rt_render \
  --scene scenes/generated/bench_100k_s1.json \
  --backend cpu --bvh median --shade lit \
  --build-threads 1 --render-threads 8 \
  --mode prepared --runs 5 \
  --out out/practice/timed.png \
  --results out/practice/prepared.json
```

| Quantity | What is timed | JSON section |
|---|---|---|
| **T_br** | Rebuild BVH, prepare backend, render, and return image to host | `build_and_render` |
| **T_frame** | Render again with the existing preparation | `frame` |

Each quantity discards a warm-up and records five measured runs by default. Changing `--runs` changes that count. Images and results are written after timing.

Inspect `median_ms`, `min_ms`, `max_ms`, and `spread` in each section. `spread = (max − min) / median`. Individual `runs` contain total and component times; `unattributed_ms` exposes time outside the component timers. Results also record machine/build identity, scene hash, effective camera, and settings.

### Fresh-application time

App mode is the default. It measures one **T_app**, from the start of `main()` through loading, preparation, and rendering, ending with the image in host memory:

```bash
./build-cpu/apps/rt_render/rt_render \
  --scene scenes/known/known_two_spheres.json \
  --mode app --results out/practice/app.json \
  --out out/practice/app.png
```

That file has an `app` section. `--runs` is rejected in app mode because each process records one sample. For five fresh processes and an aggregate report:

```bash
python3 scripts/bench/run_tapp.py \
  --rt-render build-cpu/apps/rt_render/rt_render \
  --out-dir out/practice/tapp \
  --summary out/practice/tapp-summary.json \
  -- --scene scenes/known/known_two_spheres.json \
     --backend cpu --bvh median --shade lit --render-threads 4
```

Everything after `--` is passed to the renderer. The script adds app mode and per-process result paths, checks that runs describe the same configuration/output, and reports median, range, and spread. It may warn that no image was saved; rendering still occurs. Existing app results can also be aggregated with `--from file1.json file2.json ...`.

### Fair comparisons

Use Release builds, lit shading, identical scene/camera/resolution/BVH settings, and consistent AOV choices. For CPU scaling, change render threads explicitly. For the one-core baseline, set both thread counts to 1. Thread pinning for benchmark runs is:

```bash
export OMP_PROC_BIND=close
export OMP_PLACES=cores
```

Small known scenes are useful for correctness demos; generated scenes expose meaningful rendering work. Keep all run records and compare medians with spread. Full sweep rules and speedup definitions are in [BENCHMARKING.md](BENCHMARKING.md).

## Build CUDA tests

On the reference laptop, with the CUDA 13.3 toolkit and RTX 4070 (`sm_89`):

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 \
  -DRT_ENABLE_CUDA=ON -DRT_CUDA_ARCH=89
cmake --build build -j 8
./build/apps/hello_cuda/hello_cuda
ctest --test-dir build --output-on-failure
```

`hello_cuda` reports GPU identity and runs a kernel. CUDA tests exercise intersection, traversal, shading, pixel rendering, and timers on the device. Tests requiring a device skip when no device is visible; other CUDA errors still fail.

If `nvcc` isn't found, configuration falls back to CPU-only. Put `/usr/local/cuda-13.3/bin` on PATH or provide `-DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.3/bin/nvcc` when configuring a fresh build directory.

**Enabling CUDA currently builds the device tests; it does not enable the pending production CUDA rendering backend.** `--backend cuda` still returns an error. Embree/OptiX backends and the SAH builder are also unavailable in this snapshot.

### Vista

Allocate a GH compute node and load the documented GCC/CUDA/CMake modules. Use `RT_CUDA_ARCH=90` and the default Makefiles generator, since Ninja is not available there. Follow [BUILD.md's Vista setup](BUILD.md#vista-setup) for allocation, storage, OpenMP runtime checks, and exact commands. Build on Vista rather than copying laptop executables.

## Troubleshooting

| Symptom | What to check |
|---|---|
| CMake is too old | Minimum is 3.28; check `cmake --version`. |
| Dependency download fails | First configure needs HTTPS access to dependency archives. Retry after fixing connectivity. |
| Executable or scene not found | Run from the repo root and use the paths for your actual build directory. |
| Can't save output | Create the parent directory with `mkdir -p` first. |
| Nothing saved | Request `--out` and/or `--pfm`. Timing-only commands can render without saving an image. |
| `--aov` rejected | Supply `--pfm`; use `primid`, `lightmask`, or both comma-separated. |
| Comparator reports missing files | Each PFM needs both matching `.primid.u32img` and `.lightmask.u32img` sidecars. |
| `--runs` rejected | It applies only to `--mode prepared`. Use the Python process script for repeated app measurements. |
| CUDA/backend/builder unavailable | Currently use `--backend cpu` and `--bvh median`, `lbvh` or `none`; see the scope note above. |
| Unexpectedly slow render | Check Release build, median/LBVH acceleration, resolution, scene size, and render thread count. Brute force is costly on large scenes. |
| Scene hash unchanged after moving camera | Expected: the scene hash excludes the camera. Results record the effective camera separately. |
| Fetch cannot find `scene_dump` | Set `RT_SCENE_DUMP` to the absolute executable path, as in the download example. |

## Command reference

`rt_render` defaults: CPU backend, median BVH, lit shading, app mode, one build thread and one render thread. Camera/resolution come from the scene unless overridden.

| Option | Current use |
|---|---|
| `--scene PATH` | Required version-2 scene JSON |
| `--backend cpu` | Available production backend |
| `--bvh median\|lbvh\|none` | Serial median tree, parallel LBVH, or brute force |
| `--max-leaf-size N` | Positive leaf limit; default 4 |
| `--build-threads B` | Positive preparation thread count; used by LBVH, median remains serial |
| `--render-threads R` | Positive CPU rendering thread count |
| `--width W --height H` | Override resolution |
| `--cam-pos X Y Z` | Override camera position |
| `--cam-look X Y Z` | Override camera target |
| `--cam-vfov DEGREES` | Override vertical field of view |
| `--shade lit\|normal` | Lighting/shadows or normal visualization |
| `--out PATH.png` | Save sRGB PNG |
| `--pfm PATH.pfm` | Save linear RGB |
| `--aov primid,lightmask` | Save requested sidecars next to PFM |
| `--mode app\|prepared` | Fresh-app sample or repeated prepared measurements |
| `--runs N` | Measured runs per prepared quantity; default 5 |
| `--results PATH.json` | Save timing/configuration results |
| `--help` | Print CLI usage |

For implementation details, use [ARCHITECTURE.md](ARCHITECTURE.md) and [INTERFACES.md](INTERFACES.md). For platform setup use [BUILD.md](BUILD.md); for measurement methodology use [BENCHMARKING.md](BENCHMARKING.md).
