#pragma once

#include <string_view>
#include <te/core/result.hpp>
#include <te/core/instrument.hpp>
#include <te/feed/events.hpp>
// Slice 1. JSON to MarketEvent. simdjson On Demand, zero allocation per message.
// TODO(fuaad): write this yourself. Declarations first, then the test, then the body.

namespace te {
    
/// Reason a raw Bitstamp live_orders line could not be decoded into an OrderEvent.
enum class DecoderError {
    /// text was not syntactically valid JSON.
    malformed_json,

    /// The JSON was valid but its "event" field was not one of the known order lifecycle
    /// values (order_created, order_changed, order_deleted). Bitstamp's live_orders channel
    /// also sends non-order messages such as bts:subscription_succeeded; those are not
    /// decode failures, just not orders, and land here.
    not_order_event,

    /// A field required to build an OrderEvent (id_str, price_str, amount_str,
    /// microtimestamp, order_type, ...) was absent from an otherwise recognized order event.
    missing_field,

    /// A present field's text failed exact parsing. The specific te::ParseError from
    /// parseDecimal/parseInteger is available to the caller at the parse call site, not here.
    invalid_field,
};

/// Decodes one raw JSON line from Bitstamp's live_orders_btcusd channel into a normalized
/// OrderEvent.
///
/// spec supplies the price/quantity decimal scales used to interpret price_str and amount_str
/// exactly; it is not read from text, since scale is a property of the venue/instrument pair,
/// not of any single message.
///
/// Not every line on this channel describes an order. Callers treats
/// DecoderError::not_order_event as "skip this line," not as a fault; the other DecoderError
/// reasons mean the line was supposed to be an order event but could not be decoded as one.
Result<OrderEvent, DecoderError> decodeBitstampEvent(std::string_view text, InstrumentSpec spec);

}  // namespace te


// Example JSON line:
//{"event":"bts:subscription_succeeded","channel":"live_orders_btcusd","data":{}}
//{"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269861947000",
//"amount":0.00171371,"amount_str":"0.00171371","amount_traded":"0","amount_at_create":"0.00171371","price":58356.1,"price_str":"58356.10","is_liquidation":false},
//"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-2678-0000-000101000020","pre_event_id":"0006589a-5c97-f798-0000-000100000020","order_source":"orderbook"}
