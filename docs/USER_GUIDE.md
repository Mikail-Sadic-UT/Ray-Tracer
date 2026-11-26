# User guide

A practical guide to compiling and using the tools currently in this repo. All commands run from the **repository root** in a Bash terminal. Examples use WSL2 Ubuntu 24.04 and write output under `out/`.

**Current scope:** CPU rendering works with spheres, triangles, Lambert point-light shading, hard shadows, and median or parallel LBVH acceleration. CUDA builds add GPU rendering (`rt_render --backend cuda`, the megakernel), GPU tests, and `hello_cuda`. Some CLI choices describe future features and currently return an error.

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
- [Build CUDA and render on the GPU](#build-cuda-and-render-on-the-gpu)
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
| `obj2bin` | `build-cpu/apps/obj2bin/obj2bin` | Place an OBJ mesh in a scene (E6) |

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

### Light count

`--lights N` (1 to 32) replaces the scene's lights with N copies of its first light, spaced evenly around the world Y axis, each with 1/N of the intensity, so the total power stays the same. `--lights 1` on a one-light scene renders exactly the same image. The scene hash in a results file changes with N, and `scene.num_lights` records it. The `lights` sweep preset uses this for the light-count sweep (L = 1, 2, 4).

```bash
./build-cpu/apps/rt_render/rt_render \
  --scene scenes/known/known_two_spheres.json --lights 4 --out out/practice/four_lights.png
```

### Camera orbit and video

`--orbit N --frames DIR` prepares the scene once, then renders N frames on one full turn around the camera target (about the vertical axis) and writes them as `DIR/frame_0000.png` and so on. Frame 0 is the scene's own view. It prints the preparation stages and T_frame over the path, a camera-path version of the prepared-frame time. It does not combine with `--mode`, `--runs`, `--results`, `--out` or `--pfm`.

`scripts/make_video.sh` wraps this and encodes the frames as an MP4 (needs `ffmpeg`, `sudo apt install ffmpeg`):

```bash
bash scripts/make_video.sh --scene scenes/generated/bench_1m_s1.json --out out/orbit.mp4 \
  --frames 120 --fps 30 -- --backend cuda --bvh lbvh --build-threads 16 --width 1280 --height 720
```

Options after `--` go to `rt_render`. `--keep-frames DIR` keeps the PNGs. The material showcase makes a good presentation clip (8 seconds, about 10 seconds to render on the laptop GPU):

```bash
bash scripts/make_video.sh --scene scenes/known/known_showcase.json --out out/showcase_orbit.mp4 \
  --frames 240 --fps 30 -- --backend cuda --bvh lbvh --width 1280 --height 960
```

The orbit keeps the scene camera's distance to its target, so in a scene with walls (like `known_mirror`) pass `--cam-pos`, `--cam-look` and `--cam-vfov` to keep the camera inside.

### Camera path

`--path FILE --path-frames N --frames DIR` renders N frames along a camera path instead of a circle, again preparing the scene once. The file lists waypoints; the first frame is the first waypoint and the last frame is the last:

```json
{
  "timing": "distance",
  "ease": false,
  "turn_cost": 2.0,
  "waypoints": [
    { "position": [14, 1.6, -1.3], "look_at": [0, 1.4, -1.0], "vfov_deg": 70 },
    { "position": [2.5, 1.6, -1.3], "look_at": [0, 1.4,  2.0] },
    { "position": [0.8, 1.6,  1.4], "look_at": [0.8, 1.4, 6.0] }
  ]
}
```

- `position` and `look_at` are required on every waypoint. `vfov_deg` is optional: the first waypoint without it uses the scene camera's, and later ones keep the previous value. `up` and the resolution come from the scene camera (and `--width`, `--height`, `--cam-*` overrides). Unknown keys are errors.
- Position, look-at point and field of view are smoothed with a centripetal Catmull-Rom spline that passes exactly through every waypoint and does not loop when waypoints are unevenly spaced.
- `"timing": "distance"` (default) moves at constant speed along the route. A segment that only turns (two waypoints with the same position and different `look_at`, to stop and look around) still gets time: each segment takes as long as the longer of its walking distance and `turn_cost` times the angle it turns through, in radians. `turn_cost` is optional and defaults to 2 (scene units per radian), so a quarter turn counts as about 3 units of walking. `"timing": "waypoints"` instead gives every segment equal time. `"ease": true` starts and stops gently.
- Every frame is checked before rendering starts, so a camera that ends up on its look-at point or looks straight along `up` fails at once, naming the point on the path.
- It does not combine with `--orbit`, `--mode`, `--runs`, `--results`, `--out` or `--pfm`.

`scripts/make_video.sh --path PATH.json --frames N` encodes the frames as an MP4:

```bash
bash scripts/make_video.sh --scene scenes/known/known_showcase.json --path out/walk.json \
  --out out/walk.mp4 --frames 240 --fps 30 -- --backend cuda --bvh lbvh
```

The path only chooses the camera each frame is rendered with, so CPU and GPU images stay byte-identical.

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

Edit its camera, material `albedo` colors, point-light position/intensity, sphere centers/radii, or triangle vertices, then validate and render the copy. Material references are zero-based indices into `materials`. Coordinates and colors must be finite; radii must be positive; colors/intensities must be nonnegative. Degenerate triangles are allowed but never hit.

Materials are diffuse (Lambert) unless `"specular": true`, which adds a Blinn-Phong highlight: `ks` is its color and strength, and `shininess` (at least 1, fractions allowed) its tightness, from broad at about 10 to a small sharp spot at a few hundred. Besides point lights, a scene can have directional lights such as sunlight:

```json
{"type": "directional", "direction": [1, -1.5, -0.8], "intensity": [0.5, 0.45, 0.35]}
```

`direction` is the way the light travels; it is normalized at load. A directional light has no distance falloff, so its intensity is the irradiance it gives a surface facing it, and its shadows are cast from infinitely far away. `scenes/known/known_specular.json` uses both features.

Add `"mirror": true` to a material to make it reflect. Its `ks` is then the reflection color: `[0.9, 0.9, 0.9]` is chrome, `[0.9, 0.7, 0.3]` gold, and a dark `albedo` keeps the surface's own color from washing out the reflection. A material can be both specular and a mirror. Reflections follow at most 4 bounces, so two mirrors facing each other end in a dark reflection after that. `scenes/known/known_mirror.json` has a mirror wall and mirror spheres.

For glass, set `"glass": true` and give `"ior"` (index of refraction: 1.5 for window glass, 1.33 for water) instead of `"shininess"`, with `"specular": false`:

```json
{"name": "green glass", "albedo": [0, 0, 0], "ks": [0.45, 0.85, 0.5], "ior": 1.5, "specular": false, "glass": true}
```

`ks` is the glass color: the fraction of red, green and blue that remains after one unit of distance through it, so thick glass is deeper in color than thin glass, and `[1, 1, 1]` is perfectly clear. Keep `albedo` at zero for clear-looking glass. Light both reflects off and refracts through the surface, more of it reflecting at grazing angles. Glass and mirror rays together follow at most 4 bounces. Glass spheres and closed triangle meshes work; a triangle's outside is the side its vertices wind counterclockwise around. Glass casts tinted shadows: light passes through it in a straight line and takes on its color, so a green ball casts a green shadow and clear glass almost none (the light is not bent, so there are no focused caustics). `scenes/known/known_glass.json` has three colored glass spheres in front of a striped wall, and `scenes/known/known_showcase.json` puts every material in one scene: a chrome sphere ringed by matte, glossy, mirror and glass balls.

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

## Render an OBJ mesh

`obj2bin` (E6) places a Wavefront OBJ triangle mesh in a template scene and writes a binary scene that every tool loads like a generated one. The template supplies the camera, lights, materials and any other geometry; [`scenes/templates/mesh_stage.json`](../scenes/templates/mesh_stage.json) is the showcase ring of spheres with room in the middle.

```bash
mkdir -p scenes/meshes   # gitignored
curl -L -o scenes/meshes/xyzrgb_dragon.obj \
  https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/xyzrgb_dragon.obj

./build/apps/obj2bin/obj2bin --obj scenes/meshes/xyzrgb_dragon.obj \
  --template scenes/templates/mesh_stage.json --out scenes/meshes/dragon_gold.json \
  --material 5 --size 3.6 --rotate-y 20

scripts/make_video.sh --scene scenes/meshes/dragon_gold.json --out out/dragon_gold_orbit.mp4 \
  --frames 180 -- --backend cuda --bvh lbvh --cam-pos 0 2.2 5.4 --cam-look 0 0.9 0
```

- `--material N` is an index into the template's materials (in `mesh_stage.json`: 1 chrome, 2 matte red, 3 glossy green, 5 gold mirror, 6 to 9 glass, 10 blue mirror "visor", 11 banana yellow, 12 eye white, 13 pupil black).
- `--group-material NAME=ID` (repeatable) gives the triangles of one OBJ group, named by an `o` or `g` line, their own material; the rest take `--material`. An unknown group name is an error.
- `--size S` scales the mesh so its largest extent is `S`; `--rotate-y DEG` turns it about the vertical axis; `--at X Y Z` puts the bottom center of its bounding box there (default the origin, on the floor).
- Only `v` and `f` lines are read. Polygons are split into triangles, and normals, texture coordinates and `.mtl` materials are ignored. Faces must wind counterclockwise seen from outside (the OBJ convention), which glass relies on to tell entering from leaving.
- Shading is flat per triangle. Mirror, gold and diffuse meshes look right; glass on a dense mesh looks speckled, because each tiny facet refracts differently and the bounce limit is 4. A coarse closed mesh such as Spot works better as glass.

### From a glTF model (.glb)

`scripts/glb2obj.py` converts a binary glTF to an OBJ that `obj2bin` reads: it bakes in the node transforms, drops everything but triangles, and writes each connected piece of surface as its own group (`part0`, `part1`, ...), printing their sizes and bounding boxes so you can tell which is which. It uses only the Python standard library. Textures, vertex colors and materials are dropped, since the renderer has none of them. `--group-by material` writes one group per glTF material instead (named after it, spaces replaced by underscores), which suits models built from many pieces.

```bash
python3 -I scripts/glb2obj.py model.glb scenes/meshes/model.obj
./build/apps/obj2bin/obj2bin --obj scenes/meshes/model.obj --template scenes/templates/mesh_stage.json \
  --out scenes/meshes/model.json --material 2 --group-material part2=1 --group-material part3=10
```

For example, a 5.5K-triangle crewmate model with four pieces (body, backpack, visor rim, visor pane) becomes a red body with a chrome rim and a blue mirror pane. Downloaded models keep their own licenses; check the source page before putting one in a report or slides, and give the credit it asks for. A `.glb` from Sketchfab usually names its author, license and source page in the `asset.extras` of its JSON chunk. Models of characters from commercial games are fan work whatever license the upload claims, so keep them out of the repo (`scenes/meshes/` is gitignored) and use them only for demos.

The Stanford dragon (XYZ RGB scan, 250K triangles) renders at about 14 ms per frame at 1280×720 on the laptop GPU, and CPU and CUDA images are byte-identical.

### Baking textures into colors

The renderer has no textures, but `scripts/bake_glb.py` gets most of the look of a textured `.glb` without touching it: it averages each material's base-color texture around every piece of the surface and turns the result into ordinary flat materials.

```bash
sudo apt install python3-pil python3-numpy        # the only tool here that needs them
python3 scripts/bake_glb.py model.glb mymodel --camera 0 2 6 --look 0 1 0 --fov 50 \
  --light 3 6 4 40 40 40 --ambient 0.1
./build/apps/rt_render/rt_render --scene scenes/meshes/mymodel.json --backend cuda --out out/mymodel.png
```

- Each material's triangles are split into 4^L pieces: as many levels as the piece budget (`--budget`, default 250,000 per material), the texture resolution (pieces stay at least about two texels) and `--max-level` (default 6) allow. Each piece takes the average texture color (times the material's base color factor), quantized to 5 bits per channel, and each distinct color becomes one flat material. A model with a few thousand colors gives a scene with a few thousand materials.
- Materials without a texture keep their base color factor. For `BLEND` and `MASK` materials (leaves, grilles), pieces whose texture alpha is under one half are dropped.
- Roughness and metallic factors set the highlight (`ks` and `shininess`); roughness, metallic and normal maps are not used.
- It writes `NAME.obj`, `NAME_stage.json` and `NAME.json` (+ `.bin`) to `--out-dir` (default `scenes/meshes`). `--camera`, `--look`, `--fov`, `--light` (repeat), `--ambient` and `--background` set the scene's view and lighting, `--drop-instance K` leaves out the K-th mesh instance, and `--size` and `--rotate-y` are passed to `obj2bin` (by default the model keeps its own size, so a model in metres stays 1:1).
- Detail is limited to the size of a piece, and each piece is one flat color: fine lettering survives only where the mesh is dense or the pieces are small enough. The renderer is untouched, so CPU and GPU images stay byte-identical.

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

