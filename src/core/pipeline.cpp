#include "core/pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/axis.hpp"

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

// Digital L2 / R2 (a DualShock 4 report carries them next to the analog value): the controller's
// own bit while the trigger reaches the game unchanged (1:1 settings), otherwise "pressed at all",
// so the bit always agrees with the analog value the game gets (hair trigger, trigger stop...).
ButtonMask triggerButton(const InputState& in, float out, Button b, bool oneToOne) {
    const bool pressed = oneToOne ? has(in.buttons, b) : triggerToRaw(out) > 0;
    return pressed ? bit(b) : 0;
}

void emit(const Binding& b, OutputState& out) {
    switch (b.kind) {
        case Binding::Kind::Button:
            if (b.button == Button::L2) {
                out.l2 = 1.0f;  // a full press, digital bit included
            } else if (b.button == Button::R2) {
                out.r2 = 1.0f;
            }
            out.buttons |= bit(b.button);
            break;
        case Binding::Kind::Key:
            out.keys.set(b.key);
            break;
        case Binding::Kind::Inherit:
        case Binding::Kind::Disabled:
            break;
    }
}

}  // namespace

ButtonMask fnSourceMask(FnMode mode) {
    switch (mode) {
        case FnMode::Auto: return bit(Button::FnLeft) | bit(Button::FnRight) | bit(Button::Mute);
        case FnMode::Edge: return bit(Button::FnLeft) | bit(Button::FnRight);
        case FnMode::LeftFn: return bit(Button::FnLeft);
        case FnMode::RightFn: return bit(Button::FnRight);
        case FnMode::Mute: return bit(Button::Mute);
        case FnMode::Touchpad: return bit(Button::Touchpad);
        case FnMode::Disabled:
        case FnMode::Count: break;
    }
    return 0;
}

Button touchpadZoneAt(const TouchPoint& touch, TouchpadZones zones) {
    const bool left = touch.x < 960;
    const bool top = touch.y < 540;
    if (zones == TouchpadZones::Two) return left ? Button::TouchLeft : Button::TouchRight;
    if (top) return left ? Button::TouchTopLeft : Button::TouchTopRight;
    return left ? Button::TouchBottomLeft : Button::TouchBottomRight;
}

