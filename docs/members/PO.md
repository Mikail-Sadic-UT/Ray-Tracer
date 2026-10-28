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
2. A few hand-written `scenes/known/*.json` + PNG/PFM/`.u32img` writers + the brute-force CPU loop for the first image (by Oct 30).
3. `rt_gen` uniform layout + `.bin`, then the CPU backend with OpenMP tiles (by Nov 6).

**You hand off:**

| What | To | When | Contract |
|---|---|---|---|
| `SceneHost`, `Scene`, `Image`, JSON IO, validation | Everyone | Oct 30 | §2 |
| Known-case scenes (`scenes/known/`) | Everyone's tests | Oct 30 | §2 |
| CPU backend (reference renderer) | Everyone (ground truth) | Nov 6 | §7 |
| `rt_gen` + `.bin` + published scene set | Everyone (benchmarks) | Nov 6 | §2, [scene families](../BENCHMARKING.md#scene-families) |
| `rt_compare` core checks | Mikail (GPU validation), Arek (goldens) | Nov 6 | [image comparison](../BENCHMARKING.md#2-image-comparison-rt_compare) |

**You depend on:**

| What | From | Until it lands, use |
|---|---|---|
| `vec3`, `AABB` | Arek (Oct 27) | Nothing to stub; it's small |
| `brute_closest` / `BruteQuery` | Mikail (Oct 30) | A trivial sphere-only loop in your test |
| `shade_hit` / `NormalShading` | Arek | Normal-as-color written inline, replaced when merged |
| Terrain height field | Jett (Nov 6) | A flat two-triangle floor |
| `Bvh` + `BvhQuery` | Jett, Mikail (Nov 6) | `BruteQuery` |

**Done means:**
- JSON and `.bin` round trips are exact, and the readers reject bad files.
- Load validation rejects each invalid case in the [valid input table](../INTERFACES.md#valid-input-checked-at-load).
- The CPU backend at 1 vs N threads is bit-identical.
- `rt_compare` passes identical images and rejects the four core corruption tests.

### Agent brief

Paste this into your coding agent along with `docs/INTERFACES.md`:

```text
You are implementing the scene layer, generator, image IO, CPU backend, and image comparison
tool for a C++17/CUDA ray tracer. Repo layout: docs/REPO_SKELETON.md.
Contract: docs/INTERFACES.md section 2, "Tracing queries", "Pixel result", and section 7.
Generator spec: docs/BENCHMARKING.md "Scene families". Comparison spec: docs/BENCHMARKING.md
"Image comparison (rt_compare)", core checks only. Follow them exactly.

Scope (only edit these): src/scene/, src/image/, src/cpu/, apps/rt_gen/, apps/rt_compare/,
apps/scene_dump/, scenes/known/, scenes/golden/, scripts/fetch_scenes.sh, and tests for these.
Build: CMake, GCC 13, OpenMP. Use nlohmann/json for JSON, stb_image_write for PNG.

Rules:
- Follow INTERFACES section 0 (code conventions): namespace rt, includes from src/ like
  "core/math.hpp", host errors as rt::Error, CUDA calls in RT_CUDA_CHECK, one CMake target per module.
- Types are POD; padding fields default to 0. Scene hash = FNV-1a over a canonical
  field-by-field encoding (never raw struct bytes); the camera is excluded.
- .bin layout and .u32img layout exactly as specified; little-endian.
- Validate every loaded scene against "Valid input (checked at load)"; reject with field + index.
- Generator: own PCG32, own u32->float, own Box-Muller; no std distributions; generator
  sources compiled with -ffp-contract=off. Same binary + args -> identical bytes.
- CPU backend: OpenMP over 16x16 tiles, schedule(dynamic), num_threads(R); calls
  render_pixel<Query, Shading>; output must be bit-identical for any thread count.
- rt_compare: linear floats before clamping; nonfinite, explained hit/shadow mismatch (3x3),
  mismatch budget, shading tolerance; one pass/fail line per check; nonzero exit on failure.
- Comments are C++ Doxygen (`///`, `///<`, `//`), per INTERFACES section 0. Commit messages are
  kernel style, signed off with `git commit -s`, per WORKFLOW.md "Commit messages".
- At the start of a session, run the environment check in WORKFLOW.md "Development environment".
  If it is not WSL Ubuntu 24.04 or Vista with the BUILD.md toolchain, warn the user once (never
  block), and add the `Developed-on:` / `Needs-check:` trailers to your commits.
- Stubs and local workarounds: mark each with `// STUB(S<n>, <owner>): remove when <x> lands (#<issue>)`
  and add a row to the ROADMAP.md stub register in the same PR. Delete both when the real code lands
  (WORKFLOW.md "Stubs and local workarounds").
- If the interface looks wrong or incomplete, do not change it locally. Stop and write a short
  note proposing the change.

Deliverables, in order:
1. scene types, JSON IO, hash, validation, scene_dump + tests.
2. PNG/PFM/.u32img IO; known-case scenes; brute-force CPU loop.
3. rt_gen --layout uniform --primitives N --seed S (fitted camera, light per spec) + .bin IO.
4. CPU backend (Backend interface) with AOVs.
5. rt_compare core checks + the 4 core corruption tests.
```

## Deliverables

| When | Deliverable | Size |
|---|---|---|
| Oct 27 | Review INTERFACES §2 and sign off | S |
| Oct 30 | Scene types, JSON IO, hash, validation, `scene_dump`; known scenes; image IO; brute-force CPU image | M |
| Nov 6 | `rt_gen` uniform + `.bin` (terrain from Jett); CPU backend; `rt_compare` core + 4 corruption tests | M |
| Nov 6 | Generate the benchmark set once, publish it as release `scenes-v1` with `scenes/manifest.json`, and add `scripts/fetch_scenes.sh` ([details](../INTERFACES.md#generator-determinism-and-scene-distribution)) | S |
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
| Oct 24–30 | | | |
| | | | |
