#include <gtest/gtest.h>

#include <vector>

#include <te/feed/bitstamp/replay.hpp>

namespace {

te::bitstamp::BookSnapshot makeSnapshot(std::vector<te::bitstamp::SnapshotOrder> orders,
                                        std::uint64_t timestamp = 100) {
    return te::bitstamp::BookSnapshot{
        .microtimestamp = timestamp,
        .orders = std::move(orders),
    };
}

te::bitstamp::SnapshotOrder makeSnapshotOrder(std::uint64_t id, std::int64_t price,
                                               std::int64_t quantity, te::Side side) {
    return te::bitstamp::SnapshotOrder{
        .order_id = te::OrderId{id},
        .price = te::Price{price},
        .quantity = te::Qty{quantity},
        .side = side,
    };
}

te::OrderEvent makeAdd(std::uint64_t timestamp, std::uint64_t id, std::int64_t price,
                       std::int64_t quantity, te::Side side) {
    return te::OrderEvent{
        .venue_timestamp_us = timestamp,
        .order_id = te::OrderId{id},
        .price = te::Price{price},
        .quantity = te::Qty{quantity},
        .side = side,
        .kind = te::EventKind::add,
    };
}

te::OrderEvent makeRemove(std::uint64_t timestamp, std::uint64_t id, std::int64_t price,
                          std::int64_t quantity, te::Side side) {
    return te::OrderEvent{
        .venue_timestamp_us = timestamp,
        .order_id = te::OrderId{id},
        .price = te::Price{price},
        .quantity = te::Qty{quantity},
        .side = side,
        .kind = te::EventKind::remove,
    };
}

te::TradeEvent makeTrade(std::uint64_t timestamp, std::uint64_t buyId, std::uint64_t sellId,
                         std::int64_t quantity) {
    return te::TradeEvent{
        .venue_timestamp_us = timestamp,
        .buy_order_id = te::OrderId{buyId},
        .sell_order_id = te::OrderId{sellId},
        .quantity = te::Qty{quantity},
    };
}

}  // namespace

TEST(BitstampReplay, TradeReducesSeededRestingOrder) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({makeSnapshotOrder(42, 100, 5, te::Side::buy)}), {},
        {makeTrade(101, 42, 99, 2)}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{3});
    EXPECT_EQ(replayed.stats.tradeEventsRead, 1U);
    EXPECT_EQ(replayed.stats.correctionsGenerated, 1U);
    EXPECT_EQ(replayed.stats.correctionsApplied, 1U);
}

TEST(BitstampReplay, IgnoresLateRawRemoveAlreadyAppliedByTradeCorrection) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({makeSnapshotOrder(42, 100, 5, te::Side::buy)}),
        {makeRemove(102, 42, 999, 5, te::Side::buy)},
        {makeTrade(101, 42, 99, 5)}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.levelCount(), 0U);
    EXPECT_EQ(replayed.stats.tradeEventsRead, 1U);
    EXPECT_EQ(replayed.stats.correctionsGenerated, 1U);
    EXPECT_EQ(replayed.stats.correctionsApplied, 1U);
    EXPECT_EQ(replayed.stats.orderEventsRead, 1U);
    EXPECT_EQ(replayed.stats.orderEventsApplied, 0U);
    EXPECT_EQ(replayed.stats.redundantOrderRemovals, 1U);
}

TEST(BitstampReplay, StillRejectsAnUnexplainedUnknownRawRemove) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({}), {makeRemove(101, 42, 100, 5, te::Side::buy)}, {}, 200);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::ReplayError::unexpected_order_apply_failure);
}

TEST(BitstampReplay, SameTimestampProcessesOrderBeforeTrade) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({}), {makeAdd(101, 42, 100, 5, te::Side::buy)},
        {makeTrade(101, 42, 99, 5)}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{});
    EXPECT_EQ(replayed.stats.orderEventsApplied, 1U);
    EXPECT_EQ(replayed.stats.correctionsApplied, 1U);
}

TEST(BitstampReplay, ReplaysOnlyEventsAfterSeedAndThroughCutoff) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({}, 100),
        {
            makeAdd(100, 1, 100, 1, te::Side::buy),
            makeAdd(101, 2, 101, 2, te::Side::buy),
            makeAdd(201, 3, 102, 3, te::Side::buy),
        },
        {}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{});
    EXPECT_EQ(replayed.book.qtyAt(te::Side::buy, te::Price{101}), te::Qty{2});
    EXPECT_EQ(replayed.book.qtyAt(te::Side::buy, te::Price{102}), te::Qty{});
    EXPECT_EQ(replayed.stats.orderEventsRead, 1U);
    EXPECT_EQ(replayed.stats.orderEventsApplied, 1U);
}

TEST(BitstampReplay, TracksPreSeedPriceZeroLifecycleWithoutApplyingIt) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({}, 100),
        {
            makeAdd(99, 42, 0, 5, te::Side::buy),
            makeRemove(101, 42, 100, 5, te::Side::buy),
        },
        {}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.levelCount(), 0U);
    EXPECT_EQ(replayed.stats.orderEventsRead, 1U);
    EXPECT_EQ(replayed.stats.orderEventsApplied, 0U);
}

TEST(BitstampReplay, RejectsOutOfOrderOrderInput) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({}),
        {makeAdd(102, 1, 100, 1, te::Side::buy), makeAdd(101, 2, 99, 1, te::Side::buy)},
        {}, 200);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::ReplayError::order_input_not_time_ordered);
}

TEST(BitstampReplay, RejectsOutOfOrderTradeInput) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({}), {}, {makeTrade(102, 1, 2, 1), makeTrade(101, 3, 4, 1)}, 200);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::ReplayError::trade_input_not_time_ordered);
}
