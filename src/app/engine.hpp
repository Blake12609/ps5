#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "core/dualsense.hpp"
#include "core/settings.hpp"
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
    PadError padError = PadError::None;

    float reportRate = 0.0f;  // controller reports per second
    InputState input;
    OutputState output;

    int activeProfile = 0;
    bool enabled = true;
    uint64_t revision = 0;  // increases whenever the engine itself changes the config (Fn combos)
};

// Real-time loop on its own thread: controller -> pipeline -> virtual controller.
class Engine {
public:
    // `demo` feeds a simulated controller instead of real hardware (try settings without a pad).
    explicit Engine(Config config, bool demo = false);
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

    mutable std::mutex mutex_;
    Config shared_;
    bool pendingConfig_ = false;
    EngineStatus status_;

    const bool demo_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> retryPad_{false};
    std::atomic<uint16_t> rumble_{0};
    std::thread thread_;
};

}  // namespace edgepad
