#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/config_json.hpp"
#include "core/dualsense.hpp"
#include "core/pipeline.hpp"
#include "core/usbip_session.hpp"
#include "core/virtual_dualsense.hpp"

using namespace edgepad;
namespace vds = edgepad::virtual_dualsense;

namespace {

void be32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}
uint32_t rd32(const std::vector<uint8_t>& v, size_t at) {
    return (uint32_t{v[at]} << 24) | (uint32_t{v[at + 1]} << 16) | (uint32_t{v[at + 2]} << 8) | v[at + 3];
}

std::vector<uint8_t> importRequest(const char* busid) {
    std::vector<uint8_t> v = {0x01, 0x11, 0x80, 0x03, 0, 0, 0, 0};
    char id[32] = {};
    std::strncpy(id, busid, sizeof(id) - 1);
    v.insert(v.end(), id, id + 32);
    return v;
}

std::vector<uint8_t> submit(uint32_t seq, uint32_t dir, uint32_t ep, uint32_t length, std::array<uint8_t, 8> setup = {},
                            const std::vector<uint8_t>& data = {}) {
    std::vector<uint8_t> v;
    be32(v, 1);
    be32(v, seq);
    be32(v, 0x00010001);
    be32(v, dir);
    be32(v, ep);
    be32(v, 0);  // flags
    be32(v, length);
    be32(v, 0);
    be32(v, 0xFFFFFFFF);  // not isochronous
    be32(v, 0);
    v.insert(v.end(), setup.begin(), setup.end());
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

std::array<uint8_t, 8> setupPacket(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
    return {type,
            request,
            static_cast<uint8_t>(value & 0xFF),
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(index & 0xFF),
            static_cast<uint8_t>(index >> 8),
            static_cast<uint8_t>(length & 0xFF),
            static_cast<uint8_t>(length >> 8)};
}

struct Reply {
    uint32_t command, seqnum;
    int32_t status;
    uint32_t actualLength;
    std::vector<uint8_t> data;
};

// Splits server output into RET_SUBMIT / RET_UNLINK replies. `outSeq`: requests that sent data to
// the device (their replies carry none).
std::vector<Reply> replies(const std::vector<uint8_t>& out, std::vector<uint32_t> outSeq = {}) {
    std::vector<Reply> r;
    size_t at = 0;
    while (at + 48 <= out.size()) {
        Reply x;
        x.command = rd32(out, at);
        x.seqnum = rd32(out, at + 4);
        x.status = static_cast<int32_t>(rd32(out, at + 20));
        x.actualLength = x.command == 3 ? rd32(out, at + 24) : 0;
        if (x.command == 3) CHECK(rd32(out, at + 32) == 0xFFFFFFFFu);  // not isochronous
        at += 48;
        const bool inData = std::find(outSeq.begin(), outSeq.end(), x.seqnum) == outSeq.end();
        if (x.command == 3 && inData && x.status == 0) {
            x.data.assign(out.begin() + static_cast<std::ptrdiff_t>(at),
                          out.begin() + static_cast<std::ptrdiff_t>(at + x.actualLength));
            at += x.actualLength;
        }
        r.push_back(x);
    }
    CHECK(at == out.size());
    return r;
}

struct Harness {
    std::vector<std::vector<uint8_t>> outputs;
    std::map<uint8_t, std::vector<uint8_t>> features;
    usbip::Session session{{
        [this](const uint8_t* d, size_t n) { outputs.emplace_back(d, d + n); },
        [this](uint8_t id) -> std::optional<std::vector<uint8_t>> {
            const auto it = features.find(id);
            if (it == features.end()) return std::nullopt;
            return it->second;
        },
    }};

    std::vector<Reply> send(const std::vector<uint8_t>& bytes, std::vector<uint32_t> outSeq = {}) {
        REQUIRE(session.receive(bytes.data(), bytes.size()));
        return replies(session.takeOutput(), std::move(outSeq));
    }
    Reply control(uint32_t seq, std::array<uint8_t, 8> setup, const std::vector<uint8_t>& data = {}) {
        const uint16_t length = static_cast<uint16_t>(setup[6] | (setup[7] << 8));
        const bool in = (setup[0] & 0x80) != 0;
        auto r = in ? send(submit(seq, 1, 0, length, setup, data)) : send(submit(seq, 0, 0, length, setup, data), {seq});
        REQUIRE(r.size() == 1);
        CHECK(r[0].seqnum == seq);
        return r[0];
    }
    void attach() {
        const auto req = importRequest("1-1");
        REQUIRE(session.receive(req.data(), req.size()));
        REQUIRE(session.takeOutput().size() == 320);
        REQUIRE(session.attached());
    }
};

}  // namespace

