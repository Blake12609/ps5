#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace edgepad {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error;
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// HTTPS GET into memory. Follows redirects. Windows: WinHTTP, Linux: curl.
HttpResponse httpGet(const std::string& url, const HttpHeaders& headers = {});

// HTTPS GET straight into a file.
bool httpDownload(const std::string& url, const std::filesystem::path& destination, std::string& error);

}  // namespace edgepad
