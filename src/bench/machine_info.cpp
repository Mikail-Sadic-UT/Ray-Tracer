/// @file machine_info.cpp
/// @brief Machine and build identity for results files.
#include "bench/machine_info.hpp"

#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "rt_build_info.hpp"

namespace rt {

namespace {

std::string trim(const std::string& s) {
    const std::size_t begin = s.find_first_not_of(" \t\r\n\"");
    if (begin == std::string::npos) return {};
    const std::size_t end = s.find_last_not_of(" \t\r\n\"");
    return s.substr(begin, end - begin + 1);
}

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

std::string host_name() {
    char name[256] = {};
    if (gethostname(name, sizeof name - 1) != 0) return {};
    return name;
}

std::string compiler_version() {
#if defined(__GNUC__) && !defined(__clang__)
    return std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
           std::to_string(__GNUC_PATCHLEVEL__);
#else
    return {};
#endif
}

}  // namespace

MachineInfo collect_machine_info() {
    MachineInfo info;
    info.host = host_name();
    // x86 /proc/cpuinfo names the model; aarch64 does not, so fall back to lscpu there.
    info.cpu_model = file_value("/proc/cpuinfo", "model name", ':');
    if (info.cpu_model.empty()) info.cpu_model = lscpu_value("Model name");
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    info.cpu_cores = online > 0 ? static_cast<int>(online) : 0;
    info.nvcc = RT_BUILD_NVCC;
    info.gcc = compiler_version();
    info.os = file_value("/etc/os-release", "PRETTY_NAME", '=');
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
