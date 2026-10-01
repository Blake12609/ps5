// The USB/IP server over real TCP sockets (Winsock on Windows, POSIX elsewhere): a client that
// talks like usbip-win2 imports the virtual DualSense, reads a descriptor, receives input reports
// and sends an output report.
#include <doctest/doctest.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "core/virtual_dualsense.hpp"
#include "platform/usbip_server.hpp"

using namespace edgepad;

namespace {

#if defined(_WIN32)
using Socket = SOCKET;
void closeSocket(Socket s) { closesocket(s); }
#else
using Socket = int;
void closeSocket(Socket s) { ::close(s); }
#endif

Socket connectTo(uint16_t port) {
    Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    return s;
}

void sendAll(Socket s, const std::vector<uint8_t>& data) {
    REQUIRE(send(s, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0) ==
            static_cast<int>(data.size()));
}

std::vector<uint8_t> receiveExactly(Socket s, size_t n) {
    std::vector<uint8_t> out(n);
    size_t got = 0;
    while (got < n) {
        const auto r = recv(s, reinterpret_cast<char*>(out.data() + got), static_cast<int>(n - got), 0);
        REQUIRE(r > 0);
        got += static_cast<size_t>(r);
    }
    return out;
}

void be32(std::vector<uint8_t>& v, uint32_t x) {
    for (int sh = 24; sh >= 0; sh -= 8) v.push_back(static_cast<uint8_t>(x >> sh));
}
uint32_t rd32(const std::vector<uint8_t>& v, size_t at) {
    return (uint32_t{v[at]} << 24) | (uint32_t{v[at + 1]} << 16) | (uint32_t{v[at + 2]} << 8) | v[at + 3];
}

std::vector<uint8_t> submit(uint32_t seq, uint32_t dir, uint32_t ep, uint32_t length, std::array<uint8_t, 8> setup,
                            const std::vector<uint8_t>& data = {}) {
    std::vector<uint8_t> v;
    for (uint32_t x : {1u, seq, 0x00010001u, dir, ep, 0u, length, 0u, 0xFFFFFFFFu, 0u}) be32(v, x);
    v.insert(v.end(), setup.begin(), setup.end());
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

}  // namespace

TEST_CASE("USB/IP server: import, descriptors, input and output over TCP") {
#if defined(_WIN32)
    WSADATA wsa;
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif
    std::mutex m;
    std::vector<std::vector<uint8_t>> outputs;
    UsbIpServer server({[&](const uint8_t* d, size_t n) {
                            std::lock_guard lock(m);
                            outputs.emplace_back(d, d + n);
                        },
                        [](uint8_t) { return std::optional<std::vector<uint8_t>>{}; }});
    std::string error;
    REQUIRE(server.start(0, error));  // any free port
    REQUIRE(server.port() != 0);

    const Socket c = connectTo(server.port());
    std::vector<uint8_t> import = {0x01, 0x11, 0x80, 0x03, 0, 0, 0, 0};
    import.resize(8 + 32, 0);
    std::memcpy(&import[8], "1-1", 3);
    sendAll(c, import);
    const auto reply = receiveExactly(c, 320);
    CHECK(rd32(reply, 4) == 0);
    for (int i = 0; i < 100 && !server.attached(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(server.attached());

    // Device descriptor through endpoint 0.
    sendAll(c, submit(1, 1, 0, 18, {0x80, 0x06, 0x00, 0x01, 0, 0, 18, 0}));
    auto hdr = receiveExactly(c, 48);
    CHECK(rd32(hdr, 0) == 3);
    CHECK(rd32(hdr, 4) == 1);
    CHECK(rd32(hdr, 24) == 18);
    CHECK(receiveExactly(c, 18) == virtual_dualsense::deviceDescriptor());

    // An interrupt IN request waits for the next input report.
    sendAll(c, submit(2, 1, 1, 64, {}));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    OutputState s;
    s.lx = 1.0f;
    const auto report = virtual_dualsense::buildInputReport(s);
    server.inputReport(report);
    hdr = receiveExactly(c, 48);
    CHECK(rd32(hdr, 4) == 2);
    REQUIRE(rd32(hdr, 24) == 64);
    const auto data = receiveExactly(c, 64);
    CHECK(std::vector<uint8_t>(report.begin(), report.end()) == data);
    CHECK(data[1] == 255);

    // An output report from the "game" reaches the callback.
    std::vector<uint8_t> out(48, 0);
    out[0] = 0x02;
    out[1] = 0x0C;
    sendAll(c, submit(3, 0, 2, 48, {}, out));
    hdr = receiveExactly(c, 48);
    CHECK(rd32(hdr, 4) == 3);
    CHECK(rd32(hdr, 24) == 48);
    {
        std::lock_guard lock(m);
        REQUIRE(outputs.size() == 1);
        CHECK(outputs[0] == out);
    }

    closeSocket(c);
    for (int i = 0; i < 100 && server.attached(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK_FALSE(server.attached());  // the connection is gone; the device is free again

    // A new client can attach again (usbip-win2 reconnecting).
    const Socket again = connectTo(server.port());
    sendAll(again, import);
    CHECK(rd32(receiveExactly(again, 320), 4) == 0);
    closeSocket(again);
    server.stop();
#if defined(_WIN32)
    WSACleanup();
#endif
}
