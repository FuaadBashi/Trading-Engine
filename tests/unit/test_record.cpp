#include <gtest/gtest.h>

#include <te/telemetry/record.hpp>

// Real captured values, same event used throughout the decoder/text_to_int tests.
TEST(Record, BuildRecordStampsFieldsFromEventAndClock) {
    const te::OrderEvent event{
        .venue_timestamp_us = 1786269861947000ULL,
        .order_id = te::OrderId{2037493297635328ULL},
        .price = te::Price{5835610},
        .quantity = te::Qty{171371},
        .side = te::Side::buy,
        .kind = te::EventKind::remove,
    };

    // A fake clock, same trick as Clock.FakeClockReturnsControlledValue: no real time involved,
    // so the expected receipt_timestamp_us is an exact value, not "some plausible number."
    te::Clock fakeClock;
    fakeClock.now = []() { return te::Nanos{42}; };

    const te::Record record = te::buildRecord(event, fakeClock);

    EXPECT_EQ(record.version, te::kCurrentRecordVersion);
    EXPECT_EQ(record.receipt_timestamp_us, te::Nanos{42});
    EXPECT_EQ(record.orderEvent.order_id, te::OrderId{2037493297635328ULL});
    EXPECT_EQ(record.orderEvent.price, te::Price{5835610});
    EXPECT_EQ(record.orderEvent.quantity, te::Qty{171371});
    EXPECT_EQ(record.orderEvent.side, te::Side::buy);
    EXPECT_EQ(record.orderEvent.kind, te::EventKind::remove);
    EXPECT_EQ(record.orderEvent.venue_timestamp_us, 1786269861947000ULL);
}
