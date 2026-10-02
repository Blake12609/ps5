#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/settings.hpp"

namespace edgepad {

// Automatic profile per game: while a program listed in a profile is in front, that profile is on;
// when you leave it, the profile from before comes back. A profile picked by hand in between wins
// (no switch back), and so does a hand pick while the game stays in front (no switching over it).
class AutoProfile {
public:
    // How many checks in a row the same program must be in front before anything switches: a
    // window flashing up for a moment does not flip the profile.
    static constexpr int kSteadyChecks = 2;

    // `app`: the program in front, as `normalizeGameName` stores it. Empty when it is unknown or
    // EdgePad itself - then everything stays as it is (tweaking a profile in EdgePad does not
    // switch it away). Returns the profile to switch to, if one should be switched to now.
    std::optional<int> update(const Config& cfg, std::string_view app);
    void reset() { *this = AutoProfile{}; }

    // The game whose profile is on because of it, empty when none.
    const std::string& game() const { return game_; }

private:
    std::string candidate_;  // in front, but not for long enough yet
    int steady_ = 0;
    bool decided_ = false;  // `lastApp_` / `lastMatch_` hold the last situation acted on
    std::string lastApp_;
    std::optional<int> lastMatch_;
    std::string game_;    // non-empty while a game's profile is on because of the game
    int gameProfile_ = 0;  // that profile
    int previous_ = 0;     // the profile to go back to
};

}  // namespace edgepad
