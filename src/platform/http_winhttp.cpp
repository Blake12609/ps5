#include "platform/http.hpp"

#include <windows.h>
#include <winhttp.h>

#include <fstream>
#include <functional>
#include <iterator>

#include "core/build_info.hpp"
#include "platform/paths.hpp"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace edgepad {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::string lastErrorText(const char* what) { return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")"; }

// Performs a GET and streams the body into `sink`. Returns the HTTP status or -1.
int request(const std::string& url, const HttpHeaders& headers, const std::function<bool(const char*, size_t)>& sink,
            std::string& error) {
    const std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[4096] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    wchar_t extra[4096] = {};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        error = "invalid URL";
        return -1;
    }

    const std::wstring agent = widen(std::string(build::kAppName) + "/" + build::kVersion);
    Handle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) {
        session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session.h) {
        error = lastErrorText("WinHttpOpen");
        return -1;
    }
    WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 60000);

    Handle connection{WinHttpConnect(session.h, host, parts.nPort, 0)};
    if (!connection.h) {
        error = lastErrorText("WinHttpConnect");
        return -1;
    }
    const std::wstring target = std::wstring(path) + extra;
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle req{WinHttpOpenRequest(connection.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!req.h) {
        error = lastErrorText("WinHttpOpenRequest");
        return -1;
    }
    std::wstring headerBlock;
    for (const auto& [name, value] : headers) headerBlock += widen(name + ": " + value + "\r\n");
    if (!WinHttpSendRequest(req.h, headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                            headerBlock.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        error = lastErrorText("HTTP request");
        return -1;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);

    std::string chunk;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.h, &available)) {
            error = lastErrorText("WinHttpQueryDataAvailable");
            return -1;
        }
        if (available == 0) break;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, chunk.data(), available, &read)) {
            error = lastErrorText("WinHttpReadData");
            return -1;
        }
        if (read == 0) break;
        if (!sink(chunk.data(), read)) {
            error = "could not write downloaded data";
            return -1;
        }
    }
    return static_cast<int>(status);
}

}  // namespace

HttpResponse httpGet(const std::string& url, const HttpHeaders& headers) {
    HttpResponse response;
    response.status = request(
        url, headers,
        [&](const char* data, size_t n) {
            response.body.append(data, n);
            return true;
        },
        response.error);
    return response;
}

bool httpDownload(const std::string& url, const std::filesystem::path& destination, std::string& error) {
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write " + toUtf8(destination);
        return false;
    }
    const int status = request(
        url, {},
        [&](const char* data, size_t n) {
            out.write(data, static_cast<std::streamsize>(n));
            return static_cast<bool>(out);
        },
        error);
    out.close();
    if (status < 200 || status >= 300) {
        if (error.empty()) error = "HTTP status " + std::to_string(status);
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        return false;
    }
    return true;
}

}  // namespace edgepad
