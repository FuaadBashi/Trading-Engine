#pragma once

#include <cstdint>

namespace te {

/**
 * @brief  Identifies the venue whose rules and market data an InstrumentSpec describes.
 */
enum class VenueId : std::uint8_t {
    /** @brief Unset or unrecognised venue. */
    unknown,

    /** @brief Bitstamp; the primary L3 venue, per ADR 0010. */
    bitstamp,

    /** @brief Coinbase; retained as a secondary L2 source. */
    coinbase,
};

/**
 * @brief  Identifies a market within a venue.
 *
 * @note   The pair (VenueId, InstrumentId) is what identifies a specification; neither
 *         half is meaningful alone, since different venues quote the same pair differently.
 */
enum class InstrumentId : std::uint8_t {
    /** @brief Unset or unrecognised market. */
    unknown,

    /** @brief Bitcoin quoted in US dollars. */
    btc_usd,

    /** @brief Bitcoin quoted in pounds sterling. */
    btc_gbp,

    /** @brief Bitcoin quoted in euros. */
    btc_eur,
};

/**
 * @brief  Venue/instrument metadata required to interpret decimal market-data text.
 *
 * @note   Supplied by the caller rather than read from any message: a venue does not transmit
 *         its own decimal scales, so they are external knowledge about the venue/instrument
 *         pair, established by inspecting real captured data.
 *
 * @note   The scales describe text conversion only. Tick-size increments, minimum order size
 *         and minimum notional are separate venue rules and must not be inferred from these
 *         fields.
 */
struct InstrumentSpec {
    /** @brief Identifies which venue supplies these rules. */
    VenueId venue_id{};

    /** @brief Identifies the market on that venue. */
    InstrumentId instrument_id{};

    /** @brief Fractional decimal digits to preserve when parsing venue price text. */
    std::uint8_t price_decimals{};

    /** @brief Fractional decimal digits to preserve when parsing venue quantity text. */
    std::uint8_t quantity_decimals{};
};

}  // namespace te
