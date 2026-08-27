#pragma once

#include <cstdint>

namespace te {

// Venue and instrument are separate because the same market can have different scales/rules.
enum class VenueId : std::uint8_t {
    unknown,
    bitstamp,
    coinbase,
};

enum class InstrumentId : std::uint8_t {
    unknown,
    btc_usd,
    btc_gbp,
    btc_eur,
};

// Decimal scales are caller-supplied venue metadata, not fields inferred from each message.
// They control text conversion only; minimum size/notional and tick rules are separate.
struct InstrumentSpec {
    VenueId venue_id{};
    InstrumentId instrument_id{};
    std::uint8_t price_decimals{};
    std::uint8_t quantity_decimals{};
};

}  // namespace te
