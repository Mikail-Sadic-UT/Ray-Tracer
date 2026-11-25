/// @file test_obj_io.cpp
/// @brief Tests for the OBJ reader and mesh placement used by obj2bin (E6).
#include <gtest/gtest.h>

#include <cmath>
#include <sstream>

#include "core/error.hpp"
#include "scene/obj_io.hpp"

namespace {

rt::ObjMesh parse(const std::string& text) {
    std::istringstream input(text);
    return rt::parse_obj(input, "test.obj");
}

/// A template scene with two materials and no geometry.
rt::SceneHost stage() {
    rt::SceneHost scene;
    scene.materials.push_back({{0.5f, 0.5f, 0.5f}, {}, 1.0f, 0u});
    scene.materials.push_back({{0.1f, 0.1f, 0.1f}, {0.9f, 0.9f, 0.9f}, 1.0f, rt::MAT_MIRROR});
    return scene;
}

}  // namespace

TEST(ObjIo, ReadsVerticesAndAllCornerForms) {
    const rt::ObjMesh mesh = parse(
        "# comment\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nvt 0 0\n"
        "o name\nf 1 2 3\nf 1/1 2/1 3/1\nf 1//1 2//1 3//1\nf 1/1/1 2/1/1 3/1/1\nf -3 -2 -1\n");
    ASSERT_EQ(mesh.vertices.size(), 3u);
    EXPECT_FLOAT_EQ(mesh.vertices[1].x, 1.0f);
    ASSERT_EQ(mesh.faces.size(), 5u);
    for (const auto& f : mesh.faces) {
        EXPECT_EQ(f[0], 0u);
        EXPECT_EQ(f[1], 1u);
        EXPECT_EQ(f[2], 2u);
    }
}

TEST(ObjIo, SplitsPolygonsIntoAFanKeepingWinding) {
    const rt::ObjMesh mesh = parse("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv -1 1 0\nf 1 2 3 4 5\n");
    ASSERT_EQ(mesh.faces.size(), 3u);
    EXPECT_EQ(mesh.faces[0], (std::array<uint32_t, 3>{0u, 1u, 2u}));
    EXPECT_EQ(mesh.faces[1], (std::array<uint32_t, 3>{0u, 2u, 3u}));
    EXPECT_EQ(mesh.faces[2], (std::array<uint32_t, 3>{0u, 3u, 4u}));
}

TEST(ObjIo, RejectsBadInputNamingTheLine) {
    const char* bad[] = {"v 1 2\n",
                         "v 0 0 0\nv 1 0 0\nf 1 2\n",
                         "v 0 0 0\nf 1 2 3\n",
                         "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n",
                         "v 0 0 0\nf 1 x 1\n",
                         "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -4 1 2\n"};
    for (const char* text : bad) {
        try {
            parse(text);
            ADD_FAILURE() << "accepted: " << text;
        } catch (const rt::Error& e) {
            EXPECT_NE(std::string(e.what()).find("test.obj:"), std::string::npos) << e.what();
        }
    }
    EXPECT_THROW(rt::load_obj("/nonexistent/mesh.obj"), rt::Error);
}

TEST(ObjIo, PlacementScalesRotatesAndSetsTheBase) {
    // A 4 x 2 x 1 box corner set; the largest extent (x) scales to `size`.
    const rt::ObjMesh mesh = parse("v 10 5 7\nv 14 5 7\nv 10 7 8\nf 1 2 3\n");
    rt::SceneHost scene = stage();
    rt::append_mesh(scene, mesh, {2.0f, 0.0f, {1.0f, 0.5f, -1.0f}, 1u});
    ASSERT_EQ(scene.triangles.size(), 1u);
    const rt::Triangle& t = scene.triangles[0];
    EXPECT_EQ(t.material_id, 1u);
    // Scale 0.5; bottom (y = 5) lands at 0.5 and the x/z box center at (1, -1).
    EXPECT_FLOAT_EQ(t.v0.x, 0.0f);
    EXPECT_FLOAT_EQ(t.v1.x, 2.0f);
    EXPECT_FLOAT_EQ(t.v0.y, 0.5f);
    EXPECT_FLOAT_EQ(t.v2.y, 1.5f);
    EXPECT_FLOAT_EQ(t.v0.z, -1.25f);
    EXPECT_FLOAT_EQ(t.v2.z, -0.75f);

    // A quarter turn about +Y maps +x to -z; the winding (and so the facing) is kept.
    rt::SceneHost turned = stage();
    rt::append_mesh(turned, mesh, {2.0f, 90.0f, {0.0f, 0.0f, 0.0f}, 0u});
    const rt::Triangle& r = turned.triangles[0];
    EXPECT_NEAR(r.v1.z - r.v0.z, -2.0f, 1e-6f);
    EXPECT_NEAR(r.v1.x - r.v0.x, 0.0f, 1e-6f);
    const rt::vec3 n0 = rt::cross(t.v1 - t.v0, t.v2 - t.v0);
    const rt::vec3 n1 = rt::cross(r.v1 - r.v0, r.v2 - r.v0);
    EXPECT_NEAR(n0.y, n1.y, 1e-6f);
}

TEST(ObjIo, PlacementRejectsBadMeshesAndSettings) {
    const rt::ObjMesh tri = parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    rt::SceneHost scene = stage();
    EXPECT_THROW(rt::append_mesh(scene, parse("v 0 0 0\n"), {}), rt::Error);
    EXPECT_THROW(rt::append_mesh(scene, parse("v 1 1 1\nv 1 1 1\nv 1 1 1\nf 1 2 3\n"), {}),
                 rt::Error);
    EXPECT_THROW(rt::append_mesh(scene, tri, {0.0f, 0.0f, {}, 0u}), rt::Error);
    EXPECT_THROW(rt::append_mesh(scene, tri, {1.0f, 0.0f, {}, 2u}), rt::Error);
    EXPECT_TRUE(scene.triangles.empty());
}

TEST(ObjIo, GroupsNameFacesAndTakeTheirOwnMaterial) {
    const rt::ObjMesh mesh = parse(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\no head\nf 1 2 4\ng body extra\nf 1 3 4\n"
        "o head\nf 2 3 4\n");
    ASSERT_EQ(mesh.groups.size(), 3u);
    EXPECT_EQ(mesh.groups[0], "default");
    EXPECT_EQ(mesh.groups[1], "head");
    EXPECT_EQ(mesh.groups[2], "body");
    EXPECT_EQ(mesh.face_group, (std::vector<uint32_t>{0u, 1u, 2u, 1u}));

    rt::SceneHost scene = stage();
    rt::MeshPlacement placement;
    placement.material_id = 0u;
    placement.group_materials = {{"head", 1u}};
    rt::append_mesh(scene, mesh, placement);
    ASSERT_EQ(scene.triangles.size(), 4u);
    EXPECT_EQ(scene.triangles[0].material_id, 0u);
    EXPECT_EQ(scene.triangles[1].material_id, 1u);
    EXPECT_EQ(scene.triangles[2].material_id, 0u);
    EXPECT_EQ(scene.triangles[3].material_id, 1u);

    rt::SceneHost other = stage();
    placement.group_materials = {{"tail", 1u}};
    EXPECT_THROW(rt::append_mesh(other, mesh, placement), rt::Error);
    placement.group_materials = {{"head", 7u}};
    EXPECT_THROW(rt::append_mesh(other, mesh, placement), rt::Error);
    EXPECT_TRUE(other.triangles.empty());
}
