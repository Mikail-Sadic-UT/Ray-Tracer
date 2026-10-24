# Arek Plate (Member 3)

## 1. Role

- **Core:** math library and slab test; Lambert shading with point-light shadows (frozen shadow-ray and accumulation rules); harness for T_app, T_br, T_frame and results schema v2; double-precision intersection oracle (independent check on Mikail's code); plots.
- **Extensions:** E2 Blinn-Phong + directional lights; E5 wavefront queues, shade, and accumulate.
- **Stretch:** OptiX pairing.

## 2. Owns

`src/core/`, `src/shade/`, `src/bench/`, `src/gpu/wavefront/` (queues, shade, accumulate), `tests/oracle/` intersection oracle, `scripts/plot/`.

## 3. Deliverables

| Gate | Deliverable | Size | Task |
|---|---|---|---|
| G0 (Oct 30) | Math library + `intersect_aabb` with the required tests; merged by Oct 27 | M | C2 |
| G1 (Nov 6) | `surface_at`, `offset_ray_origin`, `light_sample`, `shade_hit` returning `PixelResult` (Lambert, point lights, `prim_id`, `light_mask`) | M | C4 |
| G1 (Nov 6) | Double-precision intersection oracle | S | C8 |
| G1 (Nov 6) | Harness on CPU: three quantities, components, `unattributed`, spread, results v2, T_app process script | M | C9 |
| G2 (Nov 13) | Harness on GPU: `cuda_init`, sync points, `render_kernel` events, `--no-components` check | S | C9 |
| G3 (Nov 18) | Target ratios (T_br, plus T_frame and T_app); breakdown and cold-vs-prepared plots | M | C13 |
| After G1 | E2 Blinn-Phong + directional lights (2-day timebox) | S | E2 |
| After G3 | E5 wavefront with fixed-slot accumulation | L | E5 |

## 4. Contracts

- **Owns:** [Math library](../INTERFACES.md#1-math-library), [Shading](../INTERFACES.md#6-shading), [Timing and results schema](../INTERFACES.md#8-timing-and-results-schema).
- **Consumes:** `SceneHost` (Po), `Hit` and `traverse_any` (Mikail), backend lifecycle (Mikail).

## 5. Dependencies

- **Blocked by:** nothing for C2. Mikail's `brute_any` for integrated shadow tests.
- **Blocks:** everyone in week 1 (math library); the CPU and GPU renderers (shading); all measurements (harness).
- **Stubs:** synthetic `SurfacePoint` / `Hit` arrays; `shade_hit` is unit-testable with hand-made `Hit`s and `brute_any`.

## 6. Tests

- Slab test cases listed in INTERFACES (on-face, just outside, inside, negative zero, behind).
- Jett's expected values: pole irradiance, 1/d² falloff, shadow boundary location, no acne on an unoccluded plane (relative 1e-5).
- A light behind the surface gives no contribution and no shadow ray.
- Shadow-ray `tmax` equals the distance from the offset origin to the light.
- Harness: schema validation; median and spread correct on synthetic data; `unattributed` computed rather than assumed zero.
- E2: highlight position matches the half-vector prediction. No per-feature tolerance loosening.

## 7. References

- Ray Tracing Gems I, Ch. 6 (self-intersection offset).
- PBRT v4: floating-point error, ray-box robustness (`gamma(3)`).
- Laine, Karras & Aila 2013 (E5).
- CUDA Programming Guide: events, synchronization.

## 8. This week (Oct 24–30)

- [ ] Review INTERFACES v0.3 sections 1, 6, 8; sign off by Oct 27.
- [ ] Merge `hostdev.hpp`, `math.hpp`, `ray.hpp`, `aabb.hpp` + slab tests by Oct 27 (branch `arek/math`). Everyone is blocked on this.
- [ ] Confirm an `RT_HOSTDEV` function compiles under both g++ 13 and nvcc.
- [ ] WSL2 + CUDA setup per [BUILD.md](../BUILD.md#laptop-wsl2-setup); confirm Vista login.
- [ ] Sketch the timer API and results v2 writer.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Math library late, everyone blocked | Small and fully specified; merge by Oct 27 |
| Instrumentation distorts small-scene timings | `--no-components` comparison; report `unattributed` |
| CPU/GPU mismatches in shading | Investigate the cause (FMA contraction, `pow`) before touching tolerances |
| Wavefront complexity | Only after G3; first to cut |

## 10. Contribution log

| Week | Work done | PRs / issues | Hours |
|---|---|---|---|
| Oct 24–30 | | | |
| | | | |
