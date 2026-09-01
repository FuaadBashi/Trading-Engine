#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "te/feed/merge_cursor.hpp"

namespace {

te::CapturedOrderEvent order(std::uint64_t timestamp, std::uint64_t id) {
    te::CapturedOrderEvent captured{};
    captured.event.venue_timestamp_us = timestamp;
    captured.event.order_id = te::OrderId{id};
    return captured;
}

te::CapturedTradeEvent trade(std::uint64_t timestamp, std::uint64_t buyId) {
    te::CapturedTradeEvent captured{};
    captured.event.venue_timestamp_us = timestamp;
    captured.event.buy_order_id = te::OrderId{buyId};
    return captured;
}

// Drains the cursor so a test can assert on the whole sequence rather than step by step.
std::vector<te::MergedPick> drain(te::MergeCursor& cursor) {
    std::vector<te::MergedPick> picks;
    while (const auto pick = cursor.next()) {
        picks.push_back(*pick);
    }
    return picks;
}

constexpr std::uint64_t kNoSeed = 0;
constexpr std::uint64_t kNoCutoff = UINT64_MAX;

}  // namespace

TEST(MergeCursor, EarlierOrderComesFirst) {
    const std::vector<te::CapturedOrderEvent> orders{order(10, 1)};
    const std::vector<te::CapturedTradeEvent> trades{trade(20, 2)};
    te::MergeCursor cursor{orders, trades, kNoSeed, kNoCutoff};

    const auto picks = drain(cursor);

    ASSERT_EQ(picks.size(), 2U);
    EXPECT_EQ(picks[0].stream, te::MergedStream::order);
    EXPECT_EQ(picks[1].stream, te::MergedStream::trade);
}

TEST(MergeCursor, EarlierTradeComesFirst) {
    const std::vector<te::CapturedOrderEvent> orders{order(20, 1)};
    const std::vector<te::CapturedTradeEvent> trades{trade(10, 2)};
    te::MergeCursor cursor{orders, trades, kNoSeed, kNoCutoff};

    const auto picks = drain(cursor);

    ASSERT_EQ(picks.size(), 2U);
    EXPECT_EQ(picks[0].stream, te::MergedStream::trade);
    EXPECT_EQ(picks[1].stream, te::MergedStream::order);
}

// ADR 0013: the order must be observed before a trade at the same timestamp is reconciled.
TEST(MergeCursor, ExactTieGivesTheOrderEventPriority) {
    const std::vector<te::CapturedOrderEvent> orders{order(10, 1)};
    const std::vector<te::CapturedTradeEvent> trades{trade(10, 2)};
    te::MergeCursor cursor{orders, trades, kNoSeed, kNoCutoff};

    const auto picks = drain(cursor);

    ASSERT_EQ(picks.size(), 2U);
    EXPECT_EQ(picks[0].stream, te::MergedStream::order);
    EXPECT_EQ(picks[1].stream, te::MergedStream::trade);
}

TEST(MergeCursor, InterleavesLongerStreamsByTimestamp) {
    const std::vector<te::CapturedOrderEvent> orders{order(10, 1), order(30, 2), order(50, 3)};
    const std::vector<te::CapturedTradeEvent> trades{trade(20, 4), trade(40, 5)};
    te::MergeCursor cursor{orders, trades, kNoSeed, kNoCutoff};

    const auto picks = drain(cursor);

    ASSERT_EQ(picks.size(), 5U);
    EXPECT_EQ(picks[0].stream, te::MergedStream::order);
    EXPECT_EQ(picks[0].index, 0U);
    EXPECT_EQ(picks[1].stream, te::MergedStream::trade);
    EXPECT_EQ(picks[1].index, 0U);
    EXPECT_EQ(picks[2].stream, te::MergedStream::order);
    EXPECT_EQ(picks[2].index, 1U);
    EXPECT_EQ(picks[3].stream, te::MergedStream::trade);
    EXPECT_EQ(picks[3].index, 1U);
    EXPECT_EQ(picks[4].stream, te::MergedStream::order);
    EXPECT_EQ(picks[4].index, 2U);
}

TEST(MergeCursor, PreSeedEventsAreSkippedButCounted) {
    const std::vector<te::CapturedOrderEvent> orders{order(5, 1), order(15, 2)};
    const std::vector<te::CapturedTradeEvent> trades{trade(5, 3), trade(15, 4)};
    te::MergeCursor cursor{orders, trades, 10, kNoCutoff};

    const auto picks = drain(cursor);

    EXPECT_EQ(cursor.ordersBeforeSeed(), 1U);
    EXPECT_EQ(cursor.tradesBeforeSeed(), 1U);
    ASSERT_EQ(picks.size(), 2U);
    EXPECT_EQ(picks[0].index, 1U);
    EXPECT_EQ(picks[1].index, 1U);
}

