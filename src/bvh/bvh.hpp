/// @file bvh.hpp
/// @brief Host-side BVH construction and validation.
#pragma once

#include <string>
#include <vector>

#include "bench/timings.hpp"
#include "bvh/bvh_types.hpp"
#include "scene/scene.hpp"

namespace rt {

/// Supported BVH construction methods.
enum class BvhMethod { Median, LBVH, SAH };

/// Parameters controlling BVH construction.
struct BvhParams {
    BvhMethod method = BvhMethod::Median;
    uint32_t max_leaf_size = 4;
    uint32_t sah_bins = 16;
    int threads = 1;
};

/// Build statistics reported with an owning BVH.
struct BvhStats {
    uint32_t nodes = 0;
    uint32_t leaves = 0;
    uint32_t depth = 0;
    uint32_t fallbacks = 0;
    double sah_cost = 0.0;
};

/// Owning host BVH. Node zero is the root when the scene is nonempty.
struct Bvh {
    std::vector<BvhNode> nodes;
    std::vector<PrimRef> prim_refs;
    BvhStats stats;
};

/// Builds a BVH from the canonical sphere-then-triangle primitive order.
/// @throws Error if parameters are invalid or an unimplemented method is selected.
Bvh build_bvh(const Scene& scene, const BvhParams& params, Timings& timings);

/// Checks structure, primitive coverage, bounds, leaf sizes, and depth.
/// @return True when the BVH satisfies the frozen layout contract.
bool validate_bvh(const Bvh& bvh, const Scene& scene, std::string* error = nullptr,
                  uint32_t max_leaf_size = 4);

}  // namespace rt
