/// @file test_math.cpp
/// @brief Tests for src/core: vec3 math, AABB members, the ray-box slab test, and rt::Error.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "core/aabb.hpp"
#include "core/error.hpp"
#include "core/math.hpp"
#include "core/ray.hpp"

namespace {

/// Approximate float comparison: `|a - b| <= tolerance * (1 + |b|)`.
/// Relative for large values, absolute near zero.
bool near(float a, float b, float tolerance = 1.0e-5f) {
    return std::fabs(a - b) <= tolerance * (1.0f + std::fabs(b));
}

/// The box `[0, 1]^3`.
rt::AABB unit_box() {
    return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
}

/// Runs rt::intersect_aabb over the ray's own `[tmin, tmax]` range.
bool intersect(const rt::AABB& box, const rt::Ray& ray, float& enter) {
    return rt::intersect_aabb(box, ray.origin, rt::precompute(ray), ray.tmin, ray.tmax, enter);
}

/// Double-precision reference slab test.
///
/// Written independently of rt::intersect_aabb: divides in double and handles axis-parallel
/// rays with an explicit inside/outside check instead of infinities.
///
/// @return True on a hit; @p enter receives the entry distance clamped to `ray.tmin`.
bool intersect_double(const rt::AABB& box, const rt::Ray& ray, double& enter) {
    double entry = ray.tmin;
    double exit = ray.tmax;
    for (int axis = 0; axis < 3; ++axis) {
        const double origin = ray.origin[axis];
        const double direction = ray.dir[axis];
        const double lo = box.lo[axis];
        const double hi = box.hi[axis];
        if (direction == 0.0) {
            if (origin < lo || origin > hi) {
                return false;
            }
            continue;
        }
        const double a = (lo - origin) / direction;
        const double b = (hi - origin) / direction;
        entry = std::max(entry, std::min(a, b));
        exit = std::min(exit, std::max(a, b));
    }
    if (entry > exit) return false;
    enter = entry;
    return true;
}

/// Exact componentwise equality.
bool equal(rt::vec3 v, float x, float y, float z) {
    return v.x == x && v.y == y && v.z == z;
}

/// Approximate componentwise equality, using near().
bool near3(rt::vec3 v, float x, float y, float z) {
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
}

/// vec3 operators and geometry functions, checking every component.
///
/// Inputs are small integers with distinct values per component, so results are exact and a
/// swapped or mis-signed component changes the answer.
TEST(Math, Vec3) {
    const rt::vec3 a{1.0f, 2.0f, 3.0f};
    const rt::vec3 b{4.0f, -5.0f, 7.0f};

    // Arithmetic operators.
    EXPECT_TRUE(equal(a + b, 5.0f, -3.0f, 10.0f)) << "vec3 addition";
    EXPECT_TRUE(equal(a - b, -3.0f, 7.0f, -4.0f)) << "vec3 subtraction";
    EXPECT_TRUE(equal(-a, -1.0f, -2.0f, -3.0f)) << "vec3 unary negation";
    EXPECT_TRUE(equal(a * b, 4.0f, -10.0f, 21.0f)) << "componentwise multiplication";
    EXPECT_TRUE(equal(a * 2.0f, 2.0f, 4.0f, 6.0f)) << "vec3 * scalar";
    EXPECT_TRUE(equal(2.0f * a, 2.0f, 4.0f, 6.0f)) << "scalar * vec3";
    EXPECT_TRUE(equal(a / 2.0f, 0.5f, 1.0f, 1.5f)) << "vec3 / scalar";

    // Compound assignment modifies in place and returns the left operand.
    rt::vec3 acc = a;
    rt::vec3& sum_ref = (acc += b);
    EXPECT_TRUE(equal(acc, 5.0f, -3.0f, 10.0f) && &sum_ref == &acc) << "vec3 +=";
    rt::vec3& scale_ref = (acc *= 3.0f);
    EXPECT_TRUE(equal(acc, 15.0f, -9.0f, 30.0f) && &scale_ref == &acc) << "vec3 *=";

    // Dot and cross products. cross(a, b) is perpendicular to both inputs.
    EXPECT_TRUE(rt::dot(a, b) == 15.0f) << "dot product";
    const rt::vec3 c = rt::cross(a, b);
    EXPECT_TRUE(equal(c, 29.0f, 5.0f, -13.0f)) << "cross product";
    EXPECT_TRUE(rt::dot(c, a) == 0.0f && rt::dot(c, b) == 0.0f) << "cross product is perpendicular";
    EXPECT_TRUE(equal(rt::cross({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}), 0.0f, 0.0f, 1.0f))
        << "cross product is right-handed (x cross y = z)";

    // Length and normalization. normalize keeps the direction and gives unit length.
    EXPECT_TRUE(rt::length2(a) == 14.0f) << "squared length";
    EXPECT_TRUE(near(rt::length(a), std::sqrt(14.0f))) << "length";
    EXPECT_TRUE(near3(rt::normalize({3.0f, 0.0f, -4.0f}), 0.6f, 0.0f, -0.8f))
        << "normalize direction";
    EXPECT_TRUE(equal(rt::normalize({0.0f, -2.0f, 0.0f}), 0.0f, -1.0f, 0.0f))
        << "normalize axis vector";
    EXPECT_TRUE(near(rt::length(rt::normalize(a)), 1.0f)) << "normalize gives unit length";

    // Componentwise min/max and the largest component in each position.
    EXPECT_TRUE(equal(rt::vmin(a, b), 1.0f, -5.0f, 3.0f)) << "componentwise min";
    EXPECT_TRUE(equal(rt::vmax(a, b), 4.0f, 2.0f, 7.0f)) << "componentwise max";
    EXPECT_TRUE(rt::max_component({5.0f, 1.0f, 2.0f}) == 5.0f) << "maximum component in x";
    EXPECT_TRUE(rt::max_component({1.0f, 5.0f, 2.0f}) == 5.0f) << "maximum component in y";
    EXPECT_TRUE(rt::max_component({1.0f, 2.0f, 5.0f}) == 5.0f) << "maximum component in z";

    // Indexing: const reads and mutable writes for every component.
    EXPECT_TRUE(a[0] == 1.0f && a[1] == 2.0f && a[2] == 3.0f) << "vec3 const indexing";
    rt::vec3 indexed = a;
    indexed[0] = 7.0f;
    indexed[1] = 8.0f;
    indexed[2] = 9.0f;
    EXPECT_TRUE(equal(indexed, 7.0f, 8.0f, 9.0f)) << "vec3 mutable indexing";
}

/// AABB members: empty box, grow, centroid, extent, surface area, and is_empty.
TEST(Math, AabbMembers) {
    const float inf = std::numeric_limits<float>::infinity();
    rt::AABB box{{inf, inf, inf}, {-inf, -inf, -inf}};
    EXPECT_TRUE(box.is_empty()) << "empty AABB sentinel";
    box.grow(rt::vec3{0.0f, 1.0f, 2.0f});
    box.grow(rt::vec3{2.0f, 3.0f, 4.0f});
    EXPECT_TRUE(!box.is_empty()) << "grow point makes AABB non-empty";
    EXPECT_TRUE(box.centroid().x == 1.0f && box.extent().z == 2.0f) << "centroid and extent";
    EXPECT_TRUE(box.surface_area() == 24.0f) << "surface area";
    rt::AABB other{{-1.0f, 0.0f, 1.0f}, {0.0f, 2.0f, 3.0f}};
    box.grow(other);
    EXPECT_TRUE(box.lo.x == -1.0f && box.hi.y == 3.0f) << "grow AABB";

    // Extents 1 x 2 x 3: 2 * (1*2 + 2*3 + 3*1) = 22.
    const rt::AABB brick{{0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}};
    EXPECT_TRUE(brick.surface_area() == 22.0f) << "surface area with unequal extents";

    // Zero extent on an axis is still a valid, non-empty box; only lo > hi is empty.
    const rt::AABB flat{{0.0f, 1.0f, 0.0f}, {4.0f, 1.0f, 4.0f}};
    EXPECT_TRUE(!flat.is_empty()) << "flat box is not empty";
    const rt::AABB point{{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    EXPECT_TRUE(!point.is_empty()) << "single-point box is not empty";
    const rt::AABB inverted{{0.0f, 2.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    EXPECT_TRUE(inverted.is_empty()) << "lo > hi on one axis is empty";
}

/// Rays with a +0 or -0 direction component, for every axis and face of the unit box.
///
/// A ray lying on a face hits and enters at t = 0. A ray one float outside that face misses.
TEST(Math, ZeroDirectionCases) {
    const rt::AABB box = unit_box();
    const float zeros[] = {0.0f, -0.0f};
    for (int axis = 0; axis < 3; ++axis) {
        for (float zero : zeros) {
            for (int face = 0; face < 2; ++face) {
                rt::vec3 origin{0.5f, 0.5f, 0.5f};
                origin[axis] = face == 0 ? box.lo[axis] : box.hi[axis];
                rt::vec3 direction{1.0f, 0.25f, 0.5f};
                direction[axis] = zero;
                float enter = -1.0f;
                EXPECT_TRUE(intersect(box, {origin, 0.0f, direction, 10.0f}, enter))
                    << "signed-zero ray on either slab face hits";
                // The origin is inside the other two slabs, so entry is t_lo, not NaN.
                EXPECT_TRUE(enter == 0.0f) << "signed-zero face ray enters at t_lo, never NaN";

                origin[axis] =
                    face == 0
                        ? std::nextafter(box.lo[axis], -std::numeric_limits<float>::infinity())
                        : std::nextafter(box.hi[axis], std::numeric_limits<float>::infinity());
                enter = -1.0f;
                EXPECT_TRUE(!intersect(box, {origin, 0.0f, direction, 10.0f}, enter))
                    << "signed-zero ray just outside either face misses";
            }
        }
    }
}

/// Specific hit/miss cases: -0 direction on a face, NaN on one axis with a miss on the others,
/// rays starting inside or on the box, and boxes outside the ray's range.
TEST(Math, RegressionAndIntervals) {
    // Origin on the x = 0 face with a -0 x direction: hits.
    float enter = -1.0f;
    EXPECT_TRUE(
        intersect(unit_box(), {{0.0f, 0.5f, 0.5f}, 0.0f, {-0.0f, 1.0f, 0.0f}, 10.0f}, enter))
        << "documented negative-zero regression hits";

    // Zero y direction with the origin on a y face makes that axis NaN (tn for y = 0, tf for
    // y = 1). The x and z intervals do not overlap (x entered at t = 5, z exited at t = 4/3;
    // the second ray mirrors this), so both rays miss.
    enter = -1.0f;
    EXPECT_TRUE(
        !intersect(unit_box(), {{-5.0f, 0.0f, -1.0f}, 0.0f, {1.0f, 0.0f, 1.5f}, 100.0f}, enter))
        << "zero-y NaN does not overwrite t0 (miss)";
    enter = -1.0f;
    EXPECT_TRUE(
        !intersect(unit_box(), {{-1.0f, 1.0f, -5.0f}, 0.0f, {1.5f, 0.0f, 1.0f}, 100.0f}, enter))
        << "zero-y NaN does not overwrite t1 (miss)";

    // Ray starting inside the box enters at t_lo.
    enter = -1.0f;
    EXPECT_TRUE(
        intersect(unit_box(), {{0.5f, 0.5f, 0.5f}, 0.25f, {1.0f, 0.0f, 0.0f}, 10.0f}, enter) &&
        enter == 0.25f)
        << "inside ray enters at t_lo";

    // Origin on the exit face moving outward: entry and exit are both t = 0, which is a hit.
    enter = -1.0f;
    EXPECT_TRUE(
        intersect(unit_box(), {{1.0f, 0.5f, 0.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, 10.0f}, enter) &&
        enter == 0.0f)
        << "origin on exit face moving outward hits at t_lo";

    // Box behind the origin, and box beyond tmax: both miss.
    const rt::AABB behind{{1.0f, -1.0f, -1.0f}, {2.0f, 1.0f, 1.0f}};
    enter = -1.0f;
    EXPECT_TRUE(!intersect(behind, {{3.0f, 0.0f, 0.0f}, 0.0f, {1.0f, 0.0f, 0.0f}, 10.0f}, enter))
        << "box behind forward ray misses";
    enter = -1.0f;
    EXPECT_TRUE(
        !intersect(unit_box(), {{-2.0f, 0.5f, 0.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, 1.0f}, enter))
        << "box outside ray t interval misses";
}

/// Boundary cases with exactly representable inputs, so the expected t values are exact.
TEST(Math, DeterministicSlabBoundaries) {
    const rt::AABB box = unit_box();
    float enter = -1.0f;

    // Ray grazing the edge at (0, 1, 0.5): leaves the y slab as it enters the x slab, at t = 1.
    EXPECT_TRUE(intersect(box, {{-1.0f, 0.0f, 0.5f}, 0.0f, {1.0f, 1.0f, 0.0f}, 10.0f}, enter) &&
                enter == 1.0f)
        << "edge tangent is included";

    // Very small direction components: reaches the box at t = 1e8.
    enter = -1.0f;
    EXPECT_TRUE(
        intersect(box, {{-1.0f, -1.0f, 0.5f}, 0.0f, {1.0e-8f, 1.0e-8f, 0.0f}, 1.0e8f}, enter) &&
        enter == 1.0e8f)
        << "near-parallel ray intersects at a large parameter";

    // t_lo equal to the box entry distance is inclusive, so this hits at t = 1.
    enter = -1.0f;
    EXPECT_TRUE(intersect(box, {{-1.0f, 0.5f, 0.5f}, 1.0f, {1.0f, 0.0f, 0.0f}, 2.0f}, enter) &&
                enter == 1.0f)
        << "ray interval lower endpoint includes box entry";

    // The exact exit is t = 2. With t_lo one float above it, the ray still hits because the far
    // bound is widened by (1 + 2 * RT_GAMMA3).
    const float geometric_exit = 2.0f;
    const float expanded_exit = geometric_exit * (1.0f + 2.0f * rt::RT_GAMMA3);
    const float after_exit = std::nextafter(geometric_exit, std::numeric_limits<float>::infinity());
    EXPECT_TRUE(after_exit > geometric_exit && after_exit <= expanded_exit)
        << "test interval lies between geometric and conservative far bounds";
    enter = -1.0f;
    EXPECT_TRUE(
        intersect(box, {{-1.0f, 0.5f, 0.5f}, after_exit, {1.0f, 0.0f, 0.0f}, 3.0f}, enter) &&
        enter == after_exit)
        << "conservative far expansion overlaps interval just beyond geometric exit";
}

/// Random boxes and rays, compared against intersect_double().
///
/// Fixed seed. Directions with a zero component are redrawn. Most of these rays miss.
TEST(Math, RandomAgainstDouble) {
    std::mt19937 rng(0xA4E1u);
    std::uniform_real_distribution<float> position(-20.0f, 20.0f);
    std::uniform_real_distribution<float> size(0.01f, 5.0f);
    std::uniform_real_distribution<float> direction(-1.0f, 1.0f);
    for (int i = 0; i < 20000; ++i) {
        const rt::vec3 lo{position(rng), position(rng), position(rng)};
        const rt::vec3 extent{size(rng), size(rng), size(rng)};
        const rt::AABB box{lo, lo + extent};
        rt::vec3 dir{direction(rng), direction(rng), direction(rng)};
        while (dir.x == 0.0f || dir.y == 0.0f || dir.z == 0.0f) {
            dir = {direction(rng), direction(rng), direction(rng)};
        }
        const rt::Ray ray{{position(rng), position(rng), position(rng)}, 0.0f, dir, 100.0f};
        float float_enter = 0.0f;
        double double_enter = 0.0;
        const bool float_hit = intersect(box, ray, float_enter);
        const bool double_hit = intersect_double(box, ray, double_enter);
        EXPECT_TRUE(float_hit == double_hit) << "random slab hit agrees with double reference";
        if (float_hit && double_hit) {
            EXPECT_TRUE(near(float_enter, static_cast<float>(double_enter), 2.0e-5f))
                << "random slab entry agrees with double reference";
        }
    }
}

/// Random rays aimed at a point inside their box, compared against intersect_double().
///
/// Three in four rays are aimed, so most hit and the entry distance is compared often. The rest
/// use a random direction. Fails if fewer than half the rays hit.
TEST(Math, AimedAgainstDouble) {
    std::mt19937 rng(0xB5F2u);
    std::uniform_real_distribution<float> position(-20.0f, 20.0f);
    std::uniform_real_distribution<float> size(0.01f, 5.0f);
    std::uniform_real_distribution<float> direction(-1.0f, 1.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    int hits = 0;
    const int count = 20000;
    for (int i = 0; i < count; ++i) {
        const rt::vec3 lo{position(rng), position(rng), position(rng)};
        const rt::vec3 extent{size(rng), size(rng), size(rng)};
        const rt::AABB box{lo, lo + extent};
        const rt::vec3 origin{position(rng), position(rng), position(rng)};
        const rt::vec3 target =
            lo + rt::vec3{extent.x * unit(rng), extent.y * unit(rng), extent.z * unit(rng)};
        rt::vec3 dir = target - origin;
        if (i % 4 == 0) {
            dir = {direction(rng), direction(rng), direction(rng)};
        }
        const rt::Ray ray{origin, 0.0f, dir, 100.0f};
        float float_enter = 0.0f;
        double double_enter = 0.0;
        const bool float_hit = intersect(box, ray, float_enter);
        const bool double_hit = intersect_double(box, ray, double_enter);
        EXPECT_TRUE(float_hit == double_hit) << "aimed slab hit agrees with double reference";
        if (float_hit && double_hit) {
            ++hits;
            EXPECT_TRUE(near(float_enter, static_cast<float>(double_enter), 2.0e-5f))
                << "aimed slab entry agrees with double reference";
        }
    }
    EXPECT_TRUE(hits > count / 2) << "aimed rays mostly hit, so the entry comparison is exercised";
}

/// rt::Error can be caught as std::runtime_error and keeps its message.
TEST(Math, Error) {
    try {
        throw rt::Error("core: test message");
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "core: test message");
        return;
    }
    FAIL() << "rt::Error was not caught as std::runtime_error";
}

static_assert(std::is_base_of<std::runtime_error, rt::Error>::value,
              "rt::Error must derive from std::runtime_error (INTERFACES Sec. 0)");

// Layout checks: the core types stay POD with fixed sizes.
static_assert(std::is_standard_layout<rt::vec3>::value && std::is_trivial<rt::vec3>::value,
              "vec3 must remain a POD layout");
static_assert(std::is_standard_layout<rt::Ray>::value && std::is_trivial<rt::Ray>::value,
              "Ray must remain a POD layout");
static_assert(std::is_standard_layout<rt::AABB>::value && std::is_trivial<rt::AABB>::value,
              "AABB must remain a POD layout");
static_assert(sizeof(rt::vec3) == 12 && sizeof(rt::Ray) == 32 && sizeof(rt::AABB) == 24,
              "math type layouts changed");

}  // namespace
