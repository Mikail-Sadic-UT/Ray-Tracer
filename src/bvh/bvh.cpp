/// @file bvh.cpp
/// @brief Deterministic median and parallel LBVH construction and validation.
#include "bvh/bvh.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

#include "bvh/lbvh_internal.hpp"
#include "core/error.hpp"

namespace rt {
namespace {

using Prim = detail::BuildPrim;
using detail::LbvhNode;

AABB empty_box() {
    const float inf = std::numeric_limits<float>::infinity();
    return {{inf, inf, inf}, {-inf, -inf, -inf}};
}

/// Rounds `center ± radius` outward by one ulp, so rounding never clips the sphere.
AABB sphere_bounds(const Sphere& s) {
    const float inf = std::numeric_limits<float>::infinity();
    AABB b;
    for (int a = 0; a < 3; ++a) {
        b.lo[a] = std::nextafter(s.center[a] - s.radius, -inf);
        b.hi[a] = std::nextafter(s.center[a] + s.radius, inf);
    }
    return b;
}

AABB triangle_bounds(const Triangle& t) {
    AABB b{t.v0, t.v0};
    b.grow(t.v1);
    b.grow(t.v2);
    return b;
}

bool contains(const AABB& outer, const AABB& inner) {
    return outer.lo.x <= inner.lo.x && outer.lo.y <= inner.lo.y && outer.lo.z <= inner.lo.z &&
           outer.hi.x >= inner.hi.x && outer.hi.y >= inner.hi.y && outer.hi.z >= inner.hi.z;
}

uint32_t leading_zeroes(uint32_t value) {
    if (value == 0) return 32;
    uint32_t result = 0;
    for (uint32_t bit = 1u << 31; (value & bit) == 0; bit >>= 1) ++result;
    return result;
}

uint32_t expand_bits(uint32_t value) {
    value = (value * 0x00010001u) & 0xFF0000FFu;
    value = (value * 0x00000101u) & 0x0F00F00Fu;
    value = (value * 0x00000011u) & 0xC30C30C3u;
    value = (value * 0x00000005u) & 0x49249249u;
    return value;
}

uint32_t morton_code(vec3 point, const AABB& centroid_bounds) {
    const vec3 extent = centroid_bounds.extent();
    const auto quantize = [](float value, float lo, float size) {
        if (!(size > 0.0f)) return 0u;
        const float scaled = (value - lo) / size * 1024.0f;
        const float clamped = std::fmin(1023.0f, std::fmax(0.0f, std::floor(scaled)));
        return static_cast<uint32_t>(clamped);
    };
    const uint32_t x = quantize(point.x, centroid_bounds.lo.x, extent.x);
    const uint32_t y = quantize(point.y, centroid_bounds.lo.y, extent.y);
    const uint32_t z = quantize(point.z, centroid_bounds.lo.z, extent.z);
    return (expand_bits(x) << 2) | (expand_bits(y) << 1) | expand_bits(z);
}

void stable_radix_sort(std::vector<uint32_t>& order, const std::vector<uint32_t>& keys) {
    std::vector<uint32_t> scratch(order.size());
    for (uint32_t shift = 0; shift < 30; shift += 8) {
        std::array<uint32_t, 256> counts{};
        for (const uint32_t index : order) ++counts[(keys[index] >> shift) & 0xffu];
        uint32_t offset = 0;
        for (uint32_t& count : counts) {
            const uint32_t next = offset + count;
            count = offset;
            offset = next;
        }
        for (const uint32_t index : order)
            scratch[counts[(keys[index] >> shift) & 0xffu]++] = index;
        order.swap(scratch);
    }
}

int common_prefix(const std::vector<uint32_t>& order, const std::vector<uint32_t>& keys,
                  uint32_t first, uint32_t second) {
    const uint32_t a = keys[order[first]];
    const uint32_t b = keys[order[second]];
    if (a != b) return static_cast<int>(leading_zeroes(a ^ b));
    return 32 + static_cast<int>(leading_zeroes(first ^ second));
}

uint32_t lbvh_split(const std::vector<uint32_t>& order, const std::vector<uint32_t>& keys,
                    uint32_t first, uint32_t last) {
    const int prefix = common_prefix(order, keys, first, last);
    uint32_t split = first;
    uint32_t step = last - first;
    while (step > 1) {
        step = (step + 1) / 2;
        const uint32_t candidate = split + step;
        if (candidate < last && common_prefix(order, keys, first, candidate) > prefix)
            split = candidate;
    }
    return split;
}

/// Builds each Karras internal node independently, then refits from the leaves.
void build_lbvh_tree(const std::vector<Prim>& prims, const std::vector<uint32_t>& order,
                     const std::vector<uint32_t>& keys, std::vector<LbvhNode>& nodes, int threads) {
    const int64_t count = static_cast<int64_t>(order.size());
    const uint32_t leaf_base = static_cast<uint32_t>(count - 1);
    std::vector<uint32_t> parents(nodes.size(), 0);
    std::vector<std::atomic<uint32_t>> visits(leaf_base);
    for (auto& visit : visits) visit.store(0, std::memory_order_relaxed);
    const auto prefix = [&](int64_t i, int64_t j) {
        return j < 0 || j >= count ? -1 : common_prefix(order, keys, i, j);
    };
#pragma omp parallel for num_threads(threads) schedule(static)
    for (int64_t i = 0; i < count - 1; ++i) {
        const int64_t direction = prefix(i, i + 1) > prefix(i, i - 1) ? 1 : -1;
        const int minimum = prefix(i, i - direction);
        int64_t upper = 2;
        while (prefix(i, i + upper * direction) > minimum) upper *= 2;
        int64_t length = 0;
        for (int64_t step = upper / 2; step >= 1; step /= 2)
            if (prefix(i, i + (length + step) * direction) > minimum) length += step;
        const int64_t end = i + length * direction;
        const uint32_t first = static_cast<uint32_t>(std::min(i, end));
        const uint32_t last = static_cast<uint32_t>(std::max(i, end));
        const uint32_t split = lbvh_split(order, keys, first, last);
        LbvhNode& node = nodes[i];
        node.first = first;
        node.last = last;
        node.left = split == first ? leaf_base + split : split;
        node.right = split + 1 == last ? leaf_base + split + 1 : split + 1;
        parents[node.left] = static_cast<uint32_t>(i);
        parents[node.right] = static_cast<uint32_t>(i);
    }
#pragma omp parallel for num_threads(threads) schedule(static)
    for (int64_t i = 0; i < count; ++i) {
        uint32_t index = leaf_base + static_cast<uint32_t>(i);
        LbvhNode& leaf = nodes[index];
        leaf.first = leaf.last = static_cast<uint32_t>(i);
        leaf.leaf = true;
        leaf.bounds = prims[order[i]].bounds;
        while (index != 0) {
            const uint32_t parent = parents[index];
            // Publish this child's bounds and acquire the first child's stores.
            if (visits[parent].fetch_add(1, std::memory_order_acq_rel) == 0) break;
            LbvhNode& node = nodes[parent];
            node.bounds = nodes[node.left].bounds;
            node.bounds.grow(nodes[node.right].bounds);
            node.height = 1 + std::max(nodes[node.left].height, nodes[node.right].height);
            index = parent;
        }
    }
}

struct LayoutInfo {
    uint32_t nodes = 1;
    uint32_t leaves = 1;
    uint32_t depth = 0;
};

/// Counts collapsed output subtrees independently of task scheduling.
void count_layout(const std::vector<LbvhNode>& nodes, uint32_t index, uint32_t max_leaf_size,
                  std::vector<LayoutInfo>& counts, bool parallel) {
    const LbvhNode& node = nodes[index];
    if (node.last - node.first + 1 <= max_leaf_size) return;
    if (parallel && node.last - node.first > 1024) {
#pragma omp task shared(nodes, counts) firstprivate(index, max_leaf_size, parallel)
        count_layout(nodes, nodes[index].left, max_leaf_size, counts, parallel);
#pragma omp task shared(nodes, counts) firstprivate(index, max_leaf_size, parallel)
        count_layout(nodes, nodes[index].right, max_leaf_size, counts, parallel);
#pragma omp taskwait
    } else {
        count_layout(nodes, node.left, max_leaf_size, counts, parallel);
        count_layout(nodes, node.right, max_leaf_size, counts, parallel);
    }
    const LayoutInfo left = counts[node.left], right = counts[node.right];
    counts[index] = {1 + left.nodes + right.nodes, left.leaves + right.leaves,
                     1 + std::max(left.depth, right.depth)};
}

uint32_t update_height(std::vector<LbvhNode>& nodes, uint32_t index) {
    LbvhNode& node = nodes[index];
    node.height =
        node.leaf ? 0
                  : 1 + std::max(update_height(nodes, node.left), update_height(nodes, node.right));
    return node.height;
}

/// Collects exactly the slots owned by a subtree, including noncontiguous Karras nodes.
void subtree_slots(const std::vector<LbvhNode>& nodes, uint32_t index,
                   std::vector<uint32_t>& slots) {
    slots.push_back(index);
    if (nodes[index].leaf) return;
    subtree_slots(nodes, nodes[index].left, slots);
    subtree_slots(nodes, nodes[index].right, slots);
}

/// Reuses the recovered subtree's slots; no outside node is relocated.
void rebuild_median_subtree(const std::vector<Prim>& prims, std::vector<uint32_t>& order,
                            std::vector<LbvhNode>& nodes, uint32_t index, uint32_t first,
                            uint32_t last, const std::vector<uint32_t>& slots, uint32_t offset) {
    LbvhNode& node = nodes[index];
    node = {};
    node.first = first;
    node.last = last;
    node.bounds = empty_box();
    AABB centroids = empty_box();
    for (uint32_t i = first; i <= last; ++i) {
        node.bounds.grow(prims[order[i]].bounds);
        centroids.grow(prims[order[i]].centroid);
    }
    if (first == last) {
        node.leaf = true;
        return;
    }
    const vec3 extent = centroids.extent();
    int axis = extent.y > extent.x ? 1 : 0;
    if (extent.z > extent[axis]) axis = 2;
    const uint32_t middle = first + (last - first + 1) / 2;
    std::nth_element(order.begin() + first, order.begin() + middle, order.begin() + last + 1,
                     [&](uint32_t a, uint32_t b) {
                         const float ca = prims[a].centroid[axis];
                         const float cb = prims[b].centroid[axis];
                         return ca < cb || (ca == cb && prims[a].ref < prims[b].ref);
                     });
    node.left = slots[offset + 1];
    node.right = slots[offset + 2 * (middle - first)];
    rebuild_median_subtree(prims, order, nodes, node.left, first, middle - 1, slots, offset + 1);
    rebuild_median_subtree(prims, order, nodes, node.right, middle, last, slots,
                           offset + 2 * (middle - first));
    node.height = 1 + std::max(nodes[node.left].height, nodes[node.right].height);
}

uint32_t recover_subtrees(const std::vector<Prim>& prims, std::vector<uint32_t>& order,
                          std::vector<LbvhNode>& nodes, uint32_t index, uint32_t depth) {
    LbvhNode& node = nodes[index];
    if (node.height <= RT_MAX_BVH_DEPTH - depth) return 0;
    if (depth == RT_FALLBACK_DEPTH) {
        std::vector<uint32_t> slots;
        subtree_slots(nodes, index, slots);
        rebuild_median_subtree(prims, order, nodes, index, node.first, node.last, slots, 0);
        return 1;
    }
    const uint32_t count = recover_subtrees(prims, order, nodes, node.left, depth + 1) +
                           recover_subtrees(prims, order, nodes, node.right, depth + 1);
    node.height = 1 + std::max(nodes[node.left].height, nodes[node.right].height);
    return count;
}

/// Emits into disjoint, predetermined output slots and sorted primitive ranges.
void emit_lbvh(const std::vector<Prim>& prims, const std::vector<uint32_t>& order,
               const std::vector<LbvhNode>& nodes, const std::vector<LayoutInfo>& counts,
               uint32_t index, uint32_t output_index, uint32_t descendants, uint32_t max_leaf_size,
               Bvh& result, bool parallel) {
    const LbvhNode& node = nodes[index];
    if (node.last - node.first + 1 <= max_leaf_size) {
        for (uint32_t i = node.first; i <= node.last; ++i)
            result.prim_refs[i] = prims[order[i]].ref;
        result.nodes[output_index] = {node.bounds, node.first, node.last - node.first + 1};
        return;
    }
    const uint32_t left = descendants;
    const uint32_t right_descendants = descendants + 1 + counts[node.left].nodes;
    result.nodes[output_index] = {node.bounds, left, 0};
    if (parallel && node.last - node.first > 1024) {
#pragma omp task shared(prims, order, nodes, counts, result) \
    firstprivate(index, left, max_leaf_size, parallel)
        emit_lbvh(prims, order, nodes, counts, nodes[index].left, left, left + 2, max_leaf_size,
                  result, parallel);
#pragma omp task shared(prims, order, nodes, counts, result) \
    firstprivate(index, left, right_descendants, max_leaf_size, parallel)
        emit_lbvh(prims, order, nodes, counts, nodes[index].right, left + 1, right_descendants,
                  max_leaf_size, result, parallel);
#pragma omp taskwait
    } else {
        emit_lbvh(prims, order, nodes, counts, node.left, left, left + 2, max_leaf_size, result,
                  parallel);
        emit_lbvh(prims, order, nodes, counts, node.right, left + 1, right_descendants,
                  max_leaf_size, result, parallel);
    }
}

double sah_cost(const Bvh& bvh) {
    if (bvh.nodes.empty()) return 0.0;

    // Host-only diagnostic: scene coordinates can make a float area overflow even
    // though the normalized cost is finite. Promote before subtracting/multiplying.
    const auto area = [](const AABB& bounds) {
        const double x = static_cast<double>(bounds.hi.x) - bounds.lo.x;
        const double y = static_cast<double>(bounds.hi.y) - bounds.lo.y;
        const double z = static_cast<double>(bounds.hi.z) - bounds.lo.z;
        return 2.0 * (x * y + y * z + z * x);
    };
    const double root_area = area(bvh.nodes[0].bounds);
    if (!(root_area > 0.0) || !std::isfinite(root_area)) return 0.0;

    // The diagnostic is the usual normalized leaf SAH: each primitive in a leaf pays
    // the leaf surface-area probability relative to the root.
    double cost = 0.0;
    for (const BvhNode& node : bvh.nodes) {
        if (node.count != 0) {
            cost += static_cast<double>(node.count) * area(node.bounds) / root_area;
        }
    }
    return cost;
}

struct Builder {
    const std::vector<Prim>& prims;
    const BvhParams& params;
    Bvh& out;
    uint32_t max_depth = 0;

