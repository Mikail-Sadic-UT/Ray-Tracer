/// @file bvh_types.hpp
/// @brief Device-safe BVH node layout, primitive references, and the non-owning BVH view.
///
/// Declarations only, copied from INTERFACES §3 "Node layout (frozen)". The owning `Bvh`,
/// `BvhStats`, and `build_bvh` are host-only and belong in `bvh/bvh.hpp`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "core/aabb.hpp"

namespace rt {

/// BVH node (32 bytes, POD). Siblings are adjacent: an interior node's right child is
/// `left_or_first + 1`.
struct BvhNode {
    AABB bounds;             ///< Bounds of every primitive below this node.
    uint32_t left_or_first;  ///< Interior: left child index. Leaf: index of the first prim_ref.
    uint32_t count;          ///< 0 for an interior node; otherwise the leaf's prim_ref count.
};

static_assert(sizeof(BvhNode) == 32, "BvhNode must be 32 bytes");
static_assert(offsetof(BvhNode, left_or_first) == 24, "BvhNode::left_or_first must follow bounds");
static_assert(std::is_standard_layout<BvhNode>::value && std::is_trivially_copyable<BvhNode>::value,
              "BvhNode must have a standard layout and be trivially copyable");

/// Primitive reference. High bit set: triangle; clear: sphere. Low 31 bits: index into the
/// scene's triangle or sphere array.
using PrimRef = uint32_t;

/// High bit of a PrimRef, set for triangles.
constexpr uint32_t PRIM_TRIANGLE_BIT = 0x80000000u;

/// Maximum BVH depth; root = depth 0, leaves at depth <= 64. Also the traversal stack size.
constexpr int RT_MAX_BVH_DEPTH = 64;

/// Depth at which over-deep subtrees are rebuilt with the median method (depth cap).
constexpr int RT_FALLBACK_DEPTH = 32;

/// Non-owning view of a BVH, valid in host or device memory. `num_nodes == 0` is an empty scene.
struct BvhView {
    const BvhNode* nodes;      ///< Node array; `nodes[0]` is the root.
    const PrimRef* prim_refs;  ///< Primitive references indexed by leaf ranges.
    uint32_t num_nodes;        ///< Number of nodes in `nodes`.
};

}  // namespace rt
