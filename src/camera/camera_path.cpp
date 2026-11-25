/// @file camera_path.cpp
/// @brief Camera paths: centripetal Catmull-Rom through waypoints (`rt_render --path`).
#include "camera/camera_path.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

#include "camera/camera_io.hpp"
#include "core/error.hpp"

namespace rt {
namespace {

using nlohmann::json;

constexpr int kSamplesPerSegment = 32;  // arc-length table resolution
constexpr double kMinKnot = 1.0e-2;     // keeps knots distinct when the camera only turns

[[noreturn]] void path_error(const std::string& field, const std::string& reason) {
    throw Error("camera path: " + field + ": " + reason);
}

float read_float(const json& value, const std::string& path) {
    if (!value.is_number()) path_error(path, "expected a number");
    const double number = value.get<double>();
    const double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(number) || number < -limit || number > limit)
        path_error(path, "expected a finite float");
    return static_cast<float>(number);
}

vec3 read_vec3(const json& value, const std::string& path) {
    if (!value.is_array()) path_error(path, "expected an array");
    if (value.size() != 3) path_error(path, "expected exactly 3 components");
    return {read_float(value[0], path + "[0]"), read_float(value[1], path + "[1]"),
            read_float(value[2], path + "[2]")};
}

/// Rejects keys outside @p allowed, so a typo is an error and not a silently ignored setting.
void only_keys(const json& object, const std::set<std::string>& allowed, const std::string& where) {
    for (const auto& item : object.items()) {
        if (allowed.count(item.key()) == 0) path_error(where, "unknown key '" + item.key() + "'");
    }
}

}  // namespace

CameraPath::CameraPath(std::vector<CameraWaypoint> waypoints, PathTiming timing, bool ease)
    : count_(waypoints.size()), timing_(timing), ease_(ease) {
    if (waypoints.size() < 2) path_error("waypoints", "need at least 2");
    std::vector<Channels> pts;
    for (std::size_t i = 0; i < waypoints.size(); ++i) {
        const CameraWaypoint& w = waypoints[i];
        const std::string where = "waypoints[" + std::to_string(i) + "]";
        const Channels c = {w.position.x, w.position.y, w.position.z, w.look_at.x,
                            w.look_at.y,  w.look_at.z,  w.vfov_deg};
        for (double v : c)
            if (!std::isfinite(v)) path_error(where, "values must be finite");
        if (!(w.vfov_deg > 0.0f && w.vfov_deg < 180.0f))
            path_error(where + ".vfov_deg", "must be between 0 and 180 degrees, exclusively");
        pts.push_back(c);
    }
    // Reflect the neighbour past each end so the first and last segments need no special case.
    Channels before, after;
    const std::size_t n = pts.size();
    for (int c = 0; c < kChannels; ++c) {
        before[c] = 2.0 * pts[0][c] - pts[1][c];
        after[c] = 2.0 * pts[n - 1][c] - pts[n - 2][c];
    }
    points_.push_back(before);
    points_.insert(points_.end(), pts.begin(), pts.end());
    points_.push_back(after);

    // Centripetal knots from the camera positions: the square root of each distance. Look-at
    // points and fields of view ride on the same knots.
    auto distance = [&](const Channels& a, const Channels& b) {
        double d2 = 0.0;
        for (int c = 0; c < 3; ++c) d2 += (a[c] - b[c]) * (a[c] - b[c]);
        return std::sqrt(d2);
    };
    knots_.assign(points_.size(), 0.0);
    for (std::size_t i = 1; i < points_.size(); ++i) {
        knots_[i] =
            knots_[i - 1] + std::max(std::sqrt(distance(points_[i - 1], points_[i])), kMinKnot);
    }

    if (timing_ == PathTiming::Distance) {
        const int segments = static_cast<int>(count_) - 1;
        for (int s = 0; s < segments; ++s)
            for (int k = 0; k < kSamplesPerSegment; ++k)
                samples_.push_back({s, static_cast<double>(k) / kSamplesPerSegment});
        samples_.push_back({segments - 1, 1.0});
        arc_.assign(samples_.size(), 0.0);
        Channels previous = evaluate(samples_[0].segment, samples_[0].tau);
        for (std::size_t j = 1; j < samples_.size(); ++j) {
            const Channels here = evaluate(samples_[j].segment, samples_[j].tau);
            arc_[j] = arc_[j - 1] + distance(previous, here);
            previous = here;
        }
        length_ = arc_.back();
        // Judged against the size of the coordinates: identical positions still leave rounding
        // noise of about 1e-16 of them in the spline.
        double scale = 1.0;
        for (const Channels& c : pts)
            for (int k = 0; k < 3; ++k) scale = std::max(scale, std::fabs(c[k]));
        if (!(length_ > 1.0e-9 * scale))
            path_error("waypoints",
                       "the camera never moves; use \"timing\": \"waypoints\" to turn in place");
    } else {
        for (std::size_t i = 0; i + 1 < count_; ++i)
            length_ += distance(points_[i + 1], points_[i + 2]);
    }
}

