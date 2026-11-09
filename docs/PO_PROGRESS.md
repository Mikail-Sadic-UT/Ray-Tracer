# Po's implementation hand-off — Nov 9

Tracking issue: [#35](https://github.com/Mikail-Sadic-UT/Ray-Tracer/issues/35).
Branch: `po/cpu-scene-tools`, based on `dev` at `b5d4e44`.

## Implemented

- `cpu/normal_renderer.hpp`: real `BruteQuery` + `NormalShading` rendering through
  `render_pixel`, dynamic 16x16 OpenMP tiles, explicit thread count, optional AOVs, and output
  reuse. One-thread and four-thread outputs are bit-identical.
- `scene_dump --render-normal`: saves PFM and both AOVs before optional PNG. This inspection
  feature provides the first saved CPU image while Mikail's `rt_render` app is pending. It
  does not define a substitute `Backend`, and normal shading is not used for benchmark claims.
- `scene/binary_io.hpp`: field-by-field version 2 `.bin` IO, aligned arrays, canonical hashes,
  bounds/size checks before allocation, and full scene validation after JSON is combined.
  `load_scene` resolves binary paths relative to the JSON file. `save_binary_scene` writes the
  matching `.json`/`.bin` pair without constructing a million JSON primitive objects.
- `rt_compare`: the five core checks from BENCHMARKING, both hit-explanation directions,
  bounded neighborhoods, mismatch budgets, unclamped shading tolerance, and an informational
  sRGB summary. PFM/AOV dimensions must agree. Failures produce a nonzero exit.
- `rt_gen --layout uniform`: owned PCG32, explicit float conversion and Box–Muller sampler,
  log-uniform sphere radii, exact primitive counts, eight diffuse materials, scaled point light,
  and deterministic camera fitting of all eight AABB corners into the 95% frame.
- `scripts/fetch_scenes.sh`: staged retrieval, size/SHA-256 and decompression verification,
  content/scene hash verification with `scene_dump`, and cache reuse. It refuses development
  terrain-stub scenes as a published scene set. Python 3 is required.
- Known single-sphere and degenerate-triangle scenes, each retaining the two-triangle floor.

## Workarounds and their limits

1. **Backend/CLI hand-off:** the CPU pixel loop is a reusable function in `src/cpu/`. The
   existing `scene_dump` owns the preview command, so no implementation was added to Mikail's
   `apps/rt_render/` or `backend.hpp`. Once those land, the CPU backend can reuse this loop.
2. **Terrain S3:** `scene/terrain_stub.hpp` temporarily implements the documented height field
   and grid in Po's module. It is registered in ROADMAP. Every generated JSON records
   `generator.terrain_source = "stub-S3"`, and the CLI labels it development output. Adopt
   Jett's terrain implementation, remove the stub/metadata marker/register row, then generate
   the canonical archive. No `scenes-v1` release or canonical manifest was published here.
3. **Shadow comparisons:** three corruption cases use the real normal-shaded known scene.
   The shadow-block test uses explicitly synthetic masks because real Lambert/shadow shading
   has not landed. These test comparison logic, not physical lighting correctness. Re-run all
   four required corruption fixtures on the real lit known scene when Arek's shading lands.
4. **Platform:** local checks run on macOS arm64, Apple Clang 21 and Homebrew OpenMP, CUDA off.
   This does not establish GCC 13/CUDA correctness, GPU agreement, or performance ratios.

## Reproduce locally

Validation completed for this implementation:

- Mac CPU Release build and all 144 tests pass; feature commits also build independently.
- CPU preview bytes agree for one and four threads, including PFM and both AOV sidecars.
- Corrupted inputs trigger the named comparator checks and a nonzero CLI exit.
- Development scenes at 10K, 100K and 1M primitives load and validate. Repeating the same
  10K generator command produces identical JSON and binary bytes.
- PCG32 matches the [published reference demo sequence](https://www.pcg-random.org/using-pcg-c-basic.html).
- Archive retrieval is tested with an independently encoded binary fixture, hash corruption,
  cache reuse and verification before installation. A real remote `scenes-v1` archive is not
  available yet.
- Formatting, `git diff --check`, shell syntax and the stub register check pass. Compile
  commands include OpenMP for the CPU renderer and `-ffp-contract=off` for hash/generator.

From an already configured CPU build (substitute `build-cpu` or `build-vista` as appropriate):

```bash
cmake --build build-mac --parallel
ctest --test-dir build-mac --output-on-failure
mkdir -p build-mac/previews build-mac/generated
./build-mac/apps/scene_dump/scene_dump scenes/known/known_two_spheres.json \
  --render-normal build-mac/previews/known_two_spheres.pfm \
  --png build-mac/previews/known_two_spheres.png --render-threads 4
./build-mac/apps/rt_compare/rt_compare build-mac/previews/known_two_spheres.pfm \
  build-mac/previews/known_two_spheres.pfm
./build-mac/apps/rt_gen/rt_gen --layout uniform --primitives 10000 --seed 1 \
  --out build-mac/generated/dev_uniform_10000
./build-mac/apps/scene_dump/scene_dump --verify build-mac/generated/dev_uniform_10000.json
```

The preview writes `.primid.u32img` and `.lightmask.u32img` next to its PFM. Do not commit
generated images or binary scenes. The current build's preview is normal-as-color, so rainbow
spheres and a green floor are expected.

Retrieval after an immutable archive and `scenes/manifest.json` exist:

```bash
bash scripts/fetch_scenes.sh scenes-v1
```

The manifest can be a list of entries or an object with a `scenes` array. Each entry uses the
fields in INTERFACES §2: `set`, `name`, `content_hash`, `scene_hash`, `files` (archive filename,
`bytes`, `sha256`, plus `uncompressed_sha256` for `.bin.gz`) and `archive.release/urls`.
List a direct asset URL for every file. With `gh` installed, GitHub assets use
`gh release download`; otherwise Python downloads the direct URLs. The tool never regenerates
geometry. `RT_SCENE_DUMP`, `RT_SCENE_MANIFEST`, and `RT_SCENE_DEST` override executable,
manifest and destination paths for alternate builds and integration tests.

## Remaining hand-offs

| Owner / access | Needed for | What can proceed without it |
|---|---|---|
| Mikail: `backend.hpp`, `PrepareInput`, `RenderSettings`, `rt_render` integration | Full CPU Backend lifecycle and canonical renderer CLI | CPU normal previews, file IO and comparison logic |
| Mikail: BVH traversal / `BvhQuery` | Fast CPU rendering of large benchmark scenes and BVH-vs-brute-force validation | Exact brute force on small known scenes; generated large-scene load checks |
| Arek: Lambert, shadow rays, `LitShading` | Lit images, real shadow corruption fixtures, renderer correctness gates | Normal images and synthetic comparison tests |
| Jett: real terrain generator | Remove S3 and freeze/distribute the canonical scene set | Deterministic development scenes with the registered stand-in |
| Vista account / GH compute allocation | GCC 13/CUDA verification and real scaling/profile measurements | Mac CPU correctness tests; no GPU or speedup claims |

These hand-offs cannot be certified by adding more local stand-ins. No G0/G1/G2 gate is marked
complete by this work. Official release publication and performance studies remain pending.
