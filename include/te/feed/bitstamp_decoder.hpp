#pragma once

#include <string_view>
#include <te/core/result.hpp>
#include <te/core/instrument.hpp>
#include <te/feed/events.hpp>

namespace te {
    
/// Reason a raw Bitstamp live_orders line could not be decoded into an OrderEvent.
enum class DecoderError {
    /// text was not syntactically valid JSON.
    malformed_json,

    /// The JSON was valid but its "event" field was not one of the known order lifecycle
    not_order_event,

    /// A field required to build an OrderEvent (id_str, price_str, amount_str, microtimestamp, order_type, ...) was absent.
    missing_field,

    /// A present field's text failed exact parsing. The specific te::ParseError from parseDecimal/parseInteger is available to the caller at the parse call site, not here.
    invalid_field,
};

/// Decodes one raw JSON line from Bitstamp's live_orders_btcusd channel into a normalized
/// OrderEvent.
Result<OrderEvent, DecoderError> decodeBitstampEvent(std::string_view text, InstrumentSpec spec);

}  // namespace te


// Example JSON line:
//{"event":"bts:subscription_succeeded","channel":"live_orders_btcusd","data":{}}
//{"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269861947000",
//"amount":0.00171371,"amount_str":"0.00171371","amount_traded":"0","amount_at_create":"0.00171371","price":58356.1,"price_str":"58356.10","is_liquidation":false},
//"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-2678-0000-000101000020","pre_event_id":"0006589a-5c97-f798-0000-000100000020","order_source":"orderbook"}
