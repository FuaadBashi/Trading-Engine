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

    // ADR 0005: "id" and "id_str" carry the same value twice and must agree. They disagree only
    // if the venue changed format or an ID crossed 2^53, where the JSON number loses precision
    // and the string does not. Rejecting is right: the two representations no longer name one
    // order, and guessing which is real would corrupt every later lookup on that ID.
    id_mismatch,
};

// Decodes one live_orders payload. Numeric strings are parsed directly to exact integers;
// order_type is the exception because Bitstamp supplies only the JSON integer (0 buy, 1 sell).
Result<OrderEvent, DecoderError> decodeOrder(std::string_view text, InstrumentSpec spec);

// Everything a captured order-lifecycle line carries: the normalized event plus the venue's own
// per-event fill quantity (ADR 0013). One parse produces both instead of the two independent
// simdjson walks decodeOrder+decodeFill used to require.
// Not in scope: order_subtype (ADR 0011) stays exactly as unresolved as it is elsewhere.
struct DecodedCapturedOrder {
    OrderEvent event;
    Qty amountTraded;
};

Result<DecodedCapturedOrder, DecoderError> decodeCapturedOrder(std::string_view text,
                                                                InstrumentSpec spec);

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
