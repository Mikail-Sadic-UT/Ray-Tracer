# Jett Vanbrocklin (Member 1)

## Start here

**You build:** the scene preparation stage, which turns a scene into a BVH (median builder, then a parallel builder), plus the camera and the terrain part of the generator. You also track milestones.

**Read first, in order:**
1. [INTERFACES §3 BVH layout and build pipeline](../INTERFACES.md#3-bvh-layout-and-build-pipeline) (your contract)
2. [INTERFACES §4 Camera](../INTERFACES.md#4-camera) (your contract)
3. [INTERFACES §1 Math library](../INTERFACES.md#1-math-library) (`AABB`, `vec3` you consume)
4. [INTERFACES §2 owning storage vs views](../INTERFACES.md#owning-storage-vs-views) (the `Scene` you read)
5. [ARCHITECTURE data flow](../ARCHITECTURE.md#data-flow)

**First three tasks:**
1. `camera_ray` inline in `src/camera/camera.hpp`, with tests (by Oct 30).
2. Bounds stage + serial median builder + validator on synthetic input (by Nov 6).
3. Parallel builder: LBVH with layout conversion (decision Nov 10, due Nov 13).

**You hand off:**

| What | To | When | Contract |
|---|---|---|---|
| `camera_ray`, `Camera` parsing and overrides | Everyone's renderers | Oct 30 | §4 |
| `Bvh` from `build_bvh(Median)` | Mikail (traversal), Po (CPU backend) | Nov 6 | §3 |
| Terrain height field `h(x,z)` and triangle grid | Po (`rt_gen`) | Nov 6 | [scene families](../BENCHMARKING.md#scene-families) |
| Expected shading values for analytic tests | Arek | Nov 6 | [quality protocol §1](../BENCHMARKING.md#1-independent-correctness-checks-ci-as-each-feature-lands) |
| `Bvh` from the parallel builder | Everyone | Nov 13 | §3 |

**You depend on:**

| What | From | Until it lands, use |
|---|---|---|
| `AABB`, `vec3` | Arek (Oct 27) | A minimal local copy, swapped out when merged |
| `Scene` view | Po (Oct 30) | Synthetic AABB + centroid arrays; the builder never needs a real scene |

**Done means:** the validator passes on random, clustered, all-equal-centroid, 0, 1, and 2 primitive inputs. Output bytes are identical for 1, 8, and 72 threads. Mikail's BVH-vs-brute-force test gives identical hits.

### Agent brief

Paste this into your coding agent along with `docs/INTERFACES.md`:

```text
You are implementing the BVH builder and camera for a C++17/CUDA ray tracer.
Repo layout: docs/REPO_SKELETON.md. Contract: docs/INTERFACES.md sections 3 and 4. Follow them exactly.

Scope (only edit these): src/bvh/, src/camera/, tests/ for these modules.
Build: CMake, GCC 13, OpenMP. The CPU-only build (-DRT_ENABLE_CUDA=OFF) must compile.

Rules:
- Follow INTERFACES section 0 (code conventions): namespace rt, includes from src/ like
  "core/math.hpp", host errors as rt::Error, CUDA calls in RT_CUDA_CHECK, one CMake target per module.
- BvhNode is 32 bytes, siblings adjacent (right = left + 1), PrimRef high bit = triangle.
- Canonical primitive order: spheres then triangles. All tie-breaks use it.
- Builds must be byte-identical for any thread count.
- Depth cap: subtrees deeper than 32 below depth 32 are rebuilt with the median method.
- LBVH refit uses std::atomic fetch_add(acq_rel) visit counters (INTERFACES "LBVH refit synchronization").
- camera_ray is RT_INLINE in camera.hpp (device-callable); camera.cpp is host-only.
- float only; no -ffast-math. No std containers in RT_HOSTDEV code.
- If the interface looks wrong or incomplete, do not change it locally. Stop and write a short
  note proposing the change.

Deliverables, in order:
1. camera_ray + tests (center ray hits look_at, corner rays match vfov/aspect, row 0 = top).
2. bounds stage, median builder, validate_bvh, sah_cost + tests on synthetic inputs.
3. LBVH: 30-bit Morton, stable OpenMP radix sort, Karras hierarchy, refit, layout conversion
   (collapse to max_leaf_size, adjacent child pairs, top-down slot assignment).
Each deliverable comes with GoogleTest tests. Timings for bounds/build/layout go to the Timings struct.
```

## Deliverables

| When | Deliverable | Size |
|---|---|---|
| Oct 27 | Review INTERFACES §3–4 and sign off | S |
| Oct 30 | `camera_ray` + tests | S |
| Nov 6 | Bounds stage, median builder, validator | M |
| Nov 6 | Terrain height field for `rt_gen` | S |
| Nov 6 | Shading expected values for Arek's tests (hand calculations or Python) | S |
| Nov 10 | **Decision:** if LBVH passes the validator and determinism tests, continue with it. Otherwise ship the task-parallel median builder (an accepted path, not a miss) | |
| Nov 13 | Parallel builder (LBVH preferred), layout conversion, depth fallback | L |
| Nov 18 | Preparation scaling, B = 1→72, per stage | M |
| Weekly | Status table in [ROADMAP](../ROADMAP.md#status), weekly meeting | S |
| Later | E3 binned SAH (after G2), E6 `obj2bin` + Blender (after G3), backup for E4 Embree | |

## Risks

| Risk | Mitigation |
|---|---|
| LBVH doesn't scale (sort or refit) | Time `bounds`/`build`/`layout` separately; profile before optimizing |
| Layout conversion bugs | Validator plus BVH-vs-brute-force tests on every builder |
| SAH is tempting before LBVH works | E3 waits for G2 |

## References

Karras 2012 (LBVH); PBRT v4 Ch. 7; Jacco Bikker, "How to build a BVH"; Wald 2007 (SAH, for E3); *Ray Tracing in One Weekend* (camera).

## Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | | | |
| | | | |