    void build(std::vector<uint32_t>& order, uint32_t node_index, uint32_t begin, uint32_t end,
               uint32_t depth) {
        max_depth = std::max(max_depth, depth);
        AABB bounds = empty_box();
        for (uint32_t i = begin; i < end; ++i) bounds.grow(prims[order[i]].bounds);

        const uint32_t count = end - begin;
        if (count <= params.max_leaf_size) {
            const uint32_t first = static_cast<uint32_t>(out.prim_refs.size());
            for (uint32_t i = begin; i < end; ++i) out.prim_refs.push_back(prims[order[i]].ref);
            out.nodes[node_index] = {bounds, first, count};
            ++out.stats.leaves;
            return;
        }

        AABB centroid_bounds = empty_box();
        for (uint32_t i = begin; i < end; ++i) centroid_bounds.grow(prims[order[i]].centroid);
        const vec3 extent = centroid_bounds.extent();
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > extent[axis]) axis = 2;
        const uint32_t middle = begin + count / 2;
        std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
                         [&](uint32_t a, uint32_t b) {
                             const float ca = prims[a].centroid[axis];
                             const float cb = prims[b].centroid[axis];
                             return ca < cb || (ca == cb && prims[a].ref < prims[b].ref);
                         });
        const uint32_t left = static_cast<uint32_t>(out.nodes.size());
        out.nodes.push_back({empty_box(), 0, 0});
        out.nodes.push_back({empty_box(), 0, 0});
        build(order, left, begin, middle, depth + 1);
        build(order, left + 1, middle, end, depth + 1);
        out.nodes[node_index] = {bounds, left, 0};
    }
};

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

}  // namespace

