# Lit regression goldens

**These goldens are regression-only.** They were rendered by this repository's own renderer
(`rt_render --shade lit`), so they catch a change in the lit output, not a mistake that was
already in it. They do **not** validate shading correctness independently.

The independent check is Mikail's: `tests/oracle/shading_expected.py` (#59) recomputes the
Lambert and shadow values in double precision from INTERFACES section 6 without using any code
from `src/shade`, and `tests/test_shade_independent.cpp` checks the renderer against its
literals. The analytic tests in `tests/test_shade.cpp` and the double-precision intersection
oracle in `tests/oracle/` (#60, not yet merged) are the other independent checks. Those cover chosen points, not whole
images.

Producing commit: dd0efa7d615c14844a87aa7d0aa2202b9b685638

## What is here

For each of `known_two_spheres`, `known_single_sphere` and `known_degenerate_triangle`
(`scenes/known/`, 640x480, one point light):

| File | Content |
|---|---|
| `<scene>.pfm` | Linear RGB, little-endian PFM |
| `<scene>.primid.u32img` | `prim_id` AOV |
| `<scene>.lightmask.u32img` | `light_mask` AOV |
| `PROVENANCE.txt` | Commit, command, compiler, build type, reason and SHA-256 of every file |

`known_degenerate_triangle` renders the same image as `known_two_spheres` because its zero-area
triangle can never be hit; the two goldens are byte-identical on purpose.

Each scene was rendered with `rt_render --backend cpu --bvh median --shade lit --build-threads 1
--render-threads 1 --aov primid,lightmask`. The script renders every scene twice and refuses to
write unless both renders are byte-identical. A 4-thread run and a brute-force run also produced
the same bytes on the machine that made them.

Check the files against their recorded hashes with `cd scenes/golden && sha256sum -c <(sed -n
'/^sha256:/,$p' PROVENANCE.txt | tail -n +2)`.

## How they are used

- `test_golden` (ctest) renders each scene through `CpuBackend` (median BVH on 1 and 4 threads,
  brute force on 4 threads) and compares it with the golden using `rt::compare_images` with the
  normal `rt_compare` tolerances (`tau_abs` 1e-5, `tau_rel` 1e-4, 0.05% mismatch budget). It also
  checks that each golden has the scene's resolution, finite values and the primitives the scene
  file says are visible, and that a changed render is rejected.
- `golden_cli` (ctest) runs the real `rt_render` and `rt_compare` on the same scenes.

The comparison is tolerance-based, not bit-exact, as in docs/BUILD.md "Golden images".

**Known gaps** (found by mutating `LitShading` locally and running `test_golden`; both are
caught by the analytic tests, not by the goldens):
- A shading change smaller than about 1e-4 relative passes: a Lambert term scaled by 1.00001 was
  not caught. Larger changes were: Lambert x1.001, shadow rays ignored, 1/d instead of 1/d^2,
  and ambient x1.01 all failed `test_golden`.
- A change that makes back-facing lights contribute (`|cos|` instead of `max(0, cos)`) was not
  caught: the known scenes show no back-facing surface that its own geometry does not already
  shadow.

## Regenerating

**Regenerate deliberately, in a PR that says why. Never regenerate to make a failing test
pass.** A failing `test_golden` means the output changed: find out why first. If the change is
intended, run

    scripts/regen_goldens.sh --render build/apps/rt_render/rt_render --reason "why"

from a clean tree, then commit the files, `PROVENANCE.txt` and the updated hash above together,
and explain the visible difference in the PR.

## Not yet verified

The goldens were made and checked on one machine (Ubuntu 24.04, x86-64, GCC 13.3, Release).
Whether they agree on other CPUs, or with FMA contraction (arm64, for example), is not yet
checked. Each golden is a 3.7 MB PFM plus two 1.2 MB AOV files, 6.1 MB per scene and 18.4 MB for
the three.
