#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Slice 1. Read/write primitives over a byte span for the binary capture.

namespace te {

// All operations bounds-check first. On failure the buffer or output argument is untouched;
// bool is sufficient because out-of-bounds is the only failure mode (ADR 0003).
bool writeU8(std::span<std::byte> buffer, std::size_t offset, std::uint8_t value);

bool readU8(std::span<const std::byte> buffer, std::size_t offset, std::uint8_t& out);

// U64 values use memcpy so unaligned offsets and aliasing are safe. Byte order remains the
// host-dependent legacy format; portable v3 will define little-endian bytes (ADR 0011).
bool writeU64(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value);

bool readU64(std::span<const std::byte> buffer, std::size_t offset, std::uint64_t& out);

// Durable-format primitives. Multi-byte values are encoded explicitly in little-endian order.
bool writeU16LE(std::span<std::byte> buffer, std::size_t offset, std::uint16_t value);
bool readU16LE(std::span<const std::byte> buffer, std::size_t offset, std::uint16_t& out);

bool writeU32LE(std::span<std::byte> buffer, std::size_t offset, std::uint32_t value);
bool readU32LE(std::span<const std::byte> buffer, std::size_t offset, std::uint32_t& out);

bool writeU64LE(std::span<std::byte> buffer, std::size_t offset, std::uint64_t value);
bool readU64LE(std::span<const std::byte> buffer, std::size_t offset, std::uint64_t& out);

bool writeI64LE(std::span<std::byte> buffer, std::size_t offset, std::int64_t value);
bool readI64LE(std::span<const std::byte> buffer, std::size_t offset, std::int64_t& out);

}  // namespace te
