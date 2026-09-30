#pragma once

#include <filesystem>
#include <string>

#include "app/config_store.hpp"
#include "app/engine.hpp"
#include "app/updater.hpp"
#include "core/settings.hpp"

namespace edgepad {

struct GuiOptions {
    Config config;
    std::filesystem::path dataDir;
    bool autoUpdate = false;
    std::string startupWarning;
};

// Runs the desktop UI until the window is closed. Returns the process exit code.
int runGui(Engine& engine, const ConfigStore& store, Updater& updater, GuiOptions options);

}  // namespace edgepad