uint32_t detail::recover_lbvh_depth(const std::vector<BuildPrim>& prims,
                                    std::vector<uint32_t>& order, std::vector<LbvhNode>& nodes,
                                    bool heights_ready) {
    if (nodes.empty()) return 0;
    if (!heights_ready) update_height(nodes, 0);
    return recover_subtrees(prims, order, nodes, 0, 0);
}

Bvh build_bvh(const Scene& scene, const BvhParams& params, Timings& timings) {
    if (params.method != BvhMethod::Median && params.method != BvhMethod::LBVH)
        throw Error("bvh: only Median and LBVH are implemented");
    if (params.max_leaf_size == 0) throw Error("bvh: max_leaf_size must be positive");
    if (params.threads <= 0) throw Error("bvh: threads must be positive");

    Bvh result;
    result.stats.max_leaf_size = params.max_leaf_size;
    timings.build = 0.0;
    timings.layout = 0.0;
    const auto bounds_start = std::chrono::steady_clock::now();
    const uint32_t primitive_count = scene.num_spheres + scene.num_triangles;
    std::vector<Prim> prims(primitive_count);
    const bool parallel_bounds =
        params.method == BvhMethod::LBVH && primitive_count >= 4096 && params.threads > 1;
#pragma omp parallel for num_threads(params.threads) schedule(static) if (parallel_bounds)
    for (int64_t i = 0; i < static_cast<int64_t>(primitive_count); ++i) {
        const uint32_t index = static_cast<uint32_t>(i);
        if (index < scene.num_spheres) {
            const AABB b = sphere_bounds(scene.spheres[index]);
            prims[index] = {index, b, b.centroid()};
        } else {
            const uint32_t triangle_index = index - scene.num_spheres;
            const AABB b = triangle_bounds(scene.triangles[triangle_index]);
            prims[index] = {PRIM_TRIANGLE_BIT | triangle_index, b, b.centroid()};
        }
    }
    const auto bounds_end = std::chrono::steady_clock::now();
    timings.bounds = std::chrono::duration<double, std::milli>(bounds_end - bounds_start).count();

    if (prims.empty()) return result;

    if (params.method == BvhMethod::LBVH) {
        const auto build_start = std::chrono::steady_clock::now();
        AABB centroid_bounds = empty_box();
        for (const Prim& prim : prims) centroid_bounds.grow(prim.centroid);

        std::vector<uint32_t> keys(prims.size());
        std::vector<uint32_t> order(prims.size());
        std::iota(order.begin(), order.end(), 0u);
        for (uint32_t i = 0; i < prims.size(); ++i)
            keys[i] = morton_code(prims[i].centroid, centroid_bounds);
        stable_radix_sort(order, keys);

        std::vector<LbvhNode> temp(2 * prims.size() - 1);
        build_lbvh_tree(prims, order, keys, temp, params.threads);
        const auto build_end = std::chrono::steady_clock::now();
        timings.build = std::chrono::duration<double, std::milli>(build_end - build_start).count();

        const auto layout_start = std::chrono::steady_clock::now();
        result.stats.fallbacks = detail::recover_lbvh_depth(prims, order, temp, true);
        std::vector<LayoutInfo> counts(temp.size());
        result.prim_refs.resize(prims.size());
#pragma omp parallel num_threads(params.threads)
        {
#pragma omp single
            {
                count_layout(temp, 0, params.max_leaf_size, counts, params.threads > 1);
                result.nodes.resize(counts[0].nodes);
                emit_lbvh(prims, order, temp, counts, 0, 0, 1, params.max_leaf_size, result,
                          params.threads > 1);
            }
        }
        result.stats.nodes = counts[0].nodes;
        result.stats.leaves = counts[0].leaves;
        result.stats.depth = counts[0].depth;
        result.stats.sah_cost = sah_cost(result);
        const auto layout_end = std::chrono::steady_clock::now();
        timings.layout =
            std::chrono::duration<double, std::milli>(layout_end - layout_start).count();
        return result;
    }

    std::vector<uint32_t> order(prims.size());
    std::iota(order.begin(), order.end(), 0u);
    const auto build_start = std::chrono::steady_clock::now();
    result.nodes.push_back({empty_box(), 0, 0});
    Builder builder{prims, params, result};
    builder.build(order, 0, 0, static_cast<uint32_t>(order.size()), 0);
    const auto build_end = std::chrono::steady_clock::now();
    timings.build = std::chrono::duration<double, std::milli>(build_end - build_start).count();
    result.stats.nodes = static_cast<uint32_t>(result.nodes.size());
    result.stats.depth = builder.max_depth;
    const auto layout_start = std::chrono::steady_clock::now();
    result.stats.sah_cost = sah_cost(result);
    const auto layout_end = std::chrono::steady_clock::now();
    timings.layout = std::chrono::duration<double, std::milli>(layout_end - layout_start).count();
    return result;
}

