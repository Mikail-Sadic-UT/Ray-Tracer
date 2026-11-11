# Po's implementation hand-off — Nov 11

Tracking issue: [#35](https://github.com/Mikail-Sadic-UT/Ray-Tracer/issues/35).
Original implementation merged in [#36](https://github.com/Mikail-Sadic-UT/Ray-Tracer/pull/36).
Cleanup branch: `po/remove-render-stubs`, based on `dev` at `08a9230`.

## Implemented

- Scene types, JSON/binary version 2 IO, validation and canonical hashes. Binary paths
  resolve relative to the JSON file; binary validation checks bounds before allocation.
- Deterministic dynamic 16×16 OpenMP tiles, now used by the integrated `CpuBackend`.
  Mikail's Backend/CLI, BVH traversal and lit dispatch have landed (#47, #53), using
  Arek's Lambert/shadow shading (#49). `rt_render` writes PFM, PNG and requested AOVs.
- `rt_compare`: five core checks, symmetric hit explanations, bounded neighborhoods,
  mismatch budgets, unclamped linear tolerances and an informational sRGB summary.
- Uniform development generation: owned PCG32, explicit float conversion and Box–Muller,
  log-uniform sphere radii, exact counts, eight materials, scaled light and fitted camera.
- Verified archive retrieval with staged size/SHA-256, decompression and scene/hash checks;
  cache reuse; rejection of development `stub-` scenes as published benchmark scenes.
- Known two-sphere, single-sphere and degenerate-triangle scenes with a two-triangle floor.

## Removed workarounds

- **S4:** removed `scene_dump --render-normal`, its preview-only options and CPU/image
  dependencies, and `cpu/normal_renderer.hpp/.cpp`. Use `rt_render` for rendering and
  `scene_dump` for inspection. Migrated CPU tests to the production Backend lifecycle,
  retaining per-pixel checks against `render_pixel`, edge tiles, known pixels, output
  reuse, thread determinism and the degenerate/single-sphere cases. Removed the redundant
  test that compared the new backend to the deleted wrapper.
- **S5:** replaced fabricated shadow masks with an actual `LitShading` render through
  `CpuBackend`. All four corruption cases now use that lit image. The shadow test first
  verifies real floor occlusion, then inserts a false 20×20 shadow into a lit floor patch
  with a one-pixel lit margin. It requires exactly 400 unexplained shadow mismatches and
  a failed comparison. CLI integration tests use the real renderer and compare its files.
- Corrected the CLI repeatability test's relative executable path on macOS: resolve the
  temporary-directory alias before computing a path from it.

S4/S5 markers and register rows are removed. The shared tile loop, normal shading policy,
BruteQuery and ordinary synthetic unit-test inputs remain useful production/test components.

## Remaining workaround and hand-offs

- **Terrain S3 / Jett #41:** still open. `scene/terrain_stub.hpp` supplies development terrain;
  generator JSON records `terrain_source = "stub-S3"`. Adopt the real terrain implementation,
  remove its marker/register/metadata and generate the canonical archive afterward.
- **Canonical publication:** no `scenes-v1` release or canonical manifest has been published
  by this work. The retrieval script is tested, but no real remote archive was available.
- **Vista:** GCC/CUDA, GPU agreement and CPU scaling/profile measurements remain unverified
  by this work. Mac tests do not establish reference-platform or performance results.
- **Other integration:** `rt_bench` has landed (#51); benchmark CLI wiring, golden images and
  the independent oracle remain separate team deliverables. No full-gate claim is made here.

## Verification

- Current macOS arm64 Release build succeeds, Apple Clang with Homebrew OpenMP, CUDA off.
- **231/233 tests pass.** All cleanup-related CPU, backend, comparison and CLI tests pass.
  The remaining tests are `MachineInfo.DescribesHostAndLeavesGpuFieldsEmpty` and
  `MachineInfo.RecordsTheHostCompilerVersion`: machine collection reads Linux `/proc`,
  `lscpu` and `/etc/os-release`, and compiler identification returns empty for Clang.
- Earlier #36 validation: development 10K/100K/1M scenes loaded and validated; repeated
  10K generation produced identical JSON/bin bytes. These large scenes were not regenerated
  during this cleanup. PCG32 matches the published reference demo sequence.
- Archive tests cover independent binary encoding, bad hashes, cache reuse and verification
  before installation. Formatting, whitespace and stub-register checks accompany cleanup.

## Reproduce locally

Use your configured build directory (`build-mac`, `build-cpu` or `build-vista`):

```bash
cmake --build build-mac --parallel
ctest --test-dir build-mac --output-on-failure
mkdir -p build-mac/previews build-mac/generated
./build-mac/apps/rt_render/rt_render --scene scenes/known/known_two_spheres.json \
  --backend cpu --bvh median --shade lit --render-threads 4 \
  --pfm build-mac/previews/known_two_spheres.pfm \
  --out build-mac/previews/known_two_spheres.png --aov primid,lightmask
./build-mac/apps/rt_compare/rt_compare build-mac/previews/known_two_spheres.pfm \
  build-mac/previews/known_two_spheres.pfm
./build-mac/apps/rt_gen/rt_gen --layout uniform --primitives 10000 --seed 1 \
  --out build-mac/generated/dev_uniform_10000
./build-mac/apps/scene_dump/scene_dump --verify build-mac/generated/dev_uniform_10000.json
```

The PFM has `.primid.u32img` and `.lightmask.u32img` sidecars. Use `--shade normal` for a
normal-as-color diagnostic. Do not commit generated images or binary scenes.

After an immutable archive and `scenes/manifest.json` exist:

```bash
bash scripts/fetch_scenes.sh scenes-v1
```

The manifest accepts a list or an object with a `scenes` array. Entries use INTERFACES §2's
set/name/hashes/files/archive fields. Provide a direct URL for every asset. The script uses
`gh release download` when available and Python otherwise; it never regenerates geometry.
`RT_SCENE_DUMP`, `RT_SCENE_MANIFEST` and `RT_SCENE_DEST` override executable, manifest and
installation paths. Python 3 is required for retrieval and CLI integration tests.
