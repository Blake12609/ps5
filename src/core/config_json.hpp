#pragma once

#include <string>

#include "core/settings.hpp"

namespace edgepad {

inline constexpr int kConfigFormatVersion = 1;

std::string configToJson(const Config& cfg);

// Tolerant parser: unknown keys are ignored, missing or invalid values fall back to
// defaults and everything is clamped. Never throws. `warning` receives parse errors.
Config configFromJson(const std::string& text, std::string* warning = nullptr);

}  // namespace edgepad
