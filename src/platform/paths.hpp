#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace edgepad {

// Remembers the command line so the app can relaunch itself after an update.
void setLaunchArguments(int argc, char** argv);
const std::vector<std::string>& launchArguments();

// UTF-8 text for a path (path::string() can throw on Windows for non-ASCII folder names).
std::string toUtf8(const std::filesystem::path& path);

std::filesystem::path executablePath();
std::filesystem::path executableDirectory();

// Portable data folder: "EdgePad-data" next to the executable. Falls back to the
// per-user config folder when the executable's folder is read-only.
std::filesystem::path dataDirectory(const std::filesystem::path& overrideDir = {});

// Runs the calling thread ahead of normal programs (games included), so a busy CPU never delays a
// controller report. Windows only; elsewhere it needs privileges, so it does nothing.
void raiseThreadPriority();

bool openInFileBrowser(const std::filesystem::path& path);
bool openUrl(const std::string& url);

// Keeps two copies of the app from fighting over the same controller.
class SingleInstanceLock {
public:
    SingleInstanceLock() = default;
    ~SingleInstanceLock();
    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

    // Retries for up to `waitMs` (used when relaunching after an update).
    bool acquire(const std::filesystem::path& dataDir, int waitMs);

private:
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

}  // namespace edgepad
