// HTTP on Linux/macOS by running the system `curl` (present on practically every desktop),
// which keeps the binary free of TLS library dependencies.
#include "platform/http.hpp"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "core/build_info.hpp"

extern char** environ;

namespace edgepad {
namespace {

// Runs curl with the given arguments. stdout is captured into `out` when non-null.
int runCurl(std::vector<std::string> args, std::string* out, std::string& error) {
    args.insert(args.begin(), {"curl", "--fail", "--silent", "--show-error", "--location", "--max-time", "120",
                               "--user-agent", std::string(build::kAppName) + "/" + build::kVersion});
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return -1;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, "curl", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);
    if (rc != 0) {
        close(pipefd[0]);
        error = "curl is not installed";
        return -1;
    }
    char buffer[16384];
    ssize_t n = 0;
    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        if (out) out->append(buffer, static_cast<size_t>(n));
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (exitCode != 0) error = "download failed (curl exit code " + std::to_string(exitCode) + ")";
    return exitCode;
}

}  // namespace

HttpResponse httpGet(const std::string& url, const HttpHeaders& headers) {
    HttpResponse response;
    std::vector<std::string> args;
    for (const auto& [name, value] : headers) {
        args.push_back("--header");
        args.push_back(name + ": " + value);
    }
    args.push_back(url);
    if (runCurl(args, &response.body, response.error) == 0) response.status = 200;
    return response;
}

bool httpDownload(const std::string& url, const std::filesystem::path& destination, std::string& error) {
    if (runCurl({"--output", destination.string(), url}, nullptr, error) != 0) {
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        return false;
    }
    return true;
}

}  // namespace edgepad
