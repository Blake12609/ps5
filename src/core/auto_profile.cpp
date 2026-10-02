#include "core/auto_profile.hpp"

#include <algorithm>

namespace edgepad {

std::optional<int> AutoProfile::update(const Config& cfg, std::string_view app) {
    if (!cfg.settings.autoProfiles || cfg.profiles.empty()) {
        reset();
        return std::nullopt;
    }
    if (app.empty()) return std::nullopt;
    if (app != candidate_) {
        candidate_.assign(app);
        steady_ = 1;
    } else if (steady_ < kSteadyChecks) {
        ++steady_;
    }
    if (steady_ < kSteadyChecks) return std::nullopt;

    const int count = static_cast<int>(cfg.profiles.size());
    const int active = std::clamp(cfg.settings.activeProfile, 0, count - 1);
    // A profile picked by hand (GUI, Fn combo) while a game's profile was on: keep it, no switch back.
    if (!game_.empty() && active != gameProfile_) game_.clear();

    // Act once per situation: the program in front, or the profile listing it, changed (adding the
    // game to a profile counts). Otherwise a hand pick would be undone at the next check.
    const std::optional<int> match = profileForGame(cfg, app);
    if (decided_ && app == lastApp_ && match == lastMatch_) return std::nullopt;
    decided_ = true;
    lastApp_.assign(app);
    lastMatch_ = match;

    if (match) {
        if (game_.empty()) {
            if (*match == active) return std::nullopt;  // already on: nothing to go back to later
            previous_ = active;
        }
        game_.assign(app);  // from one game straight to another: still back to the profile from before
        gameProfile_ = *match;
        if (*match == active) return std::nullopt;
        return match;
    }
    if (game_.empty()) return std::nullopt;
    game_.clear();
    if (previous_ >= count || previous_ == active) return std::nullopt;
    return previous_;
}

}  // namespace edgepad
