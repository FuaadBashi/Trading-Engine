#pragma once

#include <cstdint>
#include <te/core/time.hpp>
#include <te/feed/events.hpp>
#include <type_traits>

namespace te {

// Legacy host-layout format. Bump on layout changes and retain old readers; durable future
// captures use ADR 0011's explicit little-endian v3 format.
constexpr std::uint8_t kCurrentRecordVersion = 2;

// Gap markers live in-stream so replay cannot accidentally ignore continuity loss.
enum class RecordKind : std::uint8_t {
    order_event = 0,
    gap = 1,
};

// Fixed-size legacy disk record. It is separate from OrderEvent so in-memory changes do not
// silently redefine persisted data. Whole-object I/O relies on trivial copyability and the
// exact size asserted below.
struct Record {
    OrderEvent orderEvent;
    std::uint8_t version;
    RecordKind kind;

    // Local receipt/detection time; distinct from the venue timestamp inside OrderEvent.
    Nanos receipt_timestamp_us;
};

static_assert(
    sizeof(Record) == 56,
    "Record must stay 56 bytes; if a field changed, bump version and update the on-disk format.");
static_assert(std::is_trivially_copyable_v<Record>,
              "Record must remain trivially copyable for the memcpy fast path.");

// Builders zero padding so identical logical records produce identical bytes.
Record buildRecord(const OrderEvent& orderEvent, const Clock& clock);

// Gap records have a zeroed OrderEvent and are written before the event that exposed the break.
Record buildGapRecord(const Clock& clock);

}  // namespace te
