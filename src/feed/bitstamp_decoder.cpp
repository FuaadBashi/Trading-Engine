#include <te/feed/bitstamp_decoder.hpp>
#include "te/core/text_to_int.hpp"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string_view.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/ondemand.h"

namespace te {

te::Result<OrderEvent, DecoderError> decodeBitstampEvent(std::string_view text,InstrumentSpec spec) {

    OrderEvent orderEvent; 
    simdjson::ondemand::parser parser;
    simdjson::padded_string buffer = simdjson::padded_string(text);

    simdjson::ondemand::document doc;
    simdjson::error_code err = parser.iterate(buffer).get(doc);
    if (err) {
        // couldn't even parse this as JSON at all
        return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::malformed_json);;
    }
// EVENT
    std::string_view event;
    err = doc["event"].get_string().get(event);
    if (err) {
        // no "event" field at all
        return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::missing_field);
    }

    if (event == "order_created") {
        orderEvent.kind = EventKind::add;
     
    } else if (event == "order_changed") {
        orderEvent.kind = EventKind::modify;
    
    }else if (event == "order_deleted") {
        orderEvent.kind = EventKind::remove;
    
    } else {
        // valid JSON, just not the kind of message we care about
        return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::not_order_event);
    }

// ID
    std::string_view id_str;
    err = doc["data"]["id_str"].get_string().get(id_str);
    if (err) {
        // no "id_str" field at all
        return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::missing_field);
    } else {
        Result<uint64_t, ParseError> result = parseInteger(id_str);
        const uint64_t* decoded_id = result.valueIf();
        if (decoded_id == nullptr){
            return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::invalid_field);
        } else {
            orderEvent.order_id = OrderId{*decoded_id};
        }
    }
    // TIME
    std::string_view str_time;
    err = doc["data"]["microtimestamp"].get_string().get(str_time);
    if (err) {
        // no "time" field at all
        return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::missing_field);
    } else { 
        Result<uint64_t, ParseError> result = parseInteger(str_time);
        const uint64_t* decoded_time = result.valueIf();
        if (decoded_time == nullptr){
            return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::invalid_field);
        } else {
            orderEvent.venue_timestamp_us = *decoded_time;
        } 
    }

// SIDE
    u_int64_t side_code;
    err = doc["data"]["order_type"].get_uint64().get(side_code);
    if (err) {
        // no side field at all
        return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::missing_field);
    } else { 
        if (side_code == 0) {
            orderEvent.side = Side::buy;
     
        } else if (side_code == 1) {
            orderEvent.side = Side::sell;
        } else {
            // valid JSON, just not the kind of message we care about
            return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::invalid_field);
        }
    }


// PRICE
std::string_view str_price;
err = doc["data"]["price_str"].get_string().get(str_price);
  if (err) {
        // no price field at all
        return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::missing_field);
    } else { 

        Result<int64_t, ParseError> result = parseDecimal(str_price, spec.price_decimals);
        const int64_t* decoded_price = result.valueIf();

        if(decoded_price != nullptr){
            orderEvent.price = Price{*decoded_price};
        } else {
            return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::invalid_field);
        }
    }

// QTY

std::string_view str_qty;
err = doc["data"]["amount_str"].get_string().get(str_qty);
  if (err) {
        // no price field at all
        return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::missing_field);
    } else { 

        Result<int64_t, ParseError> result = parseDecimal(str_qty, spec.quantity_decimals);
        const int64_t* decoded_qty= result.valueIf();
        if(decoded_qty != nullptr){
            orderEvent.quantity = Qty{*decoded_qty};
        } else {
            return te::Result<OrderEvent, DecoderError>::failure(te::DecoderError::invalid_field);
        }
    }

    return te::Result<OrderEvent, DecoderError>::success(orderEvent);

}




}  // namespace te
