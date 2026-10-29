/// @file scene_host.hpp
/// @brief Host-owned scene storage and conversion to a non-owning Scene view.
#pragma once

#include <cstdint>
#include <vector>

#include "scene/camera_stub.hpp"
#include "scene/scene.hpp"

namespace rt {

/// Owns scene data for the full rendering run. Host-only; do not include in device headers.
struct SceneHost {
    std::vector<Sphere> spheres;      ///< Owned spheres.
    std::vector<Triangle> triangles;  ///< Owned triangles.
    std::vector<Material> materials;  ///< Owned materials.
    std::vector<Light> lights;        ///< Owned lights.
    CameraStub camera{};              ///< Temporary camera storage; see camera_stub.hpp.
    color background{};               ///< Linear RGB background.
    float ambient = 0.0f;             ///< Ambient illumination.
    uint64_t hash = 0;                ///< Canonical scene hash, computed after validation.

    /// Returns a host-pointer view without copying the arrays.
    /// @pre Scene counts and fields have been validated against INTERFACES section 2.
    /// @note Keep this owner alive and its vectors unmodified while the view is in use.
    Scene view() const {
        return {
            spheres.empty() ? nullptr : spheres.data(),
            static_cast<uint32_t>(spheres.size()),
            triangles.empty() ? nullptr : triangles.data(),
            static_cast<uint32_t>(triangles.size()),
            materials.empty() ? nullptr : materials.data(),
            static_cast<uint32_t>(materials.size()),
            lights.empty() ? nullptr : lights.data(),
            static_cast<uint32_t>(lights.size()),
            background,
            ambient,
        };
    }
};

}  // namespace rt
