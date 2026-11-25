/// @file test_camera_path.cpp
/// @brief Tests for camera paths (`rt_render --path`): the spline, the timing, and the checks.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "camera/camera_path.hpp"
#include "core/error.hpp"

namespace {

rt::Camera base_camera() {
    rt::Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    camera.look_at = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.vfov_deg = 45.0f;
    camera.width = 320;
    camera.height = 180;
    return camera;
}

rt::CameraWaypoint waypoint(rt::vec3 position, rt::vec3 look_at, float vfov = 45.0f) {
    return {position, look_at, vfov};
}

/// Four waypoints on the x axis, unevenly spaced: 0, 1, 5, 6.
std::vector<rt::CameraWaypoint> straight_line() {
    const rt::vec3 target{0.0f, 0.0f, -10.0f};
    return {waypoint({0, 0, 0}, target), waypoint({1, 0, 0}, target), waypoint({5, 0, 0}, target),
            waypoint({6, 0, 0}, target)};
}

void expect_near(rt::vec3 a, rt::vec3 b, float tolerance) {
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}

}  // namespace

TEST(CameraPath, PassesThroughEveryWaypointWithWaypointTiming) {
    const std::vector<rt::CameraWaypoint> points = {
        waypoint({0, 1, 6}, {0, 0, 0}, 40.0f), waypoint({4, 2, 3}, {1, 0, 0}, 55.0f),
        waypoint({7, 1, -2}, {2, 1, -4}, 70.0f), waypoint({3, 3, -6}, {0, 1, 0}, 50.0f)};
    const rt::CameraPath path(points, rt::PathTiming::Waypoints, false);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const rt::Camera cam = path.at(base_camera(), static_cast<double>(i) / (points.size() - 1));
        expect_near(cam.position, points[i].position, 1e-5f);
        expect_near(cam.look_at, points[i].look_at, 1e-5f);
        EXPECT_NEAR(cam.vfov_deg, points[i].vfov_deg, 1e-4f);
    }
}

TEST(CameraPath, StartsAndEndsExactlyAtTheEndWaypointsWithDistanceTiming) {
    const std::vector<rt::CameraWaypoint> points = {waypoint({0, 1, 6}, {0, 0, 0}),
                                                    waypoint({4, 2, 3}, {1, 0, 0}),
                                                    waypoint({7, 1, -2}, {2, 1, -4})};
    const rt::CameraPath path(points, rt::PathTiming::Distance, false);
    const rt::Camera first = path.at(base_camera(), 0.0);
    const rt::Camera last = path.at(base_camera(), 1.0);
    EXPECT_EQ(first.position.x, 0.0f);
    EXPECT_EQ(first.position.y, 1.0f);
    EXPECT_EQ(first.position.z, 6.0f);
    EXPECT_EQ(last.position.x, 7.0f);
    EXPECT_EQ(last.position.y, 1.0f);
    EXPECT_EQ(last.position.z, -2.0f);
}

TEST(CameraPath, DistanceTimingMovesAtConstantSpeed) {
    const rt::CameraPath path(straight_line(), rt::PathTiming::Distance, false);
    EXPECT_NEAR(path.length(), 6.0, 1e-3);
    for (double t = 0.0; t <= 1.0; t += 0.1) {
        const rt::Camera cam = path.at(base_camera(), t);
        EXPECT_NEAR(cam.position.x, 6.0 * t, 2e-3) << "t " << t;
        EXPECT_NEAR(cam.position.y, 0.0f, 1e-6f);
    }
}

TEST(CameraPath, EaseStartsAndStopsGently) {
    const rt::CameraPath path(straight_line(), rt::PathTiming::Distance, true);
    // Smoothstep: 2.8% of the way after 10% of the time, and symmetric about the middle.
    EXPECT_NEAR(path.at(base_camera(), 0.1).position.x, 6.0 * 0.028, 5e-3);
    EXPECT_NEAR(path.at(base_camera(), 0.5).position.x, 3.0, 5e-3);
    EXPECT_NEAR(path.at(base_camera(), 0.9).position.x, 6.0 * 0.972, 5e-3);
}

TEST(CameraPath, UnevenlySpacedWaypointsDoNotOvershoot) {
    // A long approach, then short turns: a uniform spline would loop or overshoot here.
    const rt::vec3 target{0, 0, 0};
    const rt::CameraPath path({waypoint({20, 1, 0}, target), waypoint({4, 1, 0}, target),
                               waypoint({3, 1, 1}, target), waypoint({2, 1, 1.5f}, target)},
                              rt::PathTiming::Distance, false);
    for (double t = 0.0; t <= 1.0; t += 0.01) {
        const rt::Camera cam = path.at(base_camera(), t);
        EXPECT_GE(cam.position.x, 2.0f - 0.01f) << "t " << t;
        EXPECT_LE(cam.position.x, 20.0f + 0.01f) << "t " << t;
        EXPECT_GE(cam.position.z, -0.5f) << "t " << t;
        EXPECT_LE(cam.position.z, 1.5f + 0.5f) << "t " << t;
    }
}

TEST(CameraPath, TakesUpAndResolutionFromTheBaseCamera) {
    rt::Camera base = base_camera();
    base.up = {0.0f, 0.0f, 1.0f};
    base.width = 1920;
    base.height = 1080;
    const rt::CameraPath path(
        {waypoint({0, 5, 0}, {0, 0, 0}, 30.0f), waypoint({5, 5, 0}, {5, 0, 0}, 90.0f)},
        rt::PathTiming::Distance, false);
    const rt::Camera cam = path.at(base, 0.5);
    EXPECT_EQ(cam.up.z, 1.0f);
    EXPECT_EQ(cam.width, 1920u);
    EXPECT_EQ(cam.height, 1080u);
    EXPECT_NEAR(cam.vfov_deg, 60.0f, 1e-3f);  // halfway between 30 and 90
}

