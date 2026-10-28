# Arek Plate (Member 3)

## Start here

**You build:** the shared math library, lighting and shadows (`shade_hit`, run on both CPU and GPU), and the timing harness that every measurement goes through. You also write the independent intersection check and the golden-image tests.

**Read first, in order:**
1. [INTERFACES §1 Math library](../INTERFACES.md#1-math-library) (your contract; **everyone is blocked on it**)
2. [INTERFACES §6 Shading](../INTERFACES.md#6-shading) (your contract)
3. [INTERFACES §8 Timing and results schema](../INTERFACES.md#8-timing-and-results-schema) (your contract)
4. [BENCHMARKING timing quantities](../BENCHMARKING.md#timing-quantities) (what the harness measures)
5. [INTERFACES §5 Hit and traversal rules](../INTERFACES.md#5-intersection-and-traversal) (what you consume)

**First three tasks:**
1. Math library (`hostdev.hpp`, `math.hpp`, `ray.hpp`, `aabb.hpp`) with the ray-box slab tests, merged **by Oct 27**.
2. `shade_hit` (Lambert, point lights, shadow rays, `PixelResult`), tested on hand-made hits (by Nov 6).
3. Harness: `Timings`, timers, results JSON, T_br and T_frame on CPU (by Nov 6).

**You hand off:**

| What | To | When | Contract |
|---|---|---|---|
| `vec3`, `Ray`, `AABB`, `intersect_aabb` | Everyone | Oct 27 | §1 |
| `shade_hit`, `LitShading`, `NormalShading`, `PixelResult` | Po (CPU backend), Mikail (megakernel) | Nov 6 (`NormalShading` by Oct 30) | §6 |
| `Timings`, timers, results writer | Everyone | Nov 6 | §8 |
| Double-precision intersection oracle | Mikail (independent check) | Nov 6 | [quality §1](../BENCHMARKING.md#1-independent-correctness-checks-ci-as-each-feature-lands) |
| Golden-image tests (render known scene, `rt_compare` vs committed golden) | CI | ~Nov 6 | [BUILD golden images](../BUILD.md#golden-images) |

**You depend on:**

| What | From | Until it lands, use |
|---|---|---|
| `Hit`, `brute_any` | Mikail (Oct 30) | Hand-made `Hit`s and a fake query that returns "never occluded" |
| `Scene` view | Po (Oct 30) | A hand-filled `Scene` in tests |
| Expected shading values | Jett (Nov 6) | Your own hand checks, replaced by Jett's independent numbers |
| `rt_compare` | Po (Nov 6) | A temporary exact byte comparison for goldens |

**Done means:**
- Lambert value at a sphere's pole, 1/d² falloff, shadow boundary location, and no acne all match Jett's expected values (relative 1e-5).
- A light behind the surface casts no shadow ray.
- The results JSON validates against §8, and the median and spread are correct on synthetic data.

### Agent brief

Paste this into your coding agent along with `docs/INTERFACES.md`:

```text
You are implementing the math library, shading, and timing harness for a C++17/CUDA ray tracer.
Repo layout: docs/REPO_SKELETON.md. Contract: docs/INTERFACES.md sections 1, 6, and 8.
Timing definitions: docs/BENCHMARKING.md "Timing quantities". Follow them exactly.

Scope (only edit these): src/core/, src/shade/, src/bench/, tests/oracle/ (intersection oracle),
scripts/plot/, and tests for these.
Code must compile under both g++ 13 and nvcc (RT_HOSTDEV = __host__ __device__ under __CUDACC__).

Rules:
- Follow INTERFACES section 0 (code conventions): namespace rt, includes from src/ like
  "core/math.hpp", host errors as rt::Error, CUDA calls in RT_CUDA_CHECK, one CMake target per module.
- Header-only, POD types; no std containers, exceptions, or virtuals in RT_HOSTDEV code.
- Slab test: choose near/far planes from the sign of inv_dir BEFORE multiplying (INTERFACES 1).
  Test all 24 zero-direction cases listed there.
- Shadow rays: follow "Shadow-ray construction (frozen)" step by step. A coincident light
  (d2 == 0) contributes nothing.
- Accumulation order is fixed: ambient, then lights 0..L-1. light_mask bit i = light contributed.
- shade_hit is templated on the query type (q.any for shadow rays). Provide LitShading and NormalShading.
- Timers: steady_clock for components and totals; measure totals directly, report unattributed time.
- float only; no -ffast-math.
- Comments are C++ Doxygen (`///`, `///<`, `//`), per INTERFACES section 0. Commit messages are
  kernel style, signed off with `git commit -s`, per WORKFLOW.md "Commit messages".
- At the start of a session, run the environment check in WORKFLOW.md "Development environment".
  If it is not WSL Ubuntu 24.04 or Vista with the BUILD.md toolchain, warn the user once (never
  block), and add the `Developed-on:` / `Needs-check:` trailers to your commits.
- If the interface looks wrong or incomplete, do not change it locally. Stop and write a short
  note proposing the change.

Deliverables, in order:
1. Math library + slab tests (needed by everyone first).
2. NormalShading, then surface_at, offset_ray_origin (Ray Tracing Gems ch. 6), light_sample,
   shade_hit, with analytic tests on hand-made hits.
3. Timings struct, ScopedTimer, GpuTimer (cudaEvent), results JSON writer, T_br/T_frame loop.
4. Double-precision ray-sphere / ray-triangle oracle (test-only) compared against the float versions.
```

## Deliverables

| When | Deliverable | Size |
|---|---|---|
| Oct 27 | Math library + slab tests merged; review INTERFACES §1, §6, §8 and sign off | M |
| Oct 30 | `NormalShading` (for first images) | S |
| Nov 6 | `shade_hit` with analytic tests; double-precision oracle; harness on CPU; golden tests | M |
| Nov 13 | Harness on GPU (`cuda_init`, `cudaEvent` stages); T_app process script | S |
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
| | | | |
