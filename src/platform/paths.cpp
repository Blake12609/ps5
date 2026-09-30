#include "platform/paths.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace edgepad {
namespace {

std::vector<std::string>& storedArguments() {
    static std::vector<std::string> args;
    return args;
}

bool isWritableDirectory(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) return false;
    const fs::path probe = dir / ".write-test";
    {
        std::ofstream f(probe);
        if (!f) return false;
    }
    fs::remove(probe, ec);
    return true;
}

fs::path userConfigDirectory() {
#if defined(_WIN32)
    if (const wchar_t* appData = _wgetenv(L"APPDATA")) return fs::path(appData) / "EdgePad";
    return fs::temp_directory_path() / "EdgePad";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return fs::path(xdg) / "edgepad";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".config" / "edgepad";
    return fs::temp_directory_path() / "edgepad";
#endif
}

#if !defined(_WIN32)
bool spawnDetached(const std::vector<std::string>& args) {
    std::vector<std::string> copy = args;
    std::vector<char*> argv;
    for (auto& a : copy) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return false;
    return true;
}
#endif

}  // namespace

std::string toUtf8(const fs::path& path) {
#if defined(_WIN32)
    const std::wstring& wide = path.native();
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
#else
    return path.string();
#endif
}

void setLaunchArguments(int argc, char** argv) {
    auto& args = storedArguments();
    args.clear();
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
}

const std::vector<std::string>& launchArguments() { return storedArguments(); }

fs::path executablePath() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) {
            buffer.resize(n);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path{} : p;
#endif
}

fs::path executableDirectory() { return executablePath().parent_path(); }

fs::path dataDirectory(const fs::path& overrideDir) {
    if (!overrideDir.empty()) {
        std::error_code ec;
        fs::create_directories(overrideDir, ec);
        return overrideDir;
    }
    const fs::path portable = executableDirectory() / "EdgePad-data";
    if (isWritableDirectory(portable)) return portable;
    const fs::path fallback = userConfigDirectory();
    std::error_code ec;
    fs::create_directories(fallback, ec);
    return fallback;
}

bool openInFileBrowser(const fs::path& path) {
#if defined(_WIN32)
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
    return spawnDetached({"xdg-open", path.string()});
#endif
}

bool openUrl(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;
#if defined(_WIN32)
    const std::wstring wide(url.begin(), url.end());
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
    return spawnDetached({"xdg-open", url});
#endif
}

SingleInstanceLock::~SingleInstanceLock() {
#if defined(_WIN32)
    if (handle_) {
        ReleaseMutex(static_cast<HANDLE>(handle_));
        CloseHandle(static_cast<HANDLE>(handle_));
    }
#else
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        close(fd_);
    }
#endif
}

bool SingleInstanceLock::acquire(const fs::path& dataDir, int waitMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
#if defined(_WIN32)
    (void)dataDir;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\EdgePad.SingleInstance");
    if (!mutex) return true;  // cannot tell; do not block the user
    for (;;) {
        const DWORD r = WaitForSingleObject(mutex, 0);
        if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) {
            handle_ = mutex;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            CloseHandle(mutex);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#else
    const fs::path lockFile = dataDir / ".instance.lock";
    const int fd = open(lockFile.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return true;
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            fd_ = fd;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            close(fd);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif
}

}  // namespace edgepad