TEST_CASE("virtual DualSense: descriptors of a real wired DualSense") {
    CHECK(vds::reportDescriptor().size() == 273);
    const auto dev = vds::deviceDescriptor();
    REQUIRE(dev.size() == 18);
    CHECK(dev[8] == 0x4C);
    CHECK(dev[9] == 0x05);
    CHECK(dev[10] == 0xE6);
    CHECK(dev[11] == 0x0C);
    const auto cfg = vds::configurationDescriptor();
    CHECK(cfg.size() == static_cast<size_t>(cfg[2] | (cfg[3] << 8)));
    CHECK(cfg.size() == 9 + 9 + 9 + 7 + 7);
    const auto hid = vds::hidDescriptor();
    CHECK((hid[7] | (hid[8] << 8)) == 273);
    // Report sizes straight from the descriptor (report id included).
    CHECK(vds::featureReportSize(0x05) == 41);  // calibration
    CHECK(vds::featureReportSize(0x09) == 20);  // pairing info (MAC)
    CHECK(vds::featureReportSize(0x20) == 64);  // firmware info
    CHECK(vds::featureReportSize(0x77) == 0);
    CHECK(vds::stringDescriptor(2).size() == 2 + 2 * std::strlen("DualSense Wireless Controller"));
    CHECK(vds::stringDescriptor(9).empty());
}

TEST_CASE("virtual DualSense: a controller report passes through 1:1, byte for byte") {
    Config cfg = Config::defaults();
    cfg.settings.fnMode = FnMode::Disabled;  // Mute stays a button
    cfg.normalize();
    Pipeline pipeline;
    uint32_t seed = 12345;
    auto rnd = [&] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<uint8_t>(seed >> 16);
    };
    for (int round = 0; round < 500; ++round) {
        std::array<uint8_t, 64> usb{};
        usb[0] = dualsense::kUsbInputReportId;
        for (size_t i = 1; i < usb.size(); ++i) usb[i] = rnd();
        usb[8] = static_cast<uint8_t>((usb[8] & 0xF0) | (rnd() % 9));  // valid d-pad (0-7, 8 = none)
        usb[10] &= 0x0F;                                               // no Edge-only buttons
        const auto parsed = dualsense::parseInputReport(usb.data(), usb.size());
        REQUIRE(parsed);
        const auto out = vds::buildInputReport(pipeline.process(parsed->state, cfg, 0.004f));
        CHECK(out == usb);
    }
}

TEST_CASE("old files map Mute to itself (it reaches a virtual DualSense now)") {
    const std::string v3 = R"({"version": 3, "profiles": [{"buttons": {"mute": "none", "cross": "circle"}},
                                                          {"buttons": {"mute": "key:m"}}]})";
    const Config cfg = configFromJson(v3);
    const auto mute = static_cast<size_t>(index(Button::Mute));
    CHECK(cfg.profiles[0].buttons[mute].kind == Binding::Kind::Button);
    CHECK(cfg.profiles[0].buttons[mute].button == Button::Mute);
    CHECK(cfg.profiles[1].buttons[mute].kind == Binding::Kind::Key);
    CHECK(Config::defaults().profiles[0].buttons[mute].button == Button::Mute);
}

TEST_CASE("virtual DualSense: Bluetooth reports become the same USB report") {
    std::array<uint8_t, 78> bt{};
    bt[0] = dualsense::kBtInputReportId;
    bt[1] = 0x10;
    for (size_t i = 2; i < 65; ++i) bt[i] = static_cast<uint8_t>(i * 7);
    bt[2 + 7] = 0x08;   // d-pad none
    bt[2 + 9] = 0x01;   // PS only
    const auto parsed = dualsense::parseInputReport(bt.data(), bt.size());
    REQUIRE(parsed);
    Config cfg = Config::defaults();
    cfg.normalize();
    Pipeline pipeline;
    const auto out = vds::buildInputReport(pipeline.process(parsed->state, cfg, 0.004f));
    CHECK(out[0] == 0x01);
    CHECK(std::memcmp(out.data() + 1, bt.data() + 2, 63) == 0);
}