Small known scenes are useful for correctness demos; generated scenes expose meaningful rendering work. Keep all run records and compare medians with spread. Full sweep rules and speedup definitions are in [BENCHMARKING.md](BENCHMARKING.md). To run a whole sweep and get one CSV, use `scripts/tacc/sweep.py`. For example, after fetching `scenes-v1`:

```bash
export OMP_PROC_BIND=close OMP_PLACES=cores
python3 scripts/tacc/sweep.py plan --preset g2 --machine laptop --scenes scenes/generated   # list the runs
python3 scripts/tacc/sweep.py run --preset g2 --machine laptop --scenes scenes/generated \
  --rt-render build/apps/rt_render/rt_render --out out/sweep-g2
```

The results are in `out/sweep-g2/sweep.csv`. Summarize it with `python3 scripts/tacc/ratios.py out/sweep-g2/sweep.csv`. The `lights` preset measures 1, 2 and 4 lights (through `--lights`); `ratios.py` prints those rows as a separate light-count table. On Vista, use `scripts/tacc/sweep.sbatch` instead ([BENCHMARKING.md](BENCHMARKING.md#sweeps)); the first Vista results are in [RESULTS.md](RESULTS.md).

## Build CUDA and render on the GPU

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

A CUDA build also enables `rt_render --backend cuda`, the megakernel (one GPU thread per pixel, the same tracing and shading code as the CPU). Every other option works as with `--backend cpu`. `--render-threads` is ignored, and `--build-threads` still sets the CPU BVH build:

```bash
./build/apps/rt_render/rt_render \
  --scene scenes/known/known_two_spheres.json \
  --backend cuda --bvh lbvh --build-threads 8 --shade lit \
  --aov primid,lightmask --pfm out/two_spheres.cuda.pfm --out out/two_spheres.cuda.png
```

Compare it with a CPU render of the same scene using `rt_compare` (see [Compare two renders](#compare-two-renders)). With contraction off in both compilers ([BUILD.md](BUILD.md#compiler-flags)), the two images are normally byte-identical. CUDA results files add `cuda_init` (app mode only), `alloc`, `upload` and `download` components, a `render_kernel` diagnostic (kernel-only time, overlapping `render`), the GPU identity, and `"pipeline": "mega"`. `--pipeline wavefront` and the `--mem` options are extensions (E5, E1) and aren't implemented. Embree/OptiX backends and the SAH builder are also unavailable in this snapshot.

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
| Backend/builder unavailable | Use `--backend cpu` or (in a CUDA build) `cuda`, and `--bvh median`, `lbvh` or `none`; see the scope note above. |
| `this build has no cuda backend` | The build was configured with `RT_ENABLE_CUDA=OFF`, or `nvcc` wasn't found. Use a CUDA build directory. |
| `no CUDA-capable device is detected` | No GPU is visible (check `nvidia-smi` and `CUDA_VISIBLE_DEVICES`). |
| Unexpectedly slow render | Check Release build, median/LBVH acceleration, resolution, scene size, and render thread count. Brute force is costly on large scenes. |
| Scene hash unchanged after moving camera | Expected: the scene hash excludes the camera. Results record the effective camera separately. |
| Fetch cannot find `scene_dump` | Set `RT_SCENE_DUMP` to the absolute executable path, as in the download example. |

## Command reference

`rt_render` defaults: CPU backend, median BVH, lit shading, app mode, one build thread and one render thread. Camera/resolution come from the scene unless overridden.

| Option | Current use |
|---|---|
| `--scene PATH` | Required version-2 scene JSON |
| `--backend cpu\|cuda` | CPU renderer, or the CUDA megakernel (CUDA builds only) |
| `--pipeline mega` | CUDA pipeline; `mega` is the default and the only one implemented |
| `--bvh median\|lbvh\|none` | Serial median tree, parallel LBVH, or brute force |
| `--max-leaf-size N` | Positive leaf limit; default 4 |
| `--build-threads B` | Positive preparation thread count; used by LBVH, median remains serial |
| `--render-threads R` | Positive CPU rendering thread count (ignored by `cuda`) |
| `--width W --height H` | Override resolution |
| `--cam-pos X Y Z` | Override camera position |
| `--cam-look X Y Z` | Override camera target |
| `--cam-vfov DEGREES` | Override vertical field of view |
| `--shade lit\|normal` | Lighting/shadows or normal visualization |
| `--lights N` | Replace the lights with N copies of light 0 on a ring (light-count sweep) |
| `--orbit N --frames DIR` | Prepare once, render N frames around the target into DIR |
| `--path FILE --path-frames N --frames DIR` | Prepare once, render N frames along the camera path in FILE into DIR |
| `--out PATH.png` | Save sRGB PNG |
| `--pfm PATH.pfm` | Save linear RGB |
| `--aov primid,lightmask` | Save requested sidecars next to PFM |
| `--mode app\|prepared` | Fresh-app sample or repeated prepared measurements |
| `--runs N` | Measured runs per prepared quantity; default 5 |
| `--results PATH.json` | Save timing/configuration results |
| `--help` | Print CLI usage |

For implementation details, use [ARCHITECTURE.md](ARCHITECTURE.md) and [INTERFACES.md](INTERFACES.md). For platform setup use [BUILD.md](BUILD.md); for measurement methodology use [BENCHMARKING.md](BENCHMARKING.md).
