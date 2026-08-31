#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>

#include "te/telemetry/event_record_format.hpp"
#include "te/telemetry/segment_format.hpp"

namespace {

te::SegmentHeader makeHeader() {
    te::SegmentHeader header;
    header.recordSize = te::kEventRecordSize;
    header.instrumentSpec = te::InstrumentSpec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    header.seedTimestampMicros = 0x0102030405060708ULL;
    header.creationTimestampMicros = 0x1112131415161718ULL;
    for (std::size_t index = 0; index < header.snapshotSha256.size(); ++index) {
        header.snapshotSha256[index] = static_cast<std::byte>(index);
    }
    return header;
}

}  // namespace

TEST(SegmentFormat, EncodesExactLayoutAndDecodesRoundTrip) {
    const te::SegmentHeader header = makeHeader();
    std::array<std::byte, te::kSegmentHeaderSize> bytes{};

    const auto encoded = te::encodeSegmentHeader(header, bytes);
    ASSERT_TRUE(encoded.hasValue());
    EXPECT_EQ(*encoded.valueIf(), te::kSegmentHeaderSize);

    EXPECT_TRUE(std::equal(te::kSegmentMagic.begin(), te::kSegmentMagic.end(), bytes.begin()));
    EXPECT_EQ(bytes[8], std::byte{0x03});
    EXPECT_EQ(bytes[9], std::byte{0x00});
    EXPECT_EQ(bytes[12], std::byte{0x80});
    EXPECT_EQ(bytes[13], std::byte{0x00});
    EXPECT_EQ(bytes[14], std::byte{0x40});
    EXPECT_EQ(bytes[15], std::byte{0x00});
    EXPECT_EQ(bytes[16], std::byte{0x04});
    EXPECT_EQ(bytes[17], std::byte{0x03});
    EXPECT_EQ(bytes[18], std::byte{0x02});
    EXPECT_EQ(bytes[19], std::byte{0x01});
    EXPECT_EQ(bytes[56], std::byte{0x02});
    EXPECT_EQ(bytes[57], std::byte{0x08});
    EXPECT_EQ(bytes[64], std::byte{0x08});
    EXPECT_EQ(bytes[71], std::byte{0x01});

    const auto decoded = te::decodeSegmentHeader(bytes);
    ASSERT_TRUE(decoded.hasValue());
    const te::SegmentHeader& decodedHeader = *decoded.valueIf();
    EXPECT_EQ(decodedHeader.majorVersion, header.majorVersion);
    EXPECT_EQ(decodedHeader.minorVersion, header.minorVersion);
    EXPECT_EQ(decodedHeader.recordSize, header.recordSize);
    EXPECT_EQ(decodedHeader.flags, header.flags);
    EXPECT_EQ(decodedHeader.instrumentSpec.venue_id, header.instrumentSpec.venue_id);
    EXPECT_EQ(decodedHeader.instrumentSpec.instrument_id,
              header.instrumentSpec.instrument_id);
    EXPECT_EQ(decodedHeader.instrumentSpec.price_decimals,
              header.instrumentSpec.price_decimals);
    EXPECT_EQ(decodedHeader.instrumentSpec.quantity_decimals,
              header.instrumentSpec.quantity_decimals);
    EXPECT_EQ(decodedHeader.seedTimestampMicros, header.seedTimestampMicros);
    EXPECT_EQ(decodedHeader.creationTimestampMicros, header.creationTimestampMicros);
    EXPECT_EQ(decodedHeader.snapshotSha256, header.snapshotSha256);
}

TEST(SegmentFormat, FailedEncodeDoesNotModifyOutput) {
    te::SegmentHeader header = makeHeader();
    header.recordSize = 7;
    std::array<std::byte, te::kSegmentHeaderSize> bytes;
    bytes.fill(std::byte{0xA5});
    const auto original = bytes;

    const auto encoded = te::encodeSegmentHeader(header, bytes);

    ASSERT_FALSE(encoded.hasValue());
    ASSERT_NE(encoded.errorIf(), nullptr);
    EXPECT_EQ(*encoded.errorIf(), te::SegmentFormatError::invalid_record_size);
    EXPECT_EQ(bytes, original);
}

TEST(SegmentFormat, DecodeRejectsBadMagic) {
    std::array<std::byte, te::kSegmentHeaderSize> bytes{};
    ASSERT_TRUE(te::encodeSegmentHeader(makeHeader(), bytes).hasValue());
    bytes[0] = std::byte{0x00};

    const auto decoded = te::decodeSegmentHeader(bytes);

    ASSERT_FALSE(decoded.hasValue());
    ASSERT_NE(decoded.errorIf(), nullptr);
    EXPECT_EQ(*decoded.errorIf(), te::SegmentFormatError::bad_magic);
}

