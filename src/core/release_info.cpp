#include "core/release_info.hpp"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace edgepad {

const ReleaseAsset* ReleaseInfo::findAsset(std::string_view name) const {
    for (const auto& a : assets) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

std::optional<ReleaseInfo> parseReleaseJson(const std::string& text, std::string* error) {
    const auto root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (error) *error = "invalid JSON from GitHub";
        return std::nullopt;
    }
    auto tag = root.find("tag_name");
    if (tag == root.end() || !tag->is_string()) {
        if (error) *error = "release has no tag";
        return std::nullopt;
    }
    ReleaseInfo info;
    info.tag = tag->get<std::string>();
    info.version = info.tag;
    if (!info.version.empty() && (info.version.front() == 'v' || info.version.front() == 'V')) info.version.erase(0, 1);
    if (auto it = root.find("html_url"); it != root.end() && it->is_string()) info.htmlUrl = it->get<std::string>();
    if (auto it = root.find("body"); it != root.end() && it->is_string()) info.notes = it->get<std::string>();
    if (auto it = root.find("assets"); it != root.end() && it->is_array()) {
        for (const auto& a : *it) {
            if (!a.is_object()) continue;
            auto name = a.find("name");
            auto url = a.find("browser_download_url");
            if (name == a.end() || url == a.end() || !name->is_string() || !url->is_string()) continue;
            ReleaseAsset asset{name->get<std::string>(), url->get<std::string>(), 0};
            if (auto size = a.find("size"); size != a.end() && size->is_number_integer()) asset.size = size->get<long long>();
            info.assets.push_back(std::move(asset));
        }
    }
    return info;
}

std::string platformAssetName() {
#if defined(_WIN32)
    return "EdgePad-windows-x64.exe";
#elif defined(__APPLE__)
    return "EdgePad-macos";
#else
    return "EdgePad-linux-x64";
#endif
}

std::optional<std::string> findChecksum(std::string_view sums, std::string_view fileName) {
    size_t pos = 0;
    while (pos < sums.size()) {
        size_t end = sums.find('\n', pos);
        if (end == std::string_view::npos) end = sums.size();
        std::string_view line = sums.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        const size_t space = line.find_first_of(" \t");
        if (space != 64) continue;
        std::string_view name = line.substr(space);
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t' || name.front() == '*')) name.remove_prefix(1);
        if (name != fileName) continue;
        std::string hash(line.substr(0, 64));
        if (!std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) continue;
        std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return hash;
    }
    return std::nullopt;
}

}  // namespace edgepad
