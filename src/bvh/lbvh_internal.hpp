/// @file lbvh_internal.hpp
/// @brief Host-only temporary LBVH data and depth recovery; not a public interface.
#pragma once

#include <vector>

#include "bvh/bvh_types.hpp"

namespace rt {
namespace detail {

struct BuildPrim {
    PrimRef ref;
    AABB bounds;
    vec3 centroid;
};

/// Temporary preorder binary tree; each subtree occupies 2 * primitive_count - 1 slots.
struct LbvhNode {
    AABB bounds{};
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t first = 0;
    uint32_t last = 0;
    bool leaf = false;
    uint32_t height = 0;  ///< Edge height: a single leaf has height zero.
};

/// Rebuilds only depth-32 subtrees whose edge height exceeds 32; returns rebuild count.
/// Exposed internally so synthetic over-deep trees can test the otherwise unreachable
/// recovery path of the 30-bit Morton/index hierarchy at practical scene sizes.
uint32_t recover_lbvh_depth(const std::vector<BuildPrim>& prims, std::vector<uint32_t>& order,
                            std::vector<LbvhNode>& nodes);

}  // namespace detail
}  // namespace rt
