# Jett Vanbrocklin (Member 1)

## 1. Role

- **Core:** camera; serial median builder + validator; LBVH (the required parallel builder) with layout conversion and depth fallback; preparation scaling study; independent expected values for shading tests; milestone tracking.
- **Extensions:** E3 parallel binned SAH; E6 `obj2bin` + Blender export; backup for E4 Embree.

## 2. Owns

`src/bvh/`, `src/camera/`, `apps/obj2bin/`, `scripts/blender/`, shading expected values in `tests/oracle/`, the [status table](../ROADMAP.md#status).

## 3. Deliverables

| Gate | Deliverable | Size | Task |
|---|---|---|---|
| G0 (Oct 30) | BVH and camera sections of INTERFACES v0.3 signed off (Oct 27); `camera_ray` per spec | S | C2, C3 |
| G1 (Nov 6) | Bounds stage; median builder (canonical order, tie rules, edge cases); validator | M | C5 |
| G1 (Nov 6) | Shading expected values (hand or Python) for Arek's analytic tests | S | C8 |
| G2 (Nov 13) | LBVH: Morton quantization, stable OpenMP radix sort, Karras hierarchy, atomic refit | L | C11 |
| G2 (Nov 13) | Layout conversion (collapse, adjacent pairs, top-down slot assignment); depth fallback at depth 32 | M | C11 |
| Nov 10 check | If LBVH fails the validator or determinism tests: switch to the task-parallel median builder (byte-identical to serial) | S | C11 contingency |
| G3 (Nov 18) | Preparation scaling, B = 1→72, per stage (`bounds`, `build`, `layout`); BVH stats | M | C13 |
| After G2 | E3 binned SAH (1-week timebox) | L | E3 |
| After G3 | E6 `obj2bin` + Blender | M | E6 |
| Weekly | Status table, weekly meeting | S | |

## 4. Contracts

- **Owns:** [BVH layout and build pipeline](../INTERFACES.md#3-bvh-layout-and-build-pipeline), [Camera](../INTERFACES.md#4-camera).
- **Consumes:** `AABB`, slab test (Arek); `SceneHost`/`Scene` (Po); traversal rules (Mikail).

## 5. Dependencies

- **Blocked by:** math library (C2).
- **Blocks:** Mikail's traversal tests on real trees; Po's CPU backend through the BVH; every scaling result.
- **Stubs:** synthetic AABB and centroid arrays. The builder never needs a real scene.

## 6. Tests

- Validator on random, clustered, all-equal-centroid, 0, 1, and 2 primitive inputs.
- Determinism: byte-identical `nodes` and `prim_refs` for B = 1, 8, 72.
- Median depth ≤ ceil(log2 N). LBVH depth within 30 + ceil(log2 N) + 1; `fallbacks` reported.
- Fallback path exercised by a synthetic tree forced deeper than 32.
- `sah_cost` reported as a diagnostic, not asserted.
- Camera: center ray hits `look_at`; corner rays match vfov and aspect; row 0 is the top.

## 7. References

- Karras 2012, "Maximizing Parallelism in the Construction of BVHs, Octrees, and k-d Trees".
- PBRT v4 Ch. 7 (BVH, LBVH section, SAH).
- Jacco Bikker, "How to build a BVH" (jacco.ompf2.com).
- Wald 2007, "On Fast Construction of SAH-based BVHs" (E3).

## 8. This week (Oct 24–30)

- [ ] Review INTERFACES v0.3 sections 3–4; sign off by Oct 27.
- [ ] Create GitHub issues for C1–C14 and E1–E6, with gate milestones.
- [ ] Confirm Vista login; run an OpenMP hello world on a GH node.
- [ ] WSL2 setup per [BUILD.md](../BUILD.md#laptop-wsl2-setup).
- [ ] `camera_ray` + tests (branch `jett/camera`).
- [ ] Start the bounds stage + median builder on synthetic input (branch `jett/median-bvh`).

## 9. Risks

| Risk | Mitigation |
|---|---|
| LBVH scaling limited by sort or refit | Time each stage separately; profile before optimizing |
| Layout conversion bugs | Validator plus BVH-vs-brute-force tests on every builder |
| SAH tempting before LBVH is solid | E3 starts only after G2 |

## 10. Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | | | |
| | | | |
