/// @file bvh.cpp
/// @brief Deterministic median and parallel LBVH construction and validation.
#include "bvh/bvh.hpp"

#include <omp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>
#include <numeric>
#include <type_traits>
#include <vector>

#include "bvh/lbvh_internal.hpp"
#include "core/error.hpp"

namespace rt {
namespace {

/// Uninitialized scratch storage for trivially destructible build data. A `std::vector`
/// would value-initialize every element on one thread before the parallel loops write it;
/// at 1M primitives that serial fill was ~30 ms of the LBVH build (#68). Every element must
/// be written (placement new for non-trivial types) before it is read.
template <typename T>
class Scratch {
    static_assert(std::is_trivially_destructible<T>::value, "Scratch never runs destructors");

   public:
    explicit Scratch(size_t size)
        : data_(size ? static_cast<T*>(::operator new(size * sizeof(T))) : nullptr), size_(size) {}
    ~Scratch() { ::operator delete(data_); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    /// Frees the storage early, so the release is timed with the stage that used it.
    void reset() {
        ::operator delete(data_);
        data_ = nullptr;
        size_ = 0;
    }
    T* data() { return data_; }
    const T* data() const { return data_; }
    size_t size() const { return size_; }
    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

   private:
    T* data_;
    size_t size_;
};

/// Below this many primitives the LBVH stages run on one thread; team startup costs more.
constexpr int64_t kParallelMin = 4096;

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
    void report(int threads) const {
        if (enabled) std::fprintf(stderr, "bvh stages (B=%d):%s\n", threads, text);
    }
};

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
    return value == 0 ? 32u : static_cast<uint32_t>(__builtin_clz(value));
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

/// Sorts (Morton key, canonical index) pairs by key, stably, so equal keys keep canonical
/// order. Three 10-bit LSD passes cover the 30-bit keys. Each thread histograms one static
/// chunk, and scatter offsets are prefix sums in (digit, chunk) order, so the result is the
/// unique stable order for every thread count. Writes the sorted keys and the permutation.
void stable_radix_sort(const uint32_t* keys, size_t size, uint32_t* sorted_keys, uint32_t* order,
                       int threads) {
    constexpr int kBits = 10;
    constexpr uint32_t kBuckets = 1u << kBits;
    const int64_t n = static_cast<int64_t>(size);
    const int teams = n >= kParallelMin ? threads : 1;
    Scratch<uint64_t> a(size), b(size);
    std::vector<uint32_t> counts(static_cast<size_t>(teams) * kBuckets);
#pragma omp parallel num_threads(teams)
    {
        const int team = omp_get_thread_num();
        const int team_count = omp_get_num_threads();
        // Static chunks must not depend on the scheduler: compute them explicitly.
        const int64_t begin = n * team / team_count;
        const int64_t end = n * (team + 1) / team_count;
        for (int64_t i = begin; i < end; ++i)
            a[i] = static_cast<uint64_t>(keys[i]) << 32 | static_cast<uint32_t>(i);
        uint64_t* source = a.data();
        uint64_t* target = b.data();
        for (int shift = 32; shift < 32 + 30; shift += kBits) {
            uint32_t* mine = counts.data() + static_cast<size_t>(team) * kBuckets;
            std::fill(mine, mine + kBuckets, 0u);
            for (int64_t i = begin; i < end; ++i) ++mine[(source[i] >> shift) & (kBuckets - 1)];
#pragma omp barrier
#pragma omp single
            {
                uint32_t offset = 0;
                for (uint32_t digit = 0; digit < kBuckets; ++digit) {
                    for (int t = 0; t < team_count; ++t) {
                        uint32_t& count = counts[static_cast<size_t>(t) * kBuckets + digit];
                        const uint32_t next = offset + count;
                        count = offset;
                        offset = next;
                    }
                }
            }  // Implicit barrier publishes the offsets.
            for (int64_t i = begin; i < end; ++i)
                target[mine[(source[i] >> shift) & (kBuckets - 1)]++] = source[i];
#pragma omp barrier
            std::swap(source, target);
        }
        // Three passes leave the result in `b`.
        for (int64_t i = begin; i < end; ++i) {
            sorted_keys[i] = static_cast<uint32_t>(source[i] >> 32);
            order[i] = static_cast<uint32_t>(source[i]);
        }
    }
}

/// Karras prefix of sorted positions; equal keys fall back to the position bits.
int common_prefix(const uint32_t* sorted_keys, uint32_t first, uint32_t second) {
    const uint32_t a = sorted_keys[first];
    const uint32_t b = sorted_keys[second];
    if (a != b) return static_cast<int>(leading_zeroes(a ^ b));
    return 32 + static_cast<int>(leading_zeroes(first ^ second));
}

uint32_t lbvh_split(const uint32_t* sorted_keys, uint32_t first, uint32_t last) {
    const int prefix = common_prefix(sorted_keys, first, last);
    uint32_t split = first;
    uint32_t step = last - first;
    while (step > 1) {
        step = (step + 1) / 2;
        const uint32_t candidate = split + step;
        if (candidate < last && common_prefix(sorted_keys, first, candidate) > prefix)
            split = candidate;
    }
    return split;
}

/// Output size of a collapsed subtree: a node with at most `max_leaf_size` primitives is one
/// leaf. `nodes` counts output nodes, `depth` is the output edge height.
struct LayoutInfo {
    uint32_t nodes = 1;
    uint32_t leaves = 1;
    uint32_t depth = 0;
};

LayoutInfo combine_layout(const LbvhNode& node, uint32_t max_leaf_size, const LayoutInfo& left,
                          const LayoutInfo& right) {
    if (node.last - node.first + 1 <= max_leaf_size) return LayoutInfo{};
    return {1 + left.nodes + right.nodes, left.leaves + right.leaves,
            1 + std::max(left.depth, right.depth)};
}

/// Builds each Karras internal node independently, then refits from the leaves. The refit
/// also fills `counts` (the collapsed layout sizes) bottom-up, so layout needs no extra pass.
/// `nodes` and `counts` are uninitialized storage for `2 * count - 1` entries; every slot is
/// written here.
void build_lbvh_tree(const Prim* prims, const uint32_t* order, const uint32_t* sorted_keys,
                     int64_t count, uint32_t max_leaf_size, LbvhNode* nodes, LayoutInfo* counts,
                     int threads, StageClock& clock) {
    const uint32_t leaf_base = static_cast<uint32_t>(count - 1);
    const int teams = count >= kParallelMin ? threads : 1;
    // parents[0] (the root) is never read; every other slot is written once below.
    Scratch<uint32_t> parents(static_cast<size_t>(2 * count - 1));
    Scratch<std::atomic<uint32_t>> visits(static_cast<size_t>(leaf_base));
    const auto prefix = [&](int64_t i, int64_t j) {
        return j < 0 || j >= count
                   ? -1
                   : common_prefix(sorted_keys, static_cast<uint32_t>(i), static_cast<uint32_t>(j));
    };
#pragma omp parallel for num_threads(teams) schedule(static)
    for (int64_t i = 0; i < count - 1; ++i) {
        new (&visits[i]) std::atomic<uint32_t>(0);
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
        const uint32_t split = lbvh_split(sorted_keys, first, last);
        LbvhNode& node = *new (&nodes[i]) LbvhNode{};
        node.first = first;
        node.last = last;
        node.left = split == first ? leaf_base + split : split;
        node.right = split + 1 == last ? leaf_base + split + 1 : split + 1;
        parents[node.left] = static_cast<uint32_t>(i);
        parents[node.right] = static_cast<uint32_t>(i);
    }
    clock.mark("hierarchy");
#pragma omp parallel for num_threads(teams) schedule(static)
    for (int64_t i = 0; i < count; ++i) {
        uint32_t index = leaf_base + static_cast<uint32_t>(i);
        LbvhNode& leaf = *new (&nodes[index]) LbvhNode{};
        leaf.first = leaf.last = static_cast<uint32_t>(i);
        leaf.leaf = true;
        leaf.bounds = prims[order[i]].bounds;
        counts[index] = LayoutInfo{};
        while (index != 0) {
            const uint32_t parent = parents[index];
            // Publish this child's bounds and counts, and acquire the first child's stores.
            if (visits[parent].fetch_add(1, std::memory_order_acq_rel) == 0) break;
            LbvhNode& node = nodes[parent];
            node.bounds = nodes[node.left].bounds;
            node.bounds.grow(nodes[node.right].bounds);
            node.height = 1 + std::max(nodes[node.left].height, nodes[node.right].height);
            counts[parent] =
                combine_layout(node, max_leaf_size, counts[node.left], counts[node.right]);
            index = parent;
        }
    }
}

/// Subtrees with more primitives than `task_min` become OpenMP tasks. Output slots are
/// precomputed, so the cutoff affects speed only, never the bytes.
uint32_t layout_task_min(int64_t count, int threads) {
    if (threads <= 1 || count < kParallelMin) return std::numeric_limits<uint32_t>::max();
    // About 16 tasks per thread: enough to balance uneven subtrees, few enough that task
    // overhead stays small. The 1024 floor matches the original cutoff.
    return static_cast<uint32_t>(std::max<int64_t>(1024, count / (16 * threads)));
}

/// Recounts collapsed output subtrees from scratch. Used only after depth recovery rebuilt a
/// subtree, which invalidates the counts the refit produced.
void count_layout(const LbvhNode* nodes, uint32_t index, uint32_t max_leaf_size,
                  LayoutInfo* counts) {
    const LbvhNode& node = nodes[index];
    if (node.last - node.first + 1 <= max_leaf_size) {
        counts[index] = LayoutInfo{};
        return;
    }
    count_layout(nodes, node.left, max_leaf_size, counts);
    count_layout(nodes, node.right, max_leaf_size, counts);
    counts[index] = combine_layout(node, max_leaf_size, counts[node.left], counts[node.right]);
}

uint32_t update_height(LbvhNode* nodes, uint32_t index) {
    LbvhNode& node = nodes[index];
    node.height =
        node.leaf ? 0
                  : 1 + std::max(update_height(nodes, node.left), update_height(nodes, node.right));
    return node.height;
}

/// Collects exactly the slots owned by a subtree, including noncontiguous Karras nodes.
void subtree_slots(const LbvhNode* nodes, uint32_t index, std::vector<uint32_t>& slots) {
    slots.push_back(index);
    if (nodes[index].leaf) return;
    subtree_slots(nodes, nodes[index].left, slots);
    subtree_slots(nodes, nodes[index].right, slots);
}

/// Reuses the recovered subtree's slots; no outside node is relocated.
void rebuild_median_subtree(const Prim* prims, uint32_t* order, LbvhNode* nodes, uint32_t index,
                            uint32_t first, uint32_t last, const std::vector<uint32_t>& slots,
                            uint32_t offset) {
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
    std::nth_element(order + first, order + middle, order + last + 1, [&](uint32_t a, uint32_t b) {
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

uint32_t recover_subtrees(const Prim* prims, uint32_t* order, LbvhNode* nodes, uint32_t index,
                          uint32_t depth) {
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

/// Emits nodes into disjoint, predetermined output slots. Leaves keep their sorted primitive
/// ranges, so `prim_refs` is filled separately as a flat gather in sorted order.
void emit_lbvh(const LbvhNode* nodes, const LayoutInfo* counts, uint32_t index,
               uint32_t output_index, uint32_t descendants, uint32_t max_leaf_size,
               BvhNode* out_nodes, uint32_t task_min) {
    const LbvhNode& node = nodes[index];
    if (node.last - node.first + 1 <= max_leaf_size) {
        out_nodes[output_index] = {node.bounds, node.first, node.last - node.first + 1};
        return;
    }
    const uint32_t left = descendants;
    const uint32_t right_descendants = descendants + 1 + counts[node.left].nodes;
    out_nodes[output_index] = {node.bounds, left, 0};
    if (node.last - node.first > task_min) {
#pragma omp task firstprivate(nodes, counts, out_nodes, index, left, max_leaf_size, task_min)
        emit_lbvh(nodes, counts, nodes[index].left, left, left + 2, max_leaf_size, out_nodes,
                  task_min);
#pragma omp task firstprivate(nodes, counts, out_nodes, index, left, right_descendants, \
                                  max_leaf_size, task_min)
        emit_lbvh(nodes, counts, nodes[index].right, left + 1, right_descendants, max_leaf_size,
                  out_nodes, task_min);
#pragma omp taskwait
    } else {
        emit_lbvh(nodes, counts, node.left, left, left + 2, max_leaf_size, out_nodes, task_min);
        emit_lbvh(nodes, counts, node.right, left + 1, right_descendants, max_leaf_size, out_nodes,
                  task_min);
    }
}

double sah_cost(const Bvh& bvh, int threads = 1) {
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
    // the leaf surface-area probability relative to the root. Terms may be computed in
    // parallel, but they are summed serially in node order, so the value is the same
    // for every thread count. Interior nodes add +0.0, which leaves the sum unchanged.
    const int64_t n = static_cast<int64_t>(bvh.nodes.size());
    if (threads <= 1 || n < kParallelMin) {
        double cost = 0.0;
        for (const BvhNode& node : bvh.nodes) {
            if (node.count != 0) {
                cost += static_cast<double>(node.count) * area(node.bounds) / root_area;
            }
        }
        return cost;
    }
    Scratch<double> terms(static_cast<size_t>(n));
#pragma omp parallel for num_threads(threads) schedule(static)
    for (int64_t i = 0; i < n; ++i) {
        const BvhNode& node = bvh.nodes[i];
        terms[i] =
            node.count != 0 ? static_cast<double>(node.count) * area(node.bounds) / root_area : 0.0;
    }
    double cost = 0.0;
    for (int64_t i = 0; i < n; ++i) cost += terms[i];
    return cost;
}

struct Builder {
    const Prim* prims;
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

uint32_t detail::recover_lbvh_depth(const BuildPrim* prims, uint32_t* order, LbvhNode* nodes,
                                    size_t node_count, bool heights_ready) {
    if (node_count == 0) return 0;
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
    Scratch<Prim> prims(primitive_count);
    const int64_t count = static_cast<int64_t>(primitive_count);
    const bool parallel =
        params.method == BvhMethod::LBVH && count >= kParallelMin && params.threads > 1;
    const int teams = parallel ? params.threads : 1;
#pragma omp parallel for num_threads(teams) schedule(static)
    for (int64_t i = 0; i < count; ++i) {
        const uint32_t index = static_cast<uint32_t>(i);
        if (index < scene.num_spheres) {
            const AABB b = sphere_bounds(scene.spheres[index]);
            new (&prims[index]) Prim{index, b, b.centroid()};
        } else {
            const uint32_t triangle_index = index - scene.num_spheres;
            const AABB b = triangle_bounds(scene.triangles[triangle_index]);
            new (&prims[index]) Prim{PRIM_TRIANGLE_BIT | triangle_index, b, b.centroid()};
        }
    }
    const auto bounds_end = std::chrono::steady_clock::now();
    timings.bounds = std::chrono::duration<double, std::milli>(bounds_end - bounds_start).count();

    if (count == 0) return result;

    if (params.method == BvhMethod::LBVH) {
        const auto build_start = std::chrono::steady_clock::now();
        StageClock clock;
        // Min and max are exact, so the reduction order cannot change the box. The only
        // order-dependent case is the sign of a zero bound, and morton_code() maps both
        // signs to the same code.
        AABB centroid_bounds = empty_box();
#pragma omp parallel num_threads(teams)
        {
            AABB local = empty_box();
#pragma omp for schedule(static) nowait
            for (int64_t i = 0; i < count; ++i) local.grow(prims[i].centroid);
#pragma omp critical(rt_bvh_centroids)
            centroid_bounds.grow(local);
        }
        clock.mark("centroids");

        Scratch<uint32_t> keys(primitive_count);
#pragma omp parallel for num_threads(teams) schedule(static)
        for (int64_t i = 0; i < count; ++i)
            keys[i] = morton_code(prims[i].centroid, centroid_bounds);
        clock.mark("morton");
        // `keys` holds canonical-order codes; sorting gives the sorted codes and `order`.
        Scratch<uint32_t> sorted_keys(primitive_count);
        Scratch<uint32_t> order(primitive_count);
        stable_radix_sort(keys.data(), primitive_count, sorted_keys.data(), order.data(), teams);
        clock.mark("sort");

        const size_t node_count = 2 * static_cast<size_t>(count) - 1;
        Scratch<LbvhNode> temp(node_count);
        Scratch<LayoutInfo> counts(node_count);
        build_lbvh_tree(prims.data(), order.data(), sorted_keys.data(), count, params.max_leaf_size,
                        temp.data(), counts.data(), teams, clock);
        clock.mark("refit");
        const auto build_end = std::chrono::steady_clock::now();
        timings.build = std::chrono::duration<double, std::milli>(build_end - build_start).count();

        const auto layout_start = std::chrono::steady_clock::now();
        result.stats.fallbacks =
            detail::recover_lbvh_depth(prims.data(), order.data(), temp.data(), node_count, true);
        if (result.stats.fallbacks != 0)
            count_layout(temp.data(), 0, params.max_leaf_size, counts.data());
        clock.mark("recover");
        const uint32_t task_min = layout_task_min(count, teams);
        result.prim_refs.resize(primitive_count);
        PrimRef* refs = result.prim_refs.data();
#pragma omp parallel num_threads(teams)
        {
            // The node vector's zero fill is serial; one thread does it while the others
            // gather prim_refs. Leaves cover sorted ranges, so prim_refs is the sorted order
            // mapped to refs. Dynamic chunks let the resizing thread skip its share.
#pragma omp single nowait
            result.nodes.resize(counts[0].nodes);
#pragma omp for schedule(dynamic, 16384)
            for (int64_t i = 0; i < count; ++i) refs[i] = prims[order[i]].ref;
                // The loop's implicit barrier also orders the resize before emit.
#pragma omp single
            emit_lbvh(temp.data(), counts.data(), 0, 0, 1, params.max_leaf_size,
                      result.nodes.data(), task_min);
        }
        clock.mark("emit");
        result.stats.nodes = counts[0].nodes;
        result.stats.leaves = counts[0].leaves;
        result.stats.depth = counts[0].depth;
        result.stats.sah_cost = sah_cost(result, teams);
        clock.mark("sah");
        // Release the scratch here so freeing ~200 MB at 1M counts as layout, not as time
        // outside every component (#67).
        temp.reset();
        counts.reset();
        order.reset();
        sorted_keys.reset();
        keys.reset();
        prims.reset();
        clock.mark("free");
        const auto layout_end = std::chrono::steady_clock::now();
        timings.layout =
            std::chrono::duration<double, std::milli>(layout_end - layout_start).count();
        clock.report(params.threads);
        return result;
    }

    std::vector<uint32_t> order(prims.size());
    std::iota(order.begin(), order.end(), 0u);
    const auto build_start = std::chrono::steady_clock::now();
    result.nodes.push_back({empty_box(), 0, 0});
    Builder builder{prims.data(), params, result};
    builder.build(order, 0, 0, static_cast<uint32_t>(order.size()), 0);
    const auto build_end = std::chrono::steady_clock::now();
    timings.build = std::chrono::duration<double, std::milli>(build_end - build_start).count();
    result.stats.nodes = static_cast<uint32_t>(result.nodes.size());
    result.stats.depth = builder.max_depth;
    const auto layout_start = std::chrono::steady_clock::now();
    result.stats.sah_cost = sah_cost(result);
    prims.reset();
    std::vector<uint32_t>().swap(order);
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
