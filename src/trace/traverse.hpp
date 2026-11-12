/// @file traverse.hpp
/// @brief Stack-based BVH traversal (closest hit and any hit) and the BvhQuery tracing query.
///
/// Follows the frozen rules of INTERFACES §5: inclusive node culling against the current best
/// distance, nearer child first (left on ties), a fixed 64-entry stack with at most one push
/// per level, and the same per-primitive step as brute force (trace/accept.hpp). The result
/// is therefore bit-identical to brute_closest / brute_any for any valid BVH.
#pragma once

#include <cassert>
#include <cstdint>

#include "bvh/bvh_types.hpp"
#include "core/aabb.hpp"
#include "core/ray.hpp"
#include "scene/scene.hpp"
#include "trace/accept.hpp"
#include "trace/hit.hpp"

namespace rt {
namespace detail {

/// Traversal stack entry: a node still to visit and the entry distance it was culled with.
struct TraversalEntry {
    uint32_t node;  ///< Node index.
    float t_enter;  ///< Box entry distance, clamped to `ray.tmin`.
};

/// Records a traversal stack overflow (INTERFACES §5, "Stack overflow").
///
/// Debug host builds assert. Otherwise, sets `*overflow = 1` if @p overflow is non-null:
/// `atomicOr` on the device, a relaxed atomic OR on the host, so many threads may share one
/// flag. The caller then stops traversing the ray.
RT_INLINE void report_overflow(uint32_t* overflow) {
#ifndef __CUDA_ARCH__
    assert(!"BVH traversal stack overflow: the BVH is deeper than RT_MAX_BVH_DEPTH");
#endif
    if (overflow == nullptr) return;
#ifdef __CUDA_ARCH__
    atomicOr(overflow, 1u);
#else
    __atomic_fetch_or(overflow, 1u, __ATOMIC_RELAXED);
#endif
}

}  // namespace detail

/// Closest hit through a BVH. Returns exactly what brute_closest() returns for the same scene.
///
/// A node is visited iff `intersect_aabb(bounds, ..., r.tmin, best.t)` is true, with
/// `best.t = r.tmax` before the first hit; the far bound is inclusive, so ties are never
/// culled. A popped node is skipped iff its stored entry distance now exceeds `best.t`, which
/// is the same test with the updated bound. At an interior node with both children hit, the
/// nearer one (left on equal `t_enter`) is visited first and the farther pushed.
///
/// @param bvh      BVH over @p s; `num_nodes == 0` is an empty scene and returns a miss
///                 without reading memory.
/// @param overflow Optional flag set to 1 if the stack overflows (see detail::report_overflow),
///                 after which the best hit found so far is returned. A BVH that passes
///                 validate_bvh() (depth <= RT_MAX_BVH_DEPTH) never overflows.
/// @return The accepted hit, or `{r.tmax, RT_MISS_PRIM, 0, 0}` on a miss.
/// @pre @p bvh references only primitives of @p s, and both live in memory the caller can read.
RT_INLINE Hit traverse_closest(const Ray& r, const Scene& s, const BvhView& bvh,
                               uint32_t* overflow = nullptr) {
    Hit best{r.tmax, RT_MISS_PRIM, 0.0f, 0.0f};
    bool found = false;
    if (bvh.num_nodes == 0) return best;
    const RayPrecomp pc = precompute(r);
    float t_root;
    if (!intersect_aabb(bvh.nodes[0].bounds, r.origin, pc, r.tmin, best.t, t_root)) return best;

    detail::TraversalEntry stack[RT_MAX_BVH_DEPTH];
    int sp = 0;
    uint32_t node = 0;
    for (;;) {
        const BvhNode& n = bvh.nodes[node];
        if (n.count == 0) {
            const uint32_t left = n.left_or_first;
            const uint32_t right = left + 1;
            float tl, tr;
            const bool hl =
                intersect_aabb(bvh.nodes[left].bounds, r.origin, pc, r.tmin, best.t, tl);
            const bool hr =
                intersect_aabb(bvh.nodes[right].bounds, r.origin, pc, r.tmin, best.t, tr);
            if (hl && hr) {
                const bool right_first = tr < tl;
                if (sp == RT_MAX_BVH_DEPTH) {
                    detail::report_overflow(overflow);
                    return best;
                }
                stack[sp++] = right_first ? detail::TraversalEntry{left, tl}
                                          : detail::TraversalEntry{right, tr};
                node = right_first ? right : left;
                continue;
            }
            if (hl) {
                node = left;
                continue;
            }
            if (hr) {
                node = right;
                continue;
            }
        } else {
            const uint32_t end = n.left_or_first + n.count;
            for (uint32_t k = n.left_or_first; k < end; ++k) {
                detail::closest_prim(r, s, bvh.prim_refs[k], best, found);
            }
        }
        // Pop the next node that can still hold a hit at or before best.t.
        for (;;) {
            if (sp == 0) return best;
            const detail::TraversalEntry e = stack[--sp];
            if (!(e.t_enter > best.t)) {
                node = e.node;
                break;
            }
        }
    }
}

/// True iff any primitive in the BVH has a hit with `r.tmin < t < r.tmax` (shadow rays).
/// Returns exactly what brute_any() returns for the same scene.
///
/// Nodes are culled against `[r.tmin, r.tmax]` and visited nearer child first, like
/// traverse_closest(); the result does not depend on the order.
/// @param overflow Optional overflow flag, as for traverse_closest(). On overflow, returns
///                 whether a hit was found before it.
/// @pre Same as traverse_closest().
RT_INLINE bool traverse_any(const Ray& r, const Scene& s, const BvhView& bvh,
                            uint32_t* overflow = nullptr) {
    if (bvh.num_nodes == 0) return false;
    const RayPrecomp pc = precompute(r);
    float t_root;
    if (!intersect_aabb(bvh.nodes[0].bounds, r.origin, pc, r.tmin, r.tmax, t_root)) return false;

    uint32_t stack[RT_MAX_BVH_DEPTH];
    int sp = 0;
    uint32_t node = 0;
    for (;;) {
        const BvhNode& n = bvh.nodes[node];
        if (n.count == 0) {
            const uint32_t left = n.left_or_first;
            const uint32_t right = left + 1;
            float tl, tr;
            const bool hl =
                intersect_aabb(bvh.nodes[left].bounds, r.origin, pc, r.tmin, r.tmax, tl);
            const bool hr =
                intersect_aabb(bvh.nodes[right].bounds, r.origin, pc, r.tmin, r.tmax, tr);
            if (hl && hr) {
                const bool right_first = tr < tl;
                if (sp == RT_MAX_BVH_DEPTH) {
                    detail::report_overflow(overflow);
                    return false;
                }
                stack[sp++] = right_first ? left : right;
                node = right_first ? right : left;
                continue;
            }
            if (hl) {
                node = left;
                continue;
            }
            if (hr) {
                node = right;
                continue;
            }
        } else {
            const uint32_t end = n.left_or_first + n.count;
            for (uint32_t k = n.left_or_first; k < end; ++k) {
                if (detail::any_prim(r, s, bvh.prim_refs[k])) return true;
            }
        }
        if (sp == 0) return false;
        node = stack[--sp];
    }
}

/// Tracing query over our BVH (CPU backend, megakernel).
struct BvhQuery {
    Scene s;      ///< Scene view; its arrays live in host or device memory to match the caller.
    BvhView bvh;  ///< BVH over @p s, in the same memory space.

    /// Closest hit for a primary ray.
    RT_INLINE Hit closest(const Ray& r) const { return traverse_closest(r, s, bvh); }

    /// Occlusion test for a shadow ray over `(r.tmin, r.tmax)`.
    RT_INLINE bool any(const Ray& r) const { return traverse_any(r, s, bvh); }
};

}  // namespace rt
