/// @file bvh.cpp
/// @brief Serial bounds stage and deterministic median BVH builder.
#include "bvh/bvh.hpp"

#include <algorithm>
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

AABB empty_box() {
    const float inf = std::numeric_limits<float>::infinity();
    return {{inf, inf, inf}, {-inf, -inf, -inf}};
}

AABB sphere_bounds(const Sphere& s) {
    const vec3 r{s.radius, s.radius, s.radius};
    return {s.center - r, s.center + r};
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
    if (params.method != BvhMethod::Median) throw Error("bvh: only Median is implemented");
    if (params.max_leaf_size == 0) throw Error("bvh: max_leaf_size must be positive");
    if (params.threads <= 0) throw Error("bvh: threads must be positive");

    Bvh result;
    const auto bounds_start = std::chrono::steady_clock::now();
    std::vector<Prim> prims;
    prims.reserve(static_cast<size_t>(scene.num_spheres) + scene.num_triangles);
    for (uint32_t i = 0; i < scene.num_spheres; ++i) {
        const AABB b = sphere_bounds(scene.spheres[i]);
        prims.push_back({i, b, b.centroid()});
    }
    for (uint32_t i = 0; i < scene.num_triangles; ++i) {
        const AABB b = triangle_bounds(scene.triangles[i]);
        prims.push_back({PRIM_TRIANGLE_BIT | i, b, b.centroid()});
    }
    const auto bounds_end = std::chrono::steady_clock::now();
    timings.bounds = std::chrono::duration<double, std::milli>(bounds_end - bounds_start).count();

    if (prims.empty()) return result;
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

bool validate_bvh(const Bvh& bvh, const Scene& scene, std::string* error, uint32_t max_leaf_size) {
    if (max_leaf_size == 0) return fail(error, "bvh: max_leaf_size must be positive");
    const uint32_t primitive_count = scene.num_spheres + scene.num_triangles;
    if (primitive_count == 0) {
        if (!bvh.nodes.empty() || !bvh.prim_refs.empty())
            return fail(error, "bvh: empty scene must have empty storage");
        return true;
    }
    if (bvh.nodes.empty()) return fail(error, "bvh: nonempty scene has no root");
    if (bvh.prim_refs.size() != primitive_count) return fail(error, "bvh: primitive coverage count mismatch");
    std::vector<bool> seen(primitive_count, false);
    std::vector<bool> visited(bvh.nodes.size(), false);
    std::function<bool(uint32_t, uint32_t, AABB&)> visit = [&](uint32_t index, uint32_t depth, AABB& box) {
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
                const uint32_t ordinal = triangle ? scene.num_spheres + primitive_index : primitive_index;
                if (seen[ordinal]) return fail(error, "bvh: duplicate primitive");
                const AABB primitive_bounds = triangle ? triangle_bounds(scene.triangles[primitive_index])
                                                       : sphere_bounds(scene.spheres[primitive_index]);
                if (primitive_bounds.is_empty()) return fail(error, "bvh: primitive has empty bounds");
                if (!contains(node.bounds, primitive_bounds)) return fail(error, "bvh: primitive escapes leaf bounds");
                seen[ordinal] = true;
            }
            return true;
        }
        if (node.left_or_first >= bvh.nodes.size() - 1)
            return fail(error, "bvh: sibling index out of range");
        AABB left{}, right{};
        if (!visit(node.left_or_first, depth + 1, left) || !visit(node.left_or_first + 1, depth + 1, right)) return false;
        if (!contains(node.bounds, left) || !contains(node.bounds, right)) return fail(error, "bvh: child escapes parent bounds");
        return true;
    };
    AABB root{};
    if (!visit(0, 0, root)) return false;
    if (std::find(visited.begin(), visited.end(), false) != visited.end())
        return fail(error, "bvh: unreachable node");
    if (std::find(seen.begin(), seen.end(), false) != seen.end()) return fail(error, "bvh: primitive missing");
    return true;
}

}  // namespace rt
