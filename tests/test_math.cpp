#include "core/aabb.hpp"
#include "core/math.hpp"
#include "core/ray.hpp"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <type_traits>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1.0e-5f) {
    return std::fabs(a - b) <= tolerance * (1.0f + std::fabs(b));
}

rt::AABB unit_box() { return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}; }

bool intersect(const rt::AABB& box, const rt::Ray& ray, float& enter) {
    return rt::intersect_aabb(box, ray.origin, rt::precompute(ray), ray.tmin, ray.tmax, enter);
}

bool intersect_double(const rt::AABB& box, const rt::Ray& ray, double& enter) {
    // Independent oracle: do not use RayPrecomp or float reciprocal products.
    // Divide in double, explicitly handle parallel slabs, then intersect each
    // axis interval with the caller's closed range.
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

void test_vec3() {
    const rt::vec3 a{1.0f, 2.0f, 3.0f};
    const rt::vec3 b{4.0f, 5.0f, 6.0f};
    check((a + b).z == 9.0f, "vec3 addition");
    check((a * b).y == 10.0f, "componentwise multiplication");
    check(rt::dot(a, b) == 32.0f, "dot product");
    check(rt::cross(a, b).x == -3.0f, "cross product");
    check(near(rt::length(a), std::sqrt(14.0f)), "length");
    check(near(rt::length2(a), 14.0f), "squared length");
    check(near(rt::length(rt::normalize(a)), 1.0f), "normalize");
    check(rt::vmin(a, b).x == 1.0f && rt::vmax(a, b).z == 6.0f, "componentwise min/max");
    check(rt::max_component(a) == 3.0f, "maximum component");
    rt::vec3 indexed = a;
    indexed[1] = 8.0f;
    check(indexed.y == 8.0f && indexed[2] == 3.0f, "vec3 indexing");
}

void test_aabb_members() {
    const float inf = std::numeric_limits<float>::infinity();
    rt::AABB box{{inf, inf, inf}, {-inf, -inf, -inf}};
    check(box.is_empty(), "empty AABB sentinel");
    box.grow(rt::vec3{0.0f, 1.0f, 2.0f});
    box.grow(rt::vec3{2.0f, 3.0f, 4.0f});
    check(!box.is_empty(), "grow point makes AABB non-empty");
    check(box.centroid().x == 1.0f && box.extent().z == 2.0f, "centroid and extent");
    check(box.surface_area() == 24.0f, "surface area");
    rt::AABB other{{-1.0f, 0.0f, 1.0f}, {0.0f, 2.0f, 3.0f}};
    box.grow(other);
    check(box.lo.x == -1.0f && box.hi.y == 3.0f, "grow AABB");
}

void test_zero_direction_cases() {
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
                check(intersect(box, {origin, 0.0f, direction, 10.0f}, enter),
                      "signed-zero ray on either slab face hits");

                origin[axis] = face == 0
                                   ? std::nextafter(box.lo[axis], -std::numeric_limits<float>::infinity())
                                   : std::nextafter(box.hi[axis], std::numeric_limits<float>::infinity());
                enter = -1.0f;
                check(!intersect(box, {origin, 0.0f, direction, 10.0f}, enter),
                      "signed-zero ray just outside either face misses");
            }
        }
    }
}

void test_regression_and_intervals() {
    float enter = -1.0f;
    check(intersect(unit_box(), {{0.0f, 0.5f, 0.5f}, 0.0f, {-0.0f, 1.0f, 0.0f}, 10.0f}, enter),
          "documented negative-zero regression hits");

    enter = -1.0f;
    check(intersect(unit_box(), {{0.5f, 0.5f, 0.5f}, 0.25f, {1.0f, 0.0f, 0.0f}, 10.0f}, enter) &&
              enter == 0.25f,
          "inside ray enters at t_lo");

    const rt::AABB behind{{1.0f, -1.0f, -1.0f}, {2.0f, 1.0f, 1.0f}};
    enter = -1.0f;
    check(!intersect(behind, {{3.0f, 0.0f, 0.0f}, 0.0f, {1.0f, 0.0f, 0.0f}, 10.0f}, enter),
          "box behind forward ray misses");
    enter = -1.0f;
    check(!intersect(unit_box(), {{-2.0f, 0.5f, 0.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, 1.0f}, enter),
          "box outside ray t interval misses");
}

void test_deterministic_slab_boundaries() {
    const rt::AABB box = unit_box();
    float enter = -1.0f;

    // Closed boxes include a tangent where near and far parameters coincide.
    check(intersect(box, {{-1.0f, 0.0f, 0.5f}, 0.0f, {1.0f, 1.0f, 0.0f}, 10.0f}, enter) &&
              enter == 1.0f,
          "edge tangent is included");

    // Tiny nonzero y velocity reaches the box at t=1e8; z is parallel.
    enter = -1.0f;
    check(intersect(box, {{-1.0f, -1.0f, 0.5f}, 0.0f, {1.0e-8f, 1.0e-8f, 0.0f}, 1.0e8f}, enter) &&
              enter == 1.0e8f,
          "near-parallel ray intersects at a large parameter");

    // t_lo is inclusive for node culling; the entry is at the boundary.
    enter = -1.0f;
    check(intersect(box, {{-1.0f, 0.5f, 0.5f}, 1.0f, {1.0f, 0.0f, 0.0f}, 2.0f}, enter) &&
              enter == 1.0f,
          "ray interval lower endpoint includes box entry");

    // The geometric exit is t=2. Place t_lo one float above it, but below
    // the expanded far bound. Without the documented expansion this misses;
    // with it, the closed intervals overlap at t_lo.
    const float geometric_exit = 2.0f;
    const float expanded_exit = geometric_exit * (1.0f + 2.0f * rt::RT_GAMMA3);
    const float after_exit = std::nextafter(geometric_exit, std::numeric_limits<float>::infinity());
    check(after_exit > geometric_exit && after_exit <= expanded_exit,
          "test interval lies between geometric and conservative far bounds");
    enter = -1.0f;
    check(intersect(box, {{-1.0f, 0.5f, 0.5f}, after_exit, {1.0f, 0.0f, 0.0f}, 3.0f}, enter) &&
              enter == after_exit,
          "conservative far expansion overlaps interval just beyond geometric exit");
}

void test_random_against_double() {
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
        check(float_hit == double_hit, "random slab hit agrees with double reference");
        if (float_hit && double_hit) {
            check(near(float_enter, static_cast<float>(double_enter), 2.0e-5f),
                  "random slab entry agrees with double reference");
        }
    }
}

static_assert(std::is_standard_layout<rt::vec3>::value && std::is_trivial<rt::vec3>::value,
              "vec3 must remain a POD layout");
static_assert(std::is_standard_layout<rt::Ray>::value && std::is_trivial<rt::Ray>::value,
              "Ray must remain a POD layout");
static_assert(std::is_standard_layout<rt::AABB>::value && std::is_trivial<rt::AABB>::value,
              "AABB must remain a POD layout");
static_assert(sizeof(rt::vec3) == 12 && sizeof(rt::Ray) == 32 && sizeof(rt::AABB) == 24,
              "math type layouts changed");

}  // namespace

int main() {
    test_vec3();
    test_aabb_members();
    test_zero_direction_cases();
    test_regression_and_intervals();
    test_deterministic_slab_boundaries();
    test_random_against_double();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All math and AABB tests passed\n";
    return 0;
}
