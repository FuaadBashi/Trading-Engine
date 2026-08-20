#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/feed/events.hpp>

namespace te::bitstamp {

/**
 * @brief  Reason a raw Bitstamp live_orders line could not be decoded into an OrderEvent.
 */
enum class DecoderError {
    /** @brief The text was not syntactically valid JSON. */
    malformed_json,

    /**
     * @brief  Valid JSON, but not an order lifecycle message.
     *
     * @note   The "event" field was none of order_created, order_changed or order_deleted.
     *         The live_orders channel also carries non-order messages such as
     *         bts:subscription_succeeded; those are not faults, just not orders. Callers
     *         should treat this as "skip this line", unlike the other three reasons.
     */
    not_order_event,

    /**
     * @brief  A field needed to build an OrderEvent was absent.
     *
     * @note   One of id_str, price_str, amount_str, microtimestamp or order_type was missing
     *         from a line that was otherwise a recognised order event.
     */
    missing_field,

    /**
     * @brief  A field was present but its text failed exact parsing.
     *
     * @note   The specific te::ParseError from parseDecimal/parseInteger is deliberately not
     *         carried here; it is available at the parse call site for logging. Add it only
     *         if a caller genuinely needs to distinguish overflow from excess precision.
     */
    invalid_field,
};

/**
 * @brief  Decodes one raw JSON line from Bitstamp's live_orders_btcusd channel.
 *
 * @param  text One complete JSON message, as received. Not a whole file: exactly one line.
 * @param  spec Supplies the price/quantity decimal scales used to interpret price_str and
 *              amount_str exactly. Not read from @p text, since scale is a property of the
 *              venue/instrument pair rather than of any single message.
 *
 * @return The normalized event, or the reason it could not be decoded.
 *
 * @note   Reads the string forms of every numeric field (price_str, amount_str, id_str)
 *         rather than the JSON numbers Bitstamp sends alongside them. The bare numbers have
 *         already passed through a double; reading them would put binary rounding error on
 *         the price path, which is the failure ADR 0004 exists to prevent.
 *
 * @note   order_type is read as a JSON integer, not a string, because Bitstamp sends no
 *         string form of it. 0 maps to Side::buy and 1 to Side::sell.
 */
Result<OrderEvent, DecoderError> decodeEvent(std::string_view text, InstrumentSpec spec);

/** @brief Length of a Bitstamp event_id, which is UUID-shaped and always 36 characters. */
constexpr std::size_t kChainIdLength = 36;

/**
 * @brief  A message's position in Bitstamp's event chain.
 *
 * @note   Bitstamp does not number messages. Each one names its predecessor, so continuity is
 *         checked by matching this message's pre_event_id against the previous message's
 *         event_id (ADR 0006). A mismatch proves loss but gives no count of what was lost.
 *
 * @note   Fixed-size arrays rather than strings: these are copied out of simdjson's parse
 *         buffer, which dies with the call, so a string_view would dangle. Fixed size also
 *         keeps the decoder allocation-free per message.
 *
 * @note   Transport metadata, deliberately not part of OrderEvent. The book never reads it, and
 *         carrying 32 bytes of it through every event and every record would nearly double both.
 */
struct ChainLink {
    /** @brief This message's own identifier. */
    std::array<char, kChainIdLength> event_id{};

    /** @brief The identifier of the message that should have preceded this one. */
    std::array<char, kChainIdLength> pre_event_id{};
};

/**
 * @brief  Extracts the event-chain identifiers from one raw Bitstamp line.
 *
 * @param  text One complete JSON message, as received.
 *
 * @return The chain link, or the reason it could not be read.
 *
 * @note   Only order lifecycle messages carry these fields; protocol messages such as
 *         bts:subscription_succeeded have none and yield DecoderError::missing_field. Callers
 *         should therefore only check continuity across order events.
 *
 * @note   Separate from decodeEvent so the event decoder's contract stays unchanged. The cost
 *         is a second parse of the same line, which is acceptable while capture is file-driven
 *         and correctness matters more than throughput.
 */
Result<ChainLink, DecoderError> decodeChain(std::string_view text);

}  // namespace te::bitstamp

// Example JSON lines:
//{"event":"bts:subscription_succeeded","channel":"live_orders_btcusd","data":{}}
//{"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269861947000",
//"amount":0.00171371,"amount_str":"0.00171371","amount_traded":"0","amount_at_create":"0.00171371","price":58356.1,"price_str":"58356.10","is_liquidation":false},
//"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-2678-0000-000101000020","pre_event_id":"0006589a-5c97-f798-0000-000100000020","order_source":"orderbook"}
