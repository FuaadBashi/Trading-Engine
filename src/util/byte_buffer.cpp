#include <bit>
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

bool writeU16LE(std::span<std::byte> buffer, std::size_t offset, std::uint16_t value) {
    constexpr std::size_t width = sizeof(value);
    if (offset > buffer.size() || buffer.size() - offset < width) {
        return false;
    }

    for (std::size_t byte = 0; byte < width; ++byte) {
        buffer[offset + byte] =
            static_cast<std::byte>((value >> (byte * 8U)) & 0xFFU);
    }
    return true;
}

bool readU16LE(std::span<const std::byte> buffer, std::size_t offset, std::uint16_t& out) {
    constexpr std::size_t width = sizeof(out);
    if (offset > buffer.size() || buffer.size() - offset < width) {
        return false;
    }

    std::uint16_t value{};
    for (std::size_t byte = 0; byte < width; ++byte) {
        value |= static_cast<std::uint16_t>(
                     std::to_integer<std::uint8_t>(buffer[offset + byte]))
                 << (byte * 8U);
    }
    out = value;
    return true;
}

bool writeU32LE(std::span<std::byte> buffer, std::size_t offset, std::uint32_t value) {
    constexpr std::size_t width = sizeof(value);
    if (offset > buffer.size() || buffer.size() - offset < width) {
        return false;
    }

    for (std::size_t byte = 0; byte < width; ++byte) {
        buffer[offset + byte] =
            static_cast<std::byte>((value >> (byte * 8U)) & 0xFFU);
    }
    return true;
}

bool readU32LE(std::span<const std::byte> buffer, std::size_t offset, std::uint32_t& out) {
    constexpr std::size_t width = sizeof(out);
    if (offset > buffer.size() || buffer.size() - offset < width) {
        return false;
    }

    std::uint32_t value{};
    for (std::size_t byte = 0; byte < width; ++byte) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(buffer[offset + byte]))
                 << (byte * 8U);
    }
    out = value;
    return true;
}

bool writeU64LE(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value) {
    constexpr std::size_t width = sizeof(value);
    if (offset > buffer.size() || buffer.size() - offset < width) {
        return false;
    }

    for (std::size_t byte = 0; byte < width; ++byte) {
        buffer[offset + byte] =
            static_cast<std::byte>((value >> (byte * 8U)) & 0xFFULL);
    }
    return true;
}

bool readU64LE(std::span<const std::byte> buffer, std::size_t offset, std::uint64_t& out) {
    constexpr std::size_t width = sizeof(out);
    if (offset > buffer.size() || buffer.size() - offset < width) {
        return false;
    }

    std::uint64_t value{};
    for (std::size_t byte = 0; byte < width; ++byte) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(buffer[offset + byte]))
                 << (byte * 8U);
    }
    out = value;
    return true;
}

bool writeI64LE(std::span<std::byte> buffer, std::size_t offset, std::int64_t value) {
    return writeU64LE(buffer, offset, std::bit_cast<std::uint64_t>(value));
}

bool readI64LE(std::span<const std::byte> buffer, std::size_t offset, std::int64_t& out) {
    std::uint64_t encoded{};
    if (!readU64LE(buffer, offset, encoded)) {
        return false;
    }

    out = std::bit_cast<std::int64_t>(encoded);
    return true;
}

}  // namespace te
