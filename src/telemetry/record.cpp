
#include <te/telemetry/record.hpp>

namespace te {

    Record buildRecord(const OrderEvent& orderEvent, const Clock& clock){ 
        Record record;
        record.orderEvent = orderEvent;
        record.version = kCurrentRecordVersion;
        record.receipt_timestamp_us = clock.now();

        return record;
    };

}