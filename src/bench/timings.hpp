/// @file timings.hpp
/// @brief Shared storage for named timing components.
#pragma once

namespace rt {

/// Elapsed time recorded for named project components, in milliseconds.
///
/// This structure stores measurements; it does not measure or synchronize work. Components are
/// host wall-clock intervals, while `render_kernel` is an overlapping CUDA diagnostic. Totals are
/// measured directly, and only applicable non-overlapping components contribute to
/// `unattributed`. See INTERFACES.md section 8 for the timing contract.
struct Timings {
    double args = 0.0;       ///< Startup: command-line parsing; T_app only.
    double load = 0.0;       ///< Startup: scene/file loading; T_app only.
    double cuda_init = 0.0;  ///< Startup: CUDA context initialization; T_app only.
    double bounds = 0.0;     ///< Preparation (CPU): primitive bounds; T_br when applicable.
    double build = 0.0;      ///< Preparation (CPU): BVH construction; T_br when applicable.
    double layout = 0.0;     ///< Preparation (CPU): BVH layout work; T_br when applicable.
    double convert = 0.0;    ///< Preparation: Embree/OptiX internal build; T_br when applicable.
    double alloc = 0.0;      ///< Preparation (GPU): device allocation; T_br when applicable.
    double upload = 0.0;     ///< Preparation (GPU): device data upload; T_br when applicable.
    double render = 0.0;     ///< Frame: host wall-clock backend render; T_br and T_frame.
    double render_kernel = 0.0;  ///< Frame (CUDA): kernel-only cudaEvent time. Overlaps `render`.
    double download = 0.0;       ///< Frame: image/AOV transfer to host; T_br and T_frame when used.
};

}  // namespace rt
