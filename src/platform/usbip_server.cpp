#include "platform/usbip_server.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>

namespace edgepad {
namespace {

#if defined(_WIN32)
using Socket = SOCKET;
const Socket kInvalid = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
void shutdownSocket(Socket s) { shutdown(s, SD_BOTH); }
std::string socketError() { return "error " + std::to_string(WSAGetLastError()); }
#else
using Socket = int;
const Socket kInvalid = -1;
void closeSocket(Socket s) { ::close(s); }
void shutdownSocket(Socket s) { shutdown(s, SHUT_RDWR); }
std::string socketError() { return std::strerror(errno); }
#endif

Socket toSocket(std::intptr_t s) { return static_cast<Socket>(s); }
std::intptr_t fromSocket(Socket s) { return s == kInvalid ? -1 : static_cast<std::intptr_t>(s); }

}  // namespace

UsbIpServer::UsbIpServer(usbip::Session::Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

UsbIpServer::~UsbIpServer() { stop(); }

bool UsbIpServer::start(uint16_t port, std::string& error) {
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        error = "Winsock is not available";
        return false;
    }
#endif
    const Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid) {
        error = "socket: " + socketError();
        return false;
    }
#if !defined(_WIN32)
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));  // restart right after a stop
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // this computer only
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 1) != 0) {
        error = "cannot listen on 127.0.0.1:" + std::to_string(port) + " (" + socketError() +
                "; is another USB/IP server running?)";
        closeSocket(s);
        return false;
    }
    socklen_t len = sizeof(addr);
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    listen_ = fromSocket(s);
    stop_ = false;
    thread_ = std::thread([this] { run(); });
    return true;
}

void UsbIpServer::stop() {
    stop_ = true;
    if (listen_ != -1) shutdownSocket(toSocket(listen_));
    {
        std::lock_guard lock(mutex_);
        if (client_ != -1) shutdownSocket(toSocket(client_));
    }
    if (listen_ != -1) closeSocket(toSocket(listen_));  // wakes accept() on Windows
    if (thread_.joinable()) thread_.join();
    listen_ = -1;
#if defined(_WIN32)
    if (port_ != 0) WSACleanup();
#endif
    port_ = 0;
}

void UsbIpServer::run() {
    while (!stop_) {
        const Socket c = accept(toSocket(listen_), nullptr, nullptr);
        if (c == kInvalid) {
            if (stop_) break;
            continue;
        }
        int one = 1;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
        {
            std::lock_guard lock(mutex_);
            client_ = fromSocket(c);
            session_ = std::make_unique<usbip::Session>(callbacks_);
        }
        uint8_t buffer[4096];
        while (!stop_) {
            const auto n = recv(c, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
            if (n <= 0) break;
            std::lock_guard lock(mutex_);
            const bool keep = session_->receive(buffer, static_cast<size_t>(n));
            sendLocked(session_->takeOutput());
            if (!keep) break;
        }
        std::lock_guard lock(mutex_);
        closeSocket(c);
        client_ = -1;
        session_.reset();
    }
}

void UsbIpServer::sendLocked(const std::vector<uint8_t>& bytes) {
    size_t sent = 0;
    while (sent < bytes.size() && client_ != -1) {
        const auto n = send(toSocket(client_), reinterpret_cast<const char*>(bytes.data() + sent),
                            static_cast<int>(bytes.size() - sent), 0);
        if (n <= 0) {
            shutdownSocket(toSocket(client_));  // the receive loop ends the connection
            return;
        }
        sent += static_cast<size_t>(n);
    }
}

void UsbIpServer::inputReport(const std::array<uint8_t, 64>& report) {
    std::lock_guard lock(mutex_);
    if (!session_ || !session_->attached()) return;
    session_->inputReport(report);
    sendLocked(session_->takeOutput());
}

bool UsbIpServer::attached() const {
    std::lock_guard lock(mutex_);
    return session_ && session_->attached();
}

}  // namespace edgepad
