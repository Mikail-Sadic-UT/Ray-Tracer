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

CameraPath::CameraPath(std::vector<CameraWaypoint> waypoints, PathTiming timing, bool ease,
                       double turn_cost)
    : count_(waypoints.size()), timing_(timing), ease_(ease) {
    if (waypoints.size() < 2) path_error("waypoints", "need at least 2");
    if (!(turn_cost >= 0.0) || !std::isfinite(turn_cost))
        path_error("turn_cost", "must be a finite number, at least 0");
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
    // Judged against the size of the coordinates: identical positions still leave rounding noise
    // of about 1e-16 of them in the spline.
    double scale = 1.0;
    for (const Channels& c : pts)
        for (int k = 0; k < 3; ++k) scale = std::max(scale, std::fabs(c[k]));
    const double negligible = 1.0e-9 * scale;
    // Segments between identical positions hold the camera exactly still: the spline would
    // otherwise carry it a little past the stopping point and back while the view turns.
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        double d2 = 0.0;
        for (int k = 0; k < 3; ++k) d2 += (pts[i + 1][k] - pts[i][k]) * (pts[i + 1][k] - pts[i][k]);
        hold_.push_back(d2 <= negligible * negligible);
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

    auto distance = [&](const Channels& a, const Channels& b) {
        double d2 = 0.0;
        for (int c = 0; c < 3; ++c) d2 += (a[c] - b[c]) * (a[c] - b[c]);
        return std::sqrt(d2);
    };
    // The angle the view direction turns through between two waypoints.
    auto turn_angle = [](const Channels& a, const Channels& b) {
        double da[3], db[3], na = 0.0, nb = 0.0, dot = 0.0;
        for (int c = 0; c < 3; ++c) {
            da[c] = a[3 + c] - a[c];
            db[c] = b[3 + c] - b[c];
            na += da[c] * da[c];
            nb += db[c] * db[c];
            dot += da[c] * db[c];
        }
        if (!(na > 0.0) || !(nb > 0.0)) return 0.0;
        return std::acos(std::clamp(dot / std::sqrt(na * nb), -1.0, 1.0));
    };

    // Centripetal knots: the square root of how far each step takes the camera, walking or
    // turning (the same measure that shares out frame time below). Spacing them by position alone
    // would give a camera that stops and turns a near-zero interval, and the look-at point
    // would swing far off target in the neighbouring segment. Look-at points and fields of view
    // ride on the same knots.
    knots_.assign(points_.size(), 0.0);
    for (std::size_t i = 1; i < points_.size(); ++i) {
        const double step = std::max(distance(points_[i - 1], points_[i]),
                                     turn_cost * turn_angle(points_[i - 1], points_[i]));
        knots_[i] = knots_[i - 1] + std::max(std::sqrt(step), kMinKnot);
    }

    if (timing_ == PathTiming::Distance) {
        const int segments = static_cast<int>(count_) - 1;
        time_before_.assign(1, 0.0);
        for (int s = 0; s < segments; ++s) {
            std::vector<double> arc(kSamplesPerSegment + 1, 0.0);
            Channels previous = evaluate(s, 0.0);
            for (int k = 1; k <= kSamplesPerSegment; ++k) {
                const Channels here = evaluate(s, static_cast<double>(k) / kSamplesPerSegment);
                arc[k] = arc[k - 1] + distance(previous, here);
                previous = here;
            }
            const double walk = arc.back() > negligible ? arc.back() : 0.0;
            const double turn = turn_cost * turn_angle(pts[s], pts[s + 1]);
            segment_length_.push_back(walk);
            time_before_.push_back(time_before_.back() + std::max(walk, turn));
            length_ += walk;
            arc_.push_back(std::move(arc));
        }
        if (!(time_before_.back() > negligible))
            path_error("waypoints", "the camera neither moves nor turns");
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
    Channels out = mix(b1, b2, k[1], k[2], t);
    if (hold_[static_cast<std::size_t>(segment)]) {
        for (int c = 0; c < 3; ++c) out[c] = p[1][c];  // the camera stays put
    }
    return out;
}

void CameraPath::locate(double t, int& segment, double& tau) const {
    const int last = static_cast<int>(count_) - 2;
    if (timing_ == PathTiming::Waypoints) {
        const double u = t * static_cast<double>(count_ - 1);
        segment = std::min(static_cast<int>(std::floor(u)), last);
        tau = u - segment;
        return;
    }
    // Time is shared out by segment (walking or turning, whichever takes longer); within a
    // segment the camera moves at constant speed along its route.
    const double s = t * time_before_.back();
    const auto first = time_before_.begin() + 1;
    segment =
        std::min(static_cast<int>(std::upper_bound(first, time_before_.end(), s) - first), last);
    const double share = time_before_[segment + 1] - time_before_[segment];
    const double f = share > 0.0 ? std::clamp((s - time_before_[segment]) / share, 0.0, 1.0) : 0.0;
    const std::vector<double>& arc = arc_[segment];
    if (!(segment_length_[segment] > 0.0)) {  // a turn in place: equal progress in time
        tau = f;
        return;
    }
    const double along = f * arc.back();
    std::size_t k =
        static_cast<std::size_t>(std::upper_bound(arc.begin(), arc.end(), along) - arc.begin());
    k = (k == 0) ? 0 : std::min(k - 1, arc.size() - 2);
    const double span = arc[k + 1] - arc[k];
    const double within = span > 0.0 ? std::clamp((along - arc[k]) / span, 0.0, 1.0) : 0.0;
    tau = (static_cast<double>(k) + within) / kSamplesPerSegment;
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
    only_keys(document, {"waypoints", "timing", "ease", "turn_cost"}, "path");

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
    double turn_cost = 2.0;
    if (const auto it = document.find("turn_cost"); it != document.end()) {
        if (!it->is_number()) path_error("turn_cost", "expected a number");
        turn_cost = it->get<double>();
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
    return CameraPath(std::move(waypoints), timing, ease, turn_cost);
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
