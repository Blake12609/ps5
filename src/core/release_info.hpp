#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace edgepad {

struct ReleaseAsset {
    std::string name;
    std::string url;
    long long size = 0;
};

struct ReleaseInfo {
    std::string tag;
    std::string version;  // tag without the leading "v"
    std::string htmlUrl;
    std::string notes;
    std::vector<ReleaseAsset> assets;

    const ReleaseAsset* findAsset(std::string_view name) const;
};

inline constexpr const char* kChecksumAssetName = "SHA256SUMS.txt";

// Parses the JSON returned by GET /repos/{owner}/{repo}/releases/latest.
std::optional<ReleaseInfo> parseReleaseJson(const std::string& json, std::string* error = nullptr);

// Name of the release asset for the platform this binary was built for.
std::string platformAssetName();

// Finds the checksum for `fileName` in `sha256sum` output ("<hex>  <name>" per line).
std::optional<std::string> findChecksum(std::string_view sums, std::string_view fileName);

}  // namespace edgepad
