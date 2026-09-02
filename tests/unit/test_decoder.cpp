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

// The smallest failing test for the decodeOrder/decodeFill merge (docs/decisions -- scoped
// mechanical-only, see decoder.hpp). Dual-run against the real fixture above: decodeCapturedOrder
// must match what the two existing calls produce today, not a hand-picked expected value, so this
// test still holds if that fixture ever changes. Currently RED -- decodeCapturedOrder is a
// placeholder. Delete this test once the merge lands and decodeOrder/decodeFill are gone; it is a
// temporary safety net, not permanent coverage.
TEST(BitstampDecoder, CapturedOrderMatchesSeparateDecodesOnARealLine) {
    const te::InstrumentSpec btc_usd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    std::string_view line{
        R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269861947000","amount":0.00171371,"amount_str":"0.00171371","amount_traded":"0","amount_at_create":"0.00171371","price":58356.1,"price_str":"58356.10","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-2678-0000-000101000020","pre_event_id":"0006589a-5c97-f798-0000-000100000020","order_source":"orderbook"})"};

    const auto expectedOrder = te::bitstamp::decodeOrder(line, btc_usd);
    const auto expectedFill = te::bitstamp::decodeFill(line, btc_usd);
    ASSERT_TRUE(expectedOrder.hasValue());
    ASSERT_TRUE(expectedFill.hasValue());

    const auto actual = te::bitstamp::decodeCapturedOrder(line, btc_usd);

    ASSERT_TRUE(actual.hasValue()) << "decodeCapturedOrder is not implemented yet -- this is the "
                                       "expected red before step 4 of plan v4 §23";
    EXPECT_EQ(actual.valueIf()->event.order_id, expectedOrder.valueIf()->order_id);
    EXPECT_EQ(actual.valueIf()->event.kind, expectedOrder.valueIf()->kind);
    EXPECT_EQ(actual.valueIf()->event.side, expectedOrder.valueIf()->side);
    EXPECT_EQ(actual.valueIf()->event.price, expectedOrder.valueIf()->price);
    EXPECT_EQ(actual.valueIf()->event.quantity, expectedOrder.valueIf()->quantity);
    EXPECT_EQ(actual.valueIf()->event.venue_timestamp_us, expectedOrder.valueIf()->venue_timestamp_us);
    EXPECT_EQ(actual.valueIf()->amountTraded, *expectedFill.valueIf());
}

TEST(BitstampDecoder, DecodesZeroAmountTraded) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    constexpr std::string_view line = R"({"data":{"amount_traded":"0"}})";

    const auto result = te::bitstamp::decodeFill(line, btcUsd);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), te::Qty{});
}

TEST(BitstampDecoder, DecodesNonzeroAmountTradedExactly) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    constexpr std::string_view line = R"({"data":{"amount_traded":"0.12345678"}})";

    const auto result = te::bitstamp::decodeFill(line, btcUsd);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(*result.valueIf(), te::Qty{12345678});
}

TEST(BitstampDecoder, MissingAmountTradedIsAnError) {
    const te::InstrumentSpec btcUsd{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };

    const auto result = te::bitstamp::decodeFill(R"({"data":{}})", btcUsd);

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

    const auto result =
        te::bitstamp::decodeFill(R"({"data":{"amount_traded":"not-a-number"}})", btcUsd);

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
