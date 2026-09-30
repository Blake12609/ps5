#pragma once

#include <filesystem>
#include <string>

#include "core/settings.hpp"

namespace edgepad {

// Loads and atomically saves config.json inside the portable data folder.
class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

    const std::filesystem::path& path() const { return path_; }

    // Missing file: defaults. Broken file: defaults, and the bad file is kept as config.json.bad.
    Config load(std::string* warning = nullptr) const;
    bool save(const Config& config, std::string* error = nullptr) const;

private:
    std::filesystem::path path_;
};

}  // namespace edgepad