dualsense::Effects effectsForConfig(const Config& cfg, uint8_t rumbleLarge, uint8_t rumbleSmall,
                                    const std::optional<std::array<uint8_t, 3>>& gameLightbar) {
    dualsense::Effects fx;
    const Profile& p = cfg.active();
    fx.lightbar = (cfg.settings.gameLightbar && gameLightbar) ? *gameLightbar : p.lightbar;
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
    out.motion = in.motion;
    out.battery = in.battery;
    out.charging = in.charging;
    if (!cfg.settings.enabled) {
        leftFilter_.reset();
        rightFilter_.reset();
        l2_.reset();
        r2_.reset();
        gyro_.reset();
        resetLayers();
        out.lx = in.lx;
        out.ly = in.ly;
        out.rx = in.rx;
        out.ry = in.ry;
        out.l2 = in.l2;
        out.r2 = in.r2;
        out.buttons = (usable & passthroughMask()) | (in.buttons & (bit(Button::L2) | bit(Button::R2)));
        return out;
    }

    const Profile& p = cfg.active();
    if (cfg.settings.activeProfile != lastProfile_) {  // new profile: start with a clean slate
        lastProfile_ = cfg.settings.activeProfile;
        resetLayers();
        gyro_.reset();
    }

    // Sticks: drift correction, RC stabilizer, dead zones / curves, RC jitter.
    const Vec2 leftRaw = recenterStick({in.lx, in.ly}, cfg.settings.leftStickCenter);
    const Vec2 rightRaw = recenterStick({in.rx, in.ry}, cfg.settings.rightStickCenter);
    const bool leftActive = stickActive(leftRaw.x, leftRaw.y, p.leftStick);
    const bool rightActive = stickActive(rightRaw.x, rightRaw.y, p.rightStick);
    const Vec2 left = leftFilter_.apply(leftRaw, p.leftStick.rcFilter, dtSeconds, leftActive);
    const Vec2 right = rightFilter_.apply(rightRaw, p.rightStick.rcFilter, dtSeconds, rightActive);
    Vec2 leftOut = processStick(left.x, left.y, p.leftStick);
    Vec2 rightOut = processStick(right.x, right.y, p.rightStick);
    leftOut = leftFilter_.jitter(leftOut, p.leftStick.rcFilter, dtSeconds, leftActive);
    rightOut = rightFilter_.jitter(rightOut, p.rightStick.rcFilter, dtSeconds, rightActive);
    if (p.swapSticks) std::swap(leftOut, rightOut);

    // Gyro aiming adds to the right stick.
    const Vec2 gyro = gyro_.update(in.motion, p.gyro, cfg.settings.gyroBias, has(pressed, p.gyro.button),
                                   has(newlyPressed, p.gyro.button), dtSeconds);
    if (gyro.x != 0.0f || gyro.y != 0.0f) {
        // Never longer than full deflection, or than the stick alone (whose corners may reach a
        // bit further): without gyro movement the stick stays exactly as it is.
        const float limit = std::max(1.0f, std::hypot(rightOut.x, rightOut.y));
        rightOut.x += gyro.x;
        rightOut.y += gyro.y;
        if (const float length = std::hypot(rightOut.x, rightOut.y); length > limit) {
            rightOut.x *= limit / length;
            rightOut.y *= limit / length;
        }
    }
    out.lx = leftOut.x;
    out.ly = leftOut.y;
    out.rx = rightOut.x;
    out.ry = rightOut.y;
    out.l2 = l2_.apply(in.l2, p.l2, dtSeconds);
    out.r2 = r2_.apply(in.r2, p.r2, dtSeconds);
    out.buttons |= triggerButton(in, out.l2, Button::L2, isOneToOne(p.l2)) |
                   triggerButton(in, out.r2, Button::R2, isOneToOne(p.r2));

    // Touchpad zones: where the pad is clicked (or touched) becomes its own button.
    ButtonMask sources = usable;
    if (p.touchpadZones != TouchpadZones::Off) {
        const bool clickMode = p.zoneTrigger == ZoneTrigger::Click;
        const bool pressedZone = clickMode ? has(usable, Button::Touchpad) : in.motion.touch[0].active;
        if (pressedZone) {
            if (!zoneLatched_) zoneLatched_ = touchpadZoneAt(in.motion.touch[0], p.touchpadZones);
            sources |= bit(*zoneLatched_);
        } else {
            zoneLatched_.reset();
        }
        if (clickMode) sources &= ~bit(Button::Touchpad);
    } else {
        zoneLatched_.reset();
    }

    // Shift layer: a button uses the layer that was active when it went down, until released.
    const bool shiftHeld = p.shiftButton && has(sources, *p.shiftButton);
    if (p.shiftButton) sources &= ~bit(*p.shiftButton);
    const ButtonMask newSources = sources & ~prevSources_;
    shiftLatched_ = (shiftLatched_ & sources) | (shiftHeld ? newSources : 0);
    prevSources_ = sources;

    for (int i = 0; i < kButtonCount; ++i) {
        const Button src = buttonAt(i);
        if (!isRemapSource(src)) continue;
        const size_t k = static_cast<size_t>(i);
        const bool held = has(sources, src);
        const Binding& shifted = p.shiftButtons[k];
        const Binding& b =
            (has(shiftLatched_, src) && shifted.kind != Binding::Kind::Inherit) ? shifted : p.buttons[k];

        bool active = held;
        if (b.toggle) {
            if (has(newSources, src)) toggled_[k] = !toggled_[k];
            active = toggled_[k];
        } else {
            toggled_[k] = false;
        }
        if (b.turbo) {
            active = turbo_[k].update(active, b.turboIntervalMs, dtSeconds);
        } else {
            turbo_[k].reset();
        }
        if (active) emit(b, out);
    }
    return out;
}

void Pipeline::resetLayers() {
    prevSources_ = 0;
    shiftLatched_ = 0;
    zoneLatched_.reset();
    toggled_.fill(false);
    for (auto& t : turbo_) t.reset();
}

void Pipeline::reset() {
    previous_ = 0;
    suppressed_ = 0;
    lastProfile_ = -1;
    resetLayers();
    leftFilter_.reset();
    rightFilter_.reset();
    l2_.reset();
    r2_.reset();
    gyro_.reset();
}

}  // namespace edgepad
