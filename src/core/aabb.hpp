#pragma once

#include "core/ray.hpp"

#include <limits>

namespace rt {

constexpr float RT_EPS = 5.9604645e-8f;
constexpr float RT_GAMMA3 = (3.0f * RT_EPS) / (1.0f - 3.0f * RT_EPS);

struct AABB {
    vec3 lo;
    vec3 hi;

    RT_INLINE void grow(vec3 p) {
        lo = vmin(lo, p);
        hi = vmax(hi, p);
    }

    RT_INLINE void grow(const AABB& b) {
        lo = vmin(lo, b.lo);
        hi = vmax(hi, b.hi);
    }

    RT_INLINE vec3 centroid() const { return (lo + hi) * 0.5f; }
    RT_INLINE vec3 extent() const { return hi - lo; }
    RT_INLINE float surface_area() const {
        const vec3 e = extent();
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }
    RT_INLINE bool is_empty() const { return lo.x > hi.x || lo.y > hi.y || lo.z > hi.z; }
};

struct RayPrecomp {
    vec3 inv_d;
    int neg[3];
};

RT_INLINE RayPrecomp precompute(const Ray& r) {
    RayPrecomp p;
    p.inv_d = {1.0f / r.dir.x, 1.0f / r.dir.y, 1.0f / r.dir.z};
    for (int a = 0; a < 3; ++a) {
        p.neg[a] = p.inv_d[a] < 0.0f;
    }
    return p;
}

RT_INLINE bool intersect_aabb(const AABB& b, const vec3& o, const RayPrecomp& p, float t_lo,
                              float t_hi, float& t_enter) {
    float t0 = t_lo;
    float t1 = t_hi;
    for (int a = 0; a < 3; ++a) {
        const float near_plane = p.neg[a] ? b.hi[a] : b.lo[a];
        const float far_plane = p.neg[a] ? b.lo[a] : b.hi[a];
        const float tn = (near_plane - o[a]) * p.inv_d[a];
        float tf = (far_plane - o[a]) * p.inv_d[a];
        tf *= 1.0f + 2.0f * RT_GAMMA3;
        t0 = tn > t0 ? tn : t0;
        t1 = tf < t1 ? tf : t1;
        if (t0 > t1) {
            return false;
        }
    }
    t_enter = t0;
    return true;
}

static_assert(sizeof(AABB) == 24, "AABB must be 24 bytes");

}  // namespace rt
