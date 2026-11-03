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
| W3 | §2 Valid input | Core builds accept specular materials but reject directional lights | E2 (Blinn-Phong) starts | Po, Arek |
| W4 | §0 Names and files | `.cu` sources are listed only for `src/gpu/`, `src/optix/`, `apps/hello_cuda/`, but CUDA-only test targets in `tests/` (under `if(RT_ENABLE_CUDA)`) also need them. `tests/test_trace_device.cu` is the first | Next edit of §0, or a second CUDA test target | Mikail |

### W1: `Light::pad` default initializer

The default member initializer makes `Light` not trivially default-constructible. nvcc 13.3 accepts `__shared__ rt::Light tile[32];` with warning #20054-D (dynamic initialization is not supported for `__shared__`) and ignores the initializer there, so `pad` isn't guaranteed to be zero in shared memory.

No current impact. The megakernel reads lights from global memory, uploads copy the host bytes (where `pad` is zero), and `pad` is never hashed.

**If triggered:** drop `= 0.0f`, add `static_assert(std::is_trivial<Light>::value)`, and construct lights with `Light{}` or explicit braces. `scene_io` already rejects a non-zero `pad`, so an uninitialized one fails loudly at load. This is a type change: interface PR, Po and Mikail approve.

### W3: specular materials in Core builds

§2 accepts directional lights only in builds with E2, but says nothing about `"specular": true` (`MAT_SPECULAR`). The loader accepts specular materials, and Core shading ignores `ks`, so a scene written for E2 renders as Lambert on a Core build without a warning.

**If triggered:** gate `MAT_SPECULAR` the same way as directional lights, or state that Core ignores it.

### W4: `.cu` sources in `tests/`

INTERFACES §0 "Names and files" lists `.cu` files only in `src/gpu/`, `src/optix/`, and `apps/hello_cuda/`. The same section allows tests that need a GPU in targets that only build with `RT_ENABLE_CUDA`, and BUILD.md "Adding tests" says to wrap them in `if(RT_ENABLE_CUDA)`. Such a target needs a `.cu` source, so the list is incomplete rather than a ban. `tests/test_trace_device.cu` (target `test_trace_device`) is the first one.

**If triggered:** add `tests/` (CUDA-only targets under `if(RT_ENABLE_CUDA)`) to the `.cu` row. This is a clarification and needs only normal review.
