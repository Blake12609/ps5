#include "platform/self_update.hpp"

#include <system_error>
#include <vector>

#include "platform/paths.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <spawn.h>
#include <sys/stat.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace edgepad {
namespace {

fs::path withSuffix(const fs::path& p, const char* suffix) {
    fs::path out = p;
    out += suffix;
    return out;
}

#if defined(_WIN32)
std::wstring quoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(c);
            backslashes = 0;
            continue;
        } else {
            backslashes = 0;
        }
        out.push_back(c);
    }
    out.append(backslashes, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
#endif

}  // namespace

fs::path stagedUpdatePath() { return withSuffix(executablePath(), ".new"); }

void cleanupOldBinary() {
    std::error_code ec;
    fs::remove(withSuffix(executablePath(), ".old"), ec);
    fs::remove(stagedUpdatePath(), ec);
}

bool installUpdate(const fs::path& newBinary, std::string& error) {
    const fs::path exe = executablePath();
    if (exe.empty()) {
        error = "cannot locate the running executable";
        return false;
    }
    const fs::path old = withSuffix(exe, ".old");
    std::error_code ec;
    fs::remove(old, ec);
    // Renaming a running executable is allowed on Windows and Linux; deleting it is not (Windows).
    fs::rename(exe, old, ec);
    if (ec) {
        error = "cannot move the current version aside: " + ec.message();
        return false;
    }
    fs::rename(newBinary, exe, ec);
    if (ec) {
        std::error_code restoreEc;
        fs::rename(old, exe, restoreEc);
        error = "cannot put the new version in place: " + ec.message();
        return false;
    }
#if !defined(_WIN32)
    chmod(exe.c_str(), 0755);
#endif
    return true;
}

bool relaunch(std::string& error) {
    std::vector<std::string> args = launchArguments();
    bool hasWait = false;
    for (const auto& a : args) hasWait = hasWait || a == "--wait-for-instance";
    if (!hasWait) args.push_back("--wait-for-instance");
    const fs::path exe = executablePath();

#if defined(_WIN32)
    std::wstring commandLine = quoteArgument(exe.wstring());
    for (const auto& a : args) commandLine += L" " + quoteArgument(widen(a));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        error = "could not start the new version (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    std::vector<std::string> all{exe.string()};
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : all) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) {
        error = "could not start the new version";
        return false;
    }
    return true;
#endif
}

}  // namespace edgepad
