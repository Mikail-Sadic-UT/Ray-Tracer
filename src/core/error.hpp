/// @file error.hpp
/// @brief Error type thrown by host code. Do not include from headers used by device code.
#pragma once

#include <stdexcept>
#include <string>

namespace rt {

/// Exception thrown by host code on failure.
///
/// The message names what failed, e.g. `"scene_io: triangles[42].material_id = 7 >=
/// num_materials 3"`. Apps catch it in `main()`, print `error: <message>` to stderr, and exit
/// with code 1.
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace rt
