#pragma once

#include <cstdint>

namespace te {

/// Identifies the venue whose rules and market data an InstrumentSpec describes.
enum class VenueId : std::uint8_t {
    unknown,
    bitstamp,
    coinbase,
};

/// Identifies a market within a venue. The pair (VenueId, InstrumentId) identifies a specification.
enum class InstrumentId : std::uint8_t {
    unknown,
    btc_usd,
    btc_gbp,
    btc_eur,
};

/// Venue/instrument metadata required to interpret decimal market-data text.
///
/// The scales describe text conversion only. Tick-size increments, minimum order size, and minimum
/// notional are separate venue rules and must not be inferred from these fields.
struct InstrumentSpec {
    /// Identifies which venue supplies these rules.
    VenueId venue_id{};

    /// Identifies the market on that venue.
    InstrumentId instrument_id{};

    /// Number of fractional decimal digits to preserve when parsing venue price text.
    std::uint8_t price_decimals{};

    /// Number of fractional decimal digits to preserve when parsing venue quantity text.
    std::uint8_t quantity_decimals{};
};



}  // namespace te
