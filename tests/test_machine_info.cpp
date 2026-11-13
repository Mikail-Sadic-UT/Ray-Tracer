/// @file test_machine_info.cpp
/// @brief Tests for machine and build identity collection.
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
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

/// Checks that the compiler version is `major.minor.patch` and is the compiler this test was
/// built with (the library and the test share one toolchain). Clang defines `__GNUC__` too.
TEST(MachineInfo, RecordsTheHostCompilerVersion) {
    const std::string version = rt::collect_machine_info().gcc;
    ASSERT_FALSE(version.empty());
    EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(version[0]))) << version;
    EXPECT_EQ(std::count(version.begin(), version.end(), '.'), 2) << version;
#if defined(__clang__)
    EXPECT_EQ(version, std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) +
                           "." + std::to_string(__clang_patchlevel__));
#elif defined(__GNUC__)
    EXPECT_EQ(version, std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
                           std::to_string(__GNUC_PATCHLEVEL__));
#endif
}

/// Checks that the OS and CPU strings are clean single-line values, with no stray whitespace
/// or quotes left from parsing `/etc/os-release` or `lscpu` output.
TEST(MachineInfo, StringsAreTrimmedSingleLines) {
    const rt::MachineInfo info = rt::collect_machine_info();
    for (const std::string* field : {&info.host, &info.cpu_model, &info.os}) {
        ASSERT_FALSE(field->empty());
        EXPECT_FALSE(std::isspace(static_cast<unsigned char>(field->front()))) << *field;
        EXPECT_FALSE(std::isspace(static_cast<unsigned char>(field->back()))) << *field;
        EXPECT_EQ(field->find('\n'), std::string::npos) << *field;
        EXPECT_NE(field->front(), '"') << *field;
        EXPECT_NE(field->back(), '"') << *field;
    }
}

#if defined(__linux__)
/// Linux only: the OS is `PRETTY_NAME` from `/etc/os-release`, read here without the library's
/// parser.
TEST(MachineInfo, LinuxOsMatchesOsRelease) {
    std::ifstream os_release("/etc/os-release");
    if (!os_release) GTEST_SKIP() << "no /etc/os-release on this system";
    const std::string key = "PRETTY_NAME=";
    std::string line;
    std::string expected;
    while (std::getline(os_release, line)) {
        if (line.rfind(key, 0) != 0) continue;
        expected = line.substr(key.size());
        if (expected.size() >= 2 && expected.front() == '"' && expected.back() == '"') {
            expected = expected.substr(1, expected.size() - 2);
        }
        break;
    }
    if (expected.empty()) GTEST_SKIP() << "no PRETTY_NAME in /etc/os-release";
    EXPECT_EQ(rt::collect_machine_info().os, expected);
}
#endif

#if defined(__APPLE__)
/// macOS only: the OS is `macOS <version>` and the CPU model is the sysctl brand string, not
/// just the architecture name.
TEST(MachineInfo, MacOsUsesSysctl) {
    const rt::MachineInfo info = rt::collect_machine_info();
    EXPECT_EQ(info.os.rfind("macOS ", 0), 0u) << info.os;
    EXPECT_NE(info.cpu_model, "arm64");
    EXPECT_NE(info.cpu_model, "x86_64");
}
#endif

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