TEST_CASE("virtual DualSense: EdgePad's changes are in the report") {
    OutputState s;
    s.lx = -1.0f;
    s.ry = 1.0f;
    s.r2 = 1.0f;
    s.buttons = bit(Button::Cross) | bit(Button::DpadUp) | bit(Button::R2) | bit(Button::PS) | bit(Button::PaddleLeft);
    s.raw.valid = true;
    s.raw.body.fill(0xFF);  // the Edge reports its back buttons in byte 9
    const auto r = vds::buildInputReport(s);
    CHECK(r[1] == 0);
    CHECK(r[4] == 0);  // right stick up
    CHECK(r[6] == 255);
    CHECK(r[8] == (0x00 | 0x20));         // d-pad up, cross
    CHECK(r[9] == 0x08);                  // R2 digital
    CHECK(r[10] == (0x08 | 0x01));        // PS; Edge bits cleared, bit 3 passed on
    CHECK(r[11] == 0xFF);                 // untouched bytes stay
    // Without a controller report (demo) the report is still complete and neutral.
    const auto demo = vds::buildInputReport(OutputState{});
    CHECK(demo[1] == 128);
    CHECK(demo[8] == 0x08);
    CHECK(demo[33] == 0x80);  // first finger: not touching
    CHECK(demo[37] == 0x80);
}

TEST_CASE("virtual DualSense: game output reports go to the controller, minus what EdgePad keeps") {
    std::array<uint8_t, 48> game{};
    game[0] = 0x02;
    game[1] = 0x01 | 0x02 | 0x04 | 0x08;  // rumble, haptics, both triggers
    game[2] = 0x04 | 0x10;                // lightbar, player LEDs
    game[3] = 200;                        // right motor
    game[4] = 100;                        // left motor
    game[12] = 0x21;                      // right trigger effect
    game[46] = 255;                       // red
    auto all = vds::filterGameOutput(game.data(), game.size(), {});
    REQUIRE(all);
    CHECK(std::memcmp(all->data(), game.data() + 1, 47) == 0);
    CHECK(vds::partsChanged(*all) == dualsense::kAllParts);
    // Unchanged for a USB controller...
    const auto usb = dualsense::wrapOutputReport(all->data(), dualsense::Connection::Usb, 0);
    CHECK(std::vector<uint8_t>(game.begin(), game.end()) == usb);
    // ...wrapped with sequence and CRC for Bluetooth.
    const auto bt = dualsense::wrapOutputReport(all->data(), dualsense::Connection::Bluetooth, 3);
    REQUIRE(bt.size() == 78);
    CHECK(bt[0] == 0x31);
    CHECK(bt[1] == 0x30);
    CHECK(std::memcmp(bt.data() + 3, game.data() + 1, 47) == 0);

    auto noRumble = vds::filterGameOutput(game.data(), game.size(), {false, true});
    REQUIRE(noRumble);
    CHECK(((*noRumble)[0] & 0x03) == 0);
    CHECK((*noRumble)[2] == 0);
    CHECK(vds::partsChanged(*noRumble) == (dualsense::kPartTriggers | dualsense::kPartLights));
    auto noLights = vds::filterGameOutput(game.data(), game.size(), {true, false});
    REQUIRE(noLights);
    CHECK(((*noLights)[1] & 0x14) == 0);

    std::array<uint8_t, 48> lightsOnly{};
    lightsOnly[0] = 0x02;
    lightsOnly[2] = 0x04;
    CHECK_FALSE(vds::filterGameOutput(lightsOnly.data(), lightsOnly.size(), {true, false}));
    CHECK_FALSE(vds::filterGameOutput(lightsOnly.data(), 10, {}));  // too short
}

