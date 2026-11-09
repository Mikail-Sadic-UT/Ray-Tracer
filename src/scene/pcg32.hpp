/// @file pcg32.hpp
/// @brief Owned PCG-XSH-RR state, explicit float mapping, and Box-Muller sampling.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace rt {

/// PCG32 with independent state per generator; no global state or standard distributions.
class Pcg32 {
   public:
    /// Seeds using the reference PCG two-step initialization; stream defaults to 54.
    explicit Pcg32(uint64_t seed, uint64_t stream = 54) : increment_((stream << 1u) | 1u) {
        next();
        state_ += seed;
        next();
    }
    /// Generates one uint32 with PCG-XSH-RR output permutation.
    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ull + increment_;
        const uint32_t shifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rotation = static_cast<uint32_t>(old >> 59u);
        return (shifted >> rotation) | (shifted << ((0u - rotation) & 31u));
    }
    /// Maps the top 24 bits to exactly representable floats in [0,1).
    float uniform() { return static_cast<float>(next() >> 8u) * 0x1p-24f; }
    /// Returns two standard-normal samples using our own Box-Muller conversion.
    std::array<float, 2> normal_pair() {
        const float u = static_cast<float>((next() >> 8u) + 1u) * 0x1p-24f;
        const float angle = 6.2831853071795864769f * uniform();
        const float radius = std::sqrt(-2.0f * std::log(u));
        return {radius * std::cos(angle), radius * std::sin(angle)};
    }

   private:
    uint64_t state_ = 0;
    uint64_t increment_;
};

}  // namespace rt
