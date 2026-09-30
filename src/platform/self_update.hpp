#pragma once

#include <filesystem>
#include <string>

namespace edgepad {

// "<exe>.new": where a downloaded update is staged before it is swapped in.
std::filesystem::path stagedUpdatePath();

// Deletes "<exe>.old" left behind by a previous update.
void cleanupOldBinary();

// Swaps the running executable for `newBinary`. The running process keeps working
// (it runs from the renamed "<exe>.old"); the next launch starts the new version.
bool installUpdate(const std::filesystem::path& newBinary, std::string& error);

// Starts a fresh copy of the executable with the original arguments plus
// "--wait-for-instance" so it waits for this process to exit.
bool relaunch(std::string& error);

}  // namespace edgepad
