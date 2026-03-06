#pragma once

#include <string>

namespace infengine
{
struct InfAppMetadata
{
    const char *appName;
    int versionMajor;
    int versionMinor;
    int versionPatch;
    const char *appID;

    InfAppMetadata()
    {
        appName = nullptr;
        versionMajor = 0;
        versionMinor = 0;
        versionPatch = 0;
        appID = nullptr;
    }
    InfAppMetadata(const char *appName, int versionMajor, int versionMinor, int versionPatch, const char *appID)
        : appName(appName), versionMajor(versionMajor), versionMinor(versionMinor), versionPatch(versionPatch),
          appID(appID)
    {
    }

    /// @brief Returns a "major.minor.patch" version string.
    std::string GetVersionString() const
    {
        return std::to_string(versionMajor) + "." + std::to_string(versionMinor) + "." + std::to_string(versionPatch);
    }

    /// @brief Returns a full "AppName major.minor.patch (appID)" info string.
    std::string GetInfoString() const
    {
        std::string s;
        if (appName)
            s += appName;
        s += " " + GetVersionString();
        if (appID) {
            s += " (";
            s += appID;
            s += ")";
        }
        return s;
    }
};
} // namespace infengine
