#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace edgepad {

// Parses "v1.2.3", "1.2", "1.2.3-beta" into numeric components ({1,2,3}).
std::optional<std::vector<int>> parseVersion(std::string_view text);

// Returns <0, 0, >0. Missing components count as zero ("1.2" == "1.2.0").
int compareVersions(const std::vector<int>& a, const std::vector<int>& b);

// True when `candidate` is a strictly newer version than `current`.
bool isNewerVersion(std::string_view candidate, std::string_view current);

}  // namespace edgepad
