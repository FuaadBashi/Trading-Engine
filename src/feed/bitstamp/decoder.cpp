#include <algorithm>
#include <te/feed/bitstamp/decoder.hpp>

#include "simdjson/ondemand.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/padded_string_view.h"
#include "te/core/text_to_int.hpp"

namespace te::bitstamp {

Result<OrderEvent, DecoderError> decodeOrder(std::string_view text, InstrumentSpec spec) {
    OrderEvent orderEvent;
    simdjson::ondemand::parser parser;
    simdjson::padded_string buffer = simdjson::padded_string(text);

    simdjson::ondemand::document doc;
    simdjson::error_code err = parser.iterate(buffer).get(doc);
    if (err) {
        return Result<OrderEvent, DecoderError>::failure(DecoderError::malformed_json);
        ;
    }

    std::string_view event;
    err = doc["event"].get_string().get(event);
    if (err) {
        return Result<OrderEvent, DecoderError>::failure(DecoderError::invalid_field);
    }

    if (event == "order_created") {
        orderEvent.kind = EventKind::add;

    } else if (event == "order_changed") {
        orderEvent.kind = EventKind::modify;

    } else if (event == "order_deleted") {
        orderEvent.kind = EventKind::remove;

    } else {
        return Result<OrderEvent, DecoderError>::failure(DecoderError::not_order_event);
    }

    std::string_view id_str;
    err = doc["data"]["id_str"].get_string().get(id_str);
    if (err) {
        return Result<OrderEvent, DecoderError>::failure(DecoderError::missing_field);
    } else {
        // Keep Result alive while using valueIf(); calling it on a temporary would dangle.
        // ADR 0005 also requires a future cross-check against the numeric "id" field.
        const Result<uint64_t, ParseError> id_result = parseInteger(id_str);
        const uint64_t* decoded_id = id_result.valueIf();
        if (decoded_id == nullptr) {
            return Result<OrderEvent, DecoderError>::failure(DecoderError::invalid_field);
        } else {
            orderEvent.order_id = OrderId{*decoded_id};
        }
    }
    std::string_view str_time;
    err = doc["data"]["microtimestamp"].get_string().get(str_time);
    if (err) {
        return Result<OrderEvent, DecoderError>::failure(DecoderError::missing_field);
    } else {
        Result<uint64_t, ParseError> result = parseInteger(str_time);
        const uint64_t* decoded_time = result.valueIf();
        if (decoded_time == nullptr) {
            return Result<OrderEvent, DecoderError>::failure(DecoderError::invalid_field);
        } else {
            orderEvent.venue_timestamp_us = *decoded_time;
        }
    }

    std::uint64_t side_code;
    err = doc["data"]["order_type"].get_uint64().get(side_code);
    if (err) {
        return Result<OrderEvent, DecoderError>::failure(DecoderError::missing_field);
    } else {
        if (side_code == 0) {
            orderEvent.side = Side::buy;

        } else if (side_code == 1) {
            orderEvent.side = Side::sell;
        } else {
            return Result<OrderEvent, DecoderError>::failure(DecoderError::invalid_field);
        }
    }

    std::string_view str_price;
    err = doc["data"]["price_str"].get_string().get(str_price);
    if (err) {
        return Result<OrderEvent, DecoderError>::failure(DecoderError::missing_field);
    } else {
        Result<int64_t, ParseError> result = parseDecimal(str_price, spec.price_decimals);
        const int64_t* decoded_price = result.valueIf();

        if (decoded_price != nullptr) {
            orderEvent.price = Price{*decoded_price};
        } else {
            return Result<OrderEvent, DecoderError>::failure(DecoderError::invalid_field);
        }
    }

    std::string_view str_qty;
    err = doc["data"]["amount_str"].get_string().get(str_qty);
    if (err) {
        return Result<OrderEvent, DecoderError>::failure(DecoderError::missing_field);
    } else {
        Result<int64_t, ParseError> result = parseDecimal(str_qty, spec.quantity_decimals);
        const int64_t* decoded_qty = result.valueIf();
        if (decoded_qty != nullptr) {
            orderEvent.quantity = Qty{*decoded_qty};
        } else {
            return Result<OrderEvent, DecoderError>::failure(DecoderError::invalid_field);
        }
    }

    return Result<OrderEvent, DecoderError>::success(orderEvent);
}

Result<Qty, DecoderError> decodeFill(std::string_view text, InstrumentSpec spec){   
    
    simdjson::ondemand::parser parser;
    simdjson::padded_string buffer = simdjson::padded_string(text);

    simdjson::ondemand::document doc;
    simdjson::error_code err = parser.iterate(buffer).get(doc);
    if (err) {
        return Result<Qty, DecoderError>::failure(DecoderError::malformed_json);
        ;
    }

    std::string_view amount_traded_str;
    err = doc["data"]["amount_traded"].get_string().get(amount_traded_str);
    if (err) {
        return Result<Qty, DecoderError>::failure(DecoderError::missing_field);
    }

    Result<std::int64_t, ParseError> amount_traded = parseDecimal(amount_traded_str, spec.quantity_decimals);
    if (!amount_traded.hasValue()) {
        return Result<Qty, DecoderError>::failure(DecoderError::invalid_field);
    }
    return Result<Qty, DecoderError>::success(Qty{ *amount_traded.valueIf() });

};


Result<ChainLink, DecoderError> decodeChain(std::string_view text) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string buffer = simdjson::padded_string(text);

    simdjson::ondemand::document doc;
    if (parser.iterate(buffer).get(doc)) {
        return Result<ChainLink, DecoderError>::failure(DecoderError::malformed_json);
    }

    std::string_view eventId;
    if (doc["event_id"].get_string().get(eventId)) {
        return Result<ChainLink, DecoderError>::failure(DecoderError::missing_field);
    }

    std::string_view preEventId;
    if (doc["pre_event_id"].get_string().get(preEventId)) {
        return Result<ChainLink, DecoderError>::failure(DecoderError::missing_field);
    }

    // Fixed width is part of the contract; anything else is a protocol change, not a short id.
    if (eventId.size() != kChainIdLength || preEventId.size() != kChainIdLength) {
        return Result<ChainLink, DecoderError>::failure(DecoderError::invalid_field);
    }

    ChainLink link;
    // Copy out: these views point into the parse buffer above, which dies with this function.
    std::copy(eventId.begin(), eventId.end(), link.event_id.begin());
    std::copy(preEventId.begin(), preEventId.end(), link.pre_event_id.begin());

    return Result<ChainLink, DecoderError>::success(link);
}

}  // namespace te::bitstamp
