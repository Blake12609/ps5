#include "core/device_path.hpp"

#include <cctype>

namespace edgepad {

std::string instanceIdFromHidPath(const std::string& path) {
    // Drop the "\\?\" prefix and the "#{interface class guid}" suffix; inside, '#' stands for '\'.
    std::string id = path;
    if (const size_t slash = id.find_last_of('\\'); slash != std::string::npos) id.erase(0, slash + 1);
    const size_t guid = id.rfind("#{");
    if (guid == std::string::npos) return {};
    id.erase(guid);
    for (char& c : id) {
        if (c == '#') c = '\\';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (id.size() < 5 || id.compare(0, 4, "HID\\") != 0) return {};
    return id;
}

}  // namespace edgepad
