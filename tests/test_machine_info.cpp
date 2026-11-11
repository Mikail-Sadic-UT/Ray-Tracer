/// @file test_machine_info.cpp
/// @brief Tests for machine and build identity collection.
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "bench/machine_info.hpp"

/// Checks that the CPU-side fields are filled and the GPU fields are left to CUDA targets.
TEST(MachineInfo, DescribesHostAndLeavesGpuFieldsEmpty) {
    const rt::MachineInfo info = rt::collect_machine_info();
    EXPECT_FALSE(info.host.empty());
    EXPECT_FALSE(info.cpu_model.empty());
    EXPECT_GE(info.cpu_cores, 1);
    EXPECT_FALSE(info.os.empty());
    EXPECT_TRUE(info.gpu_name.empty());
    EXPECT_TRUE(info.gpu_uuid.empty());
    EXPECT_TRUE(info.driver.empty());
    EXPECT_TRUE(info.cuda_runtime.empty());
}

/// Checks that the compiler version is `major.minor.patch` (this repo builds with GCC).
TEST(MachineInfo, RecordsTheHostCompilerVersion) {
    const std::string gcc = rt::collect_machine_info().gcc;
    ASSERT_FALSE(gcc.empty());
    EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(gcc[0]))) << gcc;
    EXPECT_EQ(std::count(gcc.begin(), gcc.end(), '.'), 2) << gcc;
}

/// Checks the build identity: a full commit hash or "unknown", and the options string.
TEST(BuildInfo, RecordsCommitAndOptions) {
    const rt::BuildInfo info = rt::collect_build_info();
    if (info.commit != "unknown") {
        EXPECT_EQ(info.commit.size(), 40u) << info.commit;
        for (char c : info.commit) EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)));
    }
    EXPECT_NE(info.cmake_flags.find("CMAKE_BUILD_TYPE="), std::string::npos) << info.cmake_flags;
    EXPECT_NE(info.cmake_flags.find("RT_ENABLE_CUDA="), std::string::npos) << info.cmake_flags;
}
