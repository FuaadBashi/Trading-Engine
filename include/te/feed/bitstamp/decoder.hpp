#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/feed/events.hpp>

namespace te::bitstamp {

enum class DecoderError {
    malformed_json,

    // Expected for protocol/control messages; callers skip rather than count a decode failure.
    not_order_event,
    missing_field,
    invalid_field,
};

// Decodes one live_orders payload. Numeric strings are parsed directly to exact integers;
// order_type is the exception because Bitstamp supplies only the JSON integer (0 buy, 1 sell).
Result<OrderEvent, DecoderError> decodeOrder(std::string_view text, InstrumentSpec spec);

// Decodes one live_orders payload, line by line and extract the amount_trade and return it, if theres no 
// amount_trade return error.
Result<Qty, DecoderError> decodeFill(std::string_view text, InstrumentSpec spec);

constexpr std::size_t kChainIdLength = 36;

// Transport continuity metadata stays outside OrderEvent. Arrays own their bytes after the
// simdjson buffer dies; current.pre_event_id must equal previous.event_id (ADR 0006).
struct ChainLink {
    std::array<char, kChainIdLength> event_id{};
    std::array<char, kChainIdLength> pre_event_id{};
};

// Only order lifecycle messages carry chain IDs. Separate parsing keeps transport metadata out
// of the normalized event; the extra parse is acceptable on the current file-driven path.
Result<ChainLink, DecoderError> decodeChain(std::string_view text);

}  // namespace te::bitstamp
