/// @file test_camera_io.cpp
/// @brief Tests for camera JSON parsing, encoding, and validation.
#include <gtest/gtest.h>

#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "camera/camera_io.hpp"
#include "core/error.hpp"

namespace {

using nlohmann::json;

json valid_json() {
    return {{"position", {0, 1, 5}}, {"look_at", {0, 0.5, 0}}, {"up", {0, 1, 0}},
            {"vfov_deg", 45},        {"width", 640},           {"height", 480}};
}

rt::Camera valid_camera() { return rt::parse_camera(valid_json()); }

/// Requires rt::Error with a message naming the offending field.
void expect_error(const std::function<void()>& action, const std::string& field) {
    try {
        action();
        FAIL() << "Expected rt::Error naming " << field;
    } catch (const rt::Error& error) {
        EXPECT_NE(std::string(error.what()).find(field), std::string::npos) << error.what();
    }
}

}  // namespace

/// Checks that every field is read into the matching Camera member.
TEST(CameraIoTest, ParsesAllFields) {
    const rt::Camera camera = valid_camera();
    EXPECT_EQ(camera.position.y, 1.0f);
    EXPECT_EQ(camera.position.z, 5.0f);
    EXPECT_EQ(camera.look_at.y, 0.5f);
    EXPECT_EQ(camera.up.y, 1.0f);
    EXPECT_EQ(camera.vfov_deg, 45.0f);
    EXPECT_EQ(camera.width, 640u);
    EXPECT_EQ(camera.height, 480u);
    EXPECT_NO_THROW(rt::validate_camera(camera));
}

/// Checks that encoding then parsing returns bit-identical values.
TEST(CameraIoTest, JsonRoundTripPreservesValues) {
    rt::Camera camera = valid_camera();
    camera.position = {0.1f, -3.3333333f, 1.0e-30f};
    camera.vfov_deg = 33.3f;
    const rt::Camera back = rt::parse_camera(rt::camera_to_json(camera));
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(back.position[i], camera.position[i]);
        EXPECT_EQ(back.look_at[i], camera.look_at[i]);
        EXPECT_EQ(back.up[i], camera.up[i]);
    }
    EXPECT_EQ(back.vfov_deg, camera.vfov_deg);
    EXPECT_EQ(back.width, camera.width);
    EXPECT_EQ(back.height, camera.height);
}

/// Checks that missing fields and wrong types are rejected with the field name.
TEST(CameraIoTest, RejectsMalformedJson) {
    expect_error([] { rt::parse_camera(json(nullptr)); }, "camera");
    for (const char* key : {"position", "look_at", "up", "vfov_deg", "width", "height"}) {
        json document = valid_json();
        document.erase(key);
        expect_error([&] { rt::parse_camera(document); }, std::string("camera.") + key);
    }
    struct Case {
        const char* pointer;
        json value;
        const char* field;
    };
    const std::vector<Case> cases = {
        {"/width", -1, "camera.width"},
        {"/width", 4294967296ull, "camera.width"},
        {"/width", 640.5, "camera.width"},
        {"/height", true, "camera.height"},
        {"/vfov_deg", "45", "camera.vfov_deg"},
        {"/vfov_deg", 1.0e39, "camera.vfov_deg"},
        {"/position", json::array({0, 1}), "camera.position"},
        {"/position/0", "0", "camera.position[0]"},
        {"/up", 1, "camera.up"},
    };
    for (const auto& c : cases) {
        json document = valid_json();
        document[json::json_pointer(c.pointer)] = c.value;
        expect_error([&] { rt::parse_camera(document); }, c.field);
    }
}

/// Checks each camera and resolution rule from INTERFACES section 2.
TEST(CameraIoTest, ValidationRejectsInvalidCameras) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    struct Case {
        std::function<void(rt::Camera&)> mutate;
        const char* field;
    };
    const std::vector<Case> cases = {
        {[](auto& c) { c.look_at = c.position; }, "camera.look_at"},
        {[](auto& c) { c.up = {0, 0, 0}; }, "camera.up"},
        {[](auto& c) { c.up = c.position - c.look_at; }, "camera.up"},
        {[](auto& c) { c.up = c.look_at - c.position; }, "camera.up"},
        {[](auto& c) { c.vfov_deg = 0; }, "camera.vfov_deg"},
        {[](auto& c) { c.vfov_deg = 180; }, "camera.vfov_deg"},
        {[](auto& c) { c.vfov_deg = -10; }, "camera.vfov_deg"},
        {[&](auto& c) { c.vfov_deg = inf; }, "camera.vfov_deg"},
        {[&](auto& c) { c.position.z = nan; }, "camera.position[2]"},
        {[&](auto& c) { c.look_at.y = inf; }, "camera.look_at[1]"},
        {[&](auto& c) { c.up.x = nan; }, "camera.up[0]"},
        {[](auto& c) { c.width = 0; }, "camera.width"},
        {[](auto& c) { c.width = 16385; }, "camera.width"},
        {[](auto& c) { c.height = 0; }, "camera.height"},
        {[](auto& c) { c.height = 16385; }, "camera.height"},
    };
    for (size_t i = 0; i < cases.size(); ++i) {
        SCOPED_TRACE(::testing::Message() << "case " << i);
        rt::Camera camera = valid_camera();
        cases[i].mutate(camera);
        expect_error([&] { rt::validate_camera(camera); }, cases[i].field);
    }
}

/// Checks the inclusive resolution limits and a nearly (but not too) parallel up vector.
TEST(CameraIoTest, ValidationAcceptsBoundaryValues) {
    rt::Camera camera = valid_camera();
    camera.width = 1;
    camera.height = 16384;
    EXPECT_NO_THROW(rt::validate_camera(camera));
    camera.width = 16384;
    camera.height = 1;
    EXPECT_NO_THROW(rt::validate_camera(camera));

    camera.position = {0, 0, 1};
    camera.look_at = {0, 0, 0};
    camera.up = {0, 1.0e-7f, 1};
    expect_error([&] { rt::validate_camera(camera); }, "camera.up");
    camera.up = {0, 2.0e-6f, 1};
    EXPECT_NO_THROW(rt::validate_camera(camera));
}

/// Checks that validation also catches values set by a caller-side override.
TEST(CameraIoTest, ValidationAppliesAfterOverrides) {
    rt::Camera camera = valid_camera();
    camera.width = 20000;  // e.g. --width 20000
    expect_error([&] { rt::validate_camera(camera); }, "camera.width");
}
