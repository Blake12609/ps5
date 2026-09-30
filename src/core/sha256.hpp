#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace edgepad {

// Small streaming SHA-256, used to verify downloaded updates.
class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t length);
    std::array<uint8_t, 32> finish();
    std::string finishHex();

private:
    void transform(const uint8_t* block);

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> buffer_{};
    uint64_t bitLength_ = 0;
    size_t bufferSize_ = 0;
};

std::string sha256Hex(std::string_view data);

}  // namespace edgepad
