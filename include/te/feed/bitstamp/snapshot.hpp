#pragma once

#include <cstdint>
#include <string_view>
#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/core/types.hpp>
#include <vector>

namespace te::bitstamp {

// Starting state, not a live event; therefore it has no EventKind.
struct SnapshotOrder {
    OrderId order_id{};
    Price price{};
    Qty quantity{};
    Side side{};
};

// Complete group=2 L3 state. Row order is preserved for determinism, but snapshot FIFO priority
// is unproven; only later live arrivals have observed ordering (ADR 0007).
struct BookSnapshot {
    std::uint64_t microtimestamp{};
    std::vector<SnapshotOrder> orders;
};

enum class SnapshotError {
    // On Demand validates lazily; later malformed text may surface as a missing/invalid field.
    malformed_json,
    missing_microtimestamp,
    missing_bids,
    missing_asks,
    wrong_field_count,
    invalid_price,
    invalid_quantity,
    invalid_order_id,
    duplicate_order_id,
};

// Parses one complete group=2 document. Duplicate IDs are rejected across both sides rather
// than guessed away, because the snapshot is the replay's trusted starting state.
Result<BookSnapshot, SnapshotError> parseSnapshot(std::string_view text, InstrumentSpec spec);

}  // namespace te::bitstamp