TEST_CASE("USB/IP: device list and import") {
    Harness h;
    const std::vector<uint8_t> list = {0x01, 0x11, 0x80, 0x05, 0, 0, 0, 0};
    REQUIRE(h.session.receive(list.data(), list.size()));
    const auto devlist = h.session.takeOutput();
    REQUIRE(devlist.size() == 8 + 4 + 312 + 4);
    CHECK(rd32(devlist, 8) == 1);
    CHECK(std::string(reinterpret_cast<const char*>(&devlist[12 + 256])) == "1-1");

    Harness wrong;
    const auto bad = importRequest("2-7");
    CHECK_FALSE(wrong.session.receive(bad.data(), bad.size()));
    const auto refused = wrong.session.takeOutput();
    REQUIRE(refused.size() == 8);
    CHECK(rd32(refused, 4) != 0);

    Harness ok;
    const auto req = importRequest("1-1");
    REQUIRE(ok.session.receive(req.data(), req.size()));
    const auto rep = ok.session.takeOutput();
    REQUIRE(rep.size() == 320);
    CHECK(rep[0] == 0x01);
    CHECK(rep[1] == 0x11);
    CHECK(rep[3] == 0x03);
    CHECK(rd32(rep, 4) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(&rep[8 + 256])) == "1-1");
    CHECK(rd32(rep, 8 + 296) == 3);  // high speed
    CHECK(rep[8 + 300] == 0x05);     // vendor 054c
    CHECK(rep[8 + 301] == 0x4C);
    CHECK(rep[8 + 302] == 0x0C);  // product 0ce6
    CHECK(rep[8 + 303] == 0xE6);
    CHECK(rep[319] == 1);  // one interface
    CHECK(ok.session.attached());
}

TEST_CASE("USB/IP: Windows enumerates the virtual DualSense") {
    Harness h;
    h.attach();
    auto dev = h.control(1, setupPacket(0x80, 0x06, 0x0100, 0, 64));
    CHECK(dev.status == 0);
    CHECK(dev.data == vds::deviceDescriptor());
    CHECK(h.control(2, setupPacket(0x00, 0x05, 7, 0, 0)).status == 0);  // SET_ADDRESS
    auto cfgHead = h.control(3, setupPacket(0x80, 0x06, 0x0200, 0, 9));
    CHECK(cfgHead.data.size() == 9);
    auto cfg = h.control(4, setupPacket(0x80, 0x06, 0x0200, 0, 255));
    CHECK(cfg.data == vds::configurationDescriptor());
    CHECK(h.control(5, setupPacket(0x80, 0x06, 0x0300, 0, 255)).data == vds::stringDescriptor(0));
    CHECK(h.control(6, setupPacket(0x80, 0x06, 0x0302, 0x0409, 255)).data == vds::stringDescriptor(2));
    CHECK(h.control(7, setupPacket(0x80, 0x06, 0x03EE, 0, 18)).status == -32);  // no MS OS descriptor
    CHECK(h.control(8, setupPacket(0x80, 0x06, 0x0F00, 0, 5)).status == -32);   // no BOS
    CHECK(h.control(9, setupPacket(0x00, 0x09, 1, 0, 0)).status == 0);          // SET_CONFIGURATION
    CHECK(h.control(10, setupPacket(0x21, 0x0A, 0, 0, 0)).status == 0);         // SET_IDLE
    auto report = h.control(11, setupPacket(0x81, 0x06, 0x2200, 0, 273 + 64));
    CHECK(report.data == vds::reportDescriptor());
    CHECK(h.control(12, setupPacket(0x80, 0x00, 0, 0, 2)).data == std::vector<uint8_t>{1, 0});

    // Feature reports come from the real controller; unknown ones stall.
    h.features[0x05] = std::vector<uint8_t>(41, 0x55);
    h.features[0x05][0] = 0x05;
    auto cal = h.control(13, setupPacket(0xA1, 0x01, 0x0305, 0, 41));
    CHECK(cal.status == 0);
    CHECK(cal.data == h.features[0x05]);
    CHECK(h.control(14, setupPacket(0xA1, 0x01, 0x0309, 0, 20)).status == -32);
    // GET_REPORT for the input report.
    CHECK(h.control(15, setupPacket(0xA1, 0x01, 0x0101, 0, 64)).data.size() == 64);
    // SET_REPORT with an output report reaches the controller.
    std::vector<uint8_t> out(48, 0);
    out[0] = 0x02;
    out[1] = 0x01;
    CHECK(h.control(16, setupPacket(0x21, 0x09, 0x0202, 0, 48), out).status == 0);
    REQUIRE(h.outputs.size() == 1);
    CHECK(h.outputs[0] == out);
}

