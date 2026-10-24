# Stretch Goals and Future Work

None of this is committed. The core design leaves room for each item so adding one doesn't need a rewrite. Go/no-go for OptiX is at check-in 3 (see [ROADMAP.md](ROADMAP.md#task-21-optix-backend-gono-go-at-check-in-3)).

## OptiX backend

Owners: Mikail, Arek pairing.

**Goal:** measure what RT-core hardware adds over plain CUDA.

| | CUDA megakernel | OptiX |
|---|---|---|
| H100 (Vista, no RT cores) | Our BVH, software traversal | OptiX BVH, software traversal |
| RTX 4070 Laptop (RT cores) | Our BVH, software traversal | OptiX BVH, **hardware** traversal + triangle tests |

The 2×2 separates "OptiX's BVH and traversal" (H100 column) from "RT-core hardware" (4070 column).

Design:

- Lives in `src/optix/`, behind the [backend interface](ARCHITECTURE.md#backend-interface). Enabled with `RT_ENABLE_OPTIX=ON`.
- Geometry:
  - Triangles → built-in triangle GAS (hardware intersection on RT cores).
  - Spheres → OptiX built-in sphere primitives. Traversal is hardware-accelerated; the sphere test itself may not be. Report this.
  - Two GASes under one IAS, or one GAS per type.
- Programs:
  - Raygen: one thread per pixel. Calls `optixTrace` for the primary ray, then once per light for shadow rays. Same structure as our megakernel.
  - Closest-hit: fills a `Hit` (`t`, prim, barycentrics).
  - Any-hit / terminate-on-first-hit flag for shadow rays.
  - Raygen calls our shared `shade()` / `light_sample()`, so the shading is identical.
- Timing: `build` = OptiX acceleration-structure build (on GPU). Reported in the best-possible tier only, since the controlled tier requires our BVH.
- Memory: OptiX acceleration structures are always device allocations. `--mem` only affects our own buffers.

Early verification (any time before check-in 3):

1. Install the OptiX SDK and build `optixSphere` and `optixWhitted` in WSL2.
2. Run on the 4070. If it works, keep everything in WSL2.
3. If not, build only the `optix` backend natively on Windows (MSVC + CUDA). The rest stays WSL2.
4. Check OptiX availability on Vista H100 (the SDK is a header-only download; it needs the driver's OptiX runtime). **Verify on Vista.**

## SIMD packet tracing (future work)

Documented, not planned. Embree fills the "best possible CPU" role instead.

What it would take:

- **Wide BVH.** Collapse the binary BVH into 4- or 8-wide nodes with SoA child boxes, so one SIMD instruction tests all children.
- **Packet or stream traversal.** Primary rays from a tile are coherent. Trace 4–8 rays together through the BVH with masks.
- **Portability.** Use a portable SIMD library (Google Highway or xsimd) to avoid separate AVX2 and NEON/SVE code paths.
- **Expected gains differ by machine:**
  - Grace (Neoverse V2): SVE2 is 128-bit, 4 floats per vector.
  - i9-13980HX: AVX2 is 256-bit, 8 floats. No AVX-512.
  - So gains on Vista would be smaller than on the laptop.
- **Effort:** roughly one member's entire workload. That's why Embree is the stand-in.

## Mirror reflections

- Add `MAT_MIRROR` (reserved in [INTERFACES.md](INTERFACES.md#2-scene-struct-and-file-format)) and a max depth (e.g. 4).
- **Megakernel:** a bounded loop per thread: trace → shade → if mirror, reflect and continue, attenuating throughput.
- **Wavefront:** loop the stages. Shade emits reflection rays into a new queue, compact, and trace again until the queue is empty or depth is reached. This is where wavefront should beat megakernel.
- **Caveat:** small float differences compound per bounce. CPU vs GPU comparison gets fragile, so reflections need their own tolerance and are reported separately.

## GPU-side BVH build

- Port LBVH to CUDA: Morton codes, `cub::DeviceRadixSort`, Karras hierarchy, atomic refit. Same algorithm as the CPU version.
- Experiment: CPU build + upload vs GPU build (no upload of the tree). On the GH200 this changes the preparation vs transfer tradeoff, which is the proposal's main question.
- Output must use the same `BvhNode` layout and pass the same validator.

## Extra experiments

- **Grace-Grace nodes (144 cores).** One extra CPU scaling point beyond 72 cores. CPU-only, so no GPU comparison. Costs node-hours.
- **Interactive viewer via WSLg.** A small window that re-renders on camera changes. Purely for demos; not part of the study.
- **`obj2bin` demo scenes at scale.** Large public meshes (e.g. Stanford models) converted to `.bin` for a non-synthetic data point.
