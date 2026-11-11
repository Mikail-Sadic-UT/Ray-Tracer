# Po Han Ai (Member 4)

## Start here

**You build:** the scene data layer (types, JSON and `.bin` IO, validation, hashing), the scene generator, image IO, the CPU renderer (1 to N threads, the reference everything is compared against), and `rt_compare`.

**Read first, in order:**
1. [INTERFACES §2 Scene, image, and file formats](../INTERFACES.md#2-scene-image-and-file-formats) (your contract; everyone reads your types)
2. [INTERFACES §6 tracing queries and pixel result](../INTERFACES.md#tracing-queries) (what your CPU loop calls)
3. [INTERFACES §7 Backend interface](../INTERFACES.md#7-backend-interface) (the CPU backend implements it)
4. [BENCHMARKING scene families](../BENCHMARKING.md#scene-families) (what `rt_gen` produces)
5. [BENCHMARKING image comparison](../BENCHMARKING.md#2-image-comparison-rt_compare) (what `rt_compare` checks)

**First three tasks:**
1. `SceneHost` / `Scene` / `Image`, JSON reader/writer, scene hash, load validation, `scene_dump` (by Oct 30).
2. Known scenes and image IO landed. Use `rt_render --backend cpu --shade lit|normal`
   for PFM/PNG/AOV output; the temporary preview S4 and duplicate normal renderer are removed.
3. `.bin` IO, development `rt_gen` uniform layout, and the OpenMP normal-render loop are
   implemented (#35). Backend/BVH/lit integration is available (#47, #53). Terrain is integrated (#41); canonical archive publication and Vista verification remain pending.

**You hand off:**

| What | To | When | Contract |
|---|---|---|---|
| `SceneHost`, `Scene`, `Image`, JSON IO, validation | Everyone | Oct 30 | §2 |
| Known-case scenes (`scenes/known/`) | Everyone's tests | Available: two spheres, single sphere, and degenerate triangle; each has a two-triangle floor | §2 |
| CPU backend (reference renderer) | Everyone (ground truth) | Backend with brute force/BVH and normal/lit shading available (#47, #53); temporary S4 removed | §7 |
| `rt_gen` + `.bin` + published scene set | Everyone (benchmarks) | Binary IO and development generation available (#35); terrain integrated (#41), verification and canonical archive pending | §2, [scene families](../BENCHMARKING.md#scene-families) |
| `rt_compare` core checks | Mikail (GPU validation), Arek (goldens) | CLI/core logic available (#35); four corruption cases now use a real lit image; S5 removed | [image comparison](../BENCHMARKING.md#2-image-comparison-rt_compare) |

**You depend on:**

| What | From | Until it lands, use |
|---|---|---|
| `vec3`, `AABB` | Arek (landed) | Nothing |
| `Timings` | Arek (struct landed, #22) | Nothing: `Backend::prepare` and `render` take `Timings&` (CPU backend fills `render`; it has no `download`) |
| Camera JSON parsing and validation | Jett (Oct 30) | Nothing. Landed: `camera/camera.hpp` (`Camera`, `camera_ray`) and `camera/camera_io.hpp` (`parse_camera`, `camera_to_json`, `validate_camera`, #17) |
| `brute_closest` / `BruteQuery` | Mikail (landed, #21) | Nothing |
| `shade_hit` / `LitShading` | Arek (landed, #49) | Nothing. Real lit image supplies corruption fixtures, including shadow test 4; S5 removed |
| Terrain height field | Jett (#41) | Integrated in `scene/terrain.hpp` (#41); S3 removed, verification and canonical archive pending |
| `Bvh` + `BvhQuery` | Jett, Mikail | Median builder/validator and traversal landed (#31, #46). Both query paths are tested through the CPU backend |
| `Backend`, `PrepareInput`, `RenderSettings`, `rt_render` | Mikail (#42) | Nothing. Backend and CLI landed (#47); lit dispatch landed (#53). Removed preview S4 and its duplicate renderer |

The shared `render_pixel` entry point for the CPU loop has landed in `shade/render_pixel.hpp`
(#33). The first image calls `render_pixel<BruteQuery, NormalShading>(cam, px, py, scene, q)`.

**Done means:**
- JSON and `.bin` round trips are exact, and the readers reject bad files.
- Load validation rejects each invalid case in the [valid input table](../INTERFACES.md#valid-input-checked-at-load).
- The CPU backend at 1 vs N threads is bit-identical.
- `rt_compare` passes identical images and rejects the four core corruption tests.

### Agent brief

Paste this into your coding agent along with `docs/INTERFACES.md`:

```text
You are implementing the scene layer, generator, image IO, CPU backend, and image comparison
tool for a C++17/CUDA ray tracer.
Read docs/members/PO.md and docs/INTERFACES.md section 2, "Tracing queries", "Pixel result",
and section 7. Generator spec: docs/BENCHMARKING.md "Scene families". Comparison spec:
docs/BENCHMARKING.md "Image comparison (rt_compare)", core checks only. The contract is
authoritative; follow it exactly. Layout: docs/REPO_SKELETON.md.

Scope (only edit these): src/scene/, src/image/, src/cpu/, apps/rt_gen/, apps/rt_compare/,
apps/scene_dump/, scenes/known/, scenes/golden/, scripts/fetch_scenes.sh, and tests for these.
Build: CMake, GCC 13, OpenMP. Use nlohmann/json for JSON, stb_image_write for PNG.

Standing rules:
- Conventions: INTERFACES section 0 (names, includes, errors, Doxygen comments, CMake targets).
- Device-callable code is header-inline (RT_HOSTDEV / RT_INLINE), with no -rdc. float only, no
  fast-math (see "General rules" at the top of INTERFACES.md for the one double exception).
- Workflow: docs/WORKFLOW.md. Check open issues first. If you have no issue, nothing to review,
  and are blocked, tell the user to notify the group chat (WORKFLOW.md "When you're out of
  work"). Commits are kernel style and signed off (`git commit -s`).
- Before you start: `git fetch origin`, make sure `dev` is current (`git switch dev && git pull
  --ff-only`), and branch from `origin/dev`. Merge `dev` again before opening the PR.
- Assign yourself to the issue before you start and to the PR when you open it.
- Make the docs update the last commit of the PR: your member file (docs/members/<you>.md) with
  the Deliverables and hand-off rows that changed, the "You depend on" rows that landed, and this
  week's contribution log row, plus any ROADMAP status, stub register, or CONTRACT_NOTES rows the
  PR changes.
- At the start of a session, run the environment check in WORKFLOW.md "Development environment".
  If the machine doesn't match, warn the user once (never block) and add the `Developed-on:` /
  `Needs-check:` trailers to your commits.
- Stubs and local workarounds: mark each with `// STUB(S<n>, <owner>): remove when <x> lands (#<issue>)`
  and add a row to the ROADMAP.md stub register in the same PR (WORKFLOW.md "Stubs and local
  workarounds").
- If the interface looks wrong or incomplete, do not change it locally. Stop and write a short
  note proposing the change.

Before you finish, double-check against:
- INTERFACES section 2: "Valid input (checked at load)", "Scene hash", ".bin layout",
  "AOV file format", and "Generator determinism and scene distribution".
- INTERFACES section 7, and section 0 "Other shared rules" for threads (counts come from the
  parameter structs, never from OMP_NUM_THREADS).
- docs/BENCHMARKING.md "Scene families" and "Image comparison (rt_compare)".

Work in the order of "First three tasks" and the Deliverables table in docs/members/PO.md.
```

## Deliverables

| When | Deliverable | Size |
|---|---|---|
| Oct 27 | Review INTERFACES §2 and sign off | S |
| Oct 30 | Scene types, JSON IO, hash, validation, `scene_dump`; known scenes; image IO; brute-force CPU image | M |
| Nov 6 | Development `rt_gen` + `.bin` and `rt_compare` implemented (#35); Backend/BVH/lit now integrated and real lit corruption fixtures replace S5. Terrain integrated (#41); verification pending | M |
| Nov 6 | Retrieval script implemented and tested (#35). Canonical `scenes-v1` release and manifest await verification and publication ([details](../INTERFACES.md#generator-determinism-and-scene-distribution)) | S |
| Nov 18 | CPU render scaling R = 1→72; profile-driven CPU optimization | M |
| Final | Reproducibility instructions (exact commands, hashes, modules) | M |
| Later | E4 Embree (after G2); `clustered` layout and seed 2 (after G2, if time); [deferred `rt_compare` hardening](../BENCHMARKING.md#deferred-comparison-hardening) only if needed | |

## Risks

| Risk | Mitigation |
|---|---|
| Broadest workload | Terrain → Jett, golden tests → Arek, already assigned. Next if needed: `scene_dump` and `.u32img` IO → Mikail. Embree only after G2 |
| Generator bytes differ across machines | Expected (libm). Generate once, distribute the files, verify hashes |
| `rt_compare` too lenient | Core checks require explained mismatches. Turn on the deferred hardening if a wrong image passes |

## References

PCG (pcg-random.org); PBRT v4 (renderer structure); PFM format notes; stb_image_write; Embree 4 docs (E4).

## Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | Implemented shared scene types and layout checks; `SceneHost::view()`; inline JSON reader/writer, load validation, and canonical FNV-1a hashing; `scene_dump`; a known two-sphere scene; and PNG/PFM/`.u32img` image IO. Registered camera storage/IO stubs S1/S2. Verified the macOS CPU build and all 30 tests. Reviewed all 12 Markdown files under `docs/` and corrected hash compiler flags, CLI error handling, layout checks, build instructions, and dependency documentation. Vista GCC/CUDA verification and CPU-renderer integration remain pending teammate hand-offs. | Branch `po/scene`: `28c5a35`, `a2614b8`, `d66318e`, `b13ab20`, `74e3c62`; documentation-review code fixes: `e8e9737`. Build instructions, dependency notes, and this contribution log accompany the documentation update. | |
| Nov 11 | Removed temporary CPU preview S4 and duplicate normal renderer; migrated tests to the integrated backend/CLI; replaced synthetic shadow fixture S5 with real lit-image corruption checks; corrected Mac temporary-path handling in CLI tests. Mac build succeeds and 231/233 tests pass; two benchmark machine/compiler detection tests remain incompatible with macOS/Clang. | #35, branch `po/remove-render-stubs` | |
| Nov 9–15 | Implemented the deterministic tiled CPU normal renderer and saved previews; binary scene IO and relative JSON references; core `rt_compare` CLI and corruption logic tests; PCG32/uniform development generation and 95% fitted camera; verified archive retrieval; additional known scenes. Tracked terrain stand-in S3 and the pending Backend, traversal, real Lambert/shadow fixtures, canonical archive and Vista checks. Full Mac CPU build and 144 tests pass. Development 10K/100K/1M scenes load and validate; repeated 10K JSON/bin files are byte-identical. Detailed commands and limits: [PO_PROGRESS.md](../PO_PROGRESS.md). | [#35](https://github.com/Mikail-Sadic-UT/Ray-Tracer/issues/35), branch `po/cpu-scene-tools` | |

### Nov 11 integration cleanup

Removed preview S4 and the duplicate `normal_renderer` API after adopting Mikail's
Backend/CLI. Preserved the pixel oracle, edge-tile, degenerate-scene, output reuse and
thread checks through `CpuBackend`. Replaced S5 with a 20×20 false shadow on a real
lit floor, with a lit 3×3 reference neighborhood for every corrupted pixel. Updated
CLI tests and commands to use `rt_render`; fixed the test's macOS temporary-path alias.
Mac Release build succeeds: 231/233 tests pass; the two remaining failures are
Linux/GCC-only machine/compiler detection in `test_bench`. Terrain S3, canonical
publication and Vista checks remain pending. No performance or full-gate claim.
