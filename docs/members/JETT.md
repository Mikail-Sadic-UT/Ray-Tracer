# Jett Vanbrocklin (Member 1)

## 1. Role

- **Original:** parallel CPU scene preparation (BVH build), camera controls, preparation scaling measurement, milestone tracking.
- **Added:** `obj2bin` + Blender export script (Extended). Backup owner for the Embree backend.

## 2. Owns

`src/bvh/`, `src/camera/`, `apps/obj2bin/`, `scripts/blender/`, milestone status in [ROADMAP.md](../ROADMAP.md#milestone-status).

## 3. Deliverables

| When | Deliverable | Size | Task | Unblocks |
|---|---|---|---|---|
| Sept 22–29 | BVH layout contract, `Camera` struct | S | 2 | Mikail, Po |
| By ~Oct 3 | Serial median-split builder + validator | M | 5 | Mikail's real traversal, Po's BVH reference |
| By Oct 26 | Camera: look-at, vfov, aspect, JSON + CLI overrides | S | 3 | Everyone's renders |
| By Oct 26 | LBVH: Morton codes, OpenMP radix sort, Karras hierarchy, atomic refit | L | 11 | Large-scene runs |
| By Nov 20 | Parallel binned SAH (16–32 bins, parallel top levels, OpenMP tasks below) | L | 14 | Final benchmarks |
| By Nov 20 | Prep scaling study (1→72 threads), SAH cost metric | M | 14, 20 | Report |
| Late Nov | `obj2bin` + Blender export | M | 19 | Demo scenes |
| Ongoing | Milestone + issue tracking, weekly status | S/week | | |

## 4. Contracts

- **Owns:** BVH layout (`BvhNode`, `PrimRef`, `build_bvh`, validator) and `Camera` in [INTERFACES.md](../INTERFACES.md#3-bvh-layout).
- **Consumes:** `AABB`, `vec3` (Arek), `Scene` arrays (Po), traversal correctness tests (Mikail).

## 5. Dependencies

- **Blocked by:** math library `AABB` and Scene struct (week 1).
- **Blocks:** Mikail's traversal (needs layout + median builder), Po's BVH reference.
- **Stubs while waiting:** synthetic AABB/centroid arrays. The builder never needs a real scene.

## 6. Tests

- Validator on random inputs: coverage (each primitive once), containment, depth ≤ 64.
- Each builder: validator passes on random, clustered, and degenerate inputs (all centroids equal, 1 primitive, 2 primitives).
- Determinism: repeated builds with any thread count produce identical node arrays.
- SAH cost ordering: SAH ≤ LBVH on benchmark scenes (sanity, not strict).
- Camera: center ray hits `look_at`; corner rays match the vfov and aspect.
- `obj2bin`: a cube OBJ gives 12 triangles with correct Y-up coordinates.

## 7. References

- Jacco Bikker, "How to build a BVH" (jacco.ompf2.com): parts on binned SAH and fast builds.
- Karras 2012, "Maximizing Parallelism in the Construction of BVHs, Octrees, and k-d Trees".
- Wald 2007, "On Fast Construction of SAH-based BVHs".
- PBRT v4 Ch. 7 (BVH, SAH, LBVH section).
- *Ray Tracing in One Weekend* (camera chapter).

## 8. First-week checklist (before Sept 29)

- [ ] Draft BVH layout + `Camera` section of INTERFACES.md before the Sept 24–25 meeting.
- [ ] Confirm Vista login and run a CPU OpenMP hello world on a GH200 node.
- [ ] Set up WSL2 per [BUILD.md](../BUILD.md#laptop-wsl2-setup).
- [ ] Create GitHub issues for Tasks 1–21 with owners and milestones.
- [ ] Start the median-split builder on synthetic bounds (branch `jett/median-bvh`).
- [ ] Set up the weekly meeting and the milestone status table.

## 9. Risks

| Risk | Mitigation |
|---|---|
| SAH top levels serialize, scaling stalls | Data-parallel binning per node near the root; measure early; fall back to LBVH as the scaling result |
| LBVH depth > 64 with duplicate Morton codes | Break ties with primitive index (Karras); enforce depth cap in validator |
| Milestone tracking eats time | Keep it to the weekly meeting + status table |

## 10. Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Sept 22–28 | | | |
| | | | |
