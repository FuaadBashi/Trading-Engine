#include "te/telemetry/segment_format.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "te/util/byte_buffer.hpp"

namespace te {
namespace {

constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kMajorVersionOffset = 8;
constexpr std::size_t kMinorVersionOffset = 10;
constexpr std::size_t kHeaderSizeOffset = 12;
constexpr std::size_t kRecordSizeOffset = 14;
constexpr std::size_t kEndianMarkerOffset = 16;
constexpr std::size_t kFlagsOffset = 20;
constexpr std::size_t kVenueOffset = 24;
constexpr std::size_t kInstrumentOffset = 40;
constexpr std::size_t kIdentifierSize = 16;
constexpr std::size_t kPriceDecimalsOffset = 56;
constexpr std::size_t kQuantityDecimalsOffset = 57;
constexpr std::size_t kFirstReservedOffset = 58;
constexpr std::size_t kFirstReservedSize = 6;
constexpr std::size_t kSeedTimestampOffset = 64;
constexpr std::size_t kCreationTimestampOffset = 72;
constexpr std::size_t kSnapshotHashOffset = 80;
constexpr std::size_t kFinalReservedOffset = 112;
constexpr std::size_t kFinalReservedSize = 16;
constexpr std::uint8_t kMaximumDecimalScale = 18;

std::string_view venueName(VenueId venue) {
    switch (venue) {
        case VenueId::bitstamp:
            return "bitstamp";
        case VenueId::coinbase:
            return "coinbase";
        case VenueId::unknown:
            return {};
    }
    return {};
}

std::string_view instrumentName(InstrumentId instrument) {
    switch (instrument) {
        case InstrumentId::btc_usd:
            return "btcusd";
        case InstrumentId::btc_gbp:
            return "btcgbp";
        case InstrumentId::btc_eur:
            return "btceur";
        case InstrumentId::unknown:
            return {};
    }
    return {};
}

void writeIdentifier(std::span<std::byte> output, std::size_t offset, std::string_view value) {
    for (std::size_t index = 0; index < value.size(); ++index) {
        output[offset + index] = static_cast<std::byte>(value[index]);
    }
}

bool identifierMatches(std::span<const std::byte> input,
                       std::size_t offset,
                       std::string_view expected) {
    for (std::size_t index = 0; index < kIdentifierSize; ++index) {
        const std::byte expectedByte =
            index < expected.size() ? static_cast<std::byte>(expected[index]) : std::byte{0};
        if (input[offset + index] != expectedByte) {
            return false;
        }
    }
    return true;
}

bool allZero(std::span<const std::byte> input, std::size_t offset, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        if (input[offset + index] != std::byte{0}) {
            return false;
        }
    }
    return true;
}

bool validRecordSize(std::uint16_t size) {
    return size == kSnapshotRecordSize || size == kEventRecordSize;
}

}  // namespace

Result<std::size_t, SegmentFormatError> encodeSegmentHeader(
    const SegmentHeader& header, std::span<std::byte> output) {
    if (output.size() < kSegmentHeaderSize) {
        return Result<std::size_t, SegmentFormatError>::failure(
            SegmentFormatError::buffer_too_small);
    }
    if (header.majorVersion != kSegmentFormatMajor) {
        return Result<std::size_t, SegmentFormatError>::failure(
            SegmentFormatError::unsupported_major_version);
    }
    if (!validRecordSize(header.recordSize)) {
        return Result<std::size_t, SegmentFormatError>::failure(
            SegmentFormatError::invalid_record_size);
    }
    if (header.flags != 0U) {
        return Result<std::size_t, SegmentFormatError>::failure(
            SegmentFormatError::unsupported_flags);
    }

    const std::string_view venue = venueName(header.instrumentSpec.venue_id);
    if (venue.empty()) {
        return Result<std::size_t, SegmentFormatError>::failure(
            SegmentFormatError::invalid_venue);
    }
    const std::string_view instrument = instrumentName(header.instrumentSpec.instrument_id);
    if (instrument.empty()) {
        return Result<std::size_t, SegmentFormatError>::failure(
            SegmentFormatError::invalid_instrument);
    }
    if (header.instrumentSpec.price_decimals > kMaximumDecimalScale ||
        header.instrumentSpec.quantity_decimals > kMaximumDecimalScale) {
        return Result<std::size_t, SegmentFormatError>::failure(
            SegmentFormatError::invalid_scale);
    }

    std::array<std::byte, kSegmentHeaderSize> encoded{};
    std::copy(kSegmentMagic.begin(), kSegmentMagic.end(), encoded.begin() + kMagicOffset);
    const bool wroteFields =
        writeU16LE(encoded, kMajorVersionOffset, header.majorVersion) &&
        writeU16LE(encoded, kMinorVersionOffset, header.minorVersion) &&
        writeU16LE(encoded, kHeaderSizeOffset,
                   static_cast<std::uint16_t>(kSegmentHeaderSize)) &&
        writeU16LE(encoded, kRecordSizeOffset, header.recordSize) &&
        writeU32LE(encoded, kEndianMarkerOffset, kLittleEndianMarker) &&
        writeU32LE(encoded, kFlagsOffset, header.flags) &&
        writeU8(encoded, kPriceDecimalsOffset, header.instrumentSpec.price_decimals) &&
        writeU8(encoded, kQuantityDecimalsOffset, header.instrumentSpec.quantity_decimals) &&
        writeU64LE(encoded, kSeedTimestampOffset, header.seedTimestampMicros) &&
        writeU64LE(encoded, kCreationTimestampOffset, header.creationTimestampMicros);
    if (!wroteFields) {
        return Result<std::size_t, SegmentFormatError>::failure(
            SegmentFormatError::buffer_too_small);
    }

    writeIdentifier(encoded, kVenueOffset, venue);
    writeIdentifier(encoded, kInstrumentOffset, instrument);
    std::copy(header.snapshotSha256.begin(), header.snapshotSha256.end(),
              encoded.begin() + kSnapshotHashOffset);
    std::copy(encoded.begin(), encoded.end(), output.begin());
    return Result<std::size_t, SegmentFormatError>::success(kSegmentHeaderSize);
}

