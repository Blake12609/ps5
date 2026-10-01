#pragma once

#include <string>

#include "core/settings.hpp"

namespace edgepad {

// 2: hair trigger reset distance default went from 4% to 1%.
inline constexpr int kConfigFormatVersion = 3;

std::string configToJson(const Config& cfg);

// Tolerant parser: unknown keys are ignored, missing or invalid values fall back to
// defaults and everything is clamped. Never throws. `warning` receives parse errors.
Config configFromJson(const std::string& text, std::string* warning = nullptr);

}  // namespace edgepad
