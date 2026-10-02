#include "platform/foreground.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace edgepad {
namespace {

// The few XCB declarations needed, so EdgePad needs no X11 development files and still starts
// without X: libxcb is opened at run time. XCB reports errors per request (no global error
// handler shared with the GUI's own X11 connection, unlike Xlib).
struct XcbConnection;
struct XcbSetup;
struct XcbScreen {
    uint32_t root;  // first field of xcb_screen_t
};
struct XcbScreenIterator {
    XcbScreen* data;
    int rem;
    int index;
};
struct XcbCookie {
    unsigned int sequence;
};
struct XcbInternAtomReply {
    uint8_t responseType, pad0;
    uint16_t sequence;
    uint32_t length;
    uint32_t atom;
};
struct XcbGetPropertyReply;
struct XcbGenericError;

constexpr uint32_t kAtomCardinal = 6;
constexpr uint32_t kAtomWindow = 33;

struct Xcb {
    void* library = nullptr;
    XcbConnection* (*connect)(const char*, int*) = nullptr;
    int (*connectionHasError)(XcbConnection*) = nullptr;
    void (*disconnect)(XcbConnection*) = nullptr;
    const XcbSetup* (*getSetup)(XcbConnection*) = nullptr;
    XcbScreenIterator (*setupRootsIterator)(const XcbSetup*) = nullptr;
    void (*screenNext)(XcbScreenIterator*) = nullptr;
    XcbCookie (*internAtom)(XcbConnection*, uint8_t, uint16_t, const char*) = nullptr;
    XcbInternAtomReply* (*internAtomReply)(XcbConnection*, XcbCookie, XcbGenericError**) = nullptr;
    XcbCookie (*getProperty)(XcbConnection*, uint8_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = nullptr;
    XcbGetPropertyReply* (*getPropertyReply)(XcbConnection*, XcbCookie, XcbGenericError**) = nullptr;
    void* (*getPropertyValue)(const XcbGetPropertyReply*) = nullptr;
    int (*getPropertyValueLength)(const XcbGetPropertyReply*) = nullptr;

    bool load() {
        library = dlopen("libxcb.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!library) return false;
        const bool ok = find(connect, "xcb_connect") && find(connectionHasError, "xcb_connection_has_error") &&
                        find(disconnect, "xcb_disconnect") && find(getSetup, "xcb_get_setup") &&
                        find(setupRootsIterator, "xcb_setup_roots_iterator") &&
                        find(screenNext, "xcb_screen_next") && find(internAtom, "xcb_intern_atom") &&
                        find(internAtomReply, "xcb_intern_atom_reply") && find(getProperty, "xcb_get_property") &&
                        find(getPropertyReply, "xcb_get_property_reply") &&
                        find(getPropertyValue, "xcb_get_property_value") &&
                        find(getPropertyValueLength, "xcb_get_property_value_length");
        if (!ok) {
            dlclose(library);
            library = nullptr;
        }
        return ok;
    }

    template <typename F>
    bool find(F& f, const char* name) {
        f = reinterpret_cast<F>(dlsym(library, name));
        return f != nullptr;
    }
};

// Reads up to `max` bytes of a small /proc file.
std::string readSmallFile(const std::string& path, size_t max) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string out(max, '\0');
    const ssize_t n = read(fd, out.data(), max);
    close(fd);
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return out;
}

std::string_view fileName(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool endsWithExe(std::string_view s) {
    if (s.size() < 4) return false;
    const std::string_view tail = s.substr(s.size() - 4);
    return (tail[0] == '.') && (tail[1] | 0x20) == 'e' && (tail[2] | 0x20) == 'x' && (tail[3] | 0x20) == 'e';
}

std::string programOfProcess(long pid) {
    const std::string proc = "/proc/" + std::to_string(pid);
    // Wine and Proton put the game's Windows path in argv[0] ("C:\Games\cod.exe"); the executable
    // itself would only be Wine's loader.
    std::string cmdline = readSmallFile(proc + "/cmdline", 1024);
    cmdline.resize(cmdline.find('\0') == std::string::npos ? cmdline.size() : cmdline.find('\0'));
    if (cmdline.find('\\') != std::string::npos || endsWithExe(cmdline)) return std::string(fileName(cmdline));
    char target[1024];
    const ssize_t n = readlink((proc + "/exe").c_str(), target, sizeof(target));
    if (n > 0) {
        std::string_view path(target, static_cast<size_t>(n));
        constexpr std::string_view kDeleted = " (deleted)";  // updated while running
        if (path.size() > kDeleted.size() && path.substr(path.size() - kDeleted.size()) == kDeleted) {
            path.remove_suffix(kDeleted.size());
        }
        return std::string(fileName(path));
    }
    if (!cmdline.empty()) return std::string(fileName(cmdline));
    std::string comm = readSmallFile(proc + "/comm", 64);  // another user's process
    while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\0')) comm.pop_back();
    return comm;
}

}  // namespace

struct ForegroundWatcher::Impl {
    Xcb xcb;
    bool loaded = false;
    XcbConnection* connection = nullptr;
    uint32_t root = 0;
    uint32_t activeWindowAtom = 0;
    uint32_t pidAtom = 0;
    std::chrono::steady_clock::time_point nextConnect{};

