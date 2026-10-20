#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lightyt::paths {

// Returns the application data directory (e.g. %LOCALAPPDATA%\LightYT or ~/.local/share/LightYT)
[[nodiscard]] std::filesystem::path getAppDataDir();

// Returns the cache directory. Honors LIGHTYT_CACHE_DIR env var if set.
[[nodiscard]] std::filesystem::path getCacheDir();

// Returns the thumbnail cache directory (getCacheDir() / "thumbnails")
[[nodiscard]] std::filesystem::path getThumbnailCacheDir();

// Returns the directory containing the current executable
[[nodiscard]] std::filesystem::path getExecutableDir();

// Returns the settings file path (getAppDataDir() / "settings.ini")
[[nodiscard]] std::filesystem::path getSettingsFilePath();

// Ensures a directory exists, creating all parent directories if necessary
bool ensureDirExists(const std::filesystem::path &dir);

// Returns list of local/bundled candidate paths for a filename (exeDir/lib, parent/lib, lib, exeDir)
[[nodiscard]] std::vector<std::filesystem::path> getLocalCandidatePaths(std::string_view filename);

// Searches local candidate paths and optional fallback paths for an existing regular file
[[nodiscard]] std::filesystem::path findCandidateFile(std::string_view filename,
                                                      const std::vector<std::filesystem::path> &fallbackPaths = {});

}  // namespace lightyt::paths
