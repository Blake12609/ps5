// Hiding the controller on Windows through HidHide (https://github.com/nefarius/HidHide).
// HidHide's filter driver denies access to the devices on its block list for every program that
// is not on its allow list. EdgePad puts itself on the allow list and the controller on the block
// list. With HidHide 2 the block list entry is tied to EdgePad's process (the driver drops it as
// soon as EdgePad exits, even after a crash); older versions get a normal entry that EdgePad
// removes again when it is done (and on the next start, should it have crashed).
#include "platform/controller_hide.hpp"

#include <windows.h>

#include <winioctl.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <vector>

#include "core/device_path.hpp"

namespace edgepad {
namespace {

// HidHideIoctlContract.h
constexpr DWORD kHidHideDeviceType = 32769;
constexpr DWORD hidHideIoctl(DWORD function) {
    return CTL_CODE(kHidHideDeviceType, function, METHOD_BUFFERED, FILE_READ_DATA);
}
constexpr DWORD kGetWhitelist = hidHideIoctl(2048);
constexpr DWORD kSetWhitelist = hidHideIoctl(2049);
constexpr DWORD kGetBlacklist = hidHideIoctl(2050);
constexpr DWORD kSetBlacklist = hidHideIoctl(2051);
constexpr DWORD kGetActive = hidHideIoctl(2052);
constexpr DWORD kSetActive = hidHideIoctl(2053);
constexpr DWORD kGetInverse = hidHideIoctl(2054);
constexpr DWORD kAddSessionBlacklist = hidHideIoctl(2056);
constexpr DWORD kClearSessionBlacklist = hidHideIoctl(2057);

using StringList = std::vector<std::wstring>;

std::wstring widen(const std::string& text) {
    std::wstring out;
    for (char c : text) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return out;
}

std::string narrow(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
}

bool sameText(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towupper(x) == std::towupper(y);
           });
}

bool contains(const StringList& list, const std::wstring& item) {
    return std::any_of(list.begin(), list.end(), [&](const std::wstring& s) { return sameText(s, item); });
}

bool removeFrom(StringList& list, const std::wstring& item) {
    const auto it = std::remove_if(list.begin(), list.end(), [&](const std::wstring& s) { return sameText(s, item); });
    const bool removed = it != list.end();
    list.erase(it, list.end());
    return removed;
}

std::vector<wchar_t> toMultiString(const StringList& list) {
    std::vector<wchar_t> out;
    for (const auto& s : list) {
        out.insert(out.end(), s.begin(), s.end());
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

// EdgePad's own executable the way HidHide's allow list stores it: "\Device\HarddiskVolume3\...\EdgePad.exe".
std::wstring ownImageName() {
    std::vector<wchar_t> buffer(32768);
    DWORD size = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(GetCurrentProcess(), PROCESS_NAME_NATIVE, buffer.data(), &size)) return {};
    return std::wstring(buffer.data(), size);
}

// The HidHide control device, opened only for as long as it is needed: while it is open, the
// HidHide Configuration Client cannot open it.
class Control {
public:
    Control() {
        handle_ = CreateFileW(L"\\\\.\\HidHide", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) error_ = GetLastError();
    }
    ~Control() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    }
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;

    bool ok() const { return handle_ != INVALID_HANDLE_VALUE; }
    bool missing() const { return error_ == ERROR_FILE_NOT_FOUND || error_ == ERROR_PATH_NOT_FOUND; }
    DWORD error() const { return error_; }

    bool getList(DWORD code, StringList& out) {
        DWORD needed = 0;
        if (!DeviceIoControl(handle_, code, nullptr, 0, nullptr, 0, &needed, nullptr)) return false;
        std::vector<wchar_t> buffer(needed / sizeof(wchar_t) + 1, L'\0');
        if (needed > 0 && !DeviceIoControl(handle_, code, nullptr, 0, buffer.data(), needed, &needed, nullptr)) return false;
        out.clear();
        std::wstring current;
        for (wchar_t c : buffer) {
            if (c != L'\0') {
                current.push_back(c);
            } else if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        }
        return true;
    }
    bool setList(DWORD code, const StringList& list) {
        std::vector<wchar_t> buffer = toMultiString(list);
        DWORD n = 0;
        return DeviceIoControl(handle_, code, buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), nullptr,
                               0, &n, nullptr) != FALSE;
    }
    bool getFlag(DWORD code, bool& value) {
        BOOLEAN flag = FALSE;
        DWORD n = 0;
        if (!DeviceIoControl(handle_, code, nullptr, 0, &flag, sizeof(flag), &n, nullptr)) return false;
        value = flag != FALSE;
        return true;
    }
    bool setFlag(DWORD code, bool value) {
        BOOLEAN flag = value ? TRUE : FALSE;
        DWORD n = 0;
        return DeviceIoControl(handle_, code, &flag, sizeof(flag), nullptr, 0, &n, nullptr) != FALSE;
    }
    bool addSessionBlacklist(const std::wstring& instanceId) {
        std::vector<wchar_t> buffer = toMultiString({instanceId});
        DWORD n = 0;
        return DeviceIoControl(handle_, kAddSessionBlacklist, buffer.data(),
                               static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), nullptr, 0, &n, nullptr) != FALSE;
    }
    bool clearSessionBlacklist() {
        DWORD n = 0;
        return DeviceIoControl(handle_, kClearSessionBlacklist, nullptr, 0, nullptr, 0, &n, nullptr) != FALSE;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD error_ = 0;
};

