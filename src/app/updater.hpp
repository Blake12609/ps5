#pragma once

#include <memory>
#include <string>

namespace edgepad {

enum class UpdateState { Idle, Checking, UpToDate, Available, Downloading, Installed, Failed };

struct UpdateStatus {
    UpdateState state = UpdateState::Idle;
    std::string latestVersion;
    std::string releaseUrl;
    std::string message;
};

// Checks GitHub Releases of this repository, downloads the build for this platform,
// verifies it against SHA256SUMS.txt and swaps it in. Runs on a background thread.
class Updater {
public:
    Updater();

    // Official builds only: development builds never replace themselves.
    static bool canSelfUpdate();

    void check(bool installIfAvailable);
    void install();
    UpdateStatus status() const;
    bool busy() const;

    struct Shared;  // state shared with the background worker (defined in updater.cpp)

private:
    std::shared_ptr<Shared> shared_;
};

const char* updateStateLabel(UpdateState state);

}  // namespace edgepad
