#include "app/config_store.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#include "core/config_json.hpp"
#include "platform/paths.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace edgepad {
namespace {

bool replaceFile(const fs::path& from, const fs::path& to, std::string* error) {
#if defined(_WIN32)
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    if (error) *error = "could not replace " + toUtf8(to) + " (error " + std::to_string(GetLastError()) + ")";
    return false;
#else
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec && error) *error = "could not replace " + toUtf8(to) + ": " + ec.message();
    return !ec;
#endif
}

}  // namespace

Config ConfigStore::load(std::string* warning) const {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return [] {
        Config c = Config::defaults();
        c.normalize();
        return c;
    }();
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();

    std::string parseWarning;
    Config cfg = configFromJson(buffer.str(), &parseWarning);
    if (!parseWarning.empty()) {
        std::error_code ec;
        fs::path backup = path_;
        backup += ".bad";
        fs::copy_file(path_, backup, fs::copy_options::overwrite_existing, ec);
        if (warning) *warning = parseWarning + " (old file saved as " + toUtf8(backup.filename()) + ")";
    }
    return cfg;
}

bool ConfigStore::save(const Config& config, std::string* error) const {
    std::error_code ec;
    fs::create_directories(path_.parent_path(), ec);
    fs::path temp = path_;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + toUtf8(temp);
            return false;
        }
        out << configToJson(config) << '\n';
        if (!out) {
            if (error) *error = "cannot write " + toUtf8(temp);
            return false;
        }
    }
    return replaceFile(temp, path_, error);
}

}  // namespace edgepad
