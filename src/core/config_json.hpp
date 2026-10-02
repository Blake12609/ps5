#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/settings.hpp"

namespace edgepad {

// 2: hair trigger reset distance default went from 4% to 1%.
inline constexpr int kConfigFormatVersion = 5;

std::string configToJson(const Config& cfg);

// Tolerant parser: unknown keys are ignored, missing or invalid values fall back to
// defaults and everything is clamped. Never throws. `warning` receives parse errors.
Config configFromJson(const std::string& text, std::string* warning = nullptr);

// A profile as a short code to share or back up: "EP1-" + the settings that differ from a new
// profile, as compact JSON, base64url encoded with a CRC-32 check.
std::string profileShareCode(const Profile& profile);
// Reads a share code (spaces and line breaks are ignored). Nothing (with `error` set) when the
// code is incomplete, mistyped or not a profile code.
std::optional<Profile> profileFromShareCode(std::string_view code, std::string* error = nullptr);

}  // namespace edgepad
