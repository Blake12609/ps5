#include "app/updater.hpp"

#include <fstream>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>

#include "core/build_info.hpp"
#include "core/release_info.hpp"
#include "core/sha256.hpp"
#include "core/version.hpp"
#include "platform/http.hpp"
#include "platform/self_update.hpp"

namespace fs = std::filesystem;

namespace edgepad {

struct Updater::Shared {
    mutable std::mutex mutex;
    UpdateStatus status;
    std::optional<ReleaseInfo> release;
    bool busy = false;

    void set(UpdateState state, std::string message) {
        std::lock_guard lock(mutex);
        status.state = state;
        status.message = std::move(message);
    }
};

namespace {

std::optional<std::string> fileSha256(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    Sha256 hash;
    char buffer[65536];
    while (in) {
        in.read(buffer, sizeof(buffer));
        hash.update(buffer, static_cast<size_t>(in.gcount()));
    }
    return hash.finishHex();
}

void runCheck(const std::shared_ptr<Updater::Shared>& shared) {
    shared->set(UpdateState::Checking, "Checking for updates...");
    const std::string url = std::string("https://api.github.com/repos/") + build::kRepository + "/releases/latest";
    const HttpResponse response =
        httpGet(url, {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}});
    if (response.status == 404) {
        shared->set(UpdateState::UpToDate, "No releases have been published yet");
        return;
    }
    if (!response.ok()) {
        shared->set(UpdateState::Failed,
                    "Could not check for updates: " + (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error));
        return;
    }
    std::string error;
    auto release = parseReleaseJson(response.body, &error);
    if (!release) {
        shared->set(UpdateState::Failed, "Could not read the release info: " + error);
        return;
    }
    std::lock_guard lock(shared->mutex);
    shared->status.latestVersion = release->version;
    shared->status.releaseUrl = release->htmlUrl;
    if (isNewerVersion(release->version, build::kVersion)) {
        shared->status.state = UpdateState::Available;
        shared->status.message = "Version " + release->version + " is available";
    } else {
        shared->status.state = UpdateState::UpToDate;
        shared->status.message = std::string("You are on the latest version (") + build::kVersion + ")";
    }
    shared->release = std::move(release);
}

void runInstall(const std::shared_ptr<Updater::Shared>& shared) {
    std::optional<ReleaseInfo> release;
    {
        std::lock_guard lock(shared->mutex);
        release = shared->release;
    }
    if (!release) return;
    if (!Updater::canSelfUpdate()) {
        shared->set(UpdateState::Available, "Development build: download the new version from GitHub");
        return;
    }
    const std::string assetName = platformAssetName();
    const ReleaseAsset* asset = release->findAsset(assetName);
    const ReleaseAsset* sums = release->findAsset(kChecksumAssetName);
    if (asset == nullptr || sums == nullptr) {
        shared->set(UpdateState::Failed, "The release has no " + assetName + " build or checksum file");
        return;
    }

    shared->set(UpdateState::Downloading, "Downloading version " + release->version + "...");
    const HttpResponse sumsResponse = httpGet(sums->url);
    const auto expected = sumsResponse.ok() ? findChecksum(sumsResponse.body, assetName) : std::nullopt;
    if (!expected) {
        shared->set(UpdateState::Failed, "Could not get the checksum for " + assetName);
        return;
    }

    const fs::path staged = stagedUpdatePath();
    std::string error;
    if (!httpDownload(asset->url, staged, error)) {
        shared->set(UpdateState::Failed, "Download failed: " + error);
        return;
    }
    const auto actual = fileSha256(staged);
    if (!actual || *actual != *expected) {
        std::error_code ec;
        fs::remove(staged, ec);
        shared->set(UpdateState::Failed, "Downloaded file is corrupt (checksum mismatch), update skipped");
        return;
    }
    if (!installUpdate(staged, error)) {
        shared->set(UpdateState::Failed, "Could not install the update: " + error);
        return;
    }
    shared->set(UpdateState::Installed, "Version " + release->version + " installed. Restart EdgePad to use it.");
}

}  // namespace

Updater::Updater() : shared_(std::make_shared<Shared>()) {}

bool Updater::canSelfUpdate() { return build::kReleaseBuild; }

void Updater::check(bool installIfAvailable) {
    {
        std::lock_guard lock(shared_->mutex);
        if (shared_->busy) return;
        shared_->busy = true;
    }
    // Detached on purpose: the worker only touches `shared`, so quitting mid-download is safe.
    std::thread([shared = shared_, installIfAvailable] {
        runCheck(shared);
        bool available = false;
        {
            std::lock_guard lock(shared->mutex);
            available = shared->status.state == UpdateState::Available;
        }
        if (available && installIfAvailable) runInstall(shared);
        std::lock_guard lock(shared->mutex);
        shared->busy = false;
    }).detach();
}

void Updater::install() {
    {
        std::lock_guard lock(shared_->mutex);
        if (shared_->busy || !shared_->release) return;
        shared_->busy = true;
    }
    std::thread([shared = shared_] {
        runInstall(shared);
        std::lock_guard lock(shared->mutex);
        shared->busy = false;
    }).detach();
}

UpdateStatus Updater::status() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->status;
}

bool Updater::busy() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->busy;
}

const char* updateStateLabel(UpdateState state) {
    switch (state) {
        case UpdateState::Idle: return "idle";
        case UpdateState::Checking: return "checking";
        case UpdateState::UpToDate: return "up to date";
        case UpdateState::Available: return "update available";
        case UpdateState::Downloading: return "downloading";
        case UpdateState::Installed: return "installed";
        case UpdateState::Failed: return "failed";
    }
    return "idle";
}

}  // namespace edgepad
