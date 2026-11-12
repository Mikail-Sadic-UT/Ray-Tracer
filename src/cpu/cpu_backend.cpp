/// @file cpu_backend.cpp
/// @brief CPU backend lifecycle and the (query, shading) dispatch.
#include "cpu/cpu_backend.hpp"

#include <chrono>
#include <cstdint>
#include <string>

#include "core/error.hpp"
#include "cpu/tile_loop.hpp"
#include "shade/shading.hpp"
#include "trace/brute.hpp"
#include "trace/traverse.hpp"

namespace rt {
namespace {

/// BvhQuery plus the traversal overflow flag. §6's BvhQuery has no member for the flag
/// (CONTRACT_NOTES W8), so the backend keeps its own query type with the same two calls.
struct CheckedBvhQuery {
    Scene s;
    BvhView bvh;
    uint32_t* overflow;  ///< Shared by all render threads; set with a relaxed atomic OR.

    Hit closest(const Ray& r) const { return traverse_closest(r, s, bvh, overflow); }
    bool any(const Ray& r) const { return traverse_any(r, s, bvh, overflow); }
};

/// Runs the tile loop with the Shading policy that @p mode selects. prepare() has already
/// rejected any other mode.
template <class Query>
void render_shaded(const Camera& cam, const Scene& s, const Query& q, ShadeMode mode, Image& out,
                   int threads) {
    if (mode == ShadeMode::Lit) {
        cpu::render_tiles(cam, s, q, LitShading{}, out, threads);
    } else {
        cpu::render_tiles(cam, s, q, NormalShading{}, out, threads);
    }
}

}  // namespace

void CpuBackend::prepare(const PrepareInput& in, Timings& /*t*/) {
    release();
    if (in.scene == nullptr) throw Error("cpu: PrepareInput::scene is null");
    if (in.render_threads < 1) throw Error("cpu: render thread count must be positive");
    const RenderSettings& rs = in.render;
    if (rs.width == 0 || rs.height == 0)
        throw Error("cpu: render width and height must be positive");
    if ((rs.aovs & ~(AOV_PRIM_ID | AOV_LIGHT_MASK)) != 0) {
        throw Error("cpu: unknown AOV flags 0x" + std::to_string(rs.aovs));
    }
    if ((rs.aovs & AOV_LIGHT_MASK) && in.scene->lights.size() > 32) {
        throw Error("cpu: the lightmask AOV supports at most 32 lights, scene has " +
                    std::to_string(in.scene->lights.size()));
    }
    if (rs.shade != ShadeMode::Lit && rs.shade != ShadeMode::Normal) {
        throw Error("cpu: unknown shade mode " + std::to_string(static_cast<uint32_t>(rs.shade)));
    }
    if (in.bvh != nullptr) {
        const size_t prims = in.scene->spheres.size() + in.scene->triangles.size();
        if (in.bvh->prim_refs.size() != prims || in.bvh->nodes.empty() != (prims == 0)) {
            throw Error("cpu: the BVH has " + std::to_string(in.bvh->prim_refs.size()) +
                        " primitive references but the scene has " + std::to_string(prims) +
                        " primitives");
        }
        bvh_ = {in.bvh->nodes.empty() ? nullptr : in.bvh->nodes.data(),
                in.bvh->prim_refs.empty() ? nullptr : in.bvh->prim_refs.data(),
                static_cast<uint32_t>(in.bvh->nodes.size())};
        use_bvh_ = true;
    }
    scene_ = in.scene->view();
    settings_ = rs;
    threads_ = in.render_threads;
    prepared_ = true;
}

void CpuBackend::render(const Camera& cam, Image& out, Timings& t) {
    if (!prepared_) throw Error("cpu: render called before prepare");
    if (cam.width != settings_.width || cam.height != settings_.height) {
        throw Error("cpu: camera resolution " + std::to_string(cam.width) + "x" +
                    std::to_string(cam.height) + " differs from the prepared " +
                    std::to_string(settings_.width) + "x" + std::to_string(settings_.height) +
                    "; call prepare again");
    }
    cpu::resize_image(out, cam, (settings_.aovs & AOV_PRIM_ID) != 0,
                      (settings_.aovs & AOV_LIGHT_MASK) != 0);
    uint32_t overflow = 0;
    const auto begin = std::chrono::steady_clock::now();
    if (use_bvh_) {
        render_shaded(cam, scene_, CheckedBvhQuery{scene_, bvh_, &overflow}, settings_.shade, out,
                      threads_);
    } else {
        render_shaded(cam, scene_, BruteQuery{scene_}, settings_.shade, out, threads_);
    }
    t.render =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    if (overflow != 0) {
        throw Error("cpu: BVH traversal stack overflow; the BVH is deeper than 64 levels");
    }
}

void CpuBackend::release() {
    prepared_ = false;
    scene_ = Scene{};
    bvh_ = {nullptr, nullptr, 0};
    use_bvh_ = false;
    settings_ = RenderSettings{};
    threads_ = 1;
}

}  // namespace rt
