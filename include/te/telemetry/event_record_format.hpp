#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include "te/core/result.hpp"
#include "te/feed/events.hpp"
#include "te/feed/trade_event.hpp"
#include "te/telemetry/segment_format.hpp"

namespace te {

// The first byte identifies which event-specific layout occupies the record.
enum class EventRecordType : std::uint8_t {
    order = 1,
    trade = 2,
};

enum class EventRecordFormatError {
    buffer_too_small,
    unexpected_record_type,
    invalid_side,
    invalid_event_kind,
    nonzero_reserved_bytes,
};

struct DecodedOrderRecord {
    OrderEvent event;
    Qty amountTraded;
};

using DecodedEventRecord = std::variant<DecodedOrderRecord, TradeEvent>;

namespace event_record_layout {

inline constexpr std::size_t size = kEventRecordSize;

// Fields shared by every event record.
namespace common {

inline constexpr std::size_t type = 0;
inline constexpr std::size_t reserved = 1;
inline constexpr std::size_t reservedSize = 7;
inline constexpr std::size_t timestamp = 8;

}  // namespace common

// Fields used when common::type is EventRecordType::order.
namespace order {

inline constexpr std::size_t orderId = 16;
inline constexpr std::size_t price = 24;
inline constexpr std::size_t quantity = 32;
inline constexpr std::size_t amountTraded = 40;
inline constexpr std::size_t side = 48;
inline constexpr std::size_t eventKind = 49;
inline constexpr std::size_t reserved = 50;
inline constexpr std::size_t reservedSize = 14;

}  // namespace order

// Fields used when common::type is EventRecordType::trade.
namespace trade {

inline constexpr std::size_t buyOrderId = 16;
inline constexpr std::size_t sellOrderId = 24;
inline constexpr std::size_t quantity = 32;
inline constexpr std::size_t reserved = 40;
inline constexpr std::size_t reservedSize = 24;

}  // namespace trade

static_assert(common::timestamp + sizeof(std::uint64_t) <= size);
static_assert(order::reserved + order::reservedSize == size);
static_assert(trade::reserved + trade::reservedSize == size);

}  // namespace event_record_layout

Result<std::size_t, EventRecordFormatError> encodeOrderRecord(const OrderEvent& event, Qty amountTraded, std::span<std::byte> output);

Result<std::size_t, EventRecordFormatError> encodeTradeRecord(const TradeEvent& trade, std::span<std::byte> output);

Result<std::size_t, EventRecordFormatError> encodeEventRecord(
    const DecodedEventRecord& record, std::span<std::byte> output);

Result<DecodedOrderRecord, EventRecordFormatError> decodeOrderRecord(
    std::span<const std::byte> input);

Result<TradeEvent, EventRecordFormatError> decodeTradeRecord(
    std::span<const std::byte> input);

Result<DecodedEventRecord, EventRecordFormatError> decodeEventRecord(
    std::span<const std::byte> input);

}  // namespace te
