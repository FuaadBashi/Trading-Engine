#include <gtest/gtest.h>

#include <te/feed/trade_reconciler.hpp>

namespace {

te::OrderEvent makeEvent(te::EventKind kind, std::uint64_t id, std::int64_t price_ticks,
                         std::int64_t qty_units, te::Side side = te::Side::sell) {
    return te::OrderEvent{
        .venue_timestamp_us = 1'700'000'000'000'000ULL,
        .order_id = te::OrderId{id},
        .price = te::Price{price_ticks},
        .quantity = te::Qty{qty_units},
        .side = side,
        .kind = kind,
    };
}

te::TradeEvent makeTrade(std::uint64_t buyId, std::uint64_t sellId, std::int64_t qty_units) {
    return te::TradeEvent{
        .venue_timestamp_us = 1'700'000'000'500'000ULL,
        .buy_order_id = te::OrderId{buyId},
        .sell_order_id = te::OrderId{sellId},
        .quantity = te::Qty{qty_units},
    };
}

}  // namespace

TEST(TradeReconciler, UnknownIdsProduceNoCorrections) {
    te::TradeReconciler reconciler;

    const auto corrections = reconciler.reconcile(makeTrade(1, 2, 500));

    EXPECT_TRUE(corrections.empty());
}

TEST(TradeReconciler, FullyConsumedRestingOrderProducesRemove) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 555, 7'000'000, 100'000'000), te::Qty{});

    const auto corrections = reconciler.reconcile(makeTrade(999, 555, 100'000'000));

    ASSERT_EQ(corrections.size(), 1u);
    EXPECT_EQ(corrections[0].kind, te::EventKind::remove);
    EXPECT_EQ(corrections[0].order_id, te::OrderId{555});
    EXPECT_EQ(corrections[0].price, te::Price{7'000'000});
}

TEST(TradeReconciler, PartiallyConsumedRestingOrderProducesModifyWithRemainder) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 555, 7'000'000, 100'000'000), te::Qty{});

    const auto corrections = reconciler.reconcile(makeTrade(999, 555, 40'000'000));

    ASSERT_EQ(corrections.size(), 1u);
    EXPECT_EQ(corrections[0].kind, te::EventKind::modify);
    EXPECT_EQ(corrections[0].quantity, te::Qty{60'000'000});
    EXPECT_EQ(corrections[0].price, te::Price{7'000'000});  // same-price update, not a move
}

TEST(TradeReconciler, SecondTradeAgainstSameOrderUsesUpdatedRemainder) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 555, 7'000'000, 100'000'000), te::Qty{});
    reconciler.reconcile(makeTrade(999, 555, 40'000'000));  // leaves 60,000,000 resting

    const auto corrections = reconciler.reconcile(makeTrade(998, 555, 60'000'000));

    ASSERT_EQ(corrections.size(), 1u);
    EXPECT_EQ(corrections[0].kind, te::EventKind::remove);
}

TEST(TradeReconciler, BothSidesRestingProduceTwoCorrections) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 1, 7'000'000, 50'000'000, te::Side::buy),
                       te::Qty{});
    reconciler.observe(makeEvent(te::EventKind::add, 2, 7'000'000, 50'000'000, te::Side::sell),
                       te::Qty{});

    const auto corrections = reconciler.reconcile(makeTrade(1, 2, 50'000'000));

    EXPECT_EQ(corrections.size(), 2u);
}

TEST(TradeReconciler, ObserveRemoveStopsFurtherCorrections) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 555, 7'000'000, 100'000'000), te::Qty{});
    reconciler.observe(makeEvent(te::EventKind::remove, 555, 7'000'000, 100'000'000), te::Qty{});

    // live_orders already reported this removal -- the shadow state must not still believe
    // 555 is resting, or a later trade referencing it would wrongly synthesize a correction.
    const auto corrections = reconciler.reconcile(makeTrade(999, 555, 10'000'000));

    EXPECT_TRUE(corrections.empty());
}

TEST(TradeReconciler, SameTimestampReportedFillProducesNoCorrection) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 555, 7'000'000, 100'000'000), te::Qty{});

    auto trade = makeTrade(999, 555, 40'000'000);
    auto modify = makeEvent(te::EventKind::modify, 555, 7'000'000, 60'000'000);
    modify.venue_timestamp_us = trade.venue_timestamp_us;
    reconciler.observe(modify, te::Qty{40'000'000});

    EXPECT_TRUE(reconciler.reconcile(trade).empty());
}

TEST(TradeReconciler, ReportedFillCreditsOnlyPartOfTrade) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 555, 7'000'000, 100'000'000), te::Qty{});

    auto trade = makeTrade(999, 555, 30'000'000);
    auto modify = makeEvent(te::EventKind::modify, 555, 7'000'000, 70'000'000);
    modify.venue_timestamp_us = trade.venue_timestamp_us;
    reconciler.observe(modify, te::Qty{20'000'000});

    const auto corrections = reconciler.reconcile(trade);

    ASSERT_EQ(corrections.size(), 1U);
    EXPECT_EQ(corrections.front().kind, te::EventKind::modify);
    EXPECT_EQ(corrections.front().quantity, te::Qty{60'000'000});
}

TEST(TradeReconciler, SameTimestampCreditIsConsumedAcrossTrades) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 555, 7'000'000, 100'000'000), te::Qty{});

    auto firstTrade = makeTrade(999, 555, 6'000'000);
    auto firstModify = makeEvent(te::EventKind::modify, 555, 7'000'000, 90'000'000);
    firstModify.venue_timestamp_us = firstTrade.venue_timestamp_us;
    reconciler.observe(firstModify, te::Qty{6'000'000});

    auto secondModify = makeEvent(te::EventKind::modify, 555, 7'000'000, 90'000'000);
    secondModify.venue_timestamp_us = firstTrade.venue_timestamp_us;
    reconciler.observe(secondModify, te::Qty{4'000'000});

    EXPECT_TRUE(reconciler.reconcile(firstTrade).empty());

    const auto secondCorrections = reconciler.reconcile(makeTrade(998, 555, 7'000'000));
    ASSERT_EQ(secondCorrections.size(), 1U);
    EXPECT_EQ(secondCorrections.front().quantity, te::Qty{87'000'000});
}

TEST(TradeReconciler, NewTimestampDiscardsUnmatchedCredit) {
    te::TradeReconciler reconciler;
    reconciler.observe(makeEvent(te::EventKind::add, 555, 7'000'000, 100'000'000), te::Qty{});

    auto firstModify = makeEvent(te::EventKind::modify, 555, 7'000'000, 95'000'000);
    reconciler.observe(firstModify, te::Qty{5'000'000});

    auto laterModify = makeEvent(te::EventKind::modify, 555, 7'000'000, 93'000'000);
    ++laterModify.venue_timestamp_us;
    reconciler.observe(laterModify, te::Qty{2'000'000});

    EXPECT_EQ(reconciler.stats().staleFillsDiscarded, 1U);
}