    uint32_t window = 0;
    App app;

    ~Impl() { closeConnection(); }

    void closeConnection() {
        if (connection) xcb.disconnect(connection);
        connection = nullptr;
    }

    uint32_t atom(const char* name) {
        const XcbCookie cookie = xcb.internAtom(connection, 0, static_cast<uint16_t>(std::string_view(name).size()), name);
        XcbGenericError* error = nullptr;
        XcbInternAtomReply* reply = xcb.internAtomReply(connection, cookie, &error);
        std::free(error);
        const uint32_t result = reply ? reply->atom : 0;
        std::free(reply);
        return result;
    }

    bool ensureConnected() {
        if (connection && !xcb.connectionHasError(connection)) return true;
        closeConnection();
        const auto now = std::chrono::steady_clock::now();
        if (now < nextConnect) return false;
        nextConnect = now + std::chrono::seconds(10);
        if (!loaded) loaded = xcb.load();
        const char* display = std::getenv("DISPLAY");
        if (!loaded || !display || !*display) return false;
        int screenNumber = 0;
        connection = xcb.connect(display, &screenNumber);
        if (!connection || xcb.connectionHasError(connection)) {
            closeConnection();
            return false;
        }
        XcbScreenIterator it = xcb.setupRootsIterator(xcb.getSetup(connection));
        for (int i = 0; i < screenNumber && it.rem > 0; ++i) xcb.screenNext(&it);
        if (it.rem <= 0 || !it.data) {
            closeConnection();
            return false;
        }
        root = it.data->root;
        activeWindowAtom = atom("_NET_ACTIVE_WINDOW");
        pidAtom = atom("_NET_WM_PID");
        window = 0;
        app = {};
        return true;
    }

    // One 32-bit value of a window property, 0 when missing.
    uint32_t property32(uint32_t of, uint32_t property, uint32_t type) {
        const XcbCookie cookie = xcb.getProperty(connection, 0, of, property, type, 0, 1);
        XcbGenericError* error = nullptr;
        XcbGetPropertyReply* reply = xcb.getPropertyReply(connection, cookie, &error);
        std::free(error);
        uint32_t value = 0;
        if (reply && xcb.getPropertyValueLength(reply) >= 4) {
            value = *static_cast<const uint32_t*>(xcb.getPropertyValue(reply));
        }
        std::free(reply);
        return value;
    }
};

ForegroundWatcher::ForegroundWatcher() : impl_(std::make_unique<Impl>()) {}
ForegroundWatcher::~ForegroundWatcher() = default;

bool ForegroundWatcher::supported() const { return impl_->connection != nullptr; }

ForegroundWatcher::App ForegroundWatcher::current() {
    Impl& s = *impl_;
    if (!s.ensureConnected() || s.activeWindowAtom == 0) return {};
    const uint32_t window = s.property32(s.root, s.activeWindowAtom, kAtomWindow);
    // Same window: no lookup (unless its owner was not known yet - a new window gets its pid a moment later).
    if (window == s.window && (window == 0 || s.app.self || !s.app.name.empty())) return s.app;
    s.window = window;
    s.app = {};
    const uint32_t pid = window != 0 && s.pidAtom != 0 ? s.property32(window, s.pidAtom, kAtomCardinal) : 0;
    if (pid == 0) return s.app;
    if (static_cast<pid_t>(pid) == getpid()) {
        s.app.self = true;
        return s.app;
    }
    s.app.name = programOfProcess(static_cast<long>(pid));
    return s.app;
}

}  // namespace edgepad
