# Mikail Sadic (Member 2)

## Start here

**You build:** the build system and CI, ray-primitive intersection and BVH traversal (GPU visibility), the backend interface, and the CUDA renderer (`DeviceScene` + megakernel). You also handle Vista: verification, batch scripts, and the node-hour log.

**Read first, in order:**
1. [INTERFACES §5 Intersection and traversal](../INTERFACES.md#5-intersection-and-traversal) (your contract)
2. [INTERFACES §7 Backend interface](../INTERFACES.md#7-backend-interface) and [owning storage vs views](../INTERFACES.md#owning-storage-vs-views) (`DeviceScene` is yours)
3. [INTERFACES §6 tracing queries and pixel result](../INTERFACES.md#tracing-queries) (what your kernel calls)
4. [INTERFACES §3 node layout](../INTERFACES.md#node-layout-frozen) (what you traverse)
5. [BUILD.md](../BUILD.md) and [REPO_SKELETON.md](../REPO_SKELETON.md) (you create these files)

**First three tasks:**
1. Repo skeleton files: root CMake with a CPU-only fallback, CI, PR template, `hello_cuda` running on the laptop and a Vista GH node. Record the working GCC + CUDA pair (by Oct 30).
2. Intersection (watertight triangle, stable sphere) + `brute_closest` / `brute_any` (by Oct 30).
3. GPU smoke image: `render_pixel<BruteQuery, NormalShading>` in a CUDA kernel (~Nov 3).

**You hand off:**

| What | To | When | Contract |
|---|---|---|---|
| Building repo + CI | Everyone | Oct 30 | [BUILD.md](../BUILD.md) |
| `intersect_*`, `brute_*`, `Hit`, `BruteQuery` | Po (first CPU image), Arek (shadow tests) | Oct 30 (landed: `Hit` #20; intersection, `brute_*`, and `BruteQuery` #21) | §5, §6 |
| `traverse_closest` / `traverse_any`, `BvhQuery` | Po (CPU backend), Arek | Nov 6 | §5, §6 |
| `Backend` interface, `rt_render` CLI | Everyone | Nov 6 | §7 |
| CUDA megakernel backend | Everyone (GPU results) | Nov 13 | §7 |
| sbatch sweep scripts | Everyone (measurements) | Nov 13 | [BENCHMARKING](../BENCHMARKING.md#sweeps) |

**You depend on:**

| What | From | Until it lands, use |
|---|---|---|
| `vec3`, `Ray`, `AABB`, slab test | Arek (landed) | Nothing |
| `Scene` view | Po (landed, `scene/scene.hpp`) | Nothing |
| `Timings` | Arek (struct landed, #22) | Nothing: `DeviceScene::upload` and `Backend::prepare` take `Timings&` |
| `Bvh` | Jett (Nov 6) | A hand-built 3-node BVH |
| `shade_hit` | Arek (Nov 6) | `NormalShading` |

**Done means:** BVH traversal gives identical hits to brute force (same `prim`, bit-identical `t`) on random rays. GPU vs CPU passes `rt_compare` on the known scenes and `bench_100k`, on both machines. Repeated GPU runs are bit-identical.

### Agent brief

Paste this into your coding agent along with `docs/INTERFACES.md`:

```text
You are implementing intersection, traversal, the backend interface, and the CUDA backend for a
C++17/CUDA ray tracer.
Read docs/members/MIKAIL.md and docs/INTERFACES.md sections 5 and 7, plus "Owning storage vs
views", "Tracing queries", and "Pixel result". The contract is authoritative; follow it exactly.
Layout: docs/REPO_SKELETON.md. Build and toolchain: docs/BUILD.md.

Scope (only edit these): CMakeLists.txt, cmake/, .github/, src/trace/, src/backend.hpp, src/gpu/
(not wavefront/), apps/rt_render/, apps/hello_cuda/, scripts/tacc/, and tests for these.
Build: see docs/BUILD.md for the pinned GCC + CUDA pair. A CPU-only build must always work.

Standing rules:
- Conventions: INTERFACES section 0 (names, includes, errors, Doxygen comments, CMake targets).
- Device-callable code is header-inline (RT_HOSTDEV / RT_INLINE), with no -rdc. float only, no
  fast-math (see "General rules" at the top of INTERFACES.md for the one double exception).
- Workflow: docs/WORKFLOW.md. Check open issues first. Commits are kernel style and signed off
  (`git commit -s`).
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
- INTERFACES section 5 "Frozen rules" (hit acceptance, tie-breaks, triangle and sphere tests,
  traversal stack and overflow).
- INTERFACES section 7 (prepare/render split, what is timed where).
- docs/BENCHMARKING.md "Quality protocol" for the identical-hits and bit-identical checks.

Work in the order of "First three tasks" and the Deliverables table in docs/members/MIKAIL.md.
```

## Deliverables

| When | Deliverable | Size |
|---|---|---|
| Oct 27 | Review INTERFACES §5, §7 and sign off | S |
| Oct 30 | Repo skeleton files, CMake, CI, `hello_cuda` on laptop + GH node; toolchain pair recorded in BUILD.md | M |
| Oct 30 | Intersection + brute force | M |
| ~Nov 3 | GPU smoke image | S |
| Nov 6 | Traversal (`BvhQuery`) | M |
| Nov 6 | `Backend` interface (`backend.hpp`), CPU/CUDA dispatch in `rt_render` | M |
| Nov 13 | `DeviceScene` (explicit) + megakernel; sweep sbatch scripts; node-hour log | M |
| Nov 18 | GPU profiling (`-Xptxas -v`, Nsight) | M |
| Later | E1 memory modes (after G2); E5 trace kernels (after G3); OptiX (stretch) | |

## Risks

| Risk | Mitigation |
|---|---|
| Front-loaded first week | Keep the CMake minimal; intersection is small and fully specified |
| Traversal is on the critical path | `brute_*` ships first with the same rules, so others aren't blocked |
| CUDA/GCC pair unsupported on Vista | Check NVIDIA's host-compiler table for the exact CUDA version; try module pairs early |
| Wrong OpenMP runtime on Vista | `ldd` check in [BUILD.md](../BUILD.md#vista-setup) |

## References

PBRT v4 Ch. 6 (watertight test); Woop et al. 2013; Aila & Laine 2009 (GPU traversal); CUDA Programming Guide (local memory, unified memory); TACC Vista user guide.

## Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | | | |
| Nov 1–7 | Clarified INTERFACES §8: `render_kernel` is reported in a per-run `diagnostics` object, outside `components` and the totals (resolves W5). Reviewed and merged Arek's `NormalShading` and robust `normalize()` PRs | #24, #28, #29 | |
