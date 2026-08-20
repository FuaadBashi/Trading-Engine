#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <te/feed/bitstamp/bootstrap.hpp>

namespace {

te::bitstamp::SnapshotOrder makeOrder(std::uint64_t id,
                                       std::int64_t price,
                                       std::int64_t quantity,
                                       te::Side side) {
    return te::bitstamp::SnapshotOrder{
        .order_id = te::OrderId{id},
        .price = te::Price{price},
        .quantity = te::Qty{quantity},
        .side = side,
    };
}

te::bitstamp::BookSnapshot makeSnapshot(std::vector<te::bitstamp::SnapshotOrder> orders) {
    return te::bitstamp::BookSnapshot{
        .microtimestamp = 1'000'000,
        .orders = std::move(orders),
    };
}

}  // namespace

TEST(BitstampBootstrap, EmptySnapshotProducesEmptyBook) {
    const auto result = te::bitstamp::bootstrap(makeSnapshot({}));

    ASSERT_TRUE(result.hasValue());
    const te::OrderBook& book = *result.valueIf();
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.levelCount(), 0u);
}

TEST(BitstampBootstrap, SingleBidSeedsBookCorrectly) {
    auto snapshot = makeSnapshot({ makeOrder(1, 100, 5, te::Side::buy) });

    const auto result = te::bitstamp::bootstrap(std::move(snapshot));

    ASSERT_TRUE(result.hasValue());
    const te::OrderBook& book = *result.valueIf();
    EXPECT_EQ(book.bestBid(), te::Price{100});
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{5});
    EXPECT_EQ(book.levelCount(), 1u);
}

TEST(BitstampBootstrap, OrdersOnBothSidesProduceCorrectBestPrices) {
    auto snapshot = makeSnapshot({
        makeOrder(1, 100, 5, te::Side::buy),
        makeOrder(2, 105, 3, te::Side::sell),
    });

    const auto result = te::bitstamp::bootstrap(std::move(snapshot));

    ASSERT_TRUE(result.hasValue());
    const te::OrderBook& book = *result.valueIf();
    EXPECT_EQ(book.bestBid(), te::Price{100});
    EXPECT_EQ(book.bestAsk(), te::Price{105});
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{5});
    EXPECT_EQ(book.qtyAt(te::Side::sell, te::Price{105}), te::Qty{3});
    EXPECT_EQ(book.levelCount(), 2u);
}

TEST(BitstampBootstrap, MultipleOrdersAtSamePriceAggregateIntoOneLevel) {
    auto snapshot = makeSnapshot({
        makeOrder(1, 100, 5, te::Side::buy),
        makeOrder(2, 100, 7, te::Side::buy),
    });

    const auto result = te::bitstamp::bootstrap(std::move(snapshot));

    ASSERT_TRUE(result.hasValue());
    const te::OrderBook& book = *result.valueIf();
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{12});
    EXPECT_EQ(book.levelCount(), 1u);
}

TEST(BitstampBootstrap, DuplicateOrderIdInSnapshotFailsWithoutCrashing) {
    auto snapshot = makeSnapshot({
        makeOrder(1, 100, 5, te::Side::buy),
        makeOrder(1, 101, 3, te::Side::buy),
    });

    const auto result = te::bitstamp::bootstrap(std::move(snapshot));

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ApplyError::duplicate_order_id);
}

TEST(BitstampBootstrap, InvalidPriceInSnapshotFailsBootstrap) {
    auto snapshot = makeSnapshot({ makeOrder(1, 0, 5, te::Side::buy) });

    const auto result = te::bitstamp::bootstrap(std::move(snapshot));

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ApplyError::invalid_price);
}
