/// @file generator.cpp
/// @brief Explicit PRNG, log-uniform radii, deterministic camera fitting and uniform layout.
#include "scene/generator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/error.hpp"
#include "scene/hash.hpp"
#include "scene/pcg32.hpp"
#include "scene/scene_io.hpp"
#include "scene/terrain_stub.hpp"

namespace rt {

AABB scene_bounds(const SceneHost& scene) {
    const float inf = std::numeric_limits<float>::infinity();
    AABB bounds{{inf, inf, inf}, {-inf, -inf, -inf}};
    for (const auto& sphere : scene.spheres) {
        const vec3 radius{sphere.radius, sphere.radius, sphere.radius};
        bounds.grow(sphere.center - radius);
        bounds.grow(sphere.center + radius);
    }
    for (const auto& triangle : scene.triangles) {
        bounds.grow(triangle.v0);
        bounds.grow(triangle.v1);
        bounds.grow(triangle.v2);
    }
    return bounds;
}

Camera fit_camera(const AABB& bounds) {
    if (bounds.is_empty()) throw Error("generator: cannot fit an empty scene");
    constexpr float pi = 3.14159265358979323846f;
    const vec3 direction{0.0f, std::sin(35.0f * pi / 180.0f), std::cos(35.0f * pi / 180.0f)};
    Camera camera{{}, bounds.centroid(), {0, 1, 0}, 45.0f, 1920, 1080};
    const auto fits = [&](float distance) {
        camera.position = camera.look_at + distance * direction;
        const vec3 w = normalize(camera.position - camera.look_at);
        const vec3 u = normalize(cross(camera.up, w)), v = cross(w, u);
        const float half_height = std::tan(camera.vfov_deg * pi / 360.0f);
        const float half_width = half_height * (1920.0f / 1080.0f);
        for (unsigned corner = 0; corner < 8; ++corner) {
            const vec3 p{corner & 1u ? bounds.hi.x : bounds.lo.x,
                         corner & 2u ? bounds.hi.y : bounds.lo.y,
                         corner & 4u ? bounds.hi.z : bounds.lo.z};
            const vec3 relative = p - camera.position;
            const float depth = -dot(relative, w);
            if (!(depth > 0) || std::abs(dot(relative, u)) > 0.95f * half_width * depth ||
                std::abs(dot(relative, v)) > 0.95f * half_height * depth)
                return false;
        }
        return true;
    };
    float lower = 0, upper = max_component(bounds.extent()) + 1.0f;
    while (!fits(upper)) {
        upper *= 2;
        if (!std::isfinite(upper)) throw Error("generator: camera fit overflow");
    }
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        const float middle = lower + (upper - lower) * 0.5f;
        if (middle == lower || middle == upper) break;
        if (fits(middle))
            upper = middle;
        else
            lower = middle;
    }
    fits(upper);
    return camera;
}

SceneHost generate_uniform(const UniformSettings& settings) {
    if (settings.primitives < 4 || settings.primitives >= 0x80000000u)
        throw Error("generator: primitive count must be in [4, 2^31)");
    const uint32_t budget = settings.primitives / 4;
    uint32_t grid = static_cast<uint32_t>(std::sqrt(static_cast<float>(budget)));
    while (uint64_t{grid} * grid > budget) --grid;
    while (uint64_t{grid + 1} * (grid + 1) <= budget) ++grid;
    const uint32_t triangles = 2 * grid * grid;
    const uint32_t spheres = settings.primitives - triangles;
    const float side = 400.0f * std::sqrt(static_cast<float>(settings.primitives) / 1000000.0f);
    const color palette[8] = {{0.8f, 0.1f, 0.1f}, {0.1f, 0.7f, 0.2f}, {0.1f, 0.2f, 0.8f},
                              {0.8f, 0.7f, 0.1f}, {0.6f, 0.1f, 0.7f}, {0.1f, 0.7f, 0.7f},
                              {0.7f, 0.7f, 0.7f}, {0.3f, 0.3f, 0.3f}};
    SceneHost scene;
    for (const auto& albedo : palette) scene.materials.push_back({albedo, {0, 0, 0}, 1, 0});
    scene.triangles = detail::terrain_grid_stub(grid, side, spheres);
    scene.spheres.reserve(spheres);
    Pcg32 random(settings.seed);
    for (uint32_t i = 0; i < spheres; ++i) {
        const float x = side * (random.uniform() - 0.5f);
        const float z = side * (random.uniform() - 0.5f);
        const float radius =
            std::clamp(std::exp(std::log(0.1f) + random.uniform() * std::log(10.0f)), 0.1f, 1.0f);
        const float y = detail::terrain_height_stub(x, z) + radius + random.uniform();
        scene.spheres.push_back({{x, y, z}, radius, i % 8});
    }
    const float intensity = 0.5f * side * side;
    scene.lights.push_back({LIGHT_POINT,
                            {0.3f * side, 0.8f * side, 0.2f * side},
                            {intensity, intensity, intensity},
                            0});
    scene.background = {0.1f, 0.1f, 0.15f};
    scene.ambient = 0.05f;
    scene.camera = fit_camera(scene_bounds(scene));
    validate_scene(scene);
    scene.hash = scene_hash(scene.view());
    return scene;
}

}  // namespace rt
