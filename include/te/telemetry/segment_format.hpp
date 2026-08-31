#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "te/core/instrument.hpp"
#include "te/core/result.hpp"

namespace te {

inline constexpr std::size_t kSegmentHeaderSize = 128;
inline constexpr std::uint16_t kSegmentFormatMajor = 3;
inline constexpr std::uint16_t kSegmentFormatMinor = 0;
inline constexpr std::uint16_t kSnapshotRecordSize = 32;
inline constexpr std::uint16_t kEventRecordSize = 64;
inline constexpr std::uint32_t kLittleEndianMarker = 0x01020304U;

// ASCII "TESEG" followed by three zero bytes.
inline constexpr std::array<std::byte, 8> kSegmentMagic{
    std::byte{0x54}, std::byte{0x45}, std::byte{0x53}, std::byte{0x45},
    std::byte{0x47}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
};

enum class SegmentFormatError {
    buffer_too_small,
    bad_magic,
    unsupported_major_version,
    invalid_header_size,
    invalid_record_size,
    invalid_endian_marker,
    unsupported_flags,
    invalid_venue,
    invalid_instrument,
    invalid_scale,
    nonzero_reserved_bytes,
};

struct SegmentHeader {
    std::uint16_t majorVersion{kSegmentFormatMajor};
    std::uint16_t minorVersion{kSegmentFormatMinor};
    std::uint16_t recordSize{};
    std::uint32_t flags{};
    InstrumentSpec instrumentSpec{};
    std::uint64_t seedTimestampMicros{};
    std::uint64_t creationTimestampMicros{};
    std::array<std::byte, 32> snapshotSha256{};
};

Result<std::size_t, SegmentFormatError> encodeSegmentHeader(
    const SegmentHeader& header, std::span<std::byte> output);

Result<SegmentHeader, SegmentFormatError> decodeSegmentHeader(
    std::span<const std::byte> input);

}  // namespace te
