#include <gtest/gtest.h>

#include <te/feed/bitstamp/decoder.hpp>

// Real captured line: id_str 2037493297635328, order_deleted, order_type 0 (buy),
// price_str "58356.10", amount_str "0.00171371", microtimestamp 1786269861947000.
TEST(BitstampDecoder, DecodesOrderDeletedEvent) {
    const te::InstrumentSpec btc_usd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };

    std::string_view line{
        R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269861947000","amount":0.00171371,"amount_str":"0.00171371","amount_traded":"0","amount_at_create":"0.00171371","price":58356.1,"price_str":"58356.10","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-2678-0000-000101000020","pre_event_id":"0006589a-5c97-f798-0000-000100000020","order_source":"orderbook"})"};

    const auto result = te::bitstamp::decodeOrder(line, btc_usd);

    ASSERT_TRUE(result.hasValue());
    const te::OrderEvent* event = result.valueIf();
    EXPECT_EQ(event->order_id, te::OrderId{2037493297635328ULL});
    EXPECT_EQ(event->kind, te::EventKind::remove);
    EXPECT_EQ(event->side, te::Side::buy);
    EXPECT_EQ(event->price, te::Price{5835610});
    EXPECT_EQ(event->quantity, te::Qty{171371});
    EXPECT_EQ(event->venue_timestamp_us, 1786269861947000ULL);
}

// Same real line as DecodesOrderDeletedEvent above, through decodeCapturedOrder instead: proves
// the merged single-parse path gets every OrderEvent field right, not just amountTraded.
TEST(BitstampDecoder, DecodesCapturedOrderEventFields) {
    const te::InstrumentSpec btc_usd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    std::string_view line{
        R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269861947000","amount":0.00171371,"amount_str":"0.00171371","amount_traded":"0","amount_at_create":"0.00171371","price":58356.1,"price_str":"58356.10","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-2678-0000-000101000020","pre_event_id":"0006589a-5c97-f798-0000-000100000020","order_source":"orderbook"})"};

    const auto result = te::bitstamp::decodeCapturedOrder(line, btc_usd);

    ASSERT_TRUE(result.hasValue());
    const te::OrderEvent& event = result.valueIf()->event;
    EXPECT_EQ(event.order_id, te::OrderId{2037493297635328ULL});
    EXPECT_EQ(event.kind, te::EventKind::remove);
    EXPECT_EQ(event.side, te::Side::buy);
    EXPECT_EQ(event.price, te::Price{5835610});
    EXPECT_EQ(event.quantity, te::Qty{171371});
    EXPECT_EQ(event.venue_timestamp_us, 1786269861947000ULL);
    EXPECT_EQ(result.valueIf()->amountTraded, te::Qty{});
}

// decodeFill used to own amount_traded extraction and was tested with minimal
// {"data":{"amount_traded":...}} fixtures. decodeCapturedOrder decodes the whole line in one
// parse, so these need a complete valid order alongside the varying amount_traded -- same four
// cases (zero, nonzero, missing, invalid), ported rather than dropped.
TEST(BitstampDecoder, DecodesZeroAmountTraded) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    std::string_view line{
        R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,)"
        R"("microtimestamp":"1786269861947000","amount_str":"0.00171371","amount_traded":"0",)"
        R"("price_str":"58356.10"},"event":"order_deleted"})"};

    const auto result = te::bitstamp::decodeCapturedOrder(line, btcUsd);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->amountTraded, te::Qty{});
}

TEST(BitstampDecoder, DecodesNonzeroAmountTradedExactly) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    std::string_view line{
        R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,)"
        R"("microtimestamp":"1786269861947000","amount_str":"0.00171371",)"
        R"("amount_traded":"0.12345678","price_str":"58356.10"},"event":"order_deleted"})"};

    const auto result = te::bitstamp::decodeCapturedOrder(line, btcUsd);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->amountTraded, te::Qty{12345678});
}

TEST(BitstampDecoder, MissingAmountTradedIsAnError) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    std::string_view line{
        R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,)"
        R"("microtimestamp":"1786269861947000","amount_str":"0.00171371",)"
        R"("price_str":"58356.10"},"event":"order_deleted"})"};

    const auto result = te::bitstamp::decodeCapturedOrder(line, btcUsd);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::DecoderError::missing_field);
}

TEST(BitstampDecoder, InvalidAmountTradedIsAnInvalidField) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    std::string_view line{
        R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,)"
        R"("microtimestamp":"1786269861947000","amount_str":"0.00171371",)"
        R"("amount_traded":"not-a-number","price_str":"58356.10"},"event":"order_deleted"})"};

    const auto result = te::bitstamp::decodeCapturedOrder(line, btcUsd);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::DecoderError::invalid_field);
}

// ADR 0005 requires the two ID representations to agree. They have never disagreed across 619,803
// captured order events; this is a tripwire for the day one of them stops being trustworthy --
// most plausibly when IDs cross 2^53 and the JSON number starts losing precision while the string
// does not. Current IDs sit near 2.0e15 against that 9.0e15 ceiling.
TEST(BitstampDecoder, RejectsDisagreeingIdAndIdStr) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };

    std::string_view line{
        R"({"data":{"id":2037493297635328,"id_str":"2037493297635329","order_type":0,)"
        R"("microtimestamp":"1786269861947000","amount_str":"0.00171371",)"
        R"("price_str":"58356.10"},"event":"order_deleted"})"};

    const auto result = te::bitstamp::decodeOrder(line, btcUsd);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::DecoderError::id_mismatch);
}

// The numeric form is not optional: a line carrying only id_str cannot be cross-checked, and
// silently trusting it would defeat the tripwire above.
TEST(BitstampDecoder, RejectsOrderMissingNumericId) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };

    std::string_view line{
        R"({"data":{"id_str":"2037493297635328","order_type":0,)"
        R"("microtimestamp":"1786269861947000","amount_str":"0.00171371",)"
        R"("price_str":"58356.10"},"event":"order_deleted"})"};

    const auto result = te::bitstamp::decodeOrder(line, btcUsd);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::DecoderError::missing_field);
}
