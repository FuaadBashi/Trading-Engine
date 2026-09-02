
#include <cstring>
#include <te/telemetry/legacy/record.hpp>

namespace te {

Record buildRecord(const OrderEvent& orderEvent, const Clock& clock) {
    Record record;
    // Record is written byte-for-byte, so zero padding for reproducible captures. The void*
    // cast suppresses GCC's class-memaccess warning; every member is assigned below and the
    // type's trivially-copyable contract is checked in record.hpp.
    std::memset(static_cast<void*>(&record), 0, sizeof(record));
    record.orderEvent = orderEvent;
    record.version = kCurrentRecordVersion;
    record.kind = RecordKind::order_event;
    record.receipt_timestamp_ns = clock.now();

    return record;
};

Record buildGapRecord(const Clock& clock) {
    Record record;
    // Also leaves the gap's meaningless orderEvent bytes deterministically zero.
    std::memset(static_cast<void*>(&record), 0, sizeof(record));
    record.version = kCurrentRecordVersion;
    record.kind = RecordKind::gap;
    record.receipt_timestamp_ns = clock.now();

    return record;
}

}  // namespace te