// Makes sure EdgePad itself may open hidden devices (with "inverse" mode the list means the opposite).
bool allowSelf(Control& control) {
    const std::wstring image = ownImageName();
    StringList whitelist;
    bool inverse = false;
    if (image.empty() || !control.getList(kGetWhitelist, whitelist) || !control.getFlag(kGetInverse, inverse)) return false;
    if (!inverse && !contains(whitelist, image)) {
        whitelist.push_back(image);
        return control.setList(kSetWhitelist, whitelist);
    }
    if (inverse && removeFrom(whitelist, image)) return control.setList(kSetWhitelist, whitelist);
    return true;
}

std::string unavailableReason(const Control& control) {
    if (control.missing()) return "HidHide is not installed";
    if (control.error() == ERROR_ACCESS_DENIED) return "HidHide is busy (close the HidHide Configuration Client)";
    return "HidHide could not be opened (error " + std::to_string(control.error()) + ")";
}

}  // namespace

struct ControllerHider::Impl {
    std::filesystem::path stateFile;
    StringList persistentAdded;  // block list entries EdgePad added (older HidHide)
    StringList sessionIds;       // HidHide 2 process-lifetime entries
    bool activated = false;      // EdgePad switched HidHide on

    void save() const {
        std::error_code ec;
        if (persistentAdded.empty() && !activated) {
            std::filesystem::remove(stateFile, ec);
            return;
        }
        std::filesystem::create_directories(stateFile.parent_path(), ec);
        std::ofstream out(stateFile, std::ios::trunc);
        if (activated) out << "active\n";
        for (const auto& id : persistentAdded) out << "block " << narrow(id) << '\n';
    }

    void load() {
        std::ifstream in(stateFile);
        std::string line;
        while (std::getline(in, line)) {
            if (line == "active") activated = true;
            if (line.rfind("block ", 0) == 0 && line.size() > 6) persistentAdded.push_back(widen(line.substr(6)));
        }
    }

    void undo() {
        if (persistentAdded.empty() && sessionIds.empty() && !activated) return;
        Control control;
        if (!control.ok()) return;  // keep the state file, the next start tries again
        if (!sessionIds.empty()) control.clearSessionBlacklist();
        sessionIds.clear();
        bool listOk = true;
        if (!persistentAdded.empty()) {
            StringList blacklist;
            listOk = control.getList(kGetBlacklist, blacklist);
            if (listOk) {
                bool changed = false;
                for (const auto& id : persistentAdded) changed |= removeFrom(blacklist, id);
                if (changed) listOk = control.setList(kSetBlacklist, blacklist);
            }
        }
        if (activated) control.setFlag(kSetActive, false);
        if (listOk) {
            persistentAdded.clear();
            activated = false;
        }
        save();
    }
};

ControllerHider::ControllerHider(std::filesystem::path stateFile) : impl_(std::make_unique<Impl>()) {
    impl_->stateFile = std::move(stateFile);
    impl_->load();
    impl_->undo();  // left over from a crash
    // EdgePad must always be able to open the controller, also when it was hidden in HidHide by hand.
    if (Control control; control.ok()) allowSelf(control);
}

ControllerHider::~ControllerHider() { restore(); }

bool ControllerHider::needsExclusiveOpen(std::string& reason) {
    Control control;
    if (control.ok()) {
        reason.clear();
        return false;
    }
    reason = unavailableReason(control);
    return true;
}

bool ControllerHider::hide(const std::string& hidPath, std::string& message) {
    Control control;
    if (!control.ok()) {
        message = unavailableReason(control);
        return false;
    }
    const std::wstring instanceId = widen(instanceIdFromHidPath(hidPath));
    if (instanceId.empty()) {
        message = "unexpected device path, cannot hide it with HidHide";
        return false;
    }

    // Allow list first: EdgePad itself must keep access.
    if (!allowSelf(control)) {
        message = "could not add EdgePad to HidHide's allowed applications";
        return false;
    }

    // Block list: a process-lifetime entry (HidHide 2), else a normal one EdgePad removes later.
    if (contains(impl_->sessionIds, instanceId)) {
        // already hidden for this run (the controller reconnected)
    } else if (control.addSessionBlacklist(instanceId)) {
        impl_->sessionIds.push_back(instanceId);
    } else {
        StringList blacklist;
        if (!control.getList(kGetBlacklist, blacklist)) {
            message = "could not read HidHide's device list";
            return false;
        }
        if (!contains(blacklist, instanceId)) {
            blacklist.push_back(instanceId);
            if (!control.setList(kSetBlacklist, blacklist)) {
                message = "could not add the controller to HidHide's device list";
                return false;
            }
            impl_->persistentAdded.push_back(instanceId);
        }
    }

    bool active = false;
    if (control.getFlag(kGetActive, active) && !active) {
        if (!control.setFlag(kSetActive, true)) {
            message = "could not switch HidHide on";
            return false;
        }
        impl_->activated = true;
    }
    impl_->save();
    message = "Hidden from games by HidHide";
    return true;
}

void ControllerHider::restore() { impl_->undo(); }

}  // namespace edgepad
