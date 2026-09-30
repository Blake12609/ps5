#include "app/gui.hpp"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/build_info.hpp"
#include "core/pipeline.hpp"
#include "core/processing.hpp"
#include "platform/paths.hpp"
#include "platform/self_update.hpp"
#include "platform/virtual_pad.hpp"

namespace fs = std::filesystem;

namespace edgepad {
namespace {

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
constexpr ImU32 kAccent = IM_COL32(79, 140, 255, 255);
constexpr ImU32 kAccentSoft = IM_COL32(79, 140, 255, 60);
constexpr ImU32 kGood = IM_COL32(53, 196, 124, 255);
constexpr ImU32 kWarn = IM_COL32(240, 160, 48, 255);
constexpr ImU32 kBad = IM_COL32(255, 93, 93, 255);
constexpr ImU32 kMuted = IM_COL32(154, 163, 178, 255);
constexpr ImU32 kPanel = IM_COL32(28, 31, 38, 255);
constexpr ImU32 kWell = IM_COL32(20, 22, 27, 255);
constexpr ImU32 kGrid = IM_COL32(46, 51, 64, 255);
constexpr ImU32 kDeadzone = IM_COL32(255, 93, 93, 40);
constexpr ImU32 kRaw = IM_COL32(154, 163, 178, 200);

ImVec4 rgb(int r, int g, int b, int a = 255) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }

void applyTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowPadding = ImVec2(16, 14);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.GrabMinSize = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.SeparatorTextBorderSize = 1.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = rgb(230, 232, 238);
    c[ImGuiCol_TextDisabled] = rgb(154, 163, 178);
    c[ImGuiCol_WindowBg] = rgb(20, 22, 27);
    c[ImGuiCol_ChildBg] = rgb(28, 31, 38);
    c[ImGuiCol_PopupBg] = rgb(28, 31, 38);
    c[ImGuiCol_Border] = rgb(46, 51, 64);
    c[ImGuiCol_FrameBg] = rgb(38, 42, 52);
    c[ImGuiCol_FrameBgHovered] = rgb(47, 53, 66);
    c[ImGuiCol_FrameBgActive] = rgb(55, 62, 78);
    c[ImGuiCol_CheckMark] = rgb(79, 140, 255);
    c[ImGuiCol_SliderGrab] = rgb(79, 140, 255);
    c[ImGuiCol_SliderGrabActive] = rgb(120, 168, 255);
    c[ImGuiCol_Button] = rgb(38, 42, 52);
    c[ImGuiCol_ButtonHovered] = rgb(50, 57, 72);
    c[ImGuiCol_ButtonActive] = rgb(58, 120, 240);
    c[ImGuiCol_Header] = rgb(42, 48, 64);
    c[ImGuiCol_HeaderHovered] = rgb(50, 58, 77);
    c[ImGuiCol_HeaderActive] = rgb(58, 120, 240);
    c[ImGuiCol_Separator] = rgb(46, 51, 64);
    c[ImGuiCol_Tab] = rgb(28, 31, 38);
    c[ImGuiCol_TabHovered] = rgb(50, 57, 72);
    c[ImGuiCol_TabSelected] = rgb(42, 48, 64);
    c[ImGuiCol_TabSelectedOverline] = rgb(79, 140, 255);
    c[ImGuiCol_TableHeaderBg] = rgb(33, 37, 46);
    c[ImGuiCol_TableBorderStrong] = rgb(46, 51, 64);
    c[ImGuiCol_TableBorderLight] = rgb(38, 42, 52);
    c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 6);
    c[ImGuiCol_ScrollbarBg] = rgb(20, 22, 27);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 140);
    style.ScaleAllSizes(scale);
}

struct Fonts {
    ImFont* body = nullptr;
    ImFont* title = nullptr;
};

Fonts loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    Fonts fonts;
    const std::array<const char*, 5> bodyCandidates{
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    };
    const std::array<const char*, 5> boldCandidates{
        "C:\\Windows\\Fonts\\segoeuib.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
    };
    for (const char* path : bodyCandidates) {
        std::error_code ec;
        if (fs::exists(path, ec)) {
            fonts.body = io.Fonts->AddFontFromFileTTF(path, 17.0f * scale);
            if (fonts.body) break;
        }
    }
    for (const char* path : boldCandidates) {
        std::error_code ec;
        if (fs::exists(path, ec)) {
            fonts.title = io.Fonts->AddFontFromFileTTF(path, 24.0f * scale);
            if (fonts.title) break;
        }
    }
    if (!fonts.body) {
        ImFontConfig cfg;
        cfg.SizePixels = 15.0f * scale;
        fonts.body = io.Fonts->AddFontDefault(&cfg);
    }
    if (!fonts.title) fonts.title = fonts.body;
    io.FontDefault = fonts.body;
    return fonts;
}

// ---------------------------------------------------------------------------
// Small widgets
// ---------------------------------------------------------------------------
void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool sliderPercent(const char* label, float* value, float minPct, float maxPct, const char* help = nullptr,
                   const char* format = "%.0f%%") {
    float pct = *value * 100.0f;
    const bool changed = ImGui::SliderFloat(label, &pct, minPct, maxPct, format, ImGuiSliderFlags_AlwaysClamp);
    if (changed) *value = pct / 100.0f;
    if (help) helpMarker(help);
    return changed;
}

