# Stretch Goals and Future Work

None of this is committed. After the [Oct 24 re-baseline](ROADMAP.md#scope), these are unlikely to fit this term. The design leaves room for each, so adding one later doesn't need a rewrite. Extensions E1–E6 are in [ROADMAP.md](ROADMAP.md#extensions-in-priority-order-each-timeboxed).

## OptiX backend

Owners: Mikail, Arek. Considered only if all Core gates and E1–E3 are done.

**Goal:** a backend/platform comparison of our CUDA megakernel against OptiX on two GPUs.

| | CUDA megakernel | OptiX |
|---|---|---|
| Vista Hopper GPU (no RT cores) | Our BVH, software traversal | OptiX's BVH, software traversal |
| RTX 4070 Laptop (RT cores) | Our BVH, software traversal | OptiX's BVH, hardware traversal + triangle tests |

**What this does and doesn't show.** It doesn't cleanly isolate RT-core gains. The two GPUs differ in architecture, SM count, clocks, memory bandwidth, and cache sizes, and OptiX differs from our code in BVH quality and traversal implementation. On a single GPU, "OptiX vs our CUDA" mixes all of those with the hardware. RT cores can't be switched off on the 4070, so there's no clean within-GPU ablation. Report the 2×2 as "backend × platform" timings, with these confounders stated. Supporting evidence (OptiX vs CUDA speedup on the 4070 compared with the same ratio on Hopper) is suggestive, not causal.

Design:

- `src/optix/`, behind the [backend interface](ARCHITECTURE.md#backend-interface), `needs_bvh() = false`. Enabled with `RT_ENABLE_OPTIX=ON`.
- Geometry: built-in triangle GAS; built-in spheres (traversal is hardware-accelerated, but the sphere test may not be; record it). Map OptiX primitive IDs to `PrimRef` so AOVs and `rt_compare` work unchanged.
- Programs: raygen mirrors the megakernel. One `optixTrace` for the primary ray, then one per light in order, using the terminate-on-first-hit flag. It calls the shared `light_sample` and accumulates in the frozen order.
- Timing: acceleration-structure build = `convert` (on GPU). Best-possible tier only.
- Verification: build and run `optixSphere` in WSL2 on the 4070 first. If it fails, build only this backend natively on Windows. **Verify on Vista** that the driver provides the OptiX runtime.

## SIMD packet tracing (future work)

Embree fills the "best possible CPU" role instead (E4).

- **Wide BVH:** collapse the binary tree into 4- or 8-wide nodes with SoA child boxes.
- **Packet or stream traversal** for coherent primary rays from a tile.
- **Portability:** Google Highway or xsimd instead of separate AVX2 and NEON/SVE code.
- **Expected gains differ:** Grace SVE2 is 128-bit (4 floats); the i9-13980HX has AVX2 at 256-bit (8 floats) and no AVX-512.
- **Effort:** roughly one member's full workload.

## Mirror reflections

- `MAT_MIRROR` is reserved in [INTERFACES.md](INTERFACES.md#primitive-and-material-types). Max depth around 4.
- Megakernel: a bounded loop per thread. Wavefront: loop the stages with compaction, keeping fixed per-pixel slots so the output stays deterministic.
- Small float differences compound per bounce, so reflections need their own comparison category and are reported separately.

## GPU-side LBVH build

- Port the Core LBVH to CUDA (`cub::DeviceRadixSort`, Karras, refit, the same layout conversion).
- Experiment: CPU build + upload vs GPU build. This shifts the preparation vs transfer balance that the study measures.
- Must produce the same `BvhNode` layout and pass the same validator. Byte-identical output with the CPU builder is a goal, not a requirement, because of float differences in the bounds stage.

## Extra experiments

- **Grace-Grace nodes (144 cores):** one extra CPU scaling point. Costs node-hours.
- **Camera paths for T_frame:** render a sequence of cameras over a prepared scene instead of repeating one camera.
- **Interactive viewer via WSLg:** demo only.
