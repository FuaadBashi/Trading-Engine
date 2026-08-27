#include <te/feed/bitstamp/trade_decoder.hpp>

#include "simdjson/ondemand.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/padded_string_view.h"
#include "te/core/text_to_int.hpp"

namespace te::bitstamp {

Result<TradeEvent, DecoderError> decodeTrade(std::string_view text, InstrumentSpec spec) {
    // Trade price is irrelevant to ID/quantity reconciliation, so it is intentionally omitted.
    TradeEvent tradeEvent;
    simdjson::ondemand::parser parser;
    simdjson::padded_string buffer = simdjson::padded_string(text);

    simdjson::ondemand::document doc;
    if (parser.iterate(buffer).get(doc)) {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::malformed_json);
    }

    std::string_view event;
    if (doc["event"].get_string().get(event)) {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::invalid_field);
    }
    if (event != "trade") {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::not_order_event);
    }

    std::string_view str_time;
    if (doc["data"]["microtimestamp"].get_string().get(str_time)) {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::missing_field);
    }
    const Result<uint64_t, ParseError> time_result = parseInteger(str_time);
    const uint64_t* decoded_time = time_result.valueIf();
    if (decoded_time == nullptr) {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::invalid_field);
    }
    tradeEvent.venue_timestamp_us = *decoded_time;

    // buy_order_id/sell_order_id are bare JSON integers here, unlike order events' quoted
    // id_str -- not every id field in this feed is string-encoded.
    std::uint64_t buy_id;
    if (doc["data"]["buy_order_id"].get_uint64().get(buy_id)) {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::missing_field);
    }
    tradeEvent.buy_order_id = OrderId{buy_id};

    std::uint64_t sell_id;
    if (doc["data"]["sell_order_id"].get_uint64().get(sell_id)) {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::missing_field);
    }
    tradeEvent.sell_order_id = OrderId{sell_id};

    std::string_view str_qty;
    if (doc["data"]["amount_str"].get_string().get(str_qty)) {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::missing_field);
    }
    const Result<int64_t, ParseError> qty_result = parseDecimal(str_qty, spec.quantity_decimals);
    const int64_t* decoded_qty = qty_result.valueIf();
    if (decoded_qty == nullptr) {
        return Result<TradeEvent, DecoderError>::failure(DecoderError::invalid_field);
    }
    tradeEvent.quantity = Qty{*decoded_qty};

    return Result<TradeEvent, DecoderError>::success(tradeEvent);
}

}  // namespace te::bitstamp
