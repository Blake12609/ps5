#include "core/pipeline.hpp"

#include <utility>

namespace edgepad {
namespace {

constexpr ButtonMask kComboMask =
    bit(Button::Cross) | bit(Button::Circle) | bit(Button::Square) | bit(Button::Triangle) | bit(Button::Options);

ButtonMask passthroughMask() {
    ButtonMask mask = 0;
    for (int i = 0; i < kButtonCount; ++i) {
        const Button b = buttonAt(i);
        if (isRemapTarget(b) && b != Button::L2 && b != Button::R2) mask |= bit(b);
    }
    return mask;
}

}  // namespace

ButtonMask fnSourceMask(FnMode mode) {
    switch (mode) {
        case FnMode::Auto: return bit(Button::FnLeft) | bit(Button::FnRight) | bit(Button::Mute);
        case FnMode::Edge: return bit(Button::FnLeft) | bit(Button::FnRight);
        case FnMode::Mute: return bit(Button::Mute);
        case FnMode::Touchpad: return bit(Button::Touchpad);
        case FnMode::Disabled:
        case FnMode::Count: break;
    }
    return 0;
}

dualsense::Effects effectsForConfig(const Config& cfg, uint8_t rumbleLarge, uint8_t rumbleSmall) {
    dualsense::Effects fx;
    const Profile& p = cfg.active();
    fx.lightbar = p.lightbar;
    if (cfg.settings.enabled) {
        fx.playerLeds = playerLedsForProfile(cfg.settings.activeProfile);
        fx.leftTrigger = dualsense::triggerEffectFor(p.l2);
        fx.rightTrigger = dualsense::triggerEffectFor(p.r2);
    }
    if (cfg.settings.rumble) {
        fx.rumbleLeft = rumbleLarge;
        fx.rumbleRight = rumbleSmall;
    }
    return fx;
}

OutputState Pipeline::process(const InputState& in, Config& cfg, float dtSeconds, PipelineEvents* events) {
    const ButtonMask pressed = in.buttons;
    const ButtonMask newlyPressed = pressed & ~previous_;
    previous_ = pressed;

    const ButtonMask fn = fnSourceMask(cfg.settings.fnMode);
    if ((pressed & fn) != 0) {
        for (Button hotkey : kProfileHotkeys) {
            if (!has(newlyPressed, hotkey)) continue;
            for (size_t i = 0; i < cfg.profiles.size(); ++i) {
                if (cfg.profiles[i].hotkey != hotkey) continue;
                if (static_cast<int>(i) != cfg.settings.activeProfile) {
                    cfg.settings.activeProfile = static_cast<int>(i);
                    if (events) events->profileChanged = true;
                }
                break;
            }
        }
        if (has(newlyPressed, Button::Options)) {
            cfg.settings.enabled = !cfg.settings.enabled;
            if (events) events->enabledChanged = true;
        }
        // Buttons pressed as part of a combo never reach the game, even after Fn is released.
        suppressed_ |= newlyPressed & kComboMask;
    }
    suppressed_ &= pressed;
    const ButtonMask usable = pressed & ~fn & ~suppressed_;

    OutputState out;
    if (!cfg.settings.enabled) {
        leftFilter_.reset();
        rightFilter_.reset();
        out.lx = in.lx;
        out.ly = in.ly;
        out.rx = in.rx;
        out.ry = in.ry;
        out.l2 = in.l2;
        out.r2 = in.r2;
        out.buttons = usable & passthroughMask();
        return out;
    }

    const Profile& p = cfg.active();
    const Vec2 left = leftFilter_.apply({in.lx, in.ly}, p.leftStick.rcFilter, dtSeconds);
    const Vec2 right = rightFilter_.apply({in.rx, in.ry}, p.rightStick.rcFilter, dtSeconds);
    Vec2 leftOut = processStick(left.x, left.y, p.leftStick);
    Vec2 rightOut = processStick(right.x, right.y, p.rightStick);
    if (p.swapSticks) std::swap(leftOut, rightOut);
    out.lx = leftOut.x;
    out.ly = leftOut.y;
    out.rx = rightOut.x;
    out.ry = rightOut.y;
    out.l2 = processTrigger(in.l2, p.l2);
    out.r2 = processTrigger(in.r2, p.r2);

    for (int i = 0; i < kButtonCount; ++i) {
        const Button src = buttonAt(i);
        if (!has(usable, src) || !isRemapSource(src)) continue;
        const auto& target = p.buttons[static_cast<size_t>(i)];
        if (!target) continue;
        if (*target == Button::L2) {
            out.l2 = 1.0f;
        } else if (*target == Button::R2) {
            out.r2 = 1.0f;
        } else {
            out.buttons |= bit(*target);
        }
    }
    return out;
}

void Pipeline::reset() {
    previous_ = 0;
    suppressed_ = 0;
    leftFilter_.reset();
    rightFilter_.reset();
}

}  // namespace edgepad
