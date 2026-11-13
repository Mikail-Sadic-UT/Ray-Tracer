/// @file machine_info.cpp
/// @brief Machine and build identity for results files.
#include "bench/machine_info.hpp"

#include <sys/utsname.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include "rt_build_info.hpp"

namespace rt {

namespace {

#if defined(__linux__) || defined(__APPLE__)

std::string trim(const std::string& s) {
    const std::size_t begin = s.find_first_not_of(" \t\r\n\"");
    if (begin == std::string::npos) return {};
    const std::size_t end = s.find_last_not_of(" \t\r\n\"");
    return s.substr(begin, end - begin + 1);
}

#endif  // __linux__ || __APPLE__

#if defined(__linux__)

/// Value of the first line `key<sep>value` in @p path, or empty.
std::string file_value(const char* path, const std::string& key, char separator) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, key.size(), key) != 0) continue;
        const std::size_t colon = line.find(separator, key.size());
        if (colon != std::string::npos &&
            trim(line.substr(key.size(), colon - key.size())).empty()) {
            return trim(line.substr(colon + 1));
        }
    }
    return {};
}

/// Value of the first `key: value` line in the output of `lscpu`, or empty.
std::string lscpu_value(const std::string& key) {
    FILE* pipe = popen("lscpu 2>/dev/null", "r");
    if (pipe == nullptr) return {};
    std::string output;
    char buffer[256];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) output += buffer;
    pclose(pipe);
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.compare(0, key.size(), key) == 0 && line.size() > key.size() &&
            line[key.size()] == ':') {
            return trim(line.substr(key.size() + 1));
        }
    }
    return {};
}

#endif  // __linux__

#if defined(__APPLE__)

/// String value of the sysctl @p name, or empty.
std::string sysctl_string(const char* name) {
    std::size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return {};
    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) return {};
    return trim(value.c_str());  // drops the terminating NUL
}

#endif  // __APPLE__

/// `uname` fields, which POSIX systems provide: used when no better source exists.
utsname system_name() {
    utsname name{};
    if (uname(&name) != 0) name = utsname{};
    return name;
}

std::string host_name() {
    char name[256] = {};
    if (gethostname(name, sizeof name - 1) != 0) return {};
    return name;
}

/// Version of the compiler this file was built with, `major.minor.patch`. Clang is checked first
/// because it also defines `__GNUC__`. The schema key is `gcc`; which compiler it was is in the
/// build's `cmake_flags` (`CMAKE_CXX_COMPILER`).
std::string compiler_version() {
#if defined(__clang__)
    return std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." +
           std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
    return std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
           std::to_string(__GNUC_PATCHLEVEL__);
#else
    return {};
#endif
}

/// CPU model name: `/proc/cpuinfo`, then `lscpu` on Linux (aarch64 `/proc/cpuinfo` has no model
/// name); the brand string on macOS; otherwise the architecture name from `uname`.
std::string cpu_model_name() {
    std::string model;
#if defined(__linux__)
    model = file_value("/proc/cpuinfo", "model name", ':');
    if (model.empty()) model = lscpu_value("Model name");
#elif defined(__APPLE__)
    model = sysctl_string("machdep.cpu.brand_string");
#endif
    if (model.empty()) model = system_name().machine;
    return model;
}

/// Operating system: `/etc/os-release` on Linux, `macOS <version>` on macOS; otherwise
/// `<sysname> <release>` from `uname`.
std::string os_name() {
    std::string os;
#if defined(__linux__)
    os = file_value("/etc/os-release", "PRETTY_NAME", '=');
#elif defined(__APPLE__)
    const std::string version = sysctl_string("kern.osproductversion");
    if (!version.empty()) os = "macOS " + version;
#endif
    if (os.empty()) {
        const utsname name = system_name();
        os = std::string(name.sysname) + " " + name.release;
        if (os == " ") os.clear();
    }
    return os;
}

}  // namespace

MachineInfo collect_machine_info() {
    MachineInfo info;
    info.host = host_name();
    info.cpu_model = cpu_model_name();
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    info.cpu_cores = online > 0 ? static_cast<int>(online)
                                : static_cast<int>(std::thread::hardware_concurrency());
    info.nvcc = RT_BUILD_NVCC;
    info.gcc = compiler_version();
    info.os = os_name();
    return info;
}

BuildInfo collect_build_info() {
    BuildInfo info;
    info.commit = RT_BUILD_COMMIT;
    info.dirty = RT_BUILD_DIRTY;
    info.cmake_flags = RT_BUILD_CMAKE_FLAGS;
    return info;
}

}  // namespace rt
