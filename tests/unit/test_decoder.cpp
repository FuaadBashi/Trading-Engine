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
