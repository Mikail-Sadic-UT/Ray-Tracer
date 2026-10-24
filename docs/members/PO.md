# Po Han Ai (Member 4)

## 1. Role

- **Core:** `SceneHost`, `scene_io`, `.bin`, and scene hash; seeded generator sized in primitives; CPU backend on one core and many cores; `rt_compare` with AOV-based checks; golden images in CI; CPU render scaling; final reproducibility instructions.
- **Extension:** E4 Embree baseline (Jett is backup).

## 2. Owns

`src/scene/`, `src/cpu/`, `src/image/`, `src/embree/`, `apps/rt_gen/`, `apps/rt_compare/`, `apps/scene_dump/`, `scenes/known/`, `scenes/golden/`, test layout (GoogleTest). CI wiring is Mikail's.

## 3. Deliverables

| Gate | Deliverable | Size | Task |
|---|---|---|---|
| G0 (Oct 30) | `SceneHost`, `Scene` view, `Image`; JSON v2 reader/writer; scene hash; `scene_dump` | M | C2 |
| G0 (Oct 30) | Brute-force CPU render loop over `render_pixel`; PNG, PFM, and `.u32img` writers/readers; 3–4 known-case scenes | S | C3 |
| G1 (Nov 6) | `rt_gen` (`--spheres`, `--terrain-tris`, `--seed`); PCG32; `.bin` with `content_hash`; `-ffp-contract=off` | M | C6 |
| G1 (Nov 6) | CPU backend: 16×16 tiles, dynamic schedule, `num_threads(R)`, AOVs | S | C7 |
| G1 (Nov 6) | `rt_compare`: nonfinite, primary-hit, shadow, shading, localization, sRGB summary; heatmap; CI goldens | M | C8 |
| G3 (Nov 18) | CPU render scaling R = 1→72; optimization pass driven by profiles | M | C13 |
| After G2 | E4 Embree (4-day timebox): triangles + native spheres, `PrimRef` mapping, shared shading | M | E4 |
| Final | Quality review (2 reviewers); reproducibility instructions with exact commands, hashes, modules | M | C14 |

## 4. Contracts

- **Owns:** [Scene, image, and file formats](../INTERFACES.md#2-scene-image-and-file-formats), the `rt_compare` rules in the [quality protocol](../BENCHMARKING.md#2-image-comparison-rt_compare).
- **Consumes:** math (Arek), traversal (Mikail), BVH (Jett), shading (Arek), harness (Arek).

## 5. Dependencies

- **Blocked by:** math library (C2); Mikail's intersection for the brute-force image (C3).
- **Blocks:** everyone (scene types); benchmarks (generator); every GPU validation (`rt_compare`).
- **Stubs:** inline JSON scenes; `brute_*` before the BVH exists.

## 6. Tests

- JSON and `.bin` round trips; readers reject bad magic, unknown version, truncation, and hash mismatch.
- Same `rt_gen` arguments give the same `content_hash` on the laptop and on Vista.
- PCG32 matches its published reference outputs.
- CPU backend: 1 vs N threads bit-identical (PFM and AOVs).
- `rt_compare` must fail on an injected NaN, a shifted object, a 10×10 wrong block, and a flipped shadow region, and must pass on identical images.

## 7. References

- PCG (pcg-random.org).
- PBRT v4 (renderer structure).
- PFM format notes; stb_image_write.
- Embree 4 docs (E4).

## 8. This week (Oct 24–30)

- [ ] Review INTERFACES v0.3 section 2 (incl. canonical hash and `.u32img` AOV format); sign off by Oct 27.
- [ ] `scene_host.hpp`, JSON v2 reader, hash, `scene_dump` (branch `po/scene-io`).
- [ ] `scenes/known/two_spheres.json` plus 2–3 more known-case scenes.
- [ ] Brute-force CPU loop and PNG/PFM output for G0.
- [ ] GoogleTest wired in with Mikail.
- [ ] WSL2 setup per [BUILD.md](../BUILD.md#laptop-wsl2-setup); confirm Vista login.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Broadest workload (C2, C3, C6, C7, C8 by G1) | Arek takes CI golden wiring if needed; Jett takes terrain generation; Embree only after G2 |
| Generator bytes differ across machines | Own PRNG and float conversion; `-ffp-contract=off`; hash compare on Vista |
| `rt_compare` too lenient | Required failure cases above; tile localization check |
| Embree ARM64 trouble on Vista | Check early; it's an Extension and can be dropped |

## 10. Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | | | |
| | | | |
