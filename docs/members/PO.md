# Po Han Ai (Member 4)

## 1. Role

- **Original:** single- and multicore CPU reference, scene generator, image comparisons, automated CPU checks.
- **Added:** `scene_io` + Scene struct, Embree baseline (Extended; Jett is backup), CPU optimization pass, final quality review and reproducibility instructions.

## 2. Owns

`src/scene/`, `src/cpu/`, `src/embree/`, `src/image/`, `apps/rt_gen/`, `apps/rt_compare/`, `apps/scene_dump/`, `tests/` structure (GoogleTest), `scenes/known/`, `scenes/golden/`.

CI wiring is Mikail's. Po owns the tests it runs.

## 3. Deliverables

| When | Deliverable | Size | Task | Unblocks |
|---|---|---|---|---|
| Sept 22–29 | Scene struct + `scene_io` (JSON inline + `.bin` reference) | M | 2 | Everyone |
| Sept 22–29 | GoogleTest setup | S | 1 | All tests |
| By ~Oct 10 | Seeded generator: spheres, icospheres, triangle terrain, floor; 10K→1M | M | 6 | Benchmarks, Jett's tests |
| By ~Oct 10 | Known-case inline JSON scenes | S | 3, 4 | Everyone's tests |
| By Oct 26 | Single-thread CPU reference: brute force, then BVH | M | 3, 5 | Ground-truth images |
| By Oct 26 | `rt_compare`: max error, % over threshold, diff heatmap; goldens in CI | M | 7 | GPU validation |
| By Oct 26 | Multicore renderer (OpenMP 16×16 tiles, dynamic) + scaling script | S | 9 | Multicore baseline |
| By Nov 20 | Embree backend (triangles + native spheres, shared `shade()`) | M | 17 | Best-possible CPU tier |
| By Nov 20 | CPU optimization pass + CPU render scaling study | M | 18, 20 | Fair baseline |
| Late Nov | Final quality review, reproducibility instructions | M | 20 | Release |

## 4. Contracts

- **Owns:** Scene struct, JSON schema, `.bin` format in [INTERFACES.md](../INTERFACES.md#2-scene-struct-and-file-format). Image comparison thresholds (with the team).
- **Consumes:** math (Arek), traversal (Mikail), BVH (Jett), shading (Arek), harness (Arek).

## 5. Dependencies

- **Blocked by:** math library (week 1). Later, Mikail's traversal for the BVH-based reference.
- **Blocks:** everyone (Scene struct), GPU validation (reference + `rt_compare`), benchmarks (generator).
- **Stubs while waiting:** brute-force traversal (`brute_closest`/`brute_any`) so the reference needs no BVH early. Inline JSON scenes before the generator.

## 6. Tests

- JSON and `.bin` round-trip: write then read gives identical structs.
- Reader rejects bad magic, unknown version, truncated files.
- Generator determinism: same seed gives identical bytes on x86 (CI) and ARM (manual check on Vista).
- PCG32 matches its published reference outputs.
- Reference: 1 thread vs N threads bit-identical; brute force vs BVH images match.
- `rt_compare`: identical images pass; a perturbed image fails; heatmap written.
- Embree vs CPU reference within tolerance.

## 7. References

- PCG random (pcg-random.org) for PCG32.
- Embree 4 documentation (user geometry not needed: use native triangles and `RTC_GEOMETRY_TYPE_SPHERE_POINT`).
- PBRT v4 (reference renderer structure).
- *Ray Tracing in One Weekend* (camera, sphere scenes).
- stb_image_write, PFM format notes.

## 8. First-week checklist (before Sept 29)

- [ ] Draft Scene struct, JSON schema, `.bin` header sections of INTERFACES.md for the Sept 24–25 meeting.
- [ ] Implement `scene.hpp` + JSON reader/writer + `scene_dump` (branch `po/scene-io`).
- [ ] Write `scenes/known/two_spheres.json` and 2–3 more known-case scenes.
- [ ] GoogleTest wired into CMake with Mikail.
- [ ] Set up WSL2 per [BUILD.md](../BUILD.md#laptop-wsl2-setup); confirm Vista login.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Broadest set of tasks | Embree moves to Jett if Oct 26 items slip; Jett and Arek help validate the reference |
| Reference called a "weak baseline" | Optimization pass + Embree comparison |
| Generator differs across machines | Own PRNG + float conversion; no `std::` distributions; byte-compare on Vista |
| Embree ARM64 build trouble on Vista | Check early (module or source build); verify on Vista |

## 10. Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Sept 22–28 | | | |
| | | | |
