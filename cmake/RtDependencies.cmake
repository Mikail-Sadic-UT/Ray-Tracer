# Third-party dependencies, pinned to exact release archives with SHA-256 hashes (BUILD.md).
# tinyobjloader (obj2bin, Extension E6) is added when that work starts.
include(FetchContent)

FetchContent_Declare(json
  URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
FetchContent_Declare(cli11
  URL      https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.7.2.tar.gz
  URL_HASH SHA256=46eef3101da70852ec7af026e09d485ccee81813331c8c6052d39344443b83da
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
FetchContent_Declare(stb  # no releases; pinned to a commit
  URL      https://github.com/nothings/stb/archive/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5.tar.gz
  URL_HASH SHA256=a6c31950b3bdf05eb92258768ba2a11bd44449490574b1aac67028452288c40d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
FetchContent_MakeAvailable(json cli11 stb)

# stb has no CMake project: expose its headers as an INTERFACE target named stb.
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

if(RT_BUILD_TESTS)
  FetchContent_Declare(googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
