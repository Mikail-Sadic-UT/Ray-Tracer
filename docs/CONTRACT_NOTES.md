# Contract notes

Places where code follows [INTERFACES.md](INTERFACES.md) but the contract itself may need to change later. INTERFACES.md is a working agreement, expected to change during development. These notes record where it is thin or may need adjusting, so the gap is found again when it matters instead of rediscovered.

These are not bugs. A bug in someone's code is a GitHub issue for its owner.

## How to use

- **Add an entry** when a review or implementation finds a gap: code follows the contract, but the contract is incomplete or may be suboptimal. Use the next free ID.
- **Act when the trigger happens.** A change to a type, signature, or numeric rule follows [Interface changes](INTERFACES.md#interface-changes). A clarification needs only the normal review.
- **Delete the entry** in the commit that resolves it. The weekly meeting skims this list alongside the [stub register](ROADMAP.md#stub-register).

## Open notes

| ID | Contract | Gap | Trigger | Owners |
|---|---|---|---|---|
| W1 | §2 `Light` | `pad = 0.0f` makes `Light` non-trivial | A kernel stages lights in shared memory | Po, Mikail |
| W2 | §2 Valid input (camera) | Precision of the `up`-parallel check is unspecified | Jett's camera validation lands ([#10](https://github.com/Mikail-Sadic-UT/Ray-Tracer/issues/10)) | Jett, Po |
| W3 | §2 Valid input | Core builds accept specular materials but reject directional lights | E2 (Blinn-Phong) starts | Po, Arek |

### W1: `Light::pad` default initializer

The default member initializer makes `Light` not trivially default-constructible. nvcc 13.3 accepts `__shared__ rt::Light tile[32];` with warning #20054-D (dynamic initialization is not supported for `__shared__`) and ignores the initializer there, so `pad` isn't guaranteed to be zero in shared memory.

No current impact. The megakernel reads lights from global memory, uploads copy the host bytes (where `pad` is zero), and `pad` is never hashed.

**If triggered:** drop `= 0.0f`, add `static_assert(std::is_trivial<Light>::value)`, and construct lights with `Light{}` or explicit braces. `scene_io` already rejects a non-zero `pad`, so an uninitialized one fails loudly at load. This is a type change: interface PR, Po and Mikail approve.

### W2: camera `up`-parallel check precision

The rule is `|cross(normalize(up), w)| ≥ 1e-6`. The camera IO stub (`src/scene/camera_io_stub.hpp`, S2) computes it in `double`. If Jett's `validate_camera` uses `float`, the two can disagree for cameras near the threshold, so a scene accepted today could be rejected after S2 is removed, or the reverse. Fitted `rt_gen` cameras and the known scenes are far from the threshold.

**If triggered:** match the stub, or state the precision in §2. `SceneIo.ChecksNearParallelCameraUp` covers both sides of the threshold.

### W3: specular materials in Core builds

§2 accepts directional lights only in builds with E2, but says nothing about `"specular": true` (`MAT_SPECULAR`). The loader accepts specular materials, and Core shading ignores `ks`, so a scene written for E2 renders as Lambert on a Core build without a warning.

**If triggered:** gate `MAT_SPECULAR` the same way as directional lights, or state that Core ignores it.
