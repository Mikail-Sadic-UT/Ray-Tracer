/// @file lbvh_internal.hpp
/// @brief Host-only temporary LBVH data and depth recovery; not a public interface.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "bvh/bvh.hpp"
#include "bvh/bvh_types.hpp"

namespace rt {
namespace detail {

struct BuildPrim {
    PrimRef ref;
    AABB bounds;
    vec3 centroid;
};

/// Temporary binary tree with explicit child indices; subtrees may be noncontiguous.
struct LbvhNode {
    AABB bounds{};
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t first = 0;
    uint32_t last = 0;
    bool leaf = false;
    uint32_t height = 0;  ///< Edge height: a single leaf has height zero.
};

/// Output size of a collapsed subtree: a node with at most `max_leaf_size` primitives is one
/// leaf. `nodes` counts output nodes, `depth` is the output edge height.
struct LayoutInfo {
    uint32_t nodes = 1;
    uint32_t leaves = 1;
    uint32_t depth = 0;
};

/// Optional per-stage LBVH timing on stderr, enabled by `RT_BVH_STAGES=1`. Diagnostic only:
/// the results schema has no slot for builder sub-stages (#68).
struct StageClock {
    bool enabled = false;
    std::chrono::steady_clock::time_point last{};
    char text[512] = {};
    int used = 0;

    StageClock() {
        const char* value = std::getenv("RT_BVH_STAGES");
        enabled = value != nullptr && value[0] != '\0' && value[0] != '0';
        last = std::chrono::steady_clock::now();
    }
    void mark(const char* name) {
        if (!enabled) return;
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        if (used < static_cast<int>(sizeof(text)))
            used += std::snprintf(text + used, sizeof(text) - used, " %s=%.2f", name, ms);
    }
    /// @p threads is the team size the stages used; below 4096 primitives it is 1 whatever
    /// B was requested, so both are printed.
    void report(int threads, int requested) const {
        if (enabled)
            std::fprintf(stderr, "bvh stages (threads=%d, B=%d):%s\n", threads, requested, text);
    }
};

/// Rebuilds only depth-32 subtrees whose edge height exceeds 32; returns rebuild count.
/// `heights_ready` requires valid edge heights from the bottom-up refit.
/// Synthetic trees may omit heights and let recovery compute them.
uint32_t recover_lbvh_depth(const BuildPrim* prims, uint32_t* order, LbvhNode* nodes,
                            size_t node_count, bool heights_ready = false);

/// Vector form of recover_lbvh_depth for tests.
inline uint32_t recover_lbvh_depth(const std::vector<BuildPrim>& prims,
                                   std::vector<uint32_t>& order, std::vector<LbvhNode>& nodes,
                                   bool heights_ready = false) {
    return recover_lbvh_depth(prims.data(), order.data(), nodes.data(), nodes.size(),
                              heights_ready);
}

/// Layout stage of the LBVH build: depth recovery, layout counts, node emit, prim_refs and
/// stats. `nodes` and `counts` have `node_count = 2N - 1` entries. When `counts_ready`,
/// `counts` holds the refit's counts for the tree as built; they are recomputed when
/// `counts_ready` is false or when recovery rebuilt a subtree. Exposed so tests can reach the
/// recovery path, which real Morton trees cannot.
void layout_lbvh(const BuildPrim* prims, uint32_t* order, LbvhNode* nodes, size_t node_count,
                 LayoutInfo* counts, bool heights_ready, bool counts_ready, uint32_t max_leaf_size,
                 int threads, Bvh& result, StageClock* clock = nullptr);

}  // namespace detail
}  // namespace rt
