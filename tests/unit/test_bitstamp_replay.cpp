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

te::OrderEvent makeModify(std::uint64_t timestamp, std::uint64_t id, std::int64_t price,
                          std::int64_t quantity, te::Side side) {
    return te::OrderEvent{
        .venue_timestamp_us = timestamp,
        .order_id = te::OrderId{id},
        .price = te::Price{price},
        .quantity = te::Qty{quantity},
        .side = side,
        .kind = te::EventKind::modify,
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

te::bitstamp::CapturedOrderEvent capture(te::OrderEvent event,
                                         std::int64_t amountTraded = 0) {
    return te::bitstamp::CapturedOrderEvent{
        .event = event,
        .amountTraded = te::Qty{amountTraded},
    };
}

te::bitstamp::CapturedTradeEvent makeTrade(std::uint64_t timestamp, std::uint64_t buyId,
                                           std::uint64_t sellId, std::int64_t quantity) {
    return te::bitstamp::CapturedTradeEvent{
        .event =
            te::TradeEvent{
                .venue_timestamp_us = timestamp,
                .buy_order_id = te::OrderId{buyId},
                .sell_order_id = te::OrderId{sellId},
                .quantity = te::Qty{quantity},
            },
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
        {capture(makeRemove(102, 42, 999, 5, te::Side::buy))},
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
    const auto result = replay.replay(makeSnapshot({}),
                                      {capture(makeRemove(101, 42, 100, 5, te::Side::buy))},
                                      {}, 200);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::ReplayError::unexpected_order_apply_failure);
}

TEST(BitstampReplay, SameTimestampProcessesOrderBeforeTrade) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(makeSnapshot({}),
                                      {capture(makeAdd(101, 42, 100, 5, te::Side::buy))},
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
            capture(makeAdd(100, 1, 100, 1, te::Side::buy)),
            capture(makeAdd(101, 2, 101, 2, te::Side::buy)),
            capture(makeAdd(201, 3, 102, 3, te::Side::buy)),
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
            capture(makeAdd(99, 42, 0, 5, te::Side::buy)),
            capture(makeRemove(101, 42, 100, 5, te::Side::buy)),
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
        {capture(makeAdd(102, 1, 100, 1, te::Side::buy)),
         capture(makeAdd(101, 2, 99, 1, te::Side::buy))},
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

TEST(BitstampReplay, SameTimestampReportedFillPreventsDoubleSubtraction) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({makeSnapshotOrder(42, 100, 100, te::Side::buy)}),
        {capture(makeModify(101, 42, 100, 60, te::Side::buy), 40)},
        {makeTrade(101, 42, 99, 40)}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{60});
    EXPECT_EQ(replayed.stats.orderEventsApplied, 1U);
    EXPECT_EQ(replayed.stats.tradeEventsRead, 1U);
    EXPECT_EQ(replayed.stats.correctionsGenerated, 0U);
    EXPECT_EQ(replayed.stats.correctionsApplied, 0U);
}

TEST(BitstampReplay, SameTimestampUncreditedTradeRemovesRestingOrder) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({}), {capture(makeAdd(101, 42, 100, 100, te::Side::buy))},
        {makeTrade(101, 42, 99, 100)}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.levelCount(), 0U);
    EXPECT_EQ(replayed.stats.orderEventsApplied, 1U);
    EXPECT_EQ(replayed.stats.tradeEventsRead, 1U);
    EXPECT_EQ(replayed.stats.correctionsGenerated, 1U);
    EXPECT_EQ(replayed.stats.correctionsApplied, 1U);
}

TEST(BitstampReplay, ExposesStaleFillDiscardedAtNewerTimestamp) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({makeSnapshotOrder(42, 100, 100, te::Side::buy)}),
        {
            capture(makeModify(101, 42, 100, 90, te::Side::buy), 10),
            capture(makeModify(102, 42, 100, 80, te::Side::buy), 10),
        },
        {}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{80});
    EXPECT_EQ(replayed.stats.reconciler.staleFillsDiscarded, 1U);
}

// A fill reported at 101 that no trade ever matched, with the order removed at a later 102.
TEST(BitstampReplay, ExposesOrderRemovedWithUnmatchedFill) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({makeSnapshotOrder(42, 100, 100, te::Side::buy)}),
        {
            capture(makeModify(101, 42, 100, 90, te::Side::buy), 10),
            capture(makeRemove(102, 42, 100, 90, te::Side::buy)),
        },
        {}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.levelCount(), 0U);
    EXPECT_EQ(replayed.stats.reconciler.ordersRemovedWithUnmatchedFill, 1U);
}

