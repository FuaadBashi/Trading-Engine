#include <gtest/gtest.h>

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
