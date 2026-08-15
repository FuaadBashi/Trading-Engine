#include <gtest/gtest.h>

#include <type_traits>

#include <te/core/types.hpp>
#include <te/feed/events.hpp>

// A normalized event must preserve the domain values received from the decoder. The book consumes
// this compact value object and should never need to know the original JSON field names.
TEST(OrderEvent, StoresNormalizedOrderData) {
    const te::OrderEvent event{
        .venue_timestamp_us = 1'786'269'861'574'036ULL,
        .order_id = te::OrderId{2'037'293'133'250'560ULL},
        .price = te::Price{6'516'869},
        .quantity = te::Qty{15'253'549},
        .side = te::Side::buy,
        .kind = te::EventKind::add,
    };

    EXPECT_EQ(event.venue_timestamp_us, 1'786'269'861'574'036ULL);
    EXPECT_EQ(event.order_id, te::OrderId{2'037'293'133'250'560ULL});
    EXPECT_EQ(event.price, te::Price{6'516'869});
    EXPECT_EQ(event.quantity, te::Qty{15'253'549});
    EXPECT_EQ(event.side, te::Side::buy);
    EXPECT_EQ(event.kind, te::EventKind::add);
}

// A default-constructed event must be zeroed, not indeterminate: Record embeds one and is
// written to disk byte-for-byte, so an uninitialised field would reach the capture file.
TEST(OrderEvent, DefaultConstructedFieldsAreZeroed) {
    const te::OrderEvent event{};

    EXPECT_EQ(event.venue_timestamp_us, 0ULL);
    EXPECT_EQ(event.order_id, te::OrderId{0});
    EXPECT_EQ(event.price, te::Price{0});
    EXPECT_EQ(event.quantity, te::Qty{0});
    EXPECT_EQ(event.side, te::Side::buy);
    EXPECT_EQ(event.kind, te::EventKind::add);
}

// Both enums are pinned to one byte because the record layout and the fixed-capacity queue
// depend on the size. events.hpp static_asserts EventKind; this covers the pair at runtime.
TEST(OrderEvent, EnumsRetainOneByteRepresentation) {
    EXPECT_EQ(sizeof(te::EventKind), 1U);
    EXPECT_EQ(sizeof(te::Side), 1U);
}

// Distinct enumerators must stay distinct: a decoder maps three venue message types onto
// these, and a collision would silently merge two lifecycle operations.
TEST(OrderEvent, EventKindValuesAreDistinct) {
    EXPECT_NE(te::EventKind::add, te::EventKind::modify);
    EXPECT_NE(te::EventKind::modify, te::EventKind::remove);
    EXPECT_NE(te::EventKind::add, te::EventKind::remove);
}

// Trivial copyability is load-bearing: Sink memcpys a Record, which embeds an OrderEvent.
// If this ever breaks, the capture path becomes undefined behaviour rather than slow.
TEST(OrderEvent, IsTriviallyCopyableAndFixedSize) {
    EXPECT_TRUE(std::is_trivially_copyable_v<te::OrderEvent>);
    EXPECT_EQ(sizeof(te::OrderEvent), 40U);
}

// Each field must be independently settable; a shared or aliased member would corrupt events
// in ways the decoder tests would not catch.
TEST(OrderEvent, FieldsAreIndependent) {
    te::OrderEvent event{};

    event.price = te::Price{100};
    event.quantity = te::Qty{200};

    EXPECT_EQ(event.price, te::Price{100});
    EXPECT_EQ(event.quantity, te::Qty{200});
    EXPECT_EQ(event.venue_timestamp_us, 0ULL);
    EXPECT_EQ(event.order_id, te::OrderId{0});
}
