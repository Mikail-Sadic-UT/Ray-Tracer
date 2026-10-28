#pragma once

#include "core/hostdev.hpp"

#include <cmath>

namespace rt {

struct vec3 {
    float x;
    float y;
    float z;

    RT_INLINE float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    RT_INLINE float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

using color = vec3;

RT_INLINE vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
RT_INLINE vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
RT_INLINE vec3 operator-(vec3 a) { return {-a.x, -a.y, -a.z}; }
RT_INLINE vec3 operator*(vec3 a, vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
RT_INLINE vec3 operator*(vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
RT_INLINE vec3 operator*(float s, vec3 a) { return a * s; }
RT_INLINE vec3 operator/(vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
RT_INLINE vec3& operator+=(vec3& a, vec3 b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}
RT_INLINE vec3& operator*=(vec3& a, float s) {
    a.x *= s;
    a.y *= s;
    a.z *= s;
    return a;
}

RT_INLINE float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
RT_INLINE vec3 cross(vec3 a, vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
RT_INLINE float length2(vec3 a) { return dot(a, a); }
RT_INLINE float length(vec3 a) { return ::sqrtf(length2(a)); }
RT_INLINE vec3 normalize(vec3 a) { return a / length(a); }
RT_INLINE vec3 vmin(vec3 a, vec3 b) {
    return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
}
RT_INLINE vec3 vmax(vec3 a, vec3 b) {
    return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
}
RT_INLINE float max_component(vec3 a) {
    const float xy = a.x > a.y ? a.x : a.y;
    return xy > a.z ? xy : a.z;
}

static_assert(sizeof(vec3) == 12, "vec3 must be 12 bytes");

}  // namespace rt
