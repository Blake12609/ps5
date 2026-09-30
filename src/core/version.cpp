#include "core/version.hpp"

#include <algorithm>
#include <cctype>

namespace edgepad {

std::optional<std::vector<int>> parseVersion(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
    std::vector<int> parts;
    size_t i = 0;
    while (i < text.size()) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) break;
        long value = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            value = value * 10 + (text[i] - '0');
            if (value > 1'000'000'000) return std::nullopt;
            ++i;
        }
        parts.push_back(static_cast<int>(value));
        if (i < text.size() && text[i] == '.') {
            ++i;
            continue;
        }
        break;
    }
    if (parts.empty()) return std::nullopt;
    return parts;
}

int compareVersions(const std::vector<int>& a, const std::vector<int>& b) {
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

bool isNewerVersion(std::string_view candidate, std::string_view current) {
    const auto a = parseVersion(candidate);
    const auto b = parseVersion(current);
    if (!a || !b) return false;
    return compareVersions(*a, *b) > 0;
}

}  // namespace edgepad
