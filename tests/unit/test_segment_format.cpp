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

TEST(EventRecordFormat, EncodesTradeRecordWithExactBytes) {
    const te::TradeEvent trade{
        .venue_timestamp_us = 0x0102030405060708ULL,
        .buy_order_id = te::OrderId{0x1112131415161718ULL},
        .sell_order_id = te::OrderId{0x2122232425262728ULL},
        .quantity = te::Qty{0x3132333435363738LL},
    };
    std::array<std::byte, te::event_record_layout::size> bytes;
    bytes.fill(std::byte{0xA5});

    const auto encoded = te::encodeTradeRecord(trade, bytes);

    ASSERT_TRUE(encoded.hasValue());
    EXPECT_EQ(*encoded.valueIf(), te::event_record_layout::size);

    const std::array<std::byte, te::event_record_layout::size> expected{
        std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05},
        std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
        std::byte{0x18}, std::byte{0x17}, std::byte{0x16}, std::byte{0x15},
        std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11},
        std::byte{0x28}, std::byte{0x27}, std::byte{0x26}, std::byte{0x25},
        std::byte{0x24}, std::byte{0x23}, std::byte{0x22}, std::byte{0x21},
        std::byte{0x38}, std::byte{0x37}, std::byte{0x36}, std::byte{0x35},
        std::byte{0x34}, std::byte{0x33}, std::byte{0x32}, std::byte{0x31},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    EXPECT_EQ(bytes, expected);
}

TEST(EventRecordFormat, OrderRecordRoundTripsThroughPortableBytes) {
    const te::OrderEvent event{
        .venue_timestamp_us = 101,
        .order_id = te::OrderId{42},
        .price = te::Price{12345},
        .quantity = te::Qty{60000000},
        .side = te::Side::buy,
        .kind = te::EventKind::modify,
    };
    const te::Qty amountTraded{25000000};
    std::array<std::byte, te::event_record_layout::size> bytes{};
    ASSERT_TRUE(te::encodeOrderRecord(event, amountTraded, bytes).hasValue());

    const auto decoded = te::decodeOrderRecord(bytes);

    ASSERT_TRUE(decoded.hasValue());
    const te::DecodedOrderRecord& value = *decoded.valueIf();
    EXPECT_EQ(value.event.venue_timestamp_us, event.venue_timestamp_us);
    EXPECT_EQ(value.event.order_id, event.order_id);
    EXPECT_EQ(value.event.price, event.price);
    EXPECT_EQ(value.event.quantity, event.quantity);
    EXPECT_EQ(value.event.side, event.side);
    EXPECT_EQ(value.event.kind, event.kind);
    EXPECT_EQ(value.amountTraded, amountTraded);
}

TEST(EventRecordFormat, TradeRecordRoundTripsThroughPortableBytes) {
    const te::TradeEvent trade{
        .venue_timestamp_us = 202,
        .buy_order_id = te::OrderId{81},
        .sell_order_id = te::OrderId{92},
        .quantity = te::Qty{30000000},
    };
    std::array<std::byte, te::event_record_layout::size> bytes{};
    ASSERT_TRUE(te::encodeTradeRecord(trade, bytes).hasValue());

    const auto decoded = te::decodeTradeRecord(bytes);

    ASSERT_TRUE(decoded.hasValue());
    const te::TradeEvent& value = *decoded.valueIf();
    EXPECT_EQ(value.venue_timestamp_us, trade.venue_timestamp_us);
    EXPECT_EQ(value.buy_order_id, trade.buy_order_id);
    EXPECT_EQ(value.sell_order_id, trade.sell_order_id);
    EXPECT_EQ(value.quantity, trade.quantity);
}

TEST(EventRecordFormat, DecoderRejectsWrongRecordType) {
    const te::TradeEvent trade{
        .venue_timestamp_us = 1,
        .buy_order_id = te::OrderId{2},
        .sell_order_id = te::OrderId{3},
        .quantity = te::Qty{4},
    };
    std::array<std::byte, te::event_record_layout::size> bytes{};
    ASSERT_TRUE(te::encodeTradeRecord(trade, bytes).hasValue());

    const auto decoded = te::decodeOrderRecord(bytes);

    ASSERT_FALSE(decoded.hasValue());
    ASSERT_NE(decoded.errorIf(), nullptr);
    EXPECT_EQ(*decoded.errorIf(), te::EventRecordFormatError::unexpected_record_type);
}

TEST(EventRecordFormat, DecoderRejectsNonzeroReservedBytes) {
    const te::TradeEvent trade{
        .venue_timestamp_us = 1,
        .buy_order_id = te::OrderId{2},
        .sell_order_id = te::OrderId{3},
        .quantity = te::Qty{4},
    };
    std::array<std::byte, te::event_record_layout::size> bytes{};
    ASSERT_TRUE(te::encodeTradeRecord(trade, bytes).hasValue());
    bytes[40] = std::byte{0x01};

    const auto decoded = te::decodeTradeRecord(bytes);

    ASSERT_FALSE(decoded.hasValue());
    ASSERT_NE(decoded.errorIf(), nullptr);
    EXPECT_EQ(*decoded.errorIf(), te::EventRecordFormatError::nonzero_reserved_bytes);
}

