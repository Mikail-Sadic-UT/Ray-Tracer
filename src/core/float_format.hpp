/// @file float_format.hpp
/// @brief Host-only float formatting for human-readable JSON output.
#pragma once

#include <charconv>
#include <cstdint>
#include <cstring>

namespace rt {

/// Returns the double value of the shortest decimal that reads back as exactly @p f.
///
/// JSON numbers are doubles, so storing a float directly prints 17 digits
/// (`0.05f` -> `0.05000000074505806`). Storing this value prints the short form (`0.05`)
/// instead. Readers convert the double back to float, so the value is only used when that
/// conversion reproduces @p f bit for bit; otherwise the exact `double(f)` is returned.
/// @pre @p f is finite.
inline double shortest_decimal(float f) {
    char text[32];
    const auto written = std::to_chars(text, text + sizeof(text), f);
    double value = 0.0;
    std::from_chars(text, written.ptr, value);
    const float back = static_cast<float>(value);
    uint32_t a, b;
    std::memcpy(&a, &f, sizeof(f));
    std::memcpy(&b, &back, sizeof(back));
    return a == b ? value : static_cast<double>(f);
}

}  // namespace rt
