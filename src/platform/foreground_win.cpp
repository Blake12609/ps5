#include "platform/foreground.hpp"

#include <windows.h>

#include <cstring>
#include <iterator>

namespace edgepad {
namespace {

std::string fileNameOfProcess(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[1024];
    DWORD length = static_cast<DWORD>(std::size(path));
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!ok || length == 0) return {};
    const wchar_t* name = path;
    for (DWORD i = 0; i < length; ++i) {
        if (path[i] == L'\\' || path[i] == L'/') name = path + i + 1;
    }
    const int nameLength = static_cast<int>(path + length - name);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, name, nameLength, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(bytes > 0 ? bytes : 0), '\0');
    if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, name, nameLength, out.data(), bytes, nullptr, nullptr);
    return out;
}

// Store apps and some games run inside ApplicationFrameHost.exe: the real program owns a child window.
DWORD hostedProcess(HWND window, DWORD host) {
    struct Search {
        DWORD host;
        DWORD found;
    } search{host, 0};
    EnumChildWindows(
        window,
        [](HWND child, LPARAM param) -> BOOL {
            auto* s = reinterpret_cast<Search*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(child, &pid);
            if (pid != 0 && pid != s->host) {
                s->found = pid;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

}  // namespace

struct ForegroundWatcher::Impl {
    HWND window = nullptr;
    DWORD pid = 0;
    App app;
};

ForegroundWatcher::ForegroundWatcher() : impl_(std::make_unique<Impl>()) {}
ForegroundWatcher::~ForegroundWatcher() = default;

bool ForegroundWatcher::supported() const { return true; }

ForegroundWatcher::App ForegroundWatcher::current() {
    Impl& s = *impl_;
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    if (window == s.window && pid == s.pid) return s.app;  // same window: no lookup
    s.window = window;
    s.pid = pid;
    s.app = {};
    if (pid == 0) return s.app;
    if (pid == GetCurrentProcessId()) {
        s.app.self = true;
        return s.app;
    }
    s.app.name = fileNameOfProcess(pid);
    if (_stricmp(s.app.name.c_str(), "ApplicationFrameHost.exe") == 0) {
        const DWORD hosted = hostedProcess(window, pid);
        // The child window can appear a moment later: look again next time.
        s.app.name = hosted != 0 ? fileNameOfProcess(hosted) : std::string{};
        if (hosted == 0) s.window = nullptr;
    }
    return s.app;
}

}  // namespace edgepad
