#include "core/pipeline.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

#include "core/axis.hpp"

namespace edgepad {
namespace {

constexpr ButtonMask kComboMask =
    bit(Button::Cross) | bit(Button::Circle) | bit(Button::Square) | bit(Button::Triangle) | bit(Button::Options);

constexpr ButtonMask passthroughMask() {
    ButtonMask mask = 0;
    for (int i = 0; i < kButtonCount; ++i) {
        const Button b = buttonAt(i);
        if (isRemapTarget(b) && b != Button::L2 && b != Button::R2) mask |= bit(b);
    }
    return mask;
}
constexpr ButtonMask kPassthroughMask = passthroughMask();

constexpr ButtonMask remapSourceMask() {
    ButtonMask mask = 0;
    for (int i = 0; i < kButtonCount; ++i) {
        if (isRemapSource(buttonAt(i))) mask |= bit(buttonAt(i));
    }
    return mask;
}
constexpr ButtonMask kRemapSourceMask = remapSourceMask();

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

std::array<uint8_t, 3> lightbarColor(const Profile& p, double seconds, int battery, bool charging) {
    const LightbarSettings& l = p.light;
    const double period = std::clamp(static_cast<double>(l.periodSeconds), 0.5, 30.0);
    const float phase = static_cast<float>(std::fmod(std::max(seconds, 0.0) / period, 1.0));  // 0..1 through one cycle
    auto mix = [](const std::array<uint8_t, 3>& a, const std::array<uint8_t, 3>& b, float t) {
        std::array<float, 3> c{};
        for (size_t i = 0; i < 3; ++i) c[i] = static_cast<float>(a[i]) + (static_cast<float>(b[i]) - a[i]) * t;
        return c;
    };
    std::array<float, 3> color{static_cast<float>(p.lightbar[0]), static_cast<float>(p.lightbar[1]),
                               static_cast<float>(p.lightbar[2])};
    float level = 1.0f;
    switch (l.effect) {
        case LightEffect::Breathing:  // smooth in and out, never fully dark
            level = 0.08f + 0.92f * (0.5f - 0.5f * std::cos(6.2831853f * phase));
            break;
        case LightEffect::Rainbow: {  // around the color wheel
            const float h = phase * 6.0f;
            const float x = 1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f);
            const int sector = static_cast<int>(h) % 6;
            const std::array<std::array<float, 3>, 6> rgb{{{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}}};
            for (size_t i = 0; i < 3; ++i) color[i] = 255.0f * rgb[static_cast<size_t>(sector)][i];
            break;
        }
        case LightEffect::Cycle: {  // fade from color to color, the profile color first
            const int count = std::clamp(l.colorCount, 2, 4);
            std::array<std::array<uint8_t, 3>, 4> colors{p.lightbar, l.extraColors[0], l.extraColors[1], l.extraColors[2]};
            const float pos = phase * static_cast<float>(count);
            const int from = std::min(static_cast<int>(pos), count - 1);
            const float t = pos - static_cast<float>(from);
            const float smooth = t * t * (3.0f - 2.0f * t);
            color = mix(colors[static_cast<size_t>(from)], colors[static_cast<size_t>((from + 1) % count)], smooth);
            break;
        }
        case LightEffect::Battery: {  // red when empty, yellow at half, green when full; breathes while charging
            if (battery >= 0) {
                const float b = std::clamp(static_cast<float>(battery) / 100.0f, 0.0f, 1.0f);
                color = b < 0.5f ? mix({255, 0, 0}, {255, 200, 0}, b * 2.0f) : mix({255, 200, 0}, {0, 255, 40}, (b - 0.5f) * 2.0f);
                if (charging) level = 0.35f + 0.65f * (0.5f - 0.5f * std::cos(6.2831853f * phase));
            }
            break;
        }
        case LightEffect::Static:
        case LightEffect::Count:
            break;
    }
    const float scale = level * std::clamp(l.brightness, 0.05f, 1.0f);
    std::array<uint8_t, 3> out{};
    for (size_t i = 0; i < 3; ++i) out[i] = static_cast<uint8_t>(std::clamp(color[i] * scale, 0.0f, 255.0f) + 0.5f);
    return out;
}

dualsense::Effects effectsForConfig(const Config& cfg, uint8_t rumbleLarge, uint8_t rumbleSmall,
                                    const std::optional<std::array<uint8_t, 3>>& gameLightbar, double seconds,
                                    int battery, bool charging) {
    dualsense::Effects fx;
    const Profile& p = cfg.active();
    fx.lightbar = (cfg.settings.gameLightbar && gameLightbar) ? *gameLightbar : lightbarColor(p, seconds, battery, charging);
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
    out.raw = in.raw;
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
        out.buttons = (usable & kPassthroughMask) | (in.buttons & (bit(Button::L2) | bit(Button::R2)));
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
    leftOut = leftFilter_.amplify(leftOut, p.leftStick.rcFilter, dtSeconds, leftActive);
    rightOut = rightFilter_.amplify(rightOut, p.rightStick.rcFilter, dtSeconds, rightActive);
    if (p.swapSticks) std::swap(leftOut, rightOut);

    // Gyro aiming adds to the right stick.
    const Vec2 gyro = gyro_.update(in.motion, p.gyro, cfg.settings.gyroBias, has(pressed, p.gyro.button),
                                   has(newlyPressed, p.gyro.button), dtSeconds);
    if (gyro.x != 0.0f || gyro.y != 0.0f) {
        // Never longer than full deflection, or than the stick alone (whose corners may reach a
        // bit further): without gyro movement the stick stays exactly as it is.
        const float limit = std::max(1.0f, vectorLength(rightOut.x, rightOut.y));
        rightOut.x += gyro.x;
        rightOut.y += gyro.y;
        if (const float len = vectorLength(rightOut.x, rightOut.y); len > limit) {
            rightOut.x *= limit / len;
            rightOut.y *= limit / len;
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

    // Only buttons that are held, toggled on or firing turbo can produce anything or have state to
    // clear: every other button is skipped (usually all but a few).
    for (ButtonMask visit = (sources | toggled_ | turboRunning_) & kRemapSourceMask; visit != 0; visit &= visit - 1) {
        const int i = std::countr_zero(visit);
        const Button src = buttonAt(i);
        const ButtonMask m = bit(src);
        const size_t k = static_cast<size_t>(i);
        const bool held = has(sources, src);
        const Binding& shifted = p.shiftButtons[k];
        const Binding& b =
            (has(shiftLatched_, src) && shifted.kind != Binding::Kind::Inherit) ? shifted : p.buttons[k];

        bool active = held;
        if (b.toggle) {
            if (has(newSources, src)) toggled_ ^= m;
            active = has(toggled_, src);
        } else {
            toggled_ &= ~m;
        }
        if (b.turbo) {
            turboRunning_ = active ? (turboRunning_ | m) : (turboRunning_ & ~m);
            active = turbo_[k].update(active, b.turboIntervalMs, dtSeconds, b.turboRandomMs);
        } else {
            turbo_[k].reset();
            turboRunning_ &= ~m;
        }
        if (active) emit(b, out);
    }
    return out;
}

void Pipeline::resetLayers() {
    prevSources_ = 0;
    shiftLatched_ = 0;
    zoneLatched_.reset();
    toggled_ = 0;
    turboRunning_ = 0;
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
