#include <gtest/gtest.h>

#include <te/feed/bitstamp/trade_decoder.hpp>

namespace {

te::InstrumentSpec btcUsd() {
    return te::InstrumentSpec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
}

}  // namespace

// Real captured line: id 619501668, amount_str "0.34778488",
// microtimestamp 1787174919977000, buy_order_id 2041200416022642,
// sell_order_id 2041200408317957.
TEST(BitstampTradeDecoder, DecodesRealTradeLine) {
    std::string_view line{
        R"({"data":{"id":619501668,"timestamp":"1787174919","amount":0.34778488,"amount_str":"0.34778488","price":69590.13,"price_str":"69590.13","type":0,"microtimestamp":"1787174919977000","buy_order_id":2041200416022642,"sell_order_id":2041200408317957},"channel":"live_trades_btcusd","event":"trade"})"};

    const auto result = te::bitstamp::decodeTrade(line, btcUsd());

    ASSERT_TRUE(result.hasValue());
    const te::TradeEvent* trade = result.valueIf();
    EXPECT_EQ(trade->venue_timestamp_us, 1787174919977000ULL);
    EXPECT_EQ(trade->buy_order_id, te::OrderId{2041200416022642ULL});
    EXPECT_EQ(trade->sell_order_id, te::OrderId{2041200408317957ULL});
    EXPECT_EQ(trade->quantity, te::Qty{34778488});
}

// Real captured line: every live_trades session opens with this, and it is not a trade.
TEST(BitstampTradeDecoder, RejectsSubscriptionConfirmationAsNotOrderEvent) {
    std::string_view line{
        R"({"event":"bts:subscription_succeeded","channel":"live_trades_btcusd","data":{}})"};

    const auto result = te::bitstamp::decodeTrade(line, btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::DecoderError::not_order_event);
}

// simdjson's on-demand parser validates lazily -- only empty input fails at iterate() itself.
// Non-empty garbage still gets rejected, just later, as a different DecoderError.
TEST(BitstampTradeDecoder, RejectsEmptyInputAsMalformedJson) {
    const auto result = te::bitstamp::decodeTrade("", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::DecoderError::malformed_json);
}

TEST(BitstampTradeDecoder, RejectsGarbageInputWithoutAcceptingIt) {
    const auto result = te::bitstamp::decodeTrade("not json at all", btcUsd());

    ASSERT_FALSE(result.hasValue());
}

TEST(BitstampTradeDecoder, RejectsTradeMissingBuyOrderId) {
    std::string_view line{
        R"({"data":{"amount_str":"0.5","microtimestamp":"1787174919977000","sell_order_id":2041200408317957},"event":"trade"})"};

    const auto result = te::bitstamp::decodeTrade(line, btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::DecoderError::missing_field);
}

TEST(BitstampTradeDecoder, RejectsTradeWithUnparsableAmount) {
    std::string_view line{
        R"({"data":{"amount_str":"not-a-number","microtimestamp":"1787174919977000","buy_order_id":1,"sell_order_id":2},"event":"trade"})"};

    const auto result = te::bitstamp::decodeTrade(line, btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::DecoderError::invalid_field);
}
