#include <cstring>
#include <te/util/byte_buffer.hpp>

namespace te {

bool writeU8(std::span<std::byte> buffer, std::size_t offset, std::uint8_t value) {
    // Check offset before subtracting it: size_t underflow would make an invalid range look large.
    if (offset > buffer.size()) {
        return false;
    } else if ((buffer.size() - offset) < 1) {
        return false;
    }

    buffer[offset] = static_cast<std::byte>(value);
    return true;
};
bool readU8(std::span<const std::byte> buffer, std::size_t offset, std::uint8_t& out) {
    if (offset > buffer.size()) {
        return false;
    } else if ((buffer.size() - offset) < 1) {
        return false;
    }

    out = static_cast<std::uint8_t>(buffer[offset]);
    return true;
};

bool writeU64(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value) {
    if (offset > buffer.size()) {
        return false;
    } else if ((buffer.size() - offset) < 8) {
        return false;
    }

    // memcpy supports unaligned byte offsets without violating aliasing rules.
    std::memcpy(&buffer[offset], &value, sizeof(value));
    return true;
};
bool readU64(std::span<const std::byte> buffer, std::size_t offset, std::uint64_t& out) {
    if (offset > buffer.size()) {
        return false;
    } else if ((buffer.size() - offset) < 8) {
        return false;
    }

    std::memcpy(&out, &buffer[offset], sizeof(out));
    return true;
};

}  // namespace te
