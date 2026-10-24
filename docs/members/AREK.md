# Arek Plate (Member 3)

## 1. Role

- **Original:** GPU lighting and shadows, render-time and component measurement.
- **Added:** shared math library, benchmark harness + timing schema, wavefront queues/shade/accumulate kernels (Extended), plotting scripts, OptiX pairing (Stretch).

## 2. Owns

`src/core/`, `src/shade/`, `src/bench/`, `src/gpu/wavefront/` (queues, shade, accumulate), `scripts/plot/`.

## 3. Deliverables

| When | Deliverable | Size | Task | Unblocks |
|---|---|---|---|---|
| Sept 22–29 | Math library, `Material`/`Light` structs, shading API contract | M | 2 | Everyone (**first-week critical item**) |
| By ~Oct 10 | `shade()`: Lambert, point + directional lights, robust offset, analytic tests on synthetic hits | M | 4 | CPU reference, megakernel |
| By Oct 26 | Blinn-Phong behind `MAT_SPECULAR` | S | 12 | Final image quality |
| By Oct 26 | Benchmark harness: timers, cold/warm, median of 5, JSON/CSV | M | 8 | Everyone's measurements |
| By Nov 20 | Wavefront: queues, shade + accumulate kernels, compaction | L | 16 | Pipeline comparison, stage timings |
| By Nov 20 | Render-time study: sizes, light-count sweep, per-stage breakdown | M | 20 | Report |
| Late Nov | Plotting scripts | S | 20 | Report, slides |
| If go | OptiX raygen + shading integration with Mikail | M | 21 | RT-core comparison |

## 4. Contracts

- **Owns:** math library, shading API, timing/results schema in [INTERFACES.md](../INTERFACES.md#1-math-library).
- **Consumes:** Scene (Po), `Hit` + `traverse_any` (Mikail), backend interface (Mikail).

## 5. Dependencies

- **Blocked by:** only Mikail's traversal, and only for integration.
- **Blocks:** everyone in week 1 (math library), CPU reference and megakernel (shading), all measurements (harness).
- **Stubs while waiting:** synthetic `Hit` arrays and hand-placed surface points. `shade()` is fully testable without traversal.

## 6. Tests

- Math: dot/cross/normalize, AABB grow/union/surface area, ray-box with zero direction components.
- Lambert at a sphere's pole under an overhead point light matches the closed form.
- Inverse-square falloff: doubling distance quarters irradiance.
- Back-facing lights contribute nothing and emit no shadow ray.
- Shadow boundary of a sphere on a plane lands where geometry predicts.
- No shadow acne on a lit plane (all pixels lit when no occluder).
- Blinn-Phong highlight position matches the half-vector prediction.
- Harness: schema validation; components sum ≈ total; median of 5 computed correctly.

## 7. References

- Ray Tracing Gems I, Ch. 6, "A Fast and Robust Method for Avoiding Self-Intersection".
- PBRT v4: floating-point error and robust spawn-ray offsets.
- Laine, Karras & Aila 2013, "Megakernels Considered Harmful".
- *Ray Tracing in One Weekend* (vec3 and basic shading).
- CUDA Programming Guide: events, atomics, occupancy.

## 8. First-week checklist (before Sept 29)

- [ ] Draft math library + `Material`/`Light` + shading + timing sections of INTERFACES.md for the Sept 24–25 meeting.
- [ ] Write `hostdev.hpp`, `math.hpp`, `ray.hpp`, `aabb.hpp` with unit tests (branch `arek/math`).
- [ ] Set up WSL2 + CUDA per [BUILD.md](../BUILD.md#laptop-wsl2-setup); confirm a `RT_HOSTDEV` function compiles in both g++ and nvcc.
- [ ] Confirm Vista login.
- [ ] Sketch the results JSON fields with Mikail and Po.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Math library late, everyone blocked | It's small; aim to merge by Sept 26 |
| Wavefront complexity in November | Build after megakernel works; it's the first cut candidate |
| CPU/GPU mismatches near highlights | Diffuse-only compare mode; looser specular threshold |
| Timer overhead skews small scenes | Sync only at stage boundaries; report cold and warm separately |

## 10. Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Sept 22–28 | | | |
| | | | |
