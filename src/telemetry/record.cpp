
#include <te/telemetry/record.hpp>

#include <cstring>

namespace te {

    Record buildRecord(const OrderEvent& orderEvent, const Clock& clock){
        Record record;
        // Zero the whole object, padding included, before assigning any field. Record is written
        // to disk byte-for-byte, and padding bytes are otherwise unspecified: value-initialisation
        // (Record record{}) zeroes members but leaves padding untouched. Without this, two builds
        // of this program produce byte-different captures for identical input, which breaks any
        // checksum or byte-level comparison of capture files.
        //
        // The void* cast is required by GCC's -Wclass-memaccess. OrderEvent's default member
        // initialisers make Record non-trivially-default-constructible, so GCC warns that memset
        // could bypass them. It cannot matter here: every member is assigned immediately below,
        // and Record is trivially copyable (static_assert in record.hpp), which is what makes the
        // byte-level write well-defined.
        std::memset(static_cast<void*>(&record), 0, sizeof(record));
        record.orderEvent = orderEvent;
        record.version = kCurrentRecordVersion;
        record.receipt_timestamp_us = clock.now();

        return record;
    };

}