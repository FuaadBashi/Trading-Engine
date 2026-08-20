#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/core/types.hpp>

namespace te::bitstamp {

/**
 * @brief  One resting order read from a REST group=2 snapshot row.
 *
 * @note   No EventKind: this is starting state, not a live add/modify/remove message. It has no
 *         observed arrival order relative to other orders already in the snapshot.
 */
struct SnapshotOrder {
    OrderId order_id{};
    Price price{};
    Qty quantity{};
    Side side{};
};

/**
 * @brief  A parsed group=2 snapshot: every resting order at one point in time.
 *
 * @note   Row order within bids/asks is preserved (deterministic replay), but Bitstamp's public
 *         API does not document that same-price rows are returned in matching-engine FIFO order.
 *         Treat same-price ordering among snapshot orders as unproven; only orders observed
 *         joining live, after this snapshot, have known arrival order. See ADR 0007/0011.
 */
struct BookSnapshot {
    std::uint64_t microtimestamp{};
    std::vector<SnapshotOrder> orders;
};

/**
 * @brief  Reason a snapshot document could not be parsed into a BookSnapshot.
 */
enum class SnapshotError {
    /**
     * @brief  The document could not be opened as JSON at all.
     *
     * @note   Narrower than it sounds. simdjson On Demand does not validate a whole document up
     *         front, so in practice only empty input fails this early; other malformed text is
     *         rejected later, at the first field it cannot supply (usually
     *         missing_microtimestamp). Every malformed input is still rejected -- only the
     *         reported reason is less specific than the name suggests.
     */
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

/**
 * @brief  Parses one Bitstamp group=2 snapshot document into a BookSnapshot.
 *
 * @param  text One complete JSON snapshot document, as fetched from
 *              `GET /api/v2/order_book/{market_symbol}/?group=2`.
 * @param  spec Supplies price/quantity decimal scales -- same convention as decodeEvent, not
 *              read from the document itself.
 *
 * @return The parsed snapshot, or the reason parsing failed.
 *
 * @note   A duplicate order_id across bids/asks is rejected outright rather than resolved by the
 *         parser -- the caller must not have to guess which copy is real.
 */
Result<BookSnapshot, SnapshotError> parseSnapshot(std::string_view text, InstrumentSpec spec);

}  // namespace te::bitstamp