// Same timestamp on the fill and the delete is structural, not a fault: order events win exact
// ties, so the delete always runs before that microsecond's trades and the credit cannot clear.
// This shape accounts for all 21 hits the counter reported before it excluded the case.
TEST(BitstampReplay, SameTimestampDeleteAfterFillIsNotCountedAsUnmatched) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({makeSnapshotOrder(42, 100, 100, te::Side::buy)}),
        {
            capture(makeModify(101, 42, 100, 90, te::Side::buy), 10),
            capture(makeRemove(101, 42, 100, 90, te::Side::buy)),
        },
        {}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& replayed = *result.valueIf();
    EXPECT_EQ(replayed.book.levelCount(), 0U);
    EXPECT_EQ(replayed.stats.reconciler.ordersRemovedWithUnmatchedFill, 0U);
}

// Plan v4 s12 requires every input to be reported exactly once as applied, skipped, corrected or
// failed. Events outside the seed..cutoff window used to vanish from the counters entirely --
// 14.3% of the reference capture -- so a merge loop that silently skipped events looked healthy.
TEST(BitstampReplay, AccountsForEveryInputEventExactlyOnce) {
    te::bitstamp::Replay replay;
    const auto result = replay.replay(
        makeSnapshot({}, 100),
        {
            capture(makeAdd(50, 1, 100, 1, te::Side::buy)),   // before the seed
            capture(makeAdd(150, 2, 101, 2, te::Side::buy)),  // in the window
            capture(makeAdd(250, 3, 102, 3, te::Side::buy)),  // after the cutoff
        },
        {makeTrade(60, 9, 8, 1), makeTrade(150, 7, 6, 1), makeTrade(250, 5, 4, 1)}, 200);

    ASSERT_TRUE(result.hasValue());
    const auto& stats = result.valueIf()->stats;

    EXPECT_EQ(stats.orderEventsBeforeSeed, 1U);
    EXPECT_EQ(stats.orderEventsRead, 1U);
    EXPECT_EQ(stats.orderEventsAfterCutoff, 1U);
    EXPECT_EQ(stats.orderEventsAccountedFor(), 3U);

    EXPECT_EQ(stats.tradeEventsBeforeSeed, 1U);
    EXPECT_EQ(stats.tradeEventsRead, 1U);
    EXPECT_EQ(stats.tradeEventsAfterCutoff, 1U);
    EXPECT_EQ(stats.tradeEventsAccountedFor(), 3U);
}

// The digest exists so a future optimized book can be proven equivalent to this reference one
// (plan v4 Stage 8), which requires it to depend on resting liquidity and nothing else.
TEST(BitstampReplay, BookDigestIgnoresArrivalOrderButTracksQuantity) {
    te::bitstamp::Replay replay;

    const auto ascending = replay.replay(
        makeSnapshot({}, 100),
        {
            capture(makeAdd(110, 1, 100, 5, te::Side::buy)),
            capture(makeAdd(120, 2, 200, 7, te::Side::sell)),
        },
        {}, 300);
    const auto descending = replay.replay(
        makeSnapshot({}, 100),
        {
            capture(makeAdd(110, 2, 200, 7, te::Side::sell)),
            capture(makeAdd(120, 1, 100, 5, te::Side::buy)),
        },
        {}, 300);
    const auto different = replay.replay(
        makeSnapshot({}, 100),
        {
            capture(makeAdd(110, 1, 100, 5, te::Side::buy)),
            capture(makeAdd(120, 2, 200, 8, te::Side::sell)),  // one unit more
        },
        {}, 300);

    ASSERT_TRUE(ascending.hasValue());
    ASSERT_TRUE(descending.hasValue());
    ASSERT_TRUE(different.hasValue());

    // Same liquidity reached by a different route hashes the same...
    EXPECT_EQ(ascending.valueIf()->book.digest(), descending.valueIf()->book.digest());
    // ...but one unit of difference does not.
    EXPECT_NE(ascending.valueIf()->book.digest(), different.valueIf()->book.digest());
}

// Determinism is what makes the digest usable as a fixture fingerprint at all.
TEST(BitstampReplay, RepeatedRunsProduceIdenticalDigests) {
    te::bitstamp::Replay replay;
    std::uint64_t bookDigest{};
    std::uint64_t eventDigest{};

    for (int run = 0; run < 10; ++run) {
        const auto result = replay.replay(
            makeSnapshot({makeSnapshotOrder(42, 100, 10, te::Side::buy)}, 100),
            {capture(makeModify(150, 42, 100, 6, te::Side::buy), 4)},
            {makeTrade(150, 42, 99, 4)}, 200);
        ASSERT_TRUE(result.hasValue());
        if (run == 0) {
            bookDigest = result.valueIf()->book.digest();
            eventDigest = result.valueIf()->stats.appliedEventDigest;
            continue;
        }
        EXPECT_EQ(result.valueIf()->book.digest(), bookDigest) << "run " << run;
        EXPECT_EQ(result.valueIf()->stats.appliedEventDigest, eventDigest) << "run " << run;
    }
}
