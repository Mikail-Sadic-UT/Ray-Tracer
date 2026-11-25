rt_add_gtest(test_scene SOURCES test_scene.cpp test_scene_io.cpp test_binary_io.cpp test_generator.cpp
  test_obj_io.cpp LIBS rt_scene)
target_compile_definitions(test_scene PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")

add_test(NAME scene_dump_known COMMAND scene_dump --verify
  "${PROJECT_SOURCE_DIR}/scenes/known/known_two_spheres.json")
set_tests_properties(scene_dump_known PROPERTIES
  PASS_REGULAR_EXPRESSION "scene_hash: 0xcbed9151089614c9")
add_test(NAME scene_dump_missing COMMAND scene_dump "${CMAKE_CURRENT_BINARY_DIR}/missing.json")
set_tests_properties(scene_dump_missing PROPERTIES WILL_FAIL TRUE)

add_test(NAME fetch_scenes_archive COMMAND python3
  "${PROJECT_SOURCE_DIR}/tests/test_fetch_scenes.py"
  --scene-dump $<TARGET_FILE:scene_dump>
  --fetch-script "${PROJECT_SOURCE_DIR}/scripts/fetch_scenes.sh"
  --known-scene "${PROJECT_SOURCE_DIR}/scenes/known/known_two_spheres.json")

# obj2bin (E6): a tetrahedron placed in a known scene loads back with 4 more triangles.
add_test(NAME obj2bin_tetrahedron COMMAND sh -c
  "$<TARGET_FILE:obj2bin> --obj '${PROJECT_SOURCE_DIR}/tests/data/tetrahedron.obj' \
   --template '${PROJECT_SOURCE_DIR}/scenes/known/known_two_spheres.json' \
   --out '${CMAKE_CURRENT_BINARY_DIR}/obj2bin_tetra.json' --size 0.5 --at 0 0 1 --rotate-y 30 \
   && $<TARGET_FILE:scene_dump> --verify '${CMAKE_CURRENT_BINARY_DIR}/obj2bin_tetra.json'")
set_tests_properties(obj2bin_tetrahedron PROPERTIES PASS_REGULAR_EXPRESSION "triangles: 6")

# glb2obj.py: node transforms baked in, connected pieces written as OBJ groups.
add_test(NAME glb2obj_py COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_glb2obj.py"
  "${PROJECT_SOURCE_DIR}/scripts/glb2obj.py")

# obj2bin --group-material: a named group takes its material; a name the mesh lacks is an error.
add_test(NAME obj2bin_group_material COMMAND sh -c
  "$<TARGET_FILE:obj2bin> --obj '${PROJECT_SOURCE_DIR}/tests/data/two_parts.obj' \
   --template '${PROJECT_SOURCE_DIR}/scenes/known/known_two_spheres.json' \
   --out '${CMAKE_CURRENT_BINARY_DIR}/obj2bin_parts.json' --group-material back=1 \
   && $<TARGET_FILE:scene_dump> --verify '${CMAKE_CURRENT_BINARY_DIR}/obj2bin_parts.json'")
set_tests_properties(obj2bin_group_material PROPERTIES PASS_REGULAR_EXPRESSION "triangles: 6")
add_test(NAME obj2bin_unknown_group COMMAND obj2bin
  --obj "${PROJECT_SOURCE_DIR}/tests/data/two_parts.obj"
  --template "${PROJECT_SOURCE_DIR}/scenes/known/known_two_spheres.json"
  --out "${CMAKE_CURRENT_BINARY_DIR}/obj2bin_nogroup.json" --group-material nothing=1)
set_tests_properties(obj2bin_unknown_group PROPERTIES WILL_FAIL TRUE)
