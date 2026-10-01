#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "app/config_store.hpp"
#include "app/engine.hpp"
#include "app/updater.hpp"
#include "core/build_info.hpp"
#include "core/dualsense.hpp"
#include "platform/hid_device.hpp"
#include "platform/paths.hpp"
#include "platform/self_update.hpp"

#if defined(EDGEPAD_HAS_GUI)
#include "app/gui.hpp"
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace edgepad;

namespace {

struct Options {
    bool headless = false;
    bool noUpdate = false;
    bool version = false;
    bool help = false;
    bool list = false;
    bool waitForInstance = false;
    bool demo = false;
    fs::path dataDir;
    std::string error;
};

Options parseOptions(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless" || arg == "--no-gui") {
            o.headless = true;
        } else if (arg == "--no-update") {
            o.noUpdate = true;
        } else if (arg == "--version" || arg == "-v") {
            o.version = true;
        } else if (arg == "--help" || arg == "-h") {
            o.help = true;
        } else if (arg == "--list") {
            o.list = true;
        } else if (arg == "--demo") {
            o.demo = true;
        } else if (arg == "--wait-for-instance") {
            o.waitForInstance = true;
        } else if (arg == "--data-dir" && i + 1 < argc) {
            o.dataDir = argv[++i];
        } else {
            o.error = "unknown option: " + arg;
        }
    }
#if !defined(EDGEPAD_HAS_GUI)
    o.headless = true;
#endif
    return o;
}

void attachConsole() {
#if defined(_WIN32)
    // The exe is a GUI app; borrow the parent console so --help / --headless can print.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
}

void showMessage(const std::string& text) {
#if defined(_WIN32)
    MessageBoxA(nullptr, text.c_str(), "EdgePad", MB_OK | MB_ICONINFORMATION);
#else
    std::fprintf(stderr, "%s\n", text.c_str());
#endif
}

const char* kUsage =
    "EdgePad - DualSense / DualSense Edge remapper\n"
    "\n"
    "Usage: EdgePad [options]\n"
    "  --headless          run without a window (Ctrl+C to quit)\n"
    "  --no-update         skip the automatic update check\n"
    "  --data-dir <path>   store settings somewhere else\n"
    "  --list              list connected controllers and exit\n"
    "  --demo              simulate a controller (try settings without one)\n"
    "  --version           print the version and exit\n";

std::atomic<bool> gQuit{false};
void onSignal(int) { gQuit = true; }

std::string describe(const EngineStatus& s, const Config& cfg) {
    std::string line;
    if (s.connected) {
        line = s.controllerName + (s.connection == dualsense::Connection::Bluetooth ? " (Bluetooth)" : " (USB)");
        if (s.battery >= 0) line += " " + std::to_string(s.battery) + "%";
        line += " | " + std::to_string(static_cast<int>(s.reportRate)) + " Hz";
    } else {
        line = "waiting for controller";
    }
    const int active = std::clamp(s.activeProfile, 0, static_cast<int>(cfg.profiles.size()) - 1);
    line += " | profile: " + cfg.profiles[static_cast<size_t>(active)].name;
    line += s.enabled ? "" : " (passthrough)";
    line += " | " + (s.padName.empty() ? s.padMessage : s.padName);
    return line;
}

int runHeadless(Engine& engine, const ConfigStore& store, Updater& updater, bool autoUpdate) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::printf("EdgePad %s running headless. Settings: %s\n", build::kVersion, toUtf8(store.path()).c_str());
    if (autoUpdate) updater.check(true);

    std::string lastLine;
    std::string lastUpdateMessage;
    uint64_t savedRevision = 0;
    while (!gQuit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const EngineStatus status = engine.status();
        const Config cfg = engine.config();
        const std::string line = describe(status, cfg);
        if (line != lastLine) {
            std::printf("%s\n", line.c_str());
            std::fflush(stdout);
            lastLine = line;
        }
        if (status.revision != savedRevision) {
            store.save(cfg);
            savedRevision = status.revision;
        }
        const UpdateStatus up = updater.status();
        if (!up.message.empty() && up.message != lastUpdateMessage) {
            std::printf("update: %s\n", up.message.c_str());
            lastUpdateMessage = up.message;
        }
    }
    store.save(engine.config());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    setLaunchArguments(argc, argv);
    const Options options = parseOptions(argc, argv);
    if (options.headless || options.help || options.version || options.list || !options.error.empty()) attachConsole();

    if (!options.error.empty()) {
        std::fprintf(stderr, "%s\n\n%s", options.error.c_str(), kUsage);
        return 2;
    }
    if (options.help) {
        std::printf("%s", kUsage);
        return 0;
    }
    if (options.version) {
        std::printf("EdgePad %s\n", build::kVersion);
        return 0;
    }

    if (!hidInit()) {
        showMessage("Could not initialise HID access.");
        return 1;
    }
    if (options.list) {
        const auto controllers = enumerateControllers();
        if (controllers.empty()) std::printf("No DualSense controllers found.\n");
        for (const auto& c : controllers) std::printf("%s  %s\n", dualsense::productName(c.productId), c.path.c_str());
        hidShutdown();
        return 0;
    }

    cleanupOldBinary();
    const fs::path dataDir = dataDirectory(options.dataDir);
    SingleInstanceLock instance;
    if (!instance.acquire(dataDir, options.waitForInstance ? 15000 : 0)) {
        showMessage("EdgePad is already running.");
        hidShutdown();
        return 1;
    }

    const ConfigStore store(dataDir / "config.json");
    std::string warning;
    const Config config = store.load(&warning);
    if (!warning.empty()) std::fprintf(stderr, "warning: %s\n", warning.c_str());
    store.save(config);  // writes defaults on first start so the file is easy to find and edit

    Engine engine(config, options.demo, dataDir);
    engine.start();
    Updater updater;
    const bool autoUpdate = config.settings.autoUpdate && !options.noUpdate && Updater::canSelfUpdate();

    int exitCode = 0;
#if defined(EDGEPAD_HAS_GUI)
    if (!options.headless) {
        GuiOptions gui;
        gui.config = config;
        gui.dataDir = dataDir;
        gui.autoUpdate = autoUpdate;
        gui.startupWarning = warning;
        exitCode = runGui(engine, store, updater, std::move(gui));
        if (exitCode == 2) {
            showMessage("EdgePad could not open its window (it needs OpenGL 3.0 and a display).\n"
                        "Update the graphics driver, or start EdgePad with --headless.");
            exitCode = 1;
        }
    } else
#endif
    {
        exitCode = runHeadless(engine, store, updater, autoUpdate);
    }

    engine.stop();
    hidShutdown();
    return exitCode;
}