CameraPath::Channels CameraPath::evaluate(int segment, double tau) const {
    // Barry-Goldman form of the Catmull-Rom spline through points_[segment + 1 .. + 2].
    const Channels* p = &points_[static_cast<std::size_t>(segment)];
    const double* k = &knots_[static_cast<std::size_t>(segment)];
    const double t = k[1] + tau * (k[2] - k[1]);
    const auto mix = [](const Channels& a, const Channels& b, double ta, double tb, double at) {
        const double w = (at - ta) / (tb - ta);
        Channels out;
        for (int c = 0; c < kChannels; ++c) out[c] = (1.0 - w) * a[c] + w * b[c];
        return out;
    };
    const Channels a1 = mix(p[0], p[1], k[0], k[1], t);
    const Channels a2 = mix(p[1], p[2], k[1], k[2], t);
    const Channels a3 = mix(p[2], p[3], k[2], k[3], t);
    const Channels b1 = mix(a1, a2, k[0], k[2], t);
    const Channels b2 = mix(a2, a3, k[1], k[3], t);
    return mix(b1, b2, k[1], k[2], t);
}

void CameraPath::locate(double t, int& segment, double& tau) const {
    const int last = static_cast<int>(count_) - 2;
    if (timing_ == PathTiming::Waypoints) {
        const double u = t * static_cast<double>(count_ - 1);
        segment = std::min(static_cast<int>(std::floor(u)), last);
        tau = u - segment;
        return;
    }
    const double s = t * length_;
    std::size_t j =
        static_cast<std::size_t>(std::upper_bound(arc_.begin(), arc_.end(), s) - arc_.begin());
    j = (j == 0) ? 0 : std::min(j - 1, arc_.size() - 2);
    const double span = arc_[j + 1] - arc_[j];
    const double f = span > 0.0 ? std::clamp((s - arc_[j]) / span, 0.0, 1.0) : 0.0;
    const Sample a = samples_[j];
    Sample b = samples_[j + 1];
    if (b.segment != a.segment) b = {a.segment, 1.0};  // the next segment's start is this one's end
    segment = a.segment;
    tau = a.tau + (b.tau - a.tau) * f;
}

Camera CameraPath::at(const Camera& base, double t) const {
    t = std::clamp(t, 0.0, 1.0);
    if (ease_) t = t * t * (3.0 - 2.0 * t);
    int segment = 0;
    double tau = 0.0;
    locate(t, segment, tau);
    const Channels c = evaluate(segment, tau);
    Camera cam = base;
    cam.position = {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
    cam.look_at = {static_cast<float>(c[3]), static_cast<float>(c[4]), static_cast<float>(c[5])};
    cam.vfov_deg = static_cast<float>(c[6]);
    try {
        validate_camera(cam);
    } catch (const Error& e) {
        std::ostringstream message;
        message << "camera path at " << static_cast<int>(std::lround(t * 1000.0)) / 10.0
                << "% of the way: " << e.what();
        throw Error(message.str());
    }
    return cam;
}

CameraPath parse_camera_path(const json& document, float default_vfov_deg) {
    if (!document.is_object()) path_error("path", "expected an object");
    only_keys(document, {"waypoints", "timing", "ease"}, "path");

    PathTiming timing = PathTiming::Distance;
    if (const auto it = document.find("timing"); it != document.end()) {
        if (!it->is_string()) path_error("timing", "expected \"distance\" or \"waypoints\"");
        const std::string text = it->get<std::string>();
        if (text == "waypoints")
            timing = PathTiming::Waypoints;
        else if (text != "distance")
            path_error("timing", "expected \"distance\" or \"waypoints\", got \"" + text + "\"");
    }
    bool ease = false;
    if (const auto it = document.find("ease"); it != document.end()) {
        if (!it->is_boolean()) path_error("ease", "expected true or false");
        ease = it->get<bool>();
    }
    const auto list = document.find("waypoints");
    if (list == document.end()) path_error("waypoints", "missing field");
    if (!list->is_array()) path_error("waypoints", "expected an array");

    std::vector<CameraWaypoint> waypoints;
    float vfov = default_vfov_deg;
    for (std::size_t i = 0; i < list->size(); ++i) {
        const json& item = (*list)[i];
        const std::string where = "waypoints[" + std::to_string(i) + "]";
        if (!item.is_object()) path_error(where, "expected an object");
        only_keys(item, {"position", "look_at", "vfov_deg"}, where);
        const auto position = item.find("position");
        const auto look_at = item.find("look_at");
        if (position == item.end()) path_error(where + ".position", "missing field");
        if (look_at == item.end()) path_error(where + ".look_at", "missing field");
        if (const auto it = item.find("vfov_deg"); it != item.end())
            vfov = read_float(*it, where + ".vfov_deg");
        waypoints.push_back({read_vec3(*position, where + ".position"),
                             read_vec3(*look_at, where + ".look_at"), vfov});
    }
    return CameraPath(std::move(waypoints), timing, ease);
}

CameraPath load_camera_path(const std::string& file, float default_vfov_deg) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw Error("camera path: " + file + ": cannot open");
    json document;
    try {
        document = json::parse(in);
    } catch (const json::exception& e) {
        throw Error("camera path: " + file + ": " + e.what());
    }
    try {
        return parse_camera_path(document, default_vfov_deg);
    } catch (const Error& e) {
        throw Error(file + ": " + e.what());
    }
}

}  // namespace rt