TEST(EventRecordFormat, MixedDecoderDispatchesOrderRecord) {
    const te::OrderEvent order{
        .venue_timestamp_us = 100,
        .order_id = te::OrderId{42},
        .price = te::Price{5000},
        .quantity = te::Qty{600},
        .side = te::Side::sell,
        .kind = te::EventKind::add,
    };
    const te::Qty amountTraded{25};
    std::array<std::byte, te::event_record_layout::size> bytes{};
    ASSERT_TRUE(te::encodeOrderRecord(order, amountTraded, bytes).hasValue());

    const auto decoded = te::decodeEventRecord(bytes);

    ASSERT_TRUE(decoded.hasValue());
    ASSERT_TRUE(std::holds_alternative<te::DecodedOrderRecord>(*decoded.valueIf()));
    const auto& value = std::get<te::DecodedOrderRecord>(*decoded.valueIf());
    EXPECT_EQ(value.event.order_id, order.order_id);
    EXPECT_EQ(value.event.venue_timestamp_us, order.venue_timestamp_us);
    EXPECT_EQ(value.amountTraded, amountTraded);
}

TEST(EventRecordFormat, MixedDecoderDispatchesTradeRecord) {
    const te::TradeEvent trade{
        .venue_timestamp_us = 200,
        .buy_order_id = te::OrderId{51},
        .sell_order_id = te::OrderId{52},
        .quantity = te::Qty{75},
    };
    std::array<std::byte, te::event_record_layout::size> bytes{};
    ASSERT_TRUE(te::encodeTradeRecord(trade, bytes).hasValue());

    const auto decoded = te::decodeEventRecord(bytes);

    ASSERT_TRUE(decoded.hasValue());
    ASSERT_TRUE(std::holds_alternative<te::TradeEvent>(*decoded.valueIf()));
    const auto& value = std::get<te::TradeEvent>(*decoded.valueIf());
    EXPECT_EQ(value.buy_order_id, trade.buy_order_id);
    EXPECT_EQ(value.sell_order_id, trade.sell_order_id);
    EXPECT_EQ(value.quantity, trade.quantity);
}

TEST(EventRecordFormat, MixedDecoderRejectsUnknownRecordType) {
    std::array<std::byte, te::event_record_layout::size> bytes{};
    bytes[te::event_record_layout::common::type] = std::byte{0x7F};

    const auto decoded = te::decodeEventRecord(bytes);

    ASSERT_FALSE(decoded.hasValue());
    ASSERT_NE(decoded.errorIf(), nullptr);
    EXPECT_EQ(*decoded.errorIf(), te::EventRecordFormatError::unexpected_record_type);
}

TEST(EventRecordFormat, MixedEncoderDispatchesOrderRecord) {
    const te::DecodedOrderRecord order{
        .event = te::OrderEvent{
            .venue_timestamp_us = 100,
            .order_id = te::OrderId{42},
            .price = te::Price{5000},
            .quantity = te::Qty{600},
            .side = te::Side::buy,
            .kind = te::EventKind::modify,
        },
        .amountTraded = te::Qty{25},
    };
    const te::DecodedEventRecord mixed{order};
    std::array<std::byte, te::event_record_layout::size> directBytes{};
    std::array<std::byte, te::event_record_layout::size> mixedBytes{};

    ASSERT_TRUE(te::encodeOrderRecord(order.event, order.amountTraded, directBytes).hasValue());
    const auto encoded = te::encodeEventRecord(mixed, mixedBytes);

    ASSERT_TRUE(encoded.hasValue());
    EXPECT_EQ(*encoded.valueIf(), te::event_record_layout::size);
    EXPECT_EQ(mixedBytes, directBytes);
}

TEST(EventRecordFormat, MixedEncoderDispatchesTradeRecord) {
    const te::TradeEvent trade{
        .venue_timestamp_us = 200,
        .buy_order_id = te::OrderId{51},
        .sell_order_id = te::OrderId{52},
        .quantity = te::Qty{75},
    };
    const te::DecodedEventRecord mixed{trade};
    std::array<std::byte, te::event_record_layout::size> directBytes{};
    std::array<std::byte, te::event_record_layout::size> mixedBytes{};

    ASSERT_TRUE(te::encodeTradeRecord(trade, directBytes).hasValue());
    const auto encoded = te::encodeEventRecord(mixed, mixedBytes);

    ASSERT_TRUE(encoded.hasValue());
    EXPECT_EQ(*encoded.valueIf(), te::event_record_layout::size);
    EXPECT_EQ(mixedBytes, directBytes);
}