Result<SegmentHeader, SegmentFormatError> decodeSegmentHeader(
    std::span<const std::byte> input) {
    if (input.size() < kSegmentHeaderSize) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::buffer_too_small);
    }
    if (!std::equal(kSegmentMagic.begin(), kSegmentMagic.end(), input.begin() + kMagicOffset)) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::bad_magic);
    }

    SegmentHeader header;
    std::uint16_t encodedHeaderSize{};
    std::uint32_t endianMarker{};
    if (!readU16LE(input, kMajorVersionOffset, header.majorVersion) ||
        !readU16LE(input, kMinorVersionOffset, header.minorVersion) ||
        !readU16LE(input, kHeaderSizeOffset, encodedHeaderSize) ||
        !readU16LE(input, kRecordSizeOffset, header.recordSize) ||
        !readU32LE(input, kEndianMarkerOffset, endianMarker) ||
        !readU32LE(input, kFlagsOffset, header.flags) ||
        !readU8(input, kPriceDecimalsOffset, header.instrumentSpec.price_decimals) ||
        !readU8(input, kQuantityDecimalsOffset, header.instrumentSpec.quantity_decimals) ||
        !readU64LE(input, kSeedTimestampOffset, header.seedTimestampMicros) ||
        !readU64LE(input, kCreationTimestampOffset, header.creationTimestampMicros)) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::buffer_too_small);
    }

    if (header.majorVersion != kSegmentFormatMajor) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::unsupported_major_version);
    }
    if (encodedHeaderSize != kSegmentHeaderSize) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::invalid_header_size);
    }
    if (!validRecordSize(header.recordSize)) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::invalid_record_size);
    }
    if (endianMarker != kLittleEndianMarker) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::invalid_endian_marker);
    }
    if (header.flags != 0U) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::unsupported_flags);
    }

    if (identifierMatches(input, kVenueOffset, "bitstamp")) {
        header.instrumentSpec.venue_id = VenueId::bitstamp;
    } else if (identifierMatches(input, kVenueOffset, "coinbase")) {
        header.instrumentSpec.venue_id = VenueId::coinbase;
    } else {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::invalid_venue);
    }

    if (identifierMatches(input, kInstrumentOffset, "btcusd")) {
        header.instrumentSpec.instrument_id = InstrumentId::btc_usd;
    } else if (identifierMatches(input, kInstrumentOffset, "btcgbp")) {
        header.instrumentSpec.instrument_id = InstrumentId::btc_gbp;
    } else if (identifierMatches(input, kInstrumentOffset, "btceur")) {
        header.instrumentSpec.instrument_id = InstrumentId::btc_eur;
    } else {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::invalid_instrument);
    }

    if (header.instrumentSpec.price_decimals > kMaximumDecimalScale ||
        header.instrumentSpec.quantity_decimals > kMaximumDecimalScale) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::invalid_scale);
    }
    if (!allZero(input, kFirstReservedOffset, kFirstReservedSize) ||
        !allZero(input, kFinalReservedOffset, kFinalReservedSize)) {
        return Result<SegmentHeader, SegmentFormatError>::failure(
            SegmentFormatError::nonzero_reserved_bytes);
    }

    std::copy_n(input.begin() + kSnapshotHashOffset, header.snapshotSha256.size(),
                header.snapshotSha256.begin());
    return Result<SegmentHeader, SegmentFormatError>::success(header);
}

}  // namespace te
