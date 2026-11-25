/// @file camera_path.hpp
/// @brief Camera paths: a smooth walk through waypoints that yields one Camera per frame
/// (`rt_render --path`). Host-only; it only chooses the camera values each frame is rendered
/// with, so renderers and images are unaffected.
#pragma once

#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "camera/camera.hpp"

namespace rt {

/// One stop on a path: where the camera is, what it looks at, and its field of view there.
struct CameraWaypoint {
    vec3 position;
    vec3 look_at;
    float vfov_deg;
};

/// How frames are spread along the path.
enum class PathTiming {
    Distance,   ///< Constant speed along the camera's route (the default).
    Waypoints,  ///< Equal time per segment between waypoints, so a camera can turn in place.
};

/// A smooth route through waypoints. Position, look-at point and field of view are each
/// interpolated with a centripetal Catmull-Rom spline (no overshoot or loops when waypoints are
/// unevenly spaced), which passes exactly through every waypoint.
class CameraPath {
   public:
    /// @param waypoints At least two. Positions, look-at points and fields of view must be finite
    ///        and the fields of view in (0, 180).
    /// @param timing Frame spacing, see PathTiming.
    /// @param ease If true, the camera starts and stops gently (smoothstep over the whole path).
    /// @param turn_cost With PathTiming::Distance, how much walking distance one radian of turning
    ///        is worth when sharing out frame time. A segment takes as long as the longer of its
    ///        walking distance and `turn_cost` times the angle its view direction turns through,
    ///        so a camera that stops and looks around still gets frames. In world units, at
    ///        least 0.
    /// @throws Error on fewer than two waypoints, a non-finite or out-of-range value, a negative
    ///         @p turn_cost, or, with PathTiming::Distance, a path that neither moves nor turns.
    CameraPath(std::vector<CameraWaypoint> waypoints, PathTiming timing, bool ease,
               double turn_cost = 2.0);

    /// The camera at fraction @p t of the path, `0` at the first waypoint and `1` at the last.
    /// Position, look-at point and field of view come from the path; `up`, width and height from
    /// @p base. The result is validated with validate_camera().
    /// @throws Error naming @p t and the field, if the camera at @p t is invalid (for example a
    ///         look-at point that reaches the camera, or a view direction parallel to `up`).
    Camera at(const Camera& base, double t) const;

    /// World-space length of the camera's route.
    double length() const { return length_; }
    std::size_t size() const { return count_; }

   private:
    static constexpr int kChannels = 7;  // position, look-at point, field of view
    using Channels = std::array<double, kChannels>;
    Channels evaluate(int segment, double tau) const;
    void locate(double t, int& segment, double& tau) const;

    std::size_t count_;
    PathTiming timing_;
    bool ease_;
    std::vector<Channels> points_;  // waypoints with a reflected point added at each end
    std::vector<double> knots_;     // centripetal parameter of each entry of points_
    std::vector<bool> hold_;        // per segment: the camera stands still while its view turns
    // Distance timing, per segment: the route length up to each of kSamplesPerSegment + 1 points
    // along it, the segment's length, and the share of the frame time it gets.
    std::vector<std::vector<double>> arc_;
    std::vector<double> segment_length_;
    std::vector<double> time_before_;  // cumulative time share; one more entry than segments
    double length_ = 0.0;
};

/// Reads a path file's JSON:
/// ```
/// { "timing": "distance" | "waypoints",   // optional, default "distance"
///   "ease": true | false,                 // optional, default false
///   "turn_cost": 2.0,                     // optional, see CameraPath
///   "waypoints": [ { "position": [x, y, z], "look_at": [x, y, z], "vfov_deg": 70 }, ... ] }
/// ```
/// `vfov_deg` is optional on every waypoint: the first one without it uses @p default_vfov_deg and
/// later ones keep the previous value. Unknown keys are errors.
/// @throws Error naming the offending field.
CameraPath parse_camera_path(const nlohmann::json& document, float default_vfov_deg);

/// Reads and parses a path file.
/// @throws Error on a file or JSON error (including the path), or as parse_camera_path().
CameraPath load_camera_path(const std::string& file, float default_vfov_deg);

}  // namespace rt