TEST(CameraPath, WaypointTimingCanTurnInPlace) {
    const rt::CameraPath path({waypoint({0, 1, 0}, {5, 1, 0}), waypoint({0, 1, 0}, {0, 1, -5})},
                              rt::PathTiming::Waypoints, false);
    for (double t : {0.0, 0.5, 1.0}) {
        const rt::Camera cam = path.at(base_camera(), t);
        expect_near(cam.position, {0, 1, 0}, 1e-6f);
    }
    const rt::Camera middle = path.at(base_camera(), 0.5);
    EXPECT_NEAR(middle.look_at.x, 2.5f, 1e-4f);
    EXPECT_NEAR(middle.look_at.z, -2.5f, 1e-4f);
}

TEST(CameraPath, RejectsBadWaypoints) {
    const rt::vec3 target{0, 0, 0};
    EXPECT_THROW(rt::CameraPath({waypoint({0, 0, 1}, target)}, rt::PathTiming::Distance, false),
                 rt::Error);
    EXPECT_THROW(rt::CameraPath({waypoint({0, 0, 1}, target), waypoint({INFINITY, 0, 1}, target)},
                                rt::PathTiming::Distance, false),
                 rt::Error);
    EXPECT_THROW(rt::CameraPath({waypoint({0, 0, 1}, target), waypoint({1, 0, 1}, target, 180.0f)},
                                rt::PathTiming::Distance, false),
                 rt::Error);
    EXPECT_THROW(rt::CameraPath({waypoint({0, 0, 1}, target), waypoint({0, 0, 1}, {1, 0, 0})},
                                rt::PathTiming::Distance, false),
                 rt::Error);
    EXPECT_NO_THROW(rt::CameraPath({waypoint({0, 0, 1}, target), waypoint({0, 0, 1}, {1, 0, 0})},
                                   rt::PathTiming::Waypoints, false));
}

TEST(CameraPath, ChecksTheCameraAtEachFrameAndNamesWhereItFails) {
    // The look-at point reaches the camera on the way: invalid at that frame.
    const rt::CameraPath onto({waypoint({0, 0, 5}, {0, 0, 0}), waypoint({0, 0, 5}, {0, 0, 5})},
                              rt::PathTiming::Waypoints, false);
    EXPECT_NO_THROW(onto.at(base_camera(), 0.0));
    try {
        onto.at(base_camera(), 1.0);
        ADD_FAILURE() << "accepted a look-at point on the camera";
    } catch (const rt::Error& e) {
        const std::string text = e.what();
        EXPECT_NE(text.find("100%"), std::string::npos) << text;
        EXPECT_NE(text.find("look_at"), std::string::npos) << text;
    }
    // Looking straight down while up is +y.
    const rt::CameraPath down({waypoint({0, 5, 0}, {0, 0, 0}), waypoint({1, 5, 0}, {1, 0, 0})},
                              rt::PathTiming::Distance, false);
    EXPECT_THROW(down.at(base_camera(), 0.5), rt::Error);
}

TEST(CameraPathParse, ReadsWaypointsTimingEaseAndFieldOfView) {
    const nlohmann::json doc = nlohmann::json::parse(R"({
        "timing": "waypoints", "ease": false,
        "waypoints": [
            {"position": [0, 1, 5], "look_at": [0, 1, 0]},
            {"position": [3, 1, 5], "look_at": [0, 1, 0], "vfov_deg": 70},
            {"position": [6, 1, 5], "look_at": [0, 1, 0]}]})");
    const rt::CameraPath path = rt::parse_camera_path(doc, 55.0f);
    EXPECT_EQ(path.size(), 3u);
    // The first waypoint takes the default; the third keeps the second's value.
    EXPECT_NEAR(path.at(base_camera(), 0.0).vfov_deg, 55.0f, 1e-4f);
    EXPECT_NEAR(path.at(base_camera(), 0.5).vfov_deg, 70.0f, 1e-4f);
    EXPECT_NEAR(path.at(base_camera(), 1.0).vfov_deg, 70.0f, 1e-4f);
}

TEST(CameraPathParse, RejectsBadFilesNamingTheField) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {R"([])", "path"},
        {R"({"waypoints": 3})", "waypoints"},
        {R"({})", "waypoints"},
        {R"({"waypoints": [{"position": [0,0,0], "look_at": [0,0,1]}]})", "at least 2"},
        {R"({"waypoints": [{"look_at": [0,0,1]}, {"position": [1,0,0], "look_at": [0,0,1]}]})",
         "waypoints[0].position"},
        {R"({"waypoints": [{"position": [0,0], "look_at": [0,0,1]},
                           {"position": [1,0,0], "look_at": [0,0,1]}]})",
         "exactly 3"},
        {R"({"waypoints": [{"position": [0,0,0], "look_at": [0,0,1], "fov": 3},
                           {"position": [1,0,0], "look_at": [0,0,1]}]})",
         "unknown key 'fov'"},
        {R"({"timing": "fast", "waypoints": []})", "timing"},
        {R"({"ease": "yes", "waypoints": []})", "ease"},
        {R"({"speed": 2, "waypoints": []})", "unknown key 'speed'"},
    };
    for (const auto& [text, expected] : cases) {
        try {
            rt::parse_camera_path(nlohmann::json::parse(text), 45.0f);
            ADD_FAILURE() << "accepted: " << text;
        } catch (const rt::Error& e) {
            EXPECT_NE(std::string(e.what()).find(expected), std::string::npos)
                << "for " << text << ": " << e.what();
        }
    }
    EXPECT_THROW(rt::load_camera_path("/nonexistent/path.json", 45.0f), rt::Error);
}
