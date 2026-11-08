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
| `camera_ray`, `Camera` parsing and overrides | Everyone's renderers | Landed (#16, #17: `camera.hpp`, `camera_io.hpp`) | §4 |
| `Bvh` from `build_bvh(Median)` | Mikail (traversal), Po (CPU backend) | Nov 6 | §3 |
| Terrain height field `h(x,z)` and triangle grid | Po (`rt_gen`) | Nov 6 | [scene families](../BENCHMARKING.md#scene-families) |
| Expected shading values for analytic tests | Arek | Nov 6 | [quality protocol §1](../BENCHMARKING.md#1-independent-correctness-checks-ci-as-each-feature-lands) |
| `Bvh` from the parallel builder | Everyone | Nov 13 | §3 |

**You depend on:**

| What | From | Until it lands, use |
|---|---|---|
| `AABB`, `vec3` | Arek (landed) | Nothing |
| `Scene` view | Po (landed, `scene/scene.hpp`) | Synthetic AABB + centroid arrays; the builder never needs a real scene |
| `Timings` | Arek (struct landed, #22) | Nothing: `build_bvh` takes `Timings&` (fields `bounds`, `build`, `layout`) |

**Done means:** the validator passes on random, clustered, all-equal-centroid, 0, 1, and 2 primitive inputs. Output bytes are identical for 1, 8, and 72 threads. Mikail's BVH-vs-brute-force test gives identical hits.

### Agent brief

Paste this into your coding agent along with `docs/INTERFACES.md`:

```text
You are implementing the BVH builder and camera for a C++17/CUDA ray tracer.
Read docs/members/JETT.md and docs/INTERFACES.md sections 3 and 4. The contract is
authoritative; follow it exactly. Layout: docs/REPO_SKELETON.md.

Scope (only edit these): src/bvh/, src/camera/, and tests for these modules.
Build: CMake, GCC 13, OpenMP. The CPU-only build (-DRT_ENABLE_CUDA=OFF) must compile.

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
- INTERFACES section 3: "Node layout (frozen)", "Canonical primitive order", "Depth cap",
  "LBVH refit synchronization", and "Determinism and validation".
- INTERFACES section 4 for the camera conventions.
- Timings for bounds, build, and layout go to `Timings` (INTERFACES section 8).

Work in the order of "First three tasks" and the Deliverables table in docs/members/JETT.md.
```

## Deliverables

| When | Deliverable | Size |
|---|---|---|
| Oct 27 | Review INTERFACES §3–4 and sign off | S |
| Oct 30 | `camera_ray` + tests | S |
| Nov 6 | Bounds stage, median builder, validator, `sah_cost` stat | M |
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