// The seed boundary is exclusive: an event exactly at the seed timestamp is already in the snapshot.
TEST(MergeCursor, EventExactlyAtSeedIsTreatedAsPreSeed) {
    const std::vector<te::CapturedOrderEvent> orders{order(10, 1)};
    const std::vector<te::CapturedTradeEvent> trades{};
    te::MergeCursor cursor{orders, trades, 10, kNoCutoff};

    const auto picks = drain(cursor);

    EXPECT_TRUE(picks.empty());
    EXPECT_EQ(cursor.ordersBeforeSeed(), 1U);
}

// The cutoff boundary is inclusive: an event exactly at the cutoff is inside the window.
TEST(MergeCursor, EventExactlyAtCutoffIsInsideTheWindow) {
    const std::vector<te::CapturedOrderEvent> orders{order(20, 1)};
    const std::vector<te::CapturedTradeEvent> trades{};
    te::MergeCursor cursor{orders, trades, kNoSeed, 20};

    const auto picks = drain(cursor);

    ASSERT_EQ(picks.size(), 1U);
    EXPECT_EQ(cursor.ordersAfterCutoff(), 0U);
}

TEST(MergeCursor, EventsPastCutoffAreLeftAndCounted) {
    const std::vector<te::CapturedOrderEvent> orders{order(10, 1), order(30, 2)};
    const std::vector<te::CapturedTradeEvent> trades{trade(40, 3)};
    te::MergeCursor cursor{orders, trades, kNoSeed, 20};

    const auto picks = drain(cursor);

    ASSERT_EQ(picks.size(), 1U);
    EXPECT_EQ(cursor.ordersAfterCutoff(), 1U);
    EXPECT_EQ(cursor.tradesAfterCutoff(), 1U);
}

// Plan v4 §12 requires every input to be accounted for exactly once.
TEST(MergeCursor, EveryInputIsAccountedForExactlyOnce) {
    const std::vector<te::CapturedOrderEvent> orders{order(5, 1), order(15, 2), order(25, 3),
                                                     order(35, 4)};
    const std::vector<te::CapturedTradeEvent> trades{trade(5, 5), trade(15, 6), trade(45, 7)};
    te::MergeCursor cursor{orders, trades, 10, 30};

    const auto picks = drain(cursor);

    std::size_t ordersRead{};
    std::size_t tradesRead{};
    for (const auto& pick : picks) {
        if (pick.stream == te::MergedStream::order) {
            ++ordersRead;
        } else {
            ++tradesRead;
        }
    }
    EXPECT_EQ(cursor.ordersBeforeSeed() + ordersRead + cursor.ordersAfterCutoff(), orders.size());
    EXPECT_EQ(cursor.tradesBeforeSeed() + tradesRead + cursor.tradesAfterCutoff(), trades.size());
}

TEST(MergeCursor, EmptyStreamsYieldNothing) {
    const std::vector<te::CapturedOrderEvent> orders{};
    const std::vector<te::CapturedTradeEvent> trades{};
    te::MergeCursor cursor{orders, trades, kNoSeed, kNoCutoff};

    EXPECT_FALSE(cursor.next().has_value());
}

TEST(MergeCursor, OneEmptyStreamDrainsTheOther) {
    const std::vector<te::CapturedOrderEvent> orders{order(10, 1), order(20, 2)};
    const std::vector<te::CapturedTradeEvent> trades{};
    te::MergeCursor cursor{orders, trades, kNoSeed, kNoCutoff};

    const auto picks = drain(cursor);

    ASSERT_EQ(picks.size(), 2U);
    EXPECT_EQ(picks[0].stream, te::MergedStream::order);
    EXPECT_EQ(picks[1].stream, te::MergedStream::order);
}

TEST(MergeCursor, ExhaustedCursorStaysExhausted) {
    const std::vector<te::CapturedOrderEvent> orders{order(10, 1)};
    const std::vector<te::CapturedTradeEvent> trades{};
    te::MergeCursor cursor{orders, trades, kNoSeed, kNoCutoff};
    ASSERT_TRUE(cursor.next().has_value());

    EXPECT_FALSE(cursor.next().has_value());
    EXPECT_FALSE(cursor.next().has_value());
}
