#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/dualsense.hpp"
#include "core/settings.hpp"
#include "core/tester.hpp"
#include "core/types.hpp"
#include "platform/virtual_pad.hpp"

namespace edgepad {

struct EngineStatus {
    bool connected = false;
    std::string controllerName;
    dualsense::Connection connection = dualsense::Connection::Unknown;
    int battery = -1;
    bool charging = false;

    std::string padName;     // active virtual controller, empty when none
    std::string padMessage;  // why there is no virtual controller
    std::string padWarning;  // the virtual controller exists but does not fully work yet
    PadError padError = PadError::None;
    std::string keyboardMessage;  // why key bindings cannot be sent, empty when fine
    bool controllerHidden = false;  // games cannot see the real controller
    std::string hideMessage;        // how it is hidden / why it could not be, empty when not wanted

    float reportRate = 0.0f;  // controller reports per second
    // Controller tester: report timing and EdgePad's own processing time (updated ~10x a second).
    tester::ReportStats::Summary timing;
    std::array<float, tester::ReportStats::kHistory> intervals{};  // ms, oldest first
    size_t intervalCount = 0;
    bool gyroActive = false;  // gyro aiming is currently switched on
    InputState input;
    OutputState output;

    // Automatic profiles per game.
    bool foregroundKnown = false;         // this desktop says which program is in front
    std::string foregroundApp;            // the program in front ("cod.exe"), empty when unknown or EdgePad
    std::vector<std::string> recentApps;  // programs recently in front, newest first
    std::string autoGame;                 // the game whose profile is on because of it, empty when none

    int activeProfile = 0;
    bool enabled = true;
    uint64_t revision = 0;  // increases whenever the engine itself changes the config (Fn combos)
};

// Real-time loop on its own thread: controller -> pipeline -> virtual controller.
class Engine {
public:
    // `demo` feeds a simulated controller instead of real hardware (try settings without a pad).
    // `dataDir` holds state that must survive a crash (what was changed to hide the controller).
    explicit Engine(Config config, bool demo = false, std::filesystem::path dataDir = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void start();
    void stop();

    // Hands an edited config to the engine. If the engine switched profile (Fn combo) since
    // `seenRevision`, its active profile / enabled state win over the caller's copy.
    void updateConfig(const Config& config, uint64_t seenRevision);
    Config config() const;
    EngineStatus status() const;
    // Try to create the virtual controller again right away (e.g. after installing ViGEmBus).
    void retryVirtualPad();

private:
    void run();
    void watchForeground();  // automatic profiles, on a thread of its own

    mutable std::mutex mutex_;
    Config shared_;
    bool pendingConfig_ = false;
    uint64_t configVersion_ = 0;  // increases with every edited config handed over
    EngineStatus status_;

    const bool demo_;
    const std::filesystem::path dataDir_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> retryPad_{false};
    std::atomic<uint16_t> rumble_{0};
    std::atomic<uint32_t> gameLightbar_{0};  // bit 24 = a game set a colour, bits 0-23 = RGB
    std::atomic<int> autoProfileRequest_{-1};  // profile the watcher wants on, -1 = none
    std::thread thread_;
    std::thread watcher_;
    std::mutex watcherMutex_;
    std::condition_variable watcherWake_;
};

}  // namespace edgepad
