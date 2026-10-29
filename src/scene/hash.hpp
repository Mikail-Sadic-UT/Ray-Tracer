/// @file hash.hpp
/// @brief Canonical FNV-1a scene hashes, independent of padding and host byte order.
#pragma once

#include <cstdint>

#include "scene/scene.hpp"

namespace rt {

/// Hashes geometry counts and fields in canonical order (INTERFACES section 2).
/// @pre The scene view is valid and all arrays remain alive during the call.
uint64_t content_hash(const Scene& scene);

/// Hashes geometry, materials, lights, background, and ambient. Camera is excluded.
/// @pre The scene view is valid and all arrays remain alive during the call.
uint64_t scene_hash(const Scene& scene);

}  // namespace rt
