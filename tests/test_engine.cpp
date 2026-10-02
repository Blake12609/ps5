#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

#include "app/engine.hpp"

using namespace edgepad;
using Clock = std::chrono::steady_clock;

// The engine's background threads (controller loop, game watcher) must never keep the shared lock
// while they wait: the controller loop needs it for every report and the window for every frame.
TEST_CASE("engine: status is always quick to read, stopping is quick (demo controller)") {
    Config cfg = Config::defaults();
    cfg.settings.output = OutputKind::None;  // no virtual controller in tests
    cfg.profiles[1].games = {"some-game.exe"};
    Engine engine(cfg, true, std::filesystem::temp_directory_path());
    engine.start();
    double slowestMs = 0.0;
    bool connected = false;
    const auto end = Clock::now() + std::chrono::milliseconds(1500);
    while (Clock::now() < end) {
        const auto t = Clock::now();
        const EngineStatus status = engine.status();
        slowestMs = std::max(slowestMs, std::chrono::duration<double, std::milli>(Clock::now() - t).count());
        connected = connected || status.connected;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(connected);
    CHECK(slowestMs < 100.0);
    CHECK(engine.status().reportRate > 100.0f);  // the demo controller's reports keep flowing

    const auto t = Clock::now();
    engine.stop();
    CHECK(std::chrono::duration<double, std::milli>(Clock::now() - t).count() < 1000.0);
}
