/// @file bvh.cpp
/// @brief Serial bounds stage and deterministic median BVH builder.
#include "bvh/bvh.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

#include "core/error.hpp"

namespace rt {
namespace {

struct Prim {
    PrimRef ref;
    AABB bounds;
    vec3 centroid;
};

struct LbvhNode {
    AABB bounds{};
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t first = 0;
    uint32_t last = 0;
    bool leaf = false;
};

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

void build_lbvh_tree(const std::vector<Prim>& prims, const std::vector<uint32_t>& order,
                     const std::vector<uint32_t>& keys, std::vector<LbvhNode>& nodes,
                     uint32_t node_index, uint32_t first, uint32_t last, bool parallel) {
    LbvhNode& node = nodes[node_index];
    node.first = first;
    node.last = last;
    if (first == last) {
        node.leaf = true;
        node.bounds = prims[order[first]].bounds;
        return;
    }

    const uint32_t split = lbvh_split(order, keys, first, last);
    const uint32_t left_count = 2 * (split - first + 1) - 1;
    node.left = node_index + 1;
    node.right = node_index + 1 + left_count;
    if (parallel && last - first > 1024) {
#pragma omp task shared(prims, order, keys, nodes) firstprivate(parallel, first, split)
        build_lbvh_tree(prims, order, keys, nodes, node.left, first, split, parallel);
#pragma omp task shared(prims, order, keys, nodes) firstprivate(parallel, split, last)
        build_lbvh_tree(prims, order, keys, nodes, node.right, split + 1, last, parallel);
#pragma omp taskwait
    } else {
        build_lbvh_tree(prims, order, keys, nodes, node.left, first, split, parallel);
        build_lbvh_tree(prims, order, keys, nodes, node.right, split + 1, last, parallel);
    }
    node.bounds = empty_box();
    node.bounds.grow(nodes[node.left].bounds);
    node.bounds.grow(nodes[node.right].bounds);
}

uint32_t collapsed_node_count(const std::vector<LbvhNode>& nodes, uint32_t index,
                              uint32_t max_leaf_size) {
    const LbvhNode& node = nodes[index];
    if (node.last - node.first + 1 <= max_leaf_size) return 1;
    return 1 + collapsed_node_count(nodes, node.left, max_leaf_size) +
           collapsed_node_count(nodes, node.right, max_leaf_size);
}

uint32_t tree_height(const std::vector<LbvhNode>& nodes, uint32_t index) {
    const LbvhNode& node = nodes[index];
    if (node.leaf) return 1;
    return 1 + std::max(tree_height(nodes, node.left), tree_height(nodes, node.right));
}

void emit_lbvh(const std::vector<Prim>& prims, const std::vector<uint32_t>& order,
               const std::vector<LbvhNode>& nodes, uint32_t index, uint32_t output_index,
               uint32_t& next_output, uint32_t max_leaf_size, Bvh& result, uint32_t depth) {
    const LbvhNode& node = nodes[index];
    result.stats.depth = std::max(result.stats.depth, depth);
    if (node.last - node.first + 1 <= max_leaf_size) {
        const uint32_t first_ref = static_cast<uint32_t>(result.prim_refs.size());
        for (uint32_t i = node.first; i <= node.last; ++i)
            result.prim_refs.push_back(prims[order[i]].ref);
        result.nodes[output_index] = {node.bounds, first_ref, node.last - node.first + 1};
        ++result.stats.leaves;
        return;
    }

    const uint32_t left_output = next_output++;
    const uint32_t right_output = next_output++;
    result.nodes[output_index] = {node.bounds, left_output, 0};
    emit_lbvh(prims, order, nodes, node.left, left_output, next_output, max_leaf_size, result,
              depth + 1);
    emit_lbvh(prims, order, nodes, node.right, right_output, next_output, max_leaf_size, result,
              depth + 1);
}

double sah_cost(const Bvh& bvh) {
    if (bvh.nodes.empty()) return 0.0;

    const float root_area = bvh.nodes[0].bounds.surface_area();
    if (!(root_area > 0.0f) || !std::isfinite(root_area)) return 0.0;

    // The diagnostic is the usual normalized leaf SAH: each primitive in a leaf pays
    // the leaf surface-area probability relative to the root.
    double cost = 0.0;
    for (const BvhNode& node : bvh.nodes) {
        if (node.count != 0) {
            cost += static_cast<double>(node.count) *
                    static_cast<double>(node.bounds.surface_area()) / root_area;
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
#pragma omp parallel for num_threads(params.threads) schedule(static)
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
        const bool parallel = params.threads > 1;
        if (parallel) {
#pragma omp parallel num_threads(params.threads)
            {
#pragma omp single
                build_lbvh_tree(prims, order, keys, temp, 0, 0,
                                static_cast<uint32_t>(order.size() - 1), true);
            }
        } else {
            build_lbvh_tree(prims, order, keys, temp, 0, 0, static_cast<uint32_t>(order.size() - 1),
                            false);
        }
        const auto build_end = std::chrono::steady_clock::now();
        timings.build = std::chrono::duration<double, std::milli>(build_end - build_start).count();

        // A pathological Morton hierarchy is recovered with the already validated median
        // builder before layout, keeping the fixed traversal stack safe.
        if (tree_height(temp, 0) > RT_FALLBACK_DEPTH) {
            const auto layout_start = std::chrono::steady_clock::now();
            std::vector<uint32_t> median_order(prims.size());
            std::iota(median_order.begin(), median_order.end(), 0u);
            result.nodes.push_back({empty_box(), 0, 0});
            BvhParams median_params = params;
            median_params.method = BvhMethod::Median;
            Builder builder{prims, median_params, result};
            builder.build(median_order, 0, 0, static_cast<uint32_t>(median_order.size()), 0);
            result.stats.nodes = static_cast<uint32_t>(result.nodes.size());
            result.stats.depth = builder.max_depth;
            result.stats.fallbacks = 1;
            result.stats.sah_cost = sah_cost(result);
            const auto layout_end = std::chrono::steady_clock::now();
            timings.layout =
                std::chrono::duration<double, std::milli>(layout_end - layout_start).count();
            return result;
        }

        const auto layout_start = std::chrono::steady_clock::now();
        const uint32_t output_count = collapsed_node_count(temp, 0, params.max_leaf_size);
        result.nodes.resize(output_count);
        uint32_t next_output = 1;
        emit_lbvh(prims, order, temp, 0, 0, next_output, params.max_leaf_size, result, 0);
        result.stats.nodes = output_count;
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
