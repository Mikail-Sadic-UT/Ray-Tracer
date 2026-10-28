/// @file math.hpp
/// @brief vec3/color type and the free-function vector math API.
///
/// Header-only and usable from host and device code. All math is float with IEEE semantics.
#pragma once

#include <cmath>

#include "core/hostdev.hpp"

namespace rt {

/// 3-component float vector (12 bytes, POD).
struct vec3 {
    float x;
    float y;
    float z;

    /// Component access by index: 0 = x, 1 = y, any other value = z. No bounds check.
    RT_INLINE float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    /// @copydoc operator[](int) const
    RT_INLINE float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

/// Linear RGB color. Same type and operators as vec3.
using color = vec3;

/// @name Arithmetic
/// Componentwise operators. `operator*(vec3, vec3)` multiplies matching components.
/// @{
RT_INLINE vec3 operator+(vec3 a, vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
RT_INLINE vec3 operator-(vec3 a, vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
RT_INLINE vec3 operator-(vec3 a) {
    return {-a.x, -a.y, -a.z};
}
RT_INLINE vec3 operator*(vec3 a, vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
RT_INLINE vec3 operator*(vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
RT_INLINE vec3 operator*(float s, vec3 a) {
    return a * s;
}
RT_INLINE vec3 operator/(vec3 a, float s) {
    return {a.x / s, a.y / s, a.z / s};
}
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
/// @}

/// Dot product.
RT_INLINE float dot(vec3 a, vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// Cross product (right-handed).
RT_INLINE vec3 cross(vec3 a, vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// Squared length.
RT_INLINE float length2(vec3 a) {
    return dot(a, a);
}

/// Euclidean length.
RT_INLINE float length(vec3 a) {
    return ::sqrtf(length2(a));
}

/// Unit vector in the direction of @p a.
/// @pre `length2(a)` is a finite, normal float: roughly `1.1e-19 < length(a) < 1.8e19`.
/// Outside that range the result is wrong: a zero vector gives NaN, a very short vector gives
/// inf/NaN, and a very long one (e.g. `{1e20, 0, 0}`) gives zero.
RT_INLINE vec3 normalize(vec3 a) {
    return a / length(a);
}

/// Componentwise minimum (named `vmin` to avoid clashing with `std::min`).
/// @return For each component, the smaller of @p a and @p b; @p b if either is NaN.
RT_INLINE vec3 vmin(vec3 a, vec3 b) {
    return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
}

/// Componentwise maximum (named `vmax` to avoid clashing with `std::max`).
/// @return For each component, the larger of @p a and @p b; @p b if either is NaN.
RT_INLINE vec3 vmax(vec3 a, vec3 b) {
    return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
}

/// Largest of the three components.
RT_INLINE float max_component(vec3 a) {
    const float xy = a.x > a.y ? a.x : a.y;
    return xy > a.z ? xy : a.z;
}

static_assert(sizeof(vec3) == 12, "vec3 must be 12 bytes");

}  // namespace rt