TEST_CASE("USB/IP: input reports, output reports and unlinking") {
    Harness h;
    h.attach();
    // The host keeps interrupt IN requests waiting; each new report completes one.
    CHECK(h.send(submit(100, 1, 1, 64)).empty());
    CHECK(h.send(submit(101, 1, 1, 64)).empty());
    std::array<uint8_t, 64> a{};
    a[0] = 0x01;
    a[1] = 11;
    h.session.inputReport(a);
    auto r = replies(h.session.takeOutput());
    REQUIRE(r.size() == 1);
    CHECK(r[0].seqnum == 100);
    CHECK(r[0].data == std::vector<uint8_t>(a.begin(), a.end()));
    std::array<uint8_t, 64> b = a;
    b[1] = 22;
    h.session.inputReport(b);
    r = replies(h.session.takeOutput());
    REQUIRE(r.size() == 1);
    CHECK(r[0].seqnum == 101);
    CHECK(r[0].data[1] == 22);

    // A report with no request waiting goes out with the next request (only the newest).
    std::array<uint8_t, 64> c = a, d = a;
    c[1] = 33;
    d[1] = 44;
    h.session.inputReport(c);
    h.session.inputReport(d);
    CHECK(h.session.takeOutput().empty());
    r = h.send(submit(102, 1, 1, 64));
    REQUIRE(r.size() == 1);
    CHECK(r[0].data[1] == 44);

    // Unlinking a waiting request: RET_UNLINK -ECONNRESET and no RET_SUBMIT for it.
    CHECK(h.send(submit(103, 1, 1, 64)).empty());
    std::vector<uint8_t> unlink;
    be32(unlink, 2);
    be32(unlink, 104);
    be32(unlink, 0x00010001);
    be32(unlink, 0);
    be32(unlink, 1);
    be32(unlink, 103);  // the request to unlink
    unlink.insert(unlink.end(), 24, 0);
    r = h.send(unlink);
    REQUIRE(r.size() == 1);
    CHECK(r[0].command == 4);
    CHECK(r[0].seqnum == 104);
    CHECK(r[0].status == -104);
    h.session.inputReport(a);
    CHECK(h.session.takeOutput().empty());  // nothing was waiting any more
    // Unlinking a finished request: status 0.
    unlink[23] = 99;
    r = h.send(unlink);
    REQUIRE(r.size() == 1);
    CHECK(r[0].status == 0);

    // Interrupt OUT: the game's output report reaches the controller.
    std::vector<uint8_t> out(48, 7);
    out[0] = 0x02;
    r = h.send(submit(105, 0, 2, 48, {}, out), {105});
    REQUIRE(r.size() == 1);
    CHECK(r[0].status == 0);
    CHECK(r[0].actualLength == 48);
    REQUIRE(h.outputs.size() == 1);
    CHECK(h.outputs[0] == out);

    // Unknown endpoint: stall.
    r = h.send(submit(106, 1, 5, 64));
    REQUIRE(r.size() == 1);
    CHECK(r[0].status == -32);
}

TEST_CASE("USB/IP: messages split at any byte still work") {
    Harness h;
    std::vector<uint8_t> stream = importRequest("1-1");
    const auto cfg = submit(1, 1, 0, 255, setupPacket(0x80, 0x06, 0x0200, 0, 255));
    stream.insert(stream.end(), cfg.begin(), cfg.end());
    std::vector<uint8_t> out(48, 1);
    out[0] = 0x02;
    const auto o = submit(2, 0, 2, 48, {}, out);
    stream.insert(stream.end(), o.begin(), o.end());
    std::vector<uint8_t> received;
    for (uint8_t byte : stream) {
        REQUIRE(h.session.receive(&byte, 1));
        const auto chunk = h.session.takeOutput();
        received.insert(received.end(), chunk.begin(), chunk.end());
    }
    REQUIRE(received.size() > 320);
    const auto r = replies(std::vector<uint8_t>(received.begin() + 320, received.end()), {2});
    REQUIRE(r.size() == 2);
    CHECK(r[0].data == vds::configurationDescriptor());
    CHECK(r[1].actualLength == 48);
    REQUIRE(h.outputs.size() == 1);
}
