#include "app/engine.hpp"

#include <chrono>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>

#include "core/pipeline.hpp"
#include "core/virtual_dualsense.hpp"
#include "core/virtual_reports.hpp"
#include "platform/controller_hide.hpp"
#include "platform/hid_device.hpp"
#include "platform/keyboard.hpp"
#include "platform/paths.hpp"

namespace edgepad {

using Clock = std::chrono::steady_clock;

namespace {

// Smooth, repeating fake input used by --demo.
InputState simulatedInput(float t) {
    InputState in;
    const float radius = 0.5f + 0.5f * std::sin(t * 0.7f);
    in.lx = radius * std::cos(t * 1.3f);
    in.ly = radius * std::sin(t * 1.3f);
    in.rx = 0.35f * std::sin(t * 0.9f);
    in.ry = 0.2f * std::sin(t * 1.8f);
    in.l2 = 0.5f + 0.5f * std::sin(t * 1.1f);
    in.r2 = std::fabs(std::fmod(t * 0.4f, 2.0f) - 1.0f);
    constexpr Button kCycle[] = {Button::Cross, Button::Circle, Button::PaddleLeft, Button::Square,
                                 Button::Triangle, Button::PaddleRight, Button::L1, Button::R1};
    const int slot = static_cast<int>(t / 0.6f) % static_cast<int>(std::size(kCycle));
    in.buttons = bit(kCycle[slot]);
    in.battery = 80;
    return in;
}

}  // namespace

Engine::Engine(Config config, bool demo, std::filesystem::path dataDir)
    : shared_(std::move(config)), demo_(demo), dataDir_(std::move(dataDir)) {
    shared_.normalize();
    status_.activeProfile = shared_.settings.activeProfile;
    status_.enabled = shared_.settings.enabled;
}

Engine::~Engine() { stop(); }

void Engine::start() {
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread([this] { run(); });
}

void Engine::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

void Engine::updateConfig(const Config& config, uint64_t seenRevision) {
    std::lock_guard lock(mutex_);
    const Settings engineSettings = shared_.settings;
    shared_ = config;
    if (seenRevision != status_.revision) {
        shared_.settings.activeProfile = engineSettings.activeProfile;
        shared_.settings.enabled = engineSettings.enabled;
    }
    shared_.normalize();
    pendingConfig_ = true;
}

Config Engine::config() const {
    std::lock_guard lock(mutex_);
    return shared_;
}

EngineStatus Engine::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void Engine::retryVirtualPad() { retryPad_ = true; }

void Engine::run() {
    raiseThreadPriority();
    Config cfg = config();
    // Hides the real controller from games (never in demo mode). Undoes everything on exit.
    std::optional<ControllerHider> hider;
    if (!demo_) hider.emplace(dataDir_ / "hide-controller-state.txt");
    bool hideApplied = false;  // the open controller was set up with "hide controller"
    DualSenseDevice device;
    Pipeline pipeline;

    std::unique_ptr<VirtualPad> pad;
    OutputKind padKind = OutputKind::None;
    OutputKind lastAttemptKind = OutputKind::Count;
    Clock::time_point nextPadAttempt{};
    Clock::time_point nextScan{};
    // Lightbar, LEDs, trigger effects and rumble are written at most this often (the latest state
    // wins): a write can block for a few ms, especially over Bluetooth, and must never hold up
    // reading the controller - with game rumble changing every frame it otherwise could.
    constexpr auto kEffectsInterval = std::chrono::milliseconds(10);
    Clock::time_point nextEffectsWrite{};
    // Virtual DualSense: games drive parts of the real controller through it (dualsense::kPart*).
    uint8_t gameParts = 0;
    bool featuresSent = false;  // the virtual DualSense has the controller's feature reports
    int lastProfile = -1;
    Clock::time_point nextPadStatus{};

    const Clock::time_point demoStart = Clock::now();
    Clock::time_point lastReport = Clock::now();
    Clock::time_point rateWindowStart = Clock::now();
    int reportsInWindow = 0;

    const FeedbackCallback onFeedback = [this](const PadFeedback& fb) {
        rumble_ = static_cast<uint16_t>((fb.largeMotor << 8) | fb.smallMotor);
        if (fb.hasLightbar) {
            gameLightbar_ = (1u << 24) | (uint32_t{fb.lightbar[0]} << 16) | (uint32_t{fb.lightbar[1]} << 8) | fb.lightbar[2];
        }
    };
    auto setHideStatus = [&](bool hidden, std::string message) {
        std::lock_guard lock(mutex_);
        status_.controllerHidden = hidden;
        status_.hideMessage = std::move(message);
    };
    // Opens the controller, hidden from games when that is wanted.
    auto openController = [&](const HidDeviceInfo& info) {
        const bool hide = cfg.settings.hideController;
        std::string reason;
        const bool exclusive = hide && hider->needsExclusiveOpen(reason);
        std::string error;
        bool inUse = false;
        bool hidden = false;
        std::string message;
        bool opened = device.open(info, error, exclusive, &inUse);
        if (!opened && exclusive) {
            opened = device.open(info, error);  // still use it, just not hidden
            message = inUse ? "Not hidden: another program (Steam, DS4Windows, a game...) has the controller open. Close "
                              "it and reconnect the controller, or install HidHide (" + reason + ")."
                            : "Not hidden: " + error + " (" + reason + ").";
        } else if (opened && exclusive) {
            hidden = true;
            message = "Hidden from games: EdgePad has the controller to itself (" + reason + ").";
        } else if (opened && hide) {
            hidden = hider->hide(info.path, message);
            if (!hidden) message = "Not hidden: " + message + ".";
        }
        if (!opened) return false;
        if (!hide) hider->restore();  // switched off while the controller was away
        hideApplied = hide;
        setHideStatus(hidden, hide ? message : std::string());
        return true;
    };
    auto dropPad = [&] {
        pad.reset();
        rumble_ = 0;
        gameLightbar_ = 0;
    };

    // Keyboard keys / mouse buttons for key bindings. Only changes are sent, and everything is
    // released when the controller goes away or EdgePad stops, so no key can get stuck.
    std::unique_ptr<KeyboardOutput> keyboard;
    KeyMask keysDown;
    bool keyboardFailed = false;
    auto syncKeys = [&](const KeyMask& wanted) {
        if (wanted == keysDown || demo_) return;  // demo mode never types real keys
        if (!keyboard && !keyboardFailed) {
            std::string error;
            keyboard = createKeyboardOutput(error);
            keyboardFailed = !keyboard;
            std::lock_guard lock(mutex_);
            status_.keyboardMessage = error;
        }
        if (!keyboard) return;
        for (int i = 1; i < kKeyCount; ++i) {
            const Key k = static_cast<Key>(i);
            if (wanted.test(k) != keysDown.test(k)) keyboard->set(k, wanted.test(k));
        }
        keysDown = wanted;
    };

    while (!stop_) {
        {
            std::lock_guard lock(mutex_);
            if (pendingConfig_) {
                cfg = shared_;
                pendingConfig_ = false;
            }
        }
        const Clock::time_point now = Clock::now();

        // Controller connection.
        if (demo_ && !status_.connected) {
            std::lock_guard lock(mutex_);
            status_.connected = true;
            status_.controllerName = "Simulated DualSense Edge (demo)";
            status_.connection = dualsense::Connection::Usb;
        }
        if (!demo_ && !device.isOpen()) {
            if (pad) dropPad();
            {
                std::lock_guard lock(mutex_);
                status_.connected = false;
                status_.reportRate = 0.0f;
                status_.input = {};
                status_.output = {};
                status_.padName.clear();
            }
            if (now >= nextScan) {
                nextScan = now + std::chrono::seconds(1);
                for (const auto& info : enumerateControllers()) {
                    if (openController(info)) {
                        pipeline.reset();
                        featuresSent = false;
                        lastReport = Clock::now();
                        lastAttemptKind = OutputKind::Count;
                        std::lock_guard lock(mutex_);
                        status_.connected = true;
                        status_.controllerName = dualsense::productName(info.productId);
                        break;
                    }
                }
            }
            if (!device.isOpen()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }

        // "Hide controller" switched on or off: open the controller again the new way.
        if (!demo_ && device.isOpen() && cfg.settings.hideController != hideApplied) {
            syncKeys(KeyMask{});
            device.close();
            if (!cfg.settings.hideController) {
                hider->restore();
                setHideStatus(false, {});
            }
            nextScan = now;
            continue;
        }

        // Virtual controller.
        const OutputKind wanted = cfg.settings.output;
        if (pad && padKind != wanted) dropPad();
        if (!pad && wanted != OutputKind::None &&
            (wanted != lastAttemptKind || now >= nextPadAttempt || retryPad_.exchange(false))) {
            lastAttemptKind = wanted;
            PadCreateResult created = createVirtualPad(wanted, onFeedback);
            std::lock_guard lock(mutex_);
            if (created.pad) {
                pad = std::move(created.pad);
                padKind = wanted;
                gameParts = 0;
                featuresSent = false;
                status_.padName = pad->name();
                status_.padWarning = pad->warning();
                status_.padMessage.clear();
                status_.padError = PadError::None;
            } else {
                nextPadAttempt = now + std::chrono::seconds(5);
                status_.padName.clear();
                status_.padMessage = created.message;
                status_.padError = created.error;
            }
        }
        if (wanted == OutputKind::None) {
            std::lock_guard lock(mutex_);
            status_.padName.clear();
            status_.padMessage = "Monitor only: no virtual controller";
            status_.padError = PadError::None;
        }
        if (pad && !featuresSent) {
            // A virtual DualSense answers with the real controller's calibration, MAC and firmware info.
            pad->setFeatureReports(demo_ ? std::map<uint8_t, std::vector<uint8_t>>{} : device.featureReports());
            featuresSent = true;
        }
        if (pad && now >= nextPadStatus) {  // e.g. the virtual DualSense finished connecting
            nextPadStatus = now + std::chrono::milliseconds(250);
            std::string name = pad->name(), warning = pad->warning();
            std::lock_guard lock(mutex_);
            status_.padName = std::move(name);
            status_.padWarning = std::move(warning);
        } else if (!pad) {
            std::lock_guard lock(mutex_);
            status_.padWarning.clear();
        }

        // Controller input.
        InputState input;
        auto result = DualSenseDevice::ReadResult::Data;
        if (demo_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            input = simulatedInput(std::chrono::duration<float>(Clock::now() - demoStart).count());
        } else {
            result = device.read(input, 20);
        }
        if (result == DualSenseDevice::ReadResult::Error) {
            syncKeys(KeyMask{});
            device.close();
            continue;
        }
        if (result == DualSenseDevice::ReadResult::Data) {
            const Clock::time_point t = Clock::now();
            const float dt = std::chrono::duration<float>(t - lastReport).count();
            lastReport = t;

            PipelineEvents events;
            const OutputState output = pipeline.process(input, cfg, dt, &events);

            if (pad) {
                // Sent with every controller report, changed or not: ViGEmBus drops an update that
                // arrives while the game side has no read waiting (and still reports success), so
                // the next report puts the current state through within a few ms. Repeating an
                // unchanged report costs next to nothing.
                OutputState toPad = output;
                if (padKind == OutputKind::DualShock4) {
                    // Motion: the game computes the same degrees per second and g from the virtual
                    // controller as from the real one (their calibrations differ).
                    toPad.motion = virtual_reports::remapMotion(
                        output.motion, demo_ ? virtual_reports::nominalCalibration() : device.motionCalibration(),
                        virtual_reports::virtualDs4Calibration());
                }
                if (!pad->send(toPad)) {
                    dropPad();
                    nextPadAttempt = t + std::chrono::seconds(2);
                    std::lock_guard lock(mutex_);
                    status_.padName.clear();
                    status_.padMessage = "Virtual controller was removed";
                }
            }
            syncKeys(output.keys);

            ++reportsInWindow;
            const float window = std::chrono::duration<float>(t - rateWindowStart).count();
            std::lock_guard lock(mutex_);
            if (window >= 1.0f) {
                status_.reportRate = static_cast<float>(reportsInWindow) / window;
                reportsInWindow = 0;
                rateWindowStart = t;
            }
            if (events.profileChanged || events.enabledChanged) {
                shared_.settings.activeProfile = cfg.settings.activeProfile;
                shared_.settings.enabled = cfg.settings.enabled;
                ++status_.revision;
            }
            status_.connected = true;
            if (!demo_) status_.connection = device.connection();
            status_.battery = input.battery;
            status_.charging = input.charging;
            status_.input = input;
            status_.output = output;
            status_.gyroActive = pipeline.gyroActive();
            status_.activeProfile = cfg.settings.activeProfile;
            status_.enabled = cfg.settings.enabled;
        }

        // Lightbar, player LEDs, adaptive triggers and rumble back to the controller.
        const uint16_t rumble = pad ? rumble_.load() : 0;
        std::optional<std::array<uint8_t, 3>> gameLightbar;
        if (const uint32_t lb = gameLightbar_.load(); pad && padKind == OutputKind::DualShock4 && (lb & (1u << 24))) {
            gameLightbar = std::array<uint8_t, 3>{static_cast<uint8_t>(lb >> 16), static_cast<uint8_t>(lb >> 8),
                                                  static_cast<uint8_t>(lb)};
        }
        dualsense::Effects effects =
            effectsForConfig(cfg, static_cast<uint8_t>(rumble >> 8), static_cast<uint8_t>(rumble & 0xFF), gameLightbar);
        if (pad && padKind == OutputKind::DualSense) {
            // Games drive the real controller through the virtual DualSense, like on a PS5:
            // adaptive triggers, rumble, lightbar. A profile's trigger resistance and (unless games
            // may set it) the profile lightbar stay EdgePad's.
            const Profile& profile = cfg.active();
            const bool profileTriggers = profile.l2.resistance != TriggerResistance::Off ||
                                         profile.r2.resistance != TriggerResistance::Off;
            if (cfg.settings.activeProfile != lastProfile) {
                lastProfile = cfg.settings.activeProfile;
                gameParts &= static_cast<uint8_t>(~dualsense::kPartTriggers);  // clear a previous profile's wall
            }
            const virtual_dualsense::OutputFilter filter{cfg.settings.rumble, cfg.settings.gameLightbar, !profileTriggers};
            for (const auto& report : pad->takeOutputReports()) {
                const auto common = virtual_dualsense::filterGameOutput(report.data(), report.size(), filter);
                if (!common) continue;
                gameParts |= virtual_dualsense::partsChanged(*common);
                if (device.isOpen()) device.sendOutput(common->data());
            }
            effects.parts = 0;
            if (!(cfg.settings.gameLightbar && (gameParts & dualsense::kPartLights))) effects.parts |= dualsense::kPartLights;
            if (profileTriggers || !(gameParts & dualsense::kPartTriggers)) effects.parts |= dualsense::kPartTriggers;
        }
        if (device.isOpen() && effects.parts != 0 && device.effectsChanged(effects)) {
            if (const Clock::time_point t = Clock::now(); t >= nextEffectsWrite) {
                device.sendEffects(effects);
                nextEffectsWrite = t + kEffectsInterval;
            }
        }
    }

    syncKeys(KeyMask{});
    pad.reset();
    // Leave the controller in a neutral state: no trigger resistance, no rumble.
    dualsense::Effects neutral;
    neutral.lightbar = {0, 0, 64};
    device.sendEffects(neutral);
    device.close();
    if (hider) hider->restore();  // the controller is visible to other programs again
}

}  // namespace edgepad
