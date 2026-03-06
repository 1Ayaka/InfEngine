#pragma once

#include <core/log/InfLog.h>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>

namespace infengine
{
inline const char *GetExecutableDir()
{
    static std::string path;
    static bool initialized = false;

    if (initialized) {
        return path.c_str();
    }

    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (len == 0) {
        INFLOG_ERROR("Failed to get executable path, using current directory as fallback.");
        path = ".";
    } else {
        path = std::filesystem::path(buffer).parent_path().string();
    }
    initialized = true;
    return path.c_str();
}

} // namespace infengine
#else
#include <limits.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace infengine
{
inline const char *GetExecutableDir()
{
    static std::string path;
    static bool initialized = false;

    if (initialized) {
        return path.c_str();
    }

    char result[PATH_MAX];
    ssize_t len = 0;

#if defined(__linux__)
    len = readlink("/proc/self/exe", result, PATH_MAX);
#elif defined(__APPLE__)
    uint32_t size = sizeof(result);
    if (_NSGetExecutablePath(result, &size) != 0) {
        INFLOG_ERROR("Buffer too small for executable path, using current directory as fallback.");
        path = ".";
        initialized = true;
        return path.c_str();
    }
    len = strlen(result);
#endif

    if (len <= 0) {
        INFLOG_ERROR("Failed to get executable path, using current directory as fallback.");
        path = ".";
    } else {
        path = std::filesystem::path(result, result + len).parent_path().string();
    }
    initialized = true;
    return path.c_str();
}
} // namespace infengine
#endif
namespace infengine
{
inline const char *JoinPath(std::initializer_list<const char *> parts)
{
    static std::string result;
    std::filesystem::path path;

    for (const auto &part : parts) {
        path /= part;
    }
    result = path.string();
    std::replace(result.begin(), result.end(), '\\', '/');
    return result.c_str();
}

inline const char *JoinPath(std::initializer_list<std::string> parts)
{
    static std::string result;
    std::filesystem::path path;

    for (const auto &part : parts) {
        path /= part;
    }
    result = path.string();
    std::replace(result.begin(), result.end(), '\\', '/');
    return result.c_str();
}
} // namespace infengine