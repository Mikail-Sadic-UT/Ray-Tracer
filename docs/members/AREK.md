# Arek Plate (Member 3)

## Start here

**You build:** the shared math library, lighting and shadows (`shade_hit`, run on both CPU and GPU), and the timing harness that every measurement goes through. You also write the independent intersection check and the golden-image tests.

**Read first, in order:**
1. [INTERFACES §1 Math library](../INTERFACES.md#1-math-library) (your contract; **everyone is blocked on it**)
2. [INTERFACES §6 Shading](../INTERFACES.md#6-shading) (your contract)
3. [INTERFACES §8 Timing and results schema](../INTERFACES.md#8-timing-and-results-schema) (your contract)
4. [BENCHMARKING timing quantities](../BENCHMARKING.md#timing-quantities) (what the harness measures)
5. [INTERFACES §5 Hit and traversal rules](../INTERFACES.md#5-intersection-and-traversal) (what you consume)

**Open issues first** ([WORKFLOW](../WORKFLOW.md#start-with-open-issues)): #4 (proposed §1 changes). #3 (slab test with subnormal directions) is fixed by #30.

**First three tasks:**
1. Math library (`hostdev.hpp`, `math.hpp`, `ray.hpp`, `aabb.hpp`) with the ray-box slab tests, merged **by Oct 27**.
2. `shade_hit` (Lambert, point lights, shadow rays, `PixelResult`), tested on hand-made hits (by Nov 6).
3. Harness: `Timings`, timers, results JSON, T_br and T_frame on CPU (by Nov 6).

**You hand off:**

| What | To | When | Contract |
|---|---|---|---|
| `vec3`, `Ray`, `AABB`, `intersect_aabb` | Everyone | Oct 27 | §1 |
| `shade_hit`, `LitShading`, `NormalShading`, `PixelResult` | Po (CPU backend), Mikail (megakernel) | Nov 6 (`NormalShading`, `PixelResult` landed, #24; `offset_ray_origin`, `light_sample`, `shade_hit`, `LitShading` and the `render_pixel` default landed, #49, with the default being a one-line edit to Mikail's `shade/render_pixel.hpp` that he approved in the group chat on Nov 11; Po's stub S5 is ready to remove) | §6 |
| `Timings` struct (names only, `bench/timings.hpp`; [ROADMAP blocker 6](../ROADMAP.md#start-now-check-and-cross-owner-blockers-oct-28)) | Jett, Mikail, Po (`build_bvh`, `DeviceScene::upload`, `Backend::prepare` take `Timings&`) | Landed (#22) | §8 |
| Timers, results writer | Everyone | Nov 6 (landed, #51: `rt_bench` with `WallTimer`, `ScopedTimer`, `summarize`, `measure_build_and_render`, `measure_frame`, `make_app_record`, `collect_machine_info`, `to_json`, `write_results` and `validate_results`; `rt_render` wires `--mode`, `--runs` and `--results` against it) | §8 |
| Double-precision intersection oracle | Mikail (independent check) | Nov 6 | [quality §1](../BENCHMARKING.md#1-independent-correctness-checks-ci-as-each-feature-lands) |
| Golden-image tests (render known scene, `rt_compare` vs committed golden) | CI | ~Nov 6 | [BUILD golden images](../BUILD.md#golden-images) |

**You depend on:**

| What | From | Until it lands, use |
|---|---|---|
| `Hit`, `brute_any` | Mikail (landed: `Hit` #20, `brute_any` #21) | Nothing |
| `Scene` view | Po (landed, `scene/scene.hpp`) | Nothing |
| Expected shading values | Jett (Nov 6) | Your own hand checks, replaced by Jett's independent numbers |
| `rt_compare` | Po (landed, #36: `image/compare.hpp`, `apps/rt_compare`) | Nothing. `LitShading` landed (#49); corruption test 4 can now run on the real lit image (stub S5 ready to remove, Po's file) |

**Done means:**
- Lambert value at a sphere's pole, 1/d² falloff, shadow boundary location, and no acne all match Jett's expected values (relative 1e-5).
- A light behind the surface casts no shadow ray.
- The results JSON validates against §8, and the median and spread are correct on synthetic data.

### Agent brief

Paste this into your coding agent along with `docs/INTERFACES.md`:

```text
You are implementing the math library, shading, and timing harness for a C++17/CUDA ray tracer.
Read docs/members/AREK.md and docs/INTERFACES.md sections 1, 6, and 8. Timing definitions:
docs/BENCHMARKING.md "Timing quantities". The contract is authoritative; follow it exactly.
Layout: docs/REPO_SKELETON.md.

Scope (only edit these): src/core/, src/shade/, src/bench/, tests/oracle/ (intersection oracle),
scripts/plot/, and tests for these.
Build: code must compile under both g++ 13 and nvcc (RT_HOSTDEV = __host__ __device__ under
__CUDACC__). The CPU-only build must always work.

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
- INTERFACES section 1 "Ray-box slab test (frozen)" and its required tests.
- INTERFACES section 6 "Shadow-ray construction (frozen)" and "Accumulation order (frozen)".
- INTERFACES section 8 and BENCHMARKING "Timing quantities" (totals are measured directly,
  unattributed time is reported).

Work in the order of "First three tasks" and the Deliverables table in docs/members/AREK.md.
```

## Deliverables

| When | Deliverable | Size |
|---|---|---|
| Oct 27 | Math library + slab tests merged; review INTERFACES §1, §6, §8 and sign off | M |
| Oct 30 | `NormalShading` (for first images) and `PixelResult`, in `shade/shading.hpp` (landed, #24) | S |
| Nov 6 | `surface_at` (landed, #24), `offset_ray_origin`, `light_sample`, `shade_hit` with analytic tests, and the `LitShading` default for `render_pixel` (landed, #49; an edit to Mikail's `shade/render_pixel.hpp`, approved by him on Nov 11, in its own commit; tests use my own hand-derived values until Jett's independent ones exist); double-precision oracle; harness on CPU; golden tests | M |
| Nov 13 | Harness on GPU (`cuda_init`, `cudaEvent` stages) (`bench/gpu_timer.hpp` landed, #51; device tests pass on the RTX 4070 laptop, Vista not yet checked); T_app process script | S |
| Nov 18 | Target ratios and breakdown plots | M |
| Later | E2 Blinn-Phong + directional lights (after G2); E5 wavefront queues/shade/accumulate (after G3); OptiX pairing (stretch) | |

## Risks

| Risk | Mitigation |
|---|---|
| Math library late, everyone blocked | It's small and fully specified; merge by Oct 27 |
| CPU/GPU shading mismatches | Find the cause (often FMA contraction) before touching tolerances |
| Wavefront complexity | Only after G3; first to cut |

## References

Ray Tracing Gems I Ch. 6 (self-intersection offset); PBRT v4 (floating-point error, ray-box robustness); Laine, Karras & Aila 2013 (E5); CUDA Programming Guide (events).

## Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | | | |
| Nov 1–7 | Implemented scale-first `normalize()` for finite, nonzero vectors; added regression tests for extreme, subnormal, and widely separated magnitudes. `NormalShading`, `surface_at`, `SurfacePoint` and `PixelResult` in `shade/shading.hpp`, with tests | #2, #23, #24 | — |
| Nov 8–14 | Slab test for subnormal ray directions (#3): reproduction, overflow analysis, and tests against the double reference. Final fix divides on subnormal axes in `float`, with a scene coordinate bound (§1, §2). Lambert shading: `offset_ray_origin`, `light_sample`, `shade_hit`, `LitShading`, the `render_pixel` default (W7: a default template argument in Mikail's `shade/render_pixel.hpp`, approved by him in the group chat on Nov 11 and kept in its own commit); analytic tests (sphere pole, floor cosine and 1/d², shadow edge at x = √2, no acne on a 2000-wide plane, no shadow ray for a back-facing light, light order and mask); a CUDA test that compiles `LitShading` as device code (device run not done: no GPU here, needs a GPU or Vista) Benchmark harness library `rt_bench` (C9): run statistics, wall-clock timers, T_br and T_frame run loops with an untimed reset, machine and build identity, results schema v2 writer and validator, CUDA event and stage timers. Recorded the schema gaps as CONTRACT_NOTES W10 and W11 | #3, #30, #43, #49, #50, #51 | — |
