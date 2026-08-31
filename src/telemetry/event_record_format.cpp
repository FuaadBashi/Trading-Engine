#include "te/telemetry/event_record_format.hpp"

#include <algorithm>
#include <array>

#include "te/feed/trade_event.hpp"
#include "te/util/byte_buffer.hpp"

namespace te {
namespace {

bool isValidSide(Side side) {
    return side == Side::buy || side == Side::sell;
}

bool isValidEventKind(EventKind kind) {
    return kind == EventKind::add || kind == EventKind::modify ||
           kind == EventKind::remove;
}

}  // namespace

Result<std::size_t, EventRecordFormatError> encodeOrderRecord(
    const OrderEvent& event, Qty amountTraded, std::span<std::byte> output) {
    if (output.size() < event_record_layout::size) {
        return Result<std::size_t, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }
    if (!isValidSide(event.side)) {
        return Result<std::size_t, EventRecordFormatError>::failure(
            EventRecordFormatError::invalid_side);
    }
    if (!isValidEventKind(event.kind)) {
        return Result<std::size_t, EventRecordFormatError>::failure(
            EventRecordFormatError::invalid_event_kind);
    }

    // Zero initialization makes every reserved byte deterministic. Encoding into a local array
    // also keeps the caller's buffer unchanged if validation or a write fails.
    std::array<std::byte, event_record_layout::size> encoded{};
    const bool wroteFields =
        te::writeU8(encoded, event_record_layout::common::type,
                static_cast<std::uint8_t>(EventRecordType::order)) &&
        te::writeU64LE(encoded, event_record_layout::common::timestamp,
                   event.venue_timestamp_us) &&
        te::writeU64LE(encoded, event_record_layout::order::orderId, event.order_id.value) &&
        te::writeI64LE(encoded, event_record_layout::order::price, event.price.ticks) &&
        te::writeI64LE(encoded, event_record_layout::order::quantity, event.quantity.units) &&
        te::writeI64LE(encoded, event_record_layout::order::amountTraded,
                   amountTraded.units) &&
        te::writeU8(encoded, event_record_layout::order::side,
                static_cast<std::uint8_t>(event.side)) &&
        te::writeU8(encoded, event_record_layout::order::eventKind,
                static_cast<std::uint8_t>(event.kind));

    if (!wroteFields) {
        return Result<std::size_t, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }

    std::copy(encoded.begin(), encoded.end(), output.begin());
    return Result<std::size_t, EventRecordFormatError>::success(
        event_record_layout::size);
}

Result<std::size_t, EventRecordFormatError> encodeTradeRecord(
    const te::TradeEvent& trade, std::span<std::byte> output)
{  
    if (output.size() < event_record_layout::size) {
        return Result<std::size_t, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }

    std::array<std::byte, event_record_layout::size> encoded{};
    const bool wroteFields = 
    te::writeU8(encoded, event_record_layout::common::type,
                static_cast<std::uint8_t>(EventRecordType::trade)) &&
    te::writeU64LE(encoded, event_record_layout::common::timestamp,
                   trade.venue_timestamp_us) && 
    te::writeU64LE(encoded, event_record_layout::trade::buyOrderId, trade.buy_order_id.value) &&
    te::writeU64LE(encoded, event_record_layout::trade::sellOrderId, trade.sell_order_id.value) &&
    te::writeI64LE(encoded, event_record_layout::trade::quantity, trade.quantity.units);
 
    if (!wroteFields) {
        return Result<std::size_t, EventRecordFormatError>::failure(
        EventRecordFormatError::buffer_too_small);
    }  

    std::copy(encoded.begin(), encoded.end(), output.begin());
    return Result<std::size_t, EventRecordFormatError>::success(
        event_record_layout::size);
}

} // namespace te