bool validate_bvh(const Bvh& bvh, const Scene& scene, std::string* error) {
    if (error) error->clear();
    const uint32_t max_leaf_size = bvh.stats.max_leaf_size;
    if (max_leaf_size == 0) return fail(error, "bvh: max_leaf_size must be positive");
    const uint32_t primitive_count = scene.num_spheres + scene.num_triangles;
    if (primitive_count == 0) {
        if (!bvh.nodes.empty() || !bvh.prim_refs.empty())
            return fail(error, "bvh: empty scene must have empty storage");
        return true;
    }
    if (bvh.nodes.empty()) return fail(error, "bvh: nonempty scene has no root");
    if (bvh.prim_refs.size() != primitive_count)
        return fail(error, "bvh: primitive coverage count mismatch");
    std::vector<bool> seen(primitive_count, false);
    std::vector<bool> visited(bvh.nodes.size(), false);
    std::function<bool(uint32_t, uint32_t, AABB&)> visit = [&](uint32_t index, uint32_t depth,
                                                               AABB& box) {
        if (index >= bvh.nodes.size()) return fail(error, "bvh: child index out of range");
        if (depth > RT_MAX_BVH_DEPTH) return fail(error, "bvh: depth exceeds 64");
        if (visited[index]) return fail(error, "bvh: node is referenced more than once");
        visited[index] = true;
        const BvhNode& node = bvh.nodes[index];
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(node.bounds.lo[axis]) || !std::isfinite(node.bounds.hi[axis]))
                return fail(error, "bvh: node has nonfinite bounds");
        }
        if (node.bounds.is_empty()) return fail(error, "bvh: node has an empty bounds");
        box = node.bounds;
        if (node.count != 0) {
            if (node.left_or_first > bvh.prim_refs.size() ||
                node.count > bvh.prim_refs.size() - node.left_or_first)
                return fail(error, "bvh: leaf range out of range");
            if (node.count > max_leaf_size) return fail(error, "bvh: leaf exceeds max_leaf_size");
            for (uint32_t i = 0; i < node.count; ++i) {
                const PrimRef ref = bvh.prim_refs[node.left_or_first + i];
                const bool triangle = (ref & PRIM_TRIANGLE_BIT) != 0;
                const uint32_t primitive_index = ref & ~PRIM_TRIANGLE_BIT;
                if ((!triangle && primitive_index >= scene.num_spheres) ||
                    (triangle && primitive_index >= scene.num_triangles))
                    return fail(error, "bvh: invalid primitive reference");
                const uint32_t ordinal =
                    triangle ? scene.num_spheres + primitive_index : primitive_index;
                if (seen[ordinal]) return fail(error, "bvh: duplicate primitive");
                const AABB primitive_bounds =
                    triangle ? triangle_bounds(scene.triangles[primitive_index])
                             : sphere_bounds(scene.spheres[primitive_index]);
                if (primitive_bounds.is_empty())
                    return fail(error, "bvh: primitive has empty bounds");
                if (!contains(node.bounds, primitive_bounds))
                    return fail(error, "bvh: primitive escapes leaf bounds");
                seen[ordinal] = true;
            }
            return true;
        }
        if (node.left_or_first >= bvh.nodes.size() - 1)
            return fail(error, "bvh: sibling index out of range");
        AABB left{}, right{};
        if (!visit(node.left_or_first, depth + 1, left) ||
            !visit(node.left_or_first + 1, depth + 1, right))
            return false;
        if (!contains(node.bounds, left) || !contains(node.bounds, right))
            return fail(error, "bvh: child escapes parent bounds");
        return true;
    };
    AABB root{};
    if (!visit(0, 0, root)) return false;
    if (std::find(visited.begin(), visited.end(), false) != visited.end())
        return fail(error, "bvh: unreachable node");
    if (std::find(seen.begin(), seen.end(), false) != seen.end())
        return fail(error, "bvh: primitive missing");
    return true;
}

}  // namespace rt