template <typename Enum, typename LabelFn>
bool enumCombo(const char* label, Enum& value, LabelFn labelOf) {
    bool changed = false;
    const std::string preview(labelOf(value));
    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (int i = 0; i < static_cast<int>(Enum::Count); ++i) {
            const Enum option = static_cast<Enum>(i);
            const bool selected = option == value;
            const std::string text(labelOf(option));
            if (ImGui::Selectable(text.c_str(), selected)) {
                value = option;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void statusDot(ImU32 color) {
    const float r = ImGui::GetFontSize() * 0.28f;
    // Callers align their text to frame padding, so centre the dot on the frame height.
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetFrameHeight() * 0.5f), r, color, 16);
    ImGui::Dummy(ImVec2(r * 2.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
}

const char* shapeLabel(DeadzoneShape s) { return s == DeadzoneShape::Axial ? "Axial (per axis)" : "Radial (circle)"; }
const char* triggerModeLabel(TriggerMode m) { return m == TriggerMode::HairTrigger ? "Hair trigger (rapid)" : "Analog"; }
const char* resistanceLabel(TriggerResistance r) { return r == TriggerResistance::Wall ? "Wall (feels like a trigger stop)" : "Off"; }

// ---------------------------------------------------------------------------
// Visualisations
// ---------------------------------------------------------------------------
void drawStickView(const char* id, float size, const StickSettings& s, float rawX, float rawY, float outX, float outY) {
    ImGui::InvisibleButton(id, ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 c = p0 + ImVec2(size * 0.5f, size * 0.5f);
    const float r = size * 0.44f;

    dl->AddRectFilled(p0, p0 + ImVec2(size, size), kWell, 10.0f);
    dl->AddCircleFilled(c, r, kPanel, 64);
    dl->AddLine(c - ImVec2(r, 0), c + ImVec2(r, 0), kGrid);
    dl->AddLine(c - ImVec2(0, r), c + ImVec2(0, r), kGrid);
    if (s.shape == DeadzoneShape::Radial) {
        dl->AddCircleFilled(c, r * s.deadzone, kDeadzone, 48);
    } else {
        const float d = r * s.deadzone;
        dl->AddRectFilled(c - ImVec2(r, d), c + ImVec2(r, d), kDeadzone);
        dl->AddRectFilled(c - ImVec2(d, r), c + ImVec2(d, r), kDeadzone);
    }
    dl->AddCircle(c, r * (1.0f - s.outerDeadzone), kGrid, 64, 1.0f);
    dl->AddCircle(c, r, IM_COL32(70, 78, 96, 255), 64, 1.5f);

    const ImVec2 raw = c + ImVec2(clamp11(rawX) * r, -clamp11(rawY) * r);
    const ImVec2 out = c + ImVec2(clamp11(outX) * r, -clamp11(outY) * r);
    dl->AddLine(c, out, kAccentSoft, 2.0f);
    dl->AddCircleFilled(raw, size * 0.022f, kRaw, 16);
    dl->AddCircleFilled(out, size * 0.035f, kAccent, 20);
    dl->AddText(p0 + ImVec2(8, 6), kMuted, "raw");
    dl->AddText(p0 + ImVec2(8, 6 + ImGui::GetTextLineHeight()), kAccent, "output");
}

// Response curve. In Custom mode the points can be dragged.
bool drawCurveView(const char* id, float size, StickSettings& s, float rawMagnitude, int& dragIndex) {
    ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const float pad = size * 0.08f;
    const ImVec2 a = p0 + ImVec2(pad, pad);
    const ImVec2 b = p0 + ImVec2(size - pad, size - pad);
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    auto toScreen = [&](float x, float y) { return ImVec2(a.x + x * w, b.y - y * h); };

    dl->AddRectFilled(p0, p0 + ImVec2(size, size), kWell, 10.0f);
    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        dl->AddLine(toScreen(t, 0), toScreen(t, 1), kGrid);
        dl->AddLine(toScreen(0, t), toScreen(1, t), kGrid);
    }
    dl->AddRect(a, b, kGrid);
    dl->AddLine(toScreen(0, 0), toScreen(1, 1), IM_COL32(70, 78, 96, 255), 1.0f);

    StickSettings plain = s;
    plain.invertX = plain.invertY = false;
    constexpr int kSamples = 96;
    std::array<ImVec2, kSamples + 1> points{};
    for (int i = 0; i <= kSamples; ++i) {
        const float x = static_cast<float>(i) / kSamples;
        points[static_cast<size_t>(i)] = toScreen(x, std::fabs(processStick(x, 0.0f, plain).x));
    }
    dl->AddPolyline(points.data(), static_cast<int>(points.size()), kAccent, ImDrawFlags_None, 2.5f);

    const float m = clamp01(rawMagnitude);
    dl->AddCircleFilled(toScreen(m, std::fabs(processStick(m, 0.0f, plain).x)), size * 0.025f, IM_COL32(255, 255, 255, 230), 16);
    dl->AddText(p0 + ImVec2(8, 6), kMuted, "response");

    bool changed = false;
    if (s.curve == Curve::Custom) {
        // Custom points live in "throw past the dead zone" space, drawn on the same axes.
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsItemActivated()) {
            dragIndex = -1;
            float best = size * 0.06f;
            for (size_t i = 0; i < s.customCurve.size(); ++i) {
                const ImVec2 p = toScreen(s.customCurve[i].x, s.customCurve[i].y);
                const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
                if (d < best) {
                    best = d;
                    dragIndex = static_cast<int>(i);
                }
            }
        }
        if (active && dragIndex >= 0 && dragIndex < static_cast<int>(s.customCurve.size())) {
            auto& pt = s.customCurve[static_cast<size_t>(dragIndex)];
            const float lo = dragIndex > 0 ? s.customCurve[static_cast<size_t>(dragIndex) - 1].x + 0.02f : 0.02f;
            const float hi = dragIndex + 1 < static_cast<int>(s.customCurve.size())
                                 ? s.customCurve[static_cast<size_t>(dragIndex) + 1].x - 0.02f
                                 : 0.98f;
            pt.x = std::clamp((mouse.x - a.x) / w, lo, std::max(lo, hi));
            pt.y = clamp01((b.y - mouse.y) / h);
            changed = true;
        }
        if (!active) dragIndex = -1;
        for (size_t i = 0; i < s.customCurve.size(); ++i) {
            const ImVec2 p = toScreen(s.customCurve[i].x, s.customCurve[i].y);
            const bool hot = static_cast<int>(i) == dragIndex;
            dl->AddCircleFilled(p, size * (hot ? 0.035f : 0.028f), hot ? kWarn : IM_COL32(255, 255, 255, 255), 16);
            dl->AddCircle(p, size * 0.035f, kAccent, 16, 1.5f);
        }
        if (hovered && !active) ImGui::SetTooltip("Drag the points to shape the curve");
    }
    return changed;
}

void drawTriggerView(const char* id, const TriggerSettings& t, float raw, float out) {
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() * 2.4f;
    ImGui::InvisibleButton(id, ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1 = ImGui::GetItemRectMax();
    dl->AddRectFilled(p0, p1, kWell, 8.0f);

    const float pad = 8.0f;
    const float x0 = p0.x + pad;
    const float x1 = p1.x - pad;
    const float span = x1 - x0;
    const float mid = (p0.y + p1.y) * 0.5f;
    auto x = [&](float v) { return x0 + clamp01(v) * span; };

    const bool hair = t.mode == TriggerMode::HairTrigger;
    if (!hair) {
        dl->AddRectFilled(ImVec2(x(t.deadzone), p0.y + pad), ImVec2(x(t.maxRange), p1.y - pad), IM_COL32(79, 140, 255, 28), 4.0f);
    }
    dl->AddRectFilled(ImVec2(x0, p0.y + pad), ImVec2(x(raw), mid - 2), kRaw, 4.0f);
    dl->AddRectFilled(ImVec2(x0, mid + 2), ImVec2(x(out), p1.y - pad), kAccent, 4.0f);
    const float start = hair ? std::max(t.deadzone, kHairMinActivation) : t.deadzone;
    dl->AddLine(ImVec2(x(start), p0.y + 4), ImVec2(x(start), p1.y - 4), kBad, 2.0f);
    if (!hair) dl->AddLine(ImVec2(x(t.maxRange), p0.y + 4), ImVec2(x(t.maxRange), p1.y - 4), kGood, 2.0f);
    if (t.resistance == TriggerResistance::Wall) {
        dl->AddLine(ImVec2(x(t.resistancePosition), p0.y + 2), ImVec2(x(t.resistancePosition), p1.y - 2), kWarn, 3.0f);
    }
    char label[48];
    std::snprintf(label, sizeof(label), "raw %3.0f%%   out %3.0f%%", raw * 100.0f, out * 100.0f);
    dl->AddText(ImVec2(x1 - ImGui::CalcTextSize(label).x, p0.y + pad), IM_COL32(230, 232, 238, 220), label);
}

// ---------------------------------------------------------------------------
// Application UI
// ---------------------------------------------------------------------------
class App {
    enum class Calibrating { None, Sticks, Gyro };

public:
    App(Engine& engine, const ConfigStore& store, Updater& updater, GuiOptions options, Fonts fonts)
        : engine_(engine), store_(store), updater_(updater), cfg_(std::move(options.config)),
          dataDir_(std::move(options.dataDir)), fonts_(fonts), notice_(std::move(options.startupWarning)) {
        cfg_.normalize();
        if (options.autoUpdate) updater_.check(true);
    }

    bool restartRequested() const { return restartRequested_; }

    void frame(double now) {
        now_ = now;
        status_ = engine_.status();
        if (status_.revision != seenRevision_) {
            // The controller switched profile / toggled remapping with an Fn combo.
            cfg_.settings.activeProfile = status_.activeProfile;
            cfg_.settings.enabled = status_.enabled;
            seenRevision_ = status_.revision;
            markDirty(now);
        }

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        // No title bar or resizing (the OS window does that), but keep the scrollbar for small screens.
        ImGui::Begin("EdgePad", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

        changed_ = false;
        drawHeader();
        drawProfileBar();
        drawBanner();
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Sticks")) {
                drawSticksTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Triggers")) {
                drawTriggersTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Buttons")) {
                drawButtonsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Gyro")) {
                drawGyroTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                drawSettingsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Help")) {
                drawHelpTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
        updateCalibration(now);

        if (changed_) {
            cfg_.normalize();
            engine_.updateConfig(cfg_, seenRevision_);
            markDirty(now);
        }
        if (dirty_ && now - lastEdit_ > 0.75) save();
    }

    void save() {
        std::string error;
        if (store_.save(cfg_, &error)) {
            dirty_ = false;
            saveError_.clear();
        } else {
            saveError_ = error;
            dirty_ = false;  // do not retry every frame; the next edit retries
        }
    }

private:
    Profile& profile() { return cfg_.active(); }
    void markDirty(double now) {
        dirty_ = true;
        lastEdit_ = now;
    }

    // -- header ---------------------------------------------------------------
    void drawHeader() {
        ImGui::PushFont(fonts_.title);
        ImGui::TextUnformatted("EdgePad");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("v%s", build::kVersion);

        ImGui::SameLine(0.0f, 28.0f);
        ImGui::AlignTextToFramePadding();
        if (status_.connected) {
            statusDot(kGood);
            std::string text = status_.controllerName;
            text += status_.connection == dualsense::Connection::Bluetooth ? "  ·  Bluetooth" : "  ·  USB";
            if (status_.battery >= 0) {
                text += "  ·  " + std::to_string(status_.battery) + "%";
                if (status_.charging) text += " charging";
            }
            ImGui::TextUnformatted(text.c_str());
        } else {
            statusDot(kMuted);
            ImGui::TextDisabled("Waiting for a DualSense or DualSense Edge...");
        }

        ImGui::SameLine(0.0f, 28.0f);
        if (!status_.padName.empty()) {
            statusDot(kGood);
            ImGui::TextUnformatted(status_.padName.c_str());
        } else if (cfg_.settings.output == OutputKind::None) {
            statusDot(kMuted);
            ImGui::TextDisabled("Monitor only");
        } else if (!status_.padMessage.empty()) {
            statusDot(kWarn);
            ImGui::TextUnformatted("No virtual controller");
            ImGui::SetItemTooltip("%s", status_.padMessage.c_str());
        }
        if (status_.connected && status_.reportRate > 0.0f) {
            ImGui::SameLine(0.0f, 28.0f);
            ImGui::TextDisabled("%.0f Hz", status_.reportRate);
        }

        const float toggleWidth = ImGui::CalcTextSize("Remapping on").x + ImGui::GetFrameHeight() * 2.0f;
        const float toggleX = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - toggleWidth;
        // Right-align the toggle, or wrap it to the next line when the status text is too long.
        if (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x < toggleX) {
            ImGui::SameLine(toggleX);
        }
        bool enabled = cfg_.settings.enabled;
        if (ImGui::Checkbox(enabled ? "Remapping on" : "Remapping off", &enabled)) {
            cfg_.settings.enabled = enabled;
            changed_ = true;
        }
        ImGui::SetItemTooltip("Off = raw passthrough. Fn + Options toggles this from the controller.");
        ImGui::Spacing();
    }

    // -- profiles -------------------------------------------------------------
    void drawProfileBar() {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Profile");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13.0f);
        if (ImGui::BeginCombo("##profile", profile().name.c_str())) {
            for (size_t i = 0; i < cfg_.profiles.size(); ++i) {
                const Profile& p = cfg_.profiles[i];
                std::string label = p.name;
                if (p.hotkey) label += "   (Fn + " + std::string(buttonLabel(*p.hotkey)) + ")";
                label += "##" + std::to_string(i);
                const bool selected = static_cast<int>(i) == cfg_.settings.activeProfile;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    cfg_.settings.activeProfile = static_cast<int>(i);
                    changed_ = true;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(cfg_.profiles.size() >= kMaxProfiles);
        if (ImGui::Button("New")) addProfile(Profile{}, "Profile " + std::to_string(cfg_.profiles.size() + 1));
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) addProfile(profile(), profile().name + " copy");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Rename")) {
            std::snprintf(renameBuffer_.data(), renameBuffer_.size(), "%s", profile().name.c_str());
            ImGui::OpenPopup("Rename profile");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(cfg_.profiles.size() <= 1);
        if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete profile");
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 24.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Fn +");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        const std::string hotkeyPreview = profile().hotkey ? std::string(buttonLabel(*profile().hotkey)) : "None";
        if (ImGui::BeginCombo("##hotkey", hotkeyPreview.c_str())) {
            if (ImGui::Selectable("None", !profile().hotkey)) {
                profile().hotkey.reset();
                changed_ = true;
            }
            for (Button b : kProfileHotkeys) {
                if (ImGui::Selectable(std::string(buttonLabel(b)).c_str(), profile().hotkey == b)) {
                    // Hotkeys are unique: steal it from any other profile.
                    for (auto& other : cfg_.profiles) {
                        if (other.hotkey == b) other.hotkey.reset();
                    }
                    profile().hotkey = b;
                    changed_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Hold Fn and press this button on the controller to switch to this profile");

        ImGui::SameLine(0.0f, 24.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Lightbar");
        ImGui::SameLine();
        auto& lb = profile().lightbar;
        float color[3] = {lb[0] / 255.0f, lb[1] / 255.0f, lb[2] / 255.0f};
        if (ImGui::ColorEdit3("##lightbar", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            for (size_t i = 0; i < 3; ++i) lb[i] = static_cast<uint8_t>(std::lround(clamp01(color[i]) * 255.0f));
            changed_ = true;
        }

        if (ImGui::BeginPopupModal("Rename profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##name", renameBuffer_.data(), renameBuffer_.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Save") || enter) {
                if (renameBuffer_[0] != '\0') {
                    profile().name = renameBuffer_.data();
                    changed_ = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete \"%s\"?", profile().name.c_str());
            if (ImGui::Button("Delete")) {
                cfg_.profiles.erase(cfg_.profiles.begin() + cfg_.settings.activeProfile);
                cfg_.settings.activeProfile = std::max(0, cfg_.settings.activeProfile - 1);
                changed_ = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Spacing();
    }

    void addProfile(Profile p, std::string name) {
        p.name = std::move(name);
        p.hotkey.reset();
        for (Button b : kProfileHotkeys) {
            const bool used = std::any_of(cfg_.profiles.begin(), cfg_.profiles.end(), [&](const Profile& o) { return o.hotkey == b; });
            if (!used) {
                p.hotkey = b;
                break;
            }
        }
        cfg_.profiles.push_back(std::move(p));
        cfg_.settings.activeProfile = static_cast<int>(cfg_.profiles.size()) - 1;
        changed_ = true;
    }

    // -- notices / update banner ---------------------------------------------
    void drawBanner() {
        const UpdateStatus up = updater_.status();
        auto banner = [&](ImU32 color, const std::string& text) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::PopStyleColor();
        };
        if (up.state == UpdateState::Available) {
            banner(kAccent, up.message);
            ImGui::SameLine();
            if (Updater::canSelfUpdate()) {
                if (ImGui::SmallButton("Install update")) updater_.install();
            } else if (ImGui::SmallButton("Open release page")) {
                openUrl(up.releaseUrl);
            }
        } else if (up.state == UpdateState::Downloading) {
            banner(kAccent, up.message);
        } else if (up.state == UpdateState::Installed) {
            banner(kGood, up.message);
            ImGui::SameLine();
            if (ImGui::SmallButton("Restart now")) {
                std::string error;
                if (relaunch(error)) {
                    restartRequested_ = true;
                } else {
                    notice_ = error;
                }
            }
        }
        if (!status_.padName.empty() || cfg_.settings.output == OutputKind::None) {
            // nothing to warn about
        } else if (status_.padError != PadError::None && !status_.padMessage.empty()) {
#if defined(_WIN32)
            if (status_.padError == PadError::DriverMissing) {
                banner(kWarn, "Install the ViGEmBus driver so games can see the remapped controller.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Get ViGEmBus")) openUrl(virtualPadDriverUrl());
            } else {
                banner(kWarn, status_.padMessage);
            }
#else
            banner(kWarn, "No virtual controller: " + status_.padMessage);
#endif
            ImGui::SameLine();
            if (ImGui::SmallButton("Retry")) engine_.retryVirtualPad();
        }
        if (!notice_.empty()) {
            banner(kWarn, notice_);
            ImGui::SameLine();
            if (ImGui::SmallButton("Dismiss")) notice_.clear();
        }
        if (!saveError_.empty()) banner(kBad, "Could not save settings: " + saveError_);
    }

    // -- sticks ---------------------------------------------------------------
    void drawStickColumn(const char* title, StickSettings& s, float rawX, float rawY, float outX, float outY, int& dragIndex) {
        ImGui::PushID(title);
        ImGui::SeparatorText(title);
        const float avail = ImGui::GetContentRegionAvail().x;
        const float size = std::min(ImGui::GetFontSize() * 13.0f, (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
        drawStickView("stick", size, s, rawX, rawY, outX, outY);
        ImGui::SameLine();
        changed_ |= drawCurveView("curve", size, s, std::hypot(rawX, rawY), dragIndex);

        ImGui::PushItemWidth(-ImGui::GetFontSize() * 9.5f);
        changed_ |= sliderPercent("Dead zone", &s.deadzone, 0, 40,
                                  "Stick movement inside this circle is ignored. Raise it if the stick drifts.");
        changed_ |= sliderPercent("Outer dead zone", &s.outerDeadzone, 0, 30,
                                  "The outer edge that already counts as full deflection.");
        changed_ |= sliderPercent("Anti-dead zone", &s.antiDeadzone, 0, 60,
                                  "Jumps the output past the game's own dead zone so tiny movements register. "
                                  "Set it to roughly the game's dead zone (often 10-25%).");
        changed_ |= enumCombo("Response curve", s.curve, curveLabel);
        ImGui::BeginDisabled(s.curve == Curve::Default || s.curve == Curve::Custom);
        changed_ |= sliderPercent("Curve strength", &s.curveIntensity, 0, 100);
        ImGui::EndDisabled();

        float rc = s.rcFilter * 100.0f;
        const char* rcFormat = rc > 0.5f ? "%+.0f stabilizer" : (rc < -0.5f ? "%+.0f jitter" : "off");
        if (ImGui::SliderFloat("RC filter", &rc, -100.0f, 100.0f, rcFormat, ImGuiSliderFlags_AlwaysClamp)) {
            s.rcFilter = std::abs(rc) < 0.5f ? 0.0f : rc / 100.0f;
            changed_ = true;
        }
        helpMarker(
            "GameSir style RC filter. It only works while you move the stick (pushed past its dead zone): "
            "with your thumb off the stick nothing is added, and letting go stops instantly.\n\n"
            "Positive = stabilizer: an RC low-pass filter that removes micro-jitter so aim feels heavier and "
            "steadier (adds a few ms of smoothing at high values).\n\n"
            "Negative = jitter mode: while the stick is moved the aim wobbles a tiny bit side to side, across "
            "the direction you push, flipping every 5 ms. Your aim speed stays the same and the stick never "
            "drops back into the game's dead zone. This keeps some games' aim assist engaged. Some online games treat this as aim-assist abuse - check the "
            "rules of the game you play.\n\nCtrl+click the slider to type an exact value.");

        changed_ |= enumCombo("Dead zone shape", s.shape, shapeLabel);
        changed_ |= ImGui::Checkbox("Invert X", &s.invertX);
        ImGui::SameLine();
        changed_ |= ImGui::Checkbox("Invert Y", &s.invertY);
        if (s.curve == Curve::Custom) {
            ImGui::SameLine();
            if (ImGui::Button("Reset curve")) {
                s.customCurve = StickSettings{}.customCurve;
                changed_ = true;
            }
        }
        ImGui::PopItemWidth();
        ImGui::PopID();
    }

    void drawSticksTab() {
        Profile& p = profile();
        const InputState& in = status_.input;
        const OutputState& out = status_.output;
        // Output sticks are shown for the stick they come from, so swapping stays readable.
        const float lOutX = p.swapSticks ? out.rx : out.lx;
        const float lOutY = p.swapSticks ? out.ry : out.ly;
        const float rOutX = p.swapSticks ? out.lx : out.rx;
        const float rOutY = p.swapSticks ? out.ly : out.ry;
        if (ImGui::BeginTable("sticks", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            drawStickColumn("Left stick", p.leftStick, in.lx, in.ly, lOutX, lOutY, leftDrag_);
            ImGui::TableNextColumn();
            drawStickColumn("Right stick", p.rightStick, in.rx, in.ry, rOutX, rOutY, rightDrag_);
            ImGui::EndTable();
        }
        changed_ |= ImGui::Checkbox("Swap left and right sticks", &p.swapSticks);
        ImGui::SameLine(0.0f, 32.0f);
        drawCalibrationControls(Calibrating::Sticks, "Calibrate sticks (fix drift)", stickCalMessage_);
        ImGui::SameLine();
        helpMarker("Leave both sticks alone and click Calibrate: EdgePad measures where they rest and re-centres them, "
                   "so a drifting stick is fixed without raising the dead zone. Calibration belongs to this "
                   "controller and applies to every profile.");
    }

    // -- triggers -------------------------------------------------------------
    void drawTriggerColumn(const char* title, TriggerSettings& t, float raw, float out) {
        ImGui::PushID(title);
        ImGui::SeparatorText(title);
        drawTriggerView("bar", t, raw, out);
        ImGui::PushItemWidth(-ImGui::GetFontSize() * 11.0f);
        changed_ |= enumCombo("Mode", t.mode, triggerModeLabel);
        if (t.mode == TriggerMode::HairTrigger) {
            float activation = std::max(t.deadzone, kHairMinActivation);
            if (sliderPercent("Activation point", &activation, kHairMinActivation * 100.0f, 50,
                              "How far you pull before the first shot fires (at least 2%, so resting noise "
                              "cannot fire it). The game gets a full press instantly.")) {
                t.deadzone = activation;
                changed_ = true;
            }
            changed_ |= sliderPercent("Reset distance", &t.hairResetDistance, kHairMinReset * 100.0f, 20,
                                      "Rapid trigger: the press releases the moment the trigger comes back up this "
                                      "far and fires again the moment you pull down this far - at any depth, no need "
                                      "to let go completely. 1% resets on the slightest lift; raise it only if a "
                                      "steady hold ever releases by itself.",
                                      "%.1f%%");
        } else {
            changed_ |= sliderPercent("Dead zone (start)", &t.deadzone, 0, 90, "Trigger travel ignored at the start.");
            changed_ |= sliderPercent("Trigger stop (end)", &t.maxRange, 5, 100,
                                      "Where the trigger already counts as fully pressed. Short values give "
                                      "DualSense Edge style short trigger pulls.");
            changed_ |= sliderPercent("Anti-dead zone", &t.antiDeadzone, 0, 90,
                                      "Minimum output as soon as the trigger leaves its dead zone.");
        }
        changed_ |= ImGui::Checkbox("Turbo (rapid fire)", &t.turbo);
        helpMarker("While the trigger is pressed it fires full presses over and over.");
        if (t.turbo) {
            changed_ |= ImGui::SliderInt("Between presses", &t.turboIntervalMs, kTurboMinMs, kTurboMaxMs, "%d ms",
                                         ImGuiSliderFlags_AlwaysClamp);
            helpMarker("1-100 ms from one press to the next. The controller reports every ~4 ms and most games read "
                       "input once per frame, so very short times are limited by those.");
        }
        changed_ |= enumCombo("Adaptive resistance", t.resistance, resistanceLabel);
        if (t.resistance == TriggerResistance::Wall) {
            changed_ |= sliderPercent("Wall position", &t.resistancePosition, 0, 90,
                                      "Where the adaptive trigger starts pushing back.");
            changed_ |= ImGui::SliderInt("Wall strength", &t.resistanceStrength, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::PopItemWidth();
        ImGui::PopID();
    }

    void drawTriggersTab() {
        Profile& p = profile();
        if (ImGui::BeginTable("triggers", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            drawTriggerColumn("L2", p.l2, status_.input.l2, status_.output.l2);
            ImGui::TableNextColumn();
            drawTriggerColumn("R2", p.r2, status_.input.r2, status_.output.r2);
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Tip: put the resistance wall at the same spot as the trigger stop to feel exactly where the");
        ImGui::TextDisabled("shortened trigger fires - the software version of the DualSense Edge trigger stops.");
    }

    // -- buttons --------------------------------------------------------------
    // Everything a button can do: controller buttons, keyboard keys and mouse buttons.
    bool bindingCombo(const char* id, Binding& b, bool allowInherit) {
        bool changed = false;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(id, bindingLabel(b).c_str(), ImGuiComboFlags_HeightLarge)) {
            auto option = [&](const std::string& label, const Binding& value) {
                const bool selected = b.kind == value.kind && b.button == value.button && b.key == value.key;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    Binding next = value;
                    next.toggle = b.toggle;
                    next.turbo = b.turbo;
                    next.turboIntervalMs = b.turboIntervalMs;
                    b = next;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            };
            if (allowInherit) option("Same as normal", Binding::inherit());
            option("Disabled", Binding::disabled());
            ImGui::SeparatorText("Controller");
            for (int i = 0; i < kButtonCount; ++i) {
                const Button t = buttonAt(i);
                if (isRemapTarget(t)) option(std::string(buttonLabel(t)), Binding::toButton(t));
            }
            ImGui::SeparatorText("Keyboard");
            for (int i = 1; i < kKeyCount; ++i) {
                const Key k = static_cast<Key>(i);
                if (!isMouseButton(k)) option("Key: " + std::string(keyLabel(k)), Binding::toKey(k));
            }
            ImGui::SeparatorText("Mouse");
            for (int i = 1; i < kKeyCount; ++i) {
                const Key k = static_cast<Key>(i);
                if (isMouseButton(k)) option(std::string(keyLabel(k)), Binding::toKey(k));
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void drawButtonRow(Button source, ButtonMask live) {
        ImGui::PushID(static_cast<int>(source));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        statusDot(has(live, source) ? kAccent : kGrid);
        ImGui::TextUnformatted(std::string(buttonLabel(source)).c_str());
        ImGui::TableNextColumn();
        Profile& p = profile();
        if (fnSourceMask(cfg_.settings.fnMode) & bit(source)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Fn (profile switching)");
            ImGui::SetItemTooltip("To bind this button, pick another Fn button in Settings "
                                  "(for example only the left Fn, or Mute).");
        } else if (p.shiftButton == source) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccent), "Shift button");
        } else {
            BindingMap& map = editingShift_ ? p.shiftButtons : p.buttons;
            Binding& b = map[static_cast<size_t>(index(source))];
            changed_ |= bindingCombo("##target", b, editingShift_);
            ImGui::TableNextColumn();
            const bool canExtras = b.kind == Binding::Kind::Button || b.kind == Binding::Kind::Key;
            ImGui::BeginDisabled(!canExtras);
            changed_ |= ImGui::Checkbox("Toggle", &b.toggle);
            ImGui::SetItemTooltip("Tap to switch on, tap again to switch off.");
            ImGui::SameLine();
            changed_ |= ImGui::Checkbox("Turbo", &b.turbo);
            ImGui::SetItemTooltip("Repeats the press while the button is held (or toggled on).");
            if (b.turbo) {
                ImGui::SetNextItemWidth(-1.0f);
                changed_ |= ImGui::SliderInt("##turbo_ms", &b.turboIntervalMs, kTurboMinMs, kTurboMaxMs,
                                             "%d ms between presses", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetItemTooltip(
                    "Time between turbo presses (1-100 ms). The controller reports every ~4 ms and most games read "
                    "input once per frame, so very short times are limited by those.");
            }
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }

    void drawButtonGroup(const char* title, std::initializer_list<Button> buttons, ButtonMask live) {
        ImGui::SeparatorText(title);
        if (ImGui::BeginTable(title, 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Sends", ImGuiTableColumnFlags_WidthStretch, 1.25f);
            ImGui::TableSetupColumn("Extras", ImGuiTableColumnFlags_WidthStretch, 1.35f);
            for (Button b : buttons) drawButtonRow(b, live);
            ImGui::EndTable();
        }
    }

    void drawButtonsTab() {
        Profile& p = profile();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Editing");
        ImGui::SameLine();
        if (ImGui::RadioButton("Normal layer", !editingShift_)) editingShift_ = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Shift layer", editingShift_)) editingShift_ = true;

        ImGui::SameLine(0.0f, 28.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Shift button");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
        const std::string shiftPreview = p.shiftButton ? std::string(buttonLabel(*p.shiftButton)) : "None";
        if (ImGui::BeginCombo("##shift", shiftPreview.c_str())) {
            if (ImGui::Selectable("None", !p.shiftButton)) {
                p.shiftButton.reset();
                changed_ = true;
            }
            for (int i = 0; i < kButtonCount; ++i) {
                const Button b = buttonAt(i);
                if (!isRemapSource(b) || (fnSourceMask(cfg_.settings.fnMode) & bit(b))) continue;
                if (ImGui::Selectable(std::string(buttonLabel(b)).c_str(), p.shiftButton == b)) {
                    p.shiftButton = b;
                    changed_ = true;
                }
            }
            ImGui::EndCombo();
        }
        helpMarker("Hold the shift button and every button uses its shift-layer binding instead (buttons left on "
                   "\"Same as normal\" keep working as usual). A back button makes a great shift button. The shift "
                   "button itself sends nothing.");

        ImGui::SameLine(0.0f, 28.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Touchpad");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        changed_ |= enumCombo("##zones", p.touchpadZones, touchpadZonesLabel);
        if (p.touchpadZones != TouchpadZones::Off) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.5f);
            changed_ |= enumCombo("##zoneTrigger", p.zoneTrigger, zoneTriggerLabel);
        }
        helpMarker("Split the touchpad into 2 or 4 buttons that can each be bound to anything - extra buttons for a "
                   "regular DualSense. \"Click\" fires when you press the pad down in a zone, \"Touch\" as soon as "
                   "your finger is on it.");

        if (editingShift_ && !p.shiftButton) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn),
                               "Pick a shift button first - the shift layer is used while it is held.");
        }

        ButtonMask live = status_.input.buttons;
        if (p.touchpadZones != TouchpadZones::Off) {
            const TouchPoint& t = status_.input.motion.touch[0];
            const bool on = p.zoneTrigger == ZoneTrigger::Click ? has(live, Button::Touchpad) : t.active;
            if (on) live |= bit(touchpadZoneAt(t, p.touchpadZones));
        }

        if (ImGui::BeginTable("buttonColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            drawButtonGroup("Back buttons", {Button::PaddleLeft, Button::PaddleRight}, live);
            drawButtonGroup("Face buttons", {Button::Cross, Button::Circle, Button::Square, Button::Triangle}, live);
            drawButtonGroup("Shoulders and sticks", {Button::L1, Button::R1, Button::L3, Button::R3}, live);
            if (p.touchpadZones == TouchpadZones::Two) {
                drawButtonGroup("Touchpad zones", {Button::TouchLeft, Button::TouchRight}, live);
            } else if (p.touchpadZones == TouchpadZones::Four) {
                drawButtonGroup("Touchpad zones", {Button::TouchTopLeft, Button::TouchTopRight, Button::TouchBottomLeft,
                                                   Button::TouchBottomRight},
                                live);
            }
            ImGui::TableNextColumn();
            drawButtonGroup("Edge Fn buttons", {Button::FnLeft, Button::FnRight}, live);
            drawButtonGroup("D-pad", {Button::DpadUp, Button::DpadDown, Button::DpadLeft, Button::DpadRight}, live);
            drawButtonGroup("System", {Button::Create, Button::Options, Button::PS, Button::Touchpad, Button::Mute}, live);
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button(editingShift_ ? "Reset shift layer" : "Reset buttons to default")) {
            if (editingShift_) {
                p.shiftButtons = defaultShiftMap();
            } else {
                p.buttons = defaultButtonMap();
            }
            changed_ = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("L2 / R2 targets press the trigger fully. Keyboard and mouse binds work in any game.");
        if (!status_.keyboardMessage.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn), "%s", status_.keyboardMessage.c_str());
        }
    }

    // -- gyro -----------------------------------------------------------------
    void drawRateBar(const char* label, float degPerSec) {
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetFrameHeight();
        ImGui::InvisibleButton(label, ImVec2(width, height));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        dl->AddRectFilled(p0, p1, kWell, 6.0f);
        const float mid = (p0.x + p1.x) * 0.5f;
        const float value = std::clamp(degPerSec / 200.0f, -1.0f, 1.0f) * (p1.x - p0.x) * 0.5f;
        dl->AddRectFilled(ImVec2(std::min(mid, mid + value), p0.y + 4), ImVec2(std::max(mid, mid + value), p1.y - 4),
                          kAccent, 3.0f);
        dl->AddLine(ImVec2(mid, p0.y + 2), ImVec2(mid, p1.y - 2), kGrid, 1.0f);
        char text[64];
        std::snprintf(text, sizeof(text), "%s  %+.1f \xC2\xB0/s", label, degPerSec);
        dl->AddText(ImVec2(p0.x + 8, p0.y + (height - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(230, 232, 238, 230),
                    text);
    }

    void drawGyroTab() {
        Profile& p = profile();
        GyroSettings& g = p.gyro;
        if (!ImGui::BeginTable("gyroColumns", 2, ImGuiTableFlags_SizingStretchSame)) return;
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Gyro aiming");
        ImGui::PushItemWidth(-ImGui::GetFontSize() * 10.0f);
        changed_ |= enumCombo("Gyro", g.activation, gyroActivationLabel);
        helpMarker("Turn and tilt the controller to aim. The gyro adds to the right stick: the stick still does big "
                   "turns while small wrist movements do the fine aim. \"While a button is held\" with L2 turns it on "
                   "only while you aim down sights.");
        ImGui::BeginDisabled(g.activation == GyroActivation::Off);
        if (g.activation == GyroActivation::WhileHeld || g.activation == GyroActivation::Toggle) {
            if (ImGui::BeginCombo("Button", std::string(buttonLabel(g.button)).c_str())) {
                for (int i = 0; i < kButtonCount; ++i) {
                    const Button b = buttonAt(i);
                    if (!isPhysicalButton(b)) continue;
                    if (ImGui::Selectable(std::string(buttonLabel(b)).c_str(), g.button == b)) {
                        g.button = b;
                        changed_ = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        changed_ |= ImGui::SliderFloat("Sensitivity", &g.sensitivity, 0.25f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        helpMarker("How far the aim moves for the same wrist movement. At 1.0 the stick is fully pushed when you "
                   "turn the controller at 360\xC2\xB0/s, at 4.0 already at 90\xC2\xB0/s. It also depends on your "
                   "in-game sensitivity.");
        changed_ |= sliderPercent("Vertical speed", &g.verticalRatio, 0, 200, "Up / down speed compared to left / right.");
        changed_ |= enumCombo("Turn with", g.horizontalAxis, gyroAxisLabel);
        changed_ |= ImGui::SliderFloat("Dead zone", &g.deadzone, 0.0f, 10.0f, "%.1f \xC2\xB0/s", ImGuiSliderFlags_AlwaysClamp);
        helpMarker("Rotation slower than this is ignored, so hand tremor and sensor noise do not move the aim.");
        changed_ |= sliderPercent("Smoothing", &g.smoothing, 0, 100,
                                  "Smooths slow, shaky movements only. Fast flicks always stay instant.");
        changed_ |= sliderPercent("Anti-dead zone", &g.antiDeadzone, 0, 60,
                                  "Minimum right stick push as soon as the gyro moves, so small wrist movements get "
                                  "past the game's own stick dead zone. Set it to about the game's dead zone.");
        changed_ |= ImGui::Checkbox("Invert X", &g.invertX);
        ImGui::SameLine();
        changed_ |= ImGui::Checkbox("Invert Y", &g.invertY);
        ImGui::EndDisabled();
        ImGui::PopItemWidth();

        ImGui::SeparatorText("Calibration");
        ImGui::TextWrapped("Put the controller down on a flat surface and click Calibrate. This removes gyro drift so "
                           "the aim does not creep while you hold still.");
        drawCalibrationControls(Calibrating::Gyro, "Calibrate gyro", gyroCalMessage_);

        ImGui::TableNextColumn();
        ImGui::SeparatorText("Live");
        const auto& bias = cfg_.settings.gyroBias;
        auto rate = [&](size_t i) {
            return (static_cast<float>(status_.input.motion.gyro[i]) - bias[i]) / kGyroCountsPerDegPerSec;
        };
        drawRateBar("Yaw (turn)", rate(1));
        drawRateBar("Pitch (tilt up/down)", rate(0));
        drawRateBar("Roll (tilt left/right)", rate(2));
        ImGui::AlignTextToFramePadding();
        statusDot(status_.gyroActive ? kGood : kGrid);
        ImGui::TextUnformatted(status_.gyroActive ? "Gyro aiming is on" : "Gyro aiming is off");
        const float size = std::min(ImGui::GetFontSize() * 13.0f, ImGui::GetContentRegionAvail().x);
        drawStickView("gyroStick", size, p.rightStick, status_.input.rx, status_.input.ry, status_.output.rx,
                      status_.output.ry);
        ImGui::TextDisabled("Right stick output, gyro included.");
        ImGui::EndTable();
    }

    // -- calibration ----------------------------------------------------------
    void drawCalibrationControls(Calibrating kind, const char* label, const std::string& message) {
        ImGui::BeginDisabled(calibrating_ != Calibrating::None);
        if (ImGui::Button(label)) {
            calibrating_ = kind;
            calibrationStart_ = now_;
            calibrationSamples_ = 0;
            calibrationSum_.fill(0.0);
            calibrationMin_.fill(1e9);
            calibrationMax_.fill(-1e9);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(kind == Calibrating::Sticks ? "Reset##sticks" : "Reset##gyro")) {
            if (kind == Calibrating::Sticks) {
                cfg_.settings.leftStickCenter = {};
                cfg_.settings.rightStickCenter = {};
                stickCalMessage_ = "Stick calibration cleared.";
            } else {
                cfg_.settings.gyroBias = {};
                gyroCalMessage_ = "Gyro calibration cleared.";
            }
            changed_ = true;
        }
        if (!message.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", message.c_str());
        }
    }

    void updateCalibration(double now) {
        if (calibrating_ == Calibrating::None) return;
        const bool sticks = calibrating_ == Calibrating::Sticks;
        std::string& message = sticks ? stickCalMessage_ : gyroCalMessage_;
        if (!status_.connected) {
            message = "Connect the controller first.";
            calibrating_ = Calibrating::None;
            return;
        }
        const InputState& in = status_.input;
        const std::array<double, 4> sample =
            sticks ? std::array<double, 4>{in.lx, in.ly, in.rx, in.ry}
                   : std::array<double, 4>{static_cast<double>(in.motion.gyro[0]), static_cast<double>(in.motion.gyro[1]),
                                           static_cast<double>(in.motion.gyro[2]), 0.0};
        for (size_t i = 0; i < 4; ++i) {
            calibrationSum_[i] += sample[i];
            calibrationMin_[i] = std::min(calibrationMin_[i], sample[i]);
            calibrationMax_[i] = std::max(calibrationMax_[i], sample[i]);
        }
        ++calibrationSamples_;
        const double elapsed = now - calibrationStart_;
        if (elapsed < 2.0) {
            char text[80];
            std::snprintf(text, sizeof(text), sticks ? "Hands off the sticks... %.1f s" : "Keep the controller still... %.1f s",
                          2.0 - elapsed);
            message = text;
            return;
        }
        calibrating_ = Calibrating::None;
        if (calibrationSamples_ < 10) {
            message = "Not enough data - try again with the EdgePad window in front.";
            return;
        }
        std::array<double, 4> mean{};
        double spread = 0.0;
        for (size_t i = 0; i < 4; ++i) {
            mean[i] = calibrationSum_[i] / calibrationSamples_;
            spread = std::max(spread, calibrationMax_[i] - calibrationMin_[i]);
        }
        char text[160];
        if (sticks) {
            if (spread > 0.08) {
                message = "A stick moved - keep your thumbs off the sticks and try again.";
                return;
            }
            cfg_.settings.leftStickCenter = {static_cast<float>(mean[0]), static_cast<float>(mean[1])};
            cfg_.settings.rightStickCenter = {static_cast<float>(mean[2]), static_cast<float>(mean[3])};
            std::snprintf(text, sizeof(text), "Done. Resting offset left %+.1f%% / %+.1f%%, right %+.1f%% / %+.1f%%",
                          mean[0] * 100.0, mean[1] * 100.0, mean[2] * 100.0, mean[3] * 100.0);
        } else {
            if (spread > 12.0 * kGyroCountsPerDegPerSec) {
                message = "The controller moved - put it down on a flat surface and try again.";
                return;
            }
            cfg_.settings.gyroBias = {static_cast<float>(mean[0]), static_cast<float>(mean[1]), static_cast<float>(mean[2])};
            const double drift = std::sqrt(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]) / kGyroCountsPerDegPerSec;
            std::snprintf(text, sizeof(text), "Done. Removed %.2f \xC2\xB0/s of drift.", drift);
        }
        message = text;
        changed_ = true;
    }

    // -- settings -------------------------------------------------------------
    void drawSettingsTab() {
        ImGui::PushItemWidth(ImGui::GetFontSize() * 18.0f);
        ImGui::SeparatorText("Controller");
        changed_ |= enumCombo("Virtual controller", cfg_.settings.output, outputKindLabel);
        helpMarker("What games see. Xbox 360 works with almost every PC game. PlayStation (DualShock 4) shows "
                   "PlayStation button prompts and also passes the gyro, accelerometer and touchpad through, "
                   "so motion aiming works in games that support it. ViGEmBus cannot emulate a PS5 "
                   "controller, so the DualShock 4 is the PlayStation option.");
        changed_ |= enumCombo("Fn button", cfg_.settings.fnMode, fnModeLabel);
        helpMarker("Hold Fn and press Cross / Circle / Square / Triangle to switch profiles, or Options to toggle "
                   "remapping. A regular DualSense can use the Mute button as Fn.");
        changed_ |= ImGui::Checkbox("Forward game rumble to the controller", &cfg_.settings.rumble);
        ImGui::BeginDisabled(cfg_.settings.output != OutputKind::DualShock4);
        changed_ |= ImGui::Checkbox("Let games set the lightbar colour", &cfg_.settings.gameLightbar);
        ImGui::EndDisabled();
        helpMarker("PlayStation (DualShock 4) output only. Games that colour the lightbar (health, team, police "
                   "lights...) control it like on a real PlayStation controller. Off = the profile colour.");
        ImGui::PopItemWidth();
        if (status_.padError == PadError::DriverMissing || status_.padError == PadError::Failed ||
            status_.padError == PadError::PermissionDenied) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn), "%s", status_.padMessage.c_str());
#if defined(_WIN32)
            if (ImGui::Button("Get the ViGEmBus driver")) openUrl(virtualPadDriverUrl());
            ImGui::SameLine();
#endif
            if (ImGui::Button("Retry now")) engine_.retryVirtualPad();
        }

        ImGui::SeparatorText("Updates");
        changed_ |= ImGui::Checkbox("Install updates automatically", &cfg_.settings.autoUpdate);
        helpMarker("On start EdgePad checks the GitHub releases of this project, downloads a newer build, verifies "
                   "its SHA-256 checksum and swaps it in. The new version runs the next time you start EdgePad.");
        const UpdateStatus up = updater_.status();
        ImGui::BeginDisabled(updater_.busy());
        if (ImGui::Button("Check for updates")) updater_.check(false);
        ImGui::EndDisabled();
        if (up.state == UpdateState::Available && Updater::canSelfUpdate()) {
            ImGui::SameLine();
            if (ImGui::Button("Install")) updater_.install();
        }
        if (!up.message.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(up.state == UpdateState::Failed ? kBad : kMuted), "%s",
                               up.message.c_str());
        }
        if (!Updater::canSelfUpdate()) ImGui::TextDisabled("Development build: self-update is disabled.");

        ImGui::SeparatorText("Portable data");
        ImGui::TextDisabled("Profiles and settings are stored next to the app, so the folder can live on a USB stick.");
        ImGui::TextUnformatted(toUtf8(store_.path()).c_str());
        if (ImGui::Button("Open data folder")) openInFileBrowser(dataDir_);
        ImGui::SameLine();
        if (ImGui::Button("Restore default profiles")) ImGui::OpenPopup("Restore defaults");
        if (ImGui::BeginPopupModal("Restore defaults", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Replace all profiles with the built-in ones?");
            if (ImGui::Button("Replace")) {
                cfg_.profiles = Config::defaults().profiles;
                cfg_.settings.activeProfile = 0;
                changed_ = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void drawHelpTab() {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::SeparatorText("Getting started (Windows)");
        ImGui::BulletText("Install ViGEmBus once: it lets EdgePad create the virtual controller games see.");
        ImGui::BulletText("Recommended: install HidHide and allow EdgePad in it, so games only see the virtual "
                          "controller and never get double input from the real one.");
        ImGui::BulletText("Plug in the DualSense / DualSense Edge over USB or pair it over Bluetooth. "
                          "EdgePad connects automatically.");
        ImGui::BulletText("If Steam is running, turn off Steam Input's PlayStation support for the virtual "
                          "controller to keep one layer of remapping.");
        ImGui::SeparatorText("Controller shortcuts");
        ImGui::BulletText("Fn + Cross / Circle / Square / Triangle: switch to the profile using that hotkey.");
        ImGui::BulletText("Fn + Options: toggle remapping (raw passthrough) on and off.");
        ImGui::BulletText("Fn is the Edge's Fn buttons, or Mute on a regular DualSense (change it in Settings).");
        ImGui::BulletText("The player LEDs show the active profile slot, the lightbar shows its colour.");
        ImGui::SeparatorText("What the settings do");
        ImGui::BulletText("Dead zone / anti-dead zone: ignore drift, then jump past the game's own dead zone.");
        ImGui::BulletText("Curves: Quick, Precise, Steady, Digital and Dynamic mirror the DualSense Edge presets; "
                          "Custom lets you drag your own curve.");
        ImGui::BulletText("RC filter: positive values smooth the stick (stabilizer), negative values add jitter - "
                          "only while you move the stick.");
        ImGui::BulletText("PlayStation (DualShock 4) output passes gyro, accelerometer and touchpad to games.");
        ImGui::BulletText("Trigger stop + resistance wall: shorter trigger pulls like the Edge's hardware stops.");
        ImGui::BulletText("Gyro: turn / tilt the controller to fine-aim, e.g. only while L2 is held. Calibrate it once.");
        ImGui::BulletText("Buttons can send keyboard keys and mouse buttons, and each can be a toggle or turbo.");
        ImGui::BulletText("Shift layer: hold the shift button (a back button works well) for a second set of binds.");
        ImGui::BulletText("Touchpad zones: split the touchpad into 2 or 4 extra buttons.");
        ImGui::BulletText("Drifting stick? Sticks tab -> Calibrate sticks.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const std::string repoUrl = std::string("https://github.com/") + build::kRepository;
        if (ImGui::Button("Project page")) openUrl(repoUrl);
        ImGui::SameLine();
        if (ImGui::Button("ViGEmBus")) openUrl("https://github.com/nefarius/ViGEmBus/releases/latest");
        ImGui::SameLine();
        if (ImGui::Button("HidHide")) openUrl("https://github.com/nefarius/HidHide/releases/latest");
    }

    Engine& engine_;
    const ConfigStore& store_;
    Updater& updater_;
    Config cfg_;
    fs::path dataDir_;
    Fonts fonts_;
    EngineStatus status_;
    uint64_t seenRevision_ = 0;
    bool changed_ = false;
    bool dirty_ = false;
    double lastEdit_ = 0.0;
    bool restartRequested_ = false;
    Calibrating calibrating_ = Calibrating::None;
    double calibrationStart_ = 0.0;
    double now_ = 0.0;
    int calibrationSamples_ = 0;
    std::array<double, 4> calibrationSum_{};
    std::array<double, 4> calibrationMin_{};
    std::array<double, 4> calibrationMax_{};
    std::string stickCalMessage_;
    std::string gyroCalMessage_;
    bool editingShift_ = false;
    int leftDrag_ = -1;
    int rightDrag_ = -1;
    std::array<char, 64> renameBuffer_{};
    std::string notice_;
    std::string saveError_;
};

void glfwErrorCallback(int code, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", code, description); }

}  // namespace

int runGui(Engine& engine, const ConfigStore& store, Updater& updater, GuiOptions options) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1240, 860, "EdgePad", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 2;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    float scaleX = 1.0f, scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    const float scale = std::clamp(std::max(scaleX, scaleY), 1.0f, 3.0f);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // portable: never write imgui.ini next to the app
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const Fonts fonts = loadFonts(scale);
    applyTheme(scale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    int exitCode = 0;
    {
        App app(engine, store, updater, std::move(options), fonts);
        while (!glfwWindowShouldClose(window)) {
            // Keep the UI cheap while gaming: full rate only when focused and visible.
            const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
            const bool iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
            if (iconified) {
                glfwWaitEventsTimeout(0.25);
                continue;
            }
            if (focused) {
                glfwPollEvents();
            } else {
                glfwWaitEventsTimeout(1.0 / 15.0);
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            app.frame(glfwGetTime());
            ImGui::Render();

            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(20 / 255.0f, 22 / 255.0f, 27 / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);

            if (app.restartRequested()) glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        app.save();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}

}  // namespace edgepad
