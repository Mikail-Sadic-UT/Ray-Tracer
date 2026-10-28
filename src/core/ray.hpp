#pragma once

#include "core/math.hpp"

namespace rt {

struct Ray {
    vec3 origin;
    float tmin;
    vec3 dir;
    float tmax;
};

static_assert(sizeof(Ray) == 32, "Ray must be 32 bytes");

}  // namespace rt