TEST(SegmentFormat, DecodeRejectsNonzeroReservedBytes) {
    std::array<std::byte, te::kSegmentHeaderSize> bytes{};
    ASSERT_TRUE(te::encodeSegmentHeader(makeHeader(), bytes).hasValue());
    bytes[58] = std::byte{0x01};

    const auto decoded = te::decodeSegmentHeader(bytes);

    ASSERT_FALSE(decoded.hasValue());
    ASSERT_NE(decoded.errorIf(), nullptr);
    EXPECT_EQ(*decoded.errorIf(), te::SegmentFormatError::nonzero_reserved_bytes);
}

TEST(EventRecordFormat, DefinesOrderAndTradeLayoutsWithinSixtyFourBytes) {
    EXPECT_EQ(te::event_record_layout::size, 64U);
    EXPECT_EQ(te::event_record_layout::common::type, 0U);
    EXPECT_EQ(te::event_record_layout::common::timestamp, 8U);

    EXPECT_EQ(te::event_record_layout::order::orderId, 16U);
    EXPECT_EQ(te::event_record_layout::order::eventKind, 49U);
    EXPECT_EQ(te::event_record_layout::order::reserved +
                  te::event_record_layout::order::reservedSize,
              te::event_record_layout::size);

    EXPECT_EQ(te::event_record_layout::trade::buyOrderId, 16U);
    EXPECT_EQ(te::event_record_layout::trade::quantity, 32U);
    EXPECT_EQ(te::event_record_layout::trade::reserved +
                  te::event_record_layout::trade::reservedSize,
              te::event_record_layout::size);
}

TEST(EventRecordFormat, EncodesOrderRecordWithExactBytes) {
    const te::OrderEvent event{
        .venue_timestamp_us = 0x0102030405060708ULL,
        .order_id = te::OrderId{0x1112131415161718ULL},
        .price = te::Price{0x2122232425262728LL},
        .quantity = te::Qty{0x3132333435363738LL},
        .side = te::Side::sell,
        .kind = te::EventKind::modify,
    };
    std::array<std::byte, te::event_record_layout::size> bytes;
    bytes.fill(std::byte{0xA5});

    const auto encoded = te::encodeOrderRecord(
        event, te::Qty{0x4142434445464748LL}, bytes);

    ASSERT_TRUE(encoded.hasValue());
    EXPECT_EQ(*encoded.valueIf(), te::event_record_layout::size);
    EXPECT_EQ(bytes[0], std::byte{0x01});
    EXPECT_EQ(bytes[8], std::byte{0x08});
    EXPECT_EQ(bytes[15], std::byte{0x01});
    EXPECT_EQ(bytes[16], std::byte{0x18});
    EXPECT_EQ(bytes[23], std::byte{0x11});
    EXPECT_EQ(bytes[24], std::byte{0x28});
    EXPECT_EQ(bytes[31], std::byte{0x21});
    EXPECT_EQ(bytes[32], std::byte{0x38});
    EXPECT_EQ(bytes[39], std::byte{0x31});
    EXPECT_EQ(bytes[40], std::byte{0x48});
    EXPECT_EQ(bytes[47], std::byte{0x41});
    EXPECT_EQ(bytes[48], std::byte{0x01});
    EXPECT_EQ(bytes[49], std::byte{0x01});
    EXPECT_TRUE(std::all_of(bytes.begin() + 50, bytes.end(),
                            [](std::byte value) { return value == std::byte{0}; }));
}

TEST(EventRecordFormat, FailedOrderEncodeLeavesOutputUnchanged) {
    te::OrderEvent event{};
    event.side = static_cast<te::Side>(9);
    std::array<std::byte, te::event_record_layout::size> bytes;
    bytes.fill(std::byte{0xA5});
    const auto original = bytes;

    const auto encoded = te::encodeOrderRecord(event, te::Qty{}, bytes);

    ASSERT_FALSE(encoded.hasValue());
    ASSERT_NE(encoded.errorIf(), nullptr);
    EXPECT_EQ(*encoded.errorIf(), te::EventRecordFormatError::invalid_side);
    EXPECT_EQ(bytes, original);
}

TEST(EventRecordFormat, RejectsOrderOutputBufferThatIsTooSmall) {
    const te::OrderEvent event{
        .venue_timestamp_us = 1,
        .order_id = te::OrderId{2},
        .price = te::Price{3},
        .quantity = te::Qty{4},
        .side = te::Side::buy,
        .kind = te::EventKind::add,
    };
    std::array<std::byte, te::event_record_layout::size - 1> bytes{};

    const auto encoded = te::encodeOrderRecord(event, te::Qty{}, bytes);

    ASSERT_FALSE(encoded.hasValue());
    ASSERT_NE(encoded.errorIf(), nullptr);
    EXPECT_EQ(*encoded.errorIf(), te::EventRecordFormatError::buffer_too_small);
}
