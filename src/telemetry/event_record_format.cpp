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

bool allZero(std::span<const std::byte> input, std::size_t offset, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        if (input[offset + index] != std::byte{0}) {
            return false;
        }
    }
    return true;
}

}  // namespace

Result<std::size_t, EventRecordFormatError> encodeOrderRecord(const OrderEvent& event, Qty amountTraded, std::span<std::byte> output) {
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

Result<std::size_t, EventRecordFormatError> encodeTradeRecord(const te::TradeEvent& trade, std::span<std::byte> output)
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

Result<std::size_t, EventRecordFormatError> encodeEventRecord(
    const DecodedEventRecord& record, std::span<std::byte> output) {
    if (const auto* order = std::get_if<DecodedOrderRecord>(&record)) {
        return encodeOrderRecord(order->event, order->amountTraded, output);
    }

    if (const auto* trade = std::get_if<TradeEvent>(&record)) {
        return encodeTradeRecord(*trade, output);
    }

    return Result<std::size_t, EventRecordFormatError>::failure(
        EventRecordFormatError::unexpected_record_type);
}

Result<DecodedOrderRecord, EventRecordFormatError> decodeOrderRecord( std::span<const std::byte> input) {
    if (input.size() < event_record_layout::size) {
        return Result<DecodedOrderRecord, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }

    std::uint8_t recordType{};
    if (!readU8(input, event_record_layout::common::type, recordType) ||
        recordType != static_cast<std::uint8_t>(EventRecordType::order)) {
        return Result<DecodedOrderRecord, EventRecordFormatError>::failure(
            EventRecordFormatError::unexpected_record_type);
    }
    if (!allZero(input, event_record_layout::common::reserved,
                 event_record_layout::common::reservedSize) ||
        !allZero(input, event_record_layout::order::reserved,
                 event_record_layout::order::reservedSize)) {
        return Result<DecodedOrderRecord, EventRecordFormatError>::failure(
            EventRecordFormatError::nonzero_reserved_bytes);
    }

    DecodedOrderRecord decoded;
    std::uint8_t side{};
    std::uint8_t eventKind{};
    const bool readFields =
        readU64LE(input, event_record_layout::common::timestamp,
                  decoded.event.venue_timestamp_us) &&
        readU64LE(input, event_record_layout::order::orderId,
                  decoded.event.order_id.value) &&
        readI64LE(input, event_record_layout::order::price,
                  decoded.event.price.ticks) &&
        readI64LE(input, event_record_layout::order::quantity,
                  decoded.event.quantity.units) &&
        readI64LE(input, event_record_layout::order::amountTraded,
                  decoded.amountTraded.units) &&
        readU8(input, event_record_layout::order::side, side) &&
        readU8(input, event_record_layout::order::eventKind, eventKind);
    if (!readFields) {
        return Result<DecodedOrderRecord, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }

    decoded.event.side = static_cast<Side>(side);
    if (!isValidSide(decoded.event.side)) {
        return Result<DecodedOrderRecord, EventRecordFormatError>::failure(
            EventRecordFormatError::invalid_side);
    }
    decoded.event.kind = static_cast<EventKind>(eventKind);
    if (!isValidEventKind(decoded.event.kind)) {
        return Result<DecodedOrderRecord, EventRecordFormatError>::failure(
            EventRecordFormatError::invalid_event_kind);
    }

    return Result<DecodedOrderRecord, EventRecordFormatError>::success(decoded);
}

Result<TradeEvent, EventRecordFormatError> decodeTradeRecord(std::span<const std::byte> input)
{

    if (input.size() < event_record_layout::size) {
        return Result<TradeEvent, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }

    std::uint8_t recordType{};
    if (!readU8(input, event_record_layout::common::type, recordType) ||
        recordType != static_cast<std::uint8_t>(EventRecordType::trade)) {
        return Result<TradeEvent, EventRecordFormatError>::failure(
            EventRecordFormatError::unexpected_record_type);
    }
    if (!allZero(input, event_record_layout::common::reserved,
                 event_record_layout::common::reservedSize) ||
        !allZero(input, event_record_layout::trade::reserved,
                 event_record_layout::trade::reservedSize)) {
        return Result<TradeEvent, EventRecordFormatError>::failure(
            EventRecordFormatError::nonzero_reserved_bytes);
    }

    TradeEvent decoded;
    const bool readFields =
        readU64LE(input, event_record_layout::common::timestamp,
                  decoded.venue_timestamp_us) &&
        readU64LE(input, event_record_layout::trade::buyOrderId,
                  decoded.buy_order_id.value) &&
        readU64LE(input, event_record_layout::trade::sellOrderId,
                  decoded.sell_order_id.value) &&
        readI64LE(input, event_record_layout::trade::quantity,
                  decoded.quantity.units);
    if (!readFields) {
        return Result<TradeEvent, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }

    return Result<TradeEvent, EventRecordFormatError>::success(decoded);
}

Result<DecodedEventRecord, EventRecordFormatError> decodeEventRecord(std::span<const std::byte> input)
{
    if (input.size() < event_record_layout::size) {
        return Result<DecodedEventRecord, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }

    std::uint8_t recordType{};
    if (!readU8(input, event_record_layout::common::type, recordType)) {
        return Result<DecodedEventRecord, EventRecordFormatError>::failure(
            EventRecordFormatError::buffer_too_small);
    }

    if (recordType == static_cast<std::uint8_t>(EventRecordType::order)) {
        const auto decoded = decodeOrderRecord(input);
        if (!decoded.hasValue()) {
            return Result<DecodedEventRecord, EventRecordFormatError>::failure(
                *decoded.errorIf());
        }
        return Result<DecodedEventRecord, EventRecordFormatError>::success(
            DecodedEventRecord{*decoded.valueIf()});
    }

    if (recordType == static_cast<std::uint8_t>(EventRecordType::trade)) {
        const auto decoded = decodeTradeRecord(input);
        if (!decoded.hasValue()) {
            return Result<DecodedEventRecord, EventRecordFormatError>::failure(
                *decoded.errorIf());
        }
        return Result<DecodedEventRecord, EventRecordFormatError>::success(
            DecodedEventRecord{*decoded.valueIf()});
    }

    return Result<DecodedEventRecord, EventRecordFormatError>::failure(
        EventRecordFormatError::unexpected_record_type);
}

}  // namespace te
