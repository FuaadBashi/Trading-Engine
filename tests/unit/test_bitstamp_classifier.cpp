#include <gtest/gtest.h>

#include <te/feed/bitstamp_classifier.hpp>

namespace {

te::OrderEvent makeEvent(te::EventKind kind, std::uint64_t id, std::int64_t price_ticks,
                         std::int64_t qty_units = 1'000) {
    return te::OrderEvent{
        .venue_timestamp_us = 1'700'000'000'000'000ULL,
        .order_id = te::OrderId{id},
        .price = te::Price{price_ticks},
        .quantity = te::Qty{qty_units},
        .side = te::Side::buy,
        .kind = kind,
    };
}

}  // namespace

// ---- ordinary traffic passes through

TEST(BitstampClassifier, PassesNormalPricedEventsToTheBook) {
    te::BitstampEventClassifier classifier;

    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::add, 1, 6'484'011)),
              te::EventDisposition::apply_to_book);
    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::modify, 1, 6'484'011)),
              te::EventDisposition::apply_to_book);
    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::remove, 1, 6'484'011)),
              te::EventDisposition::apply_to_book);

    EXPECT_EQ(classifier.stats().appliedToBook, 3U);
    EXPECT_EQ(classifier.stats().zeroPriceLifecycle, 0U);
}

// An unknown id on a modify/remove is NOT the classifier's problem to hide. ADR 0012 makes the
// book report it, because only a driver knows whether it is startup-boundary noise or corruption.
TEST(BitstampClassifier, PassesUnknownIdModifyAndRemoveThroughToTheBook) {
    te::BitstampEventClassifier classifier;

    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::remove, 999, 6'484'011)),
              te::EventDisposition::apply_to_book);
    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::modify, 998, 6'484'011)),
              te::EventDisposition::apply_to_book);

    EXPECT_EQ(classifier.stats().appliedToBook, 2U);
}

// ---- price-zero lifecycles

TEST(BitstampClassifier, ExcludesPriceZeroCreate) {
    te::BitstampEventClassifier classifier;

    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::add, 42, 0)),
              te::EventDisposition::zero_price_lifecycle);

    EXPECT_EQ(classifier.stats().appliedToBook, 0U);
    EXPECT_EQ(classifier.stats().zeroPriceLifecycle, 1U);
}

// The whole point of the classifier holding state: these later events carry an ordinary-looking
// price and are indistinguishable from real traffic without the remembered id.
TEST(BitstampClassifier, ExcludesLaterEventsOfAPriceZeroLifecycleEvenAtANormalPrice) {
    te::BitstampEventClassifier classifier;

    ASSERT_EQ(classifier.classify(makeEvent(te::EventKind::add, 42, 0)),
              te::EventDisposition::zero_price_lifecycle);

    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::modify, 42, 6'484'011)),
              te::EventDisposition::zero_price_lifecycle);
    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::remove, 42, 6'484'011)),
              te::EventDisposition::zero_price_lifecycle);

    EXPECT_EQ(classifier.stats().appliedToBook, 0U);
    EXPECT_EQ(classifier.stats().zeroPriceLifecycle, 3U);
}

TEST(BitstampClassifier, ReleasesTrackedIdWhenTheLifecycleEnds) {
    te::BitstampEventClassifier classifier;

    classifier.classify(makeEvent(te::EventKind::add, 42, 0));
    EXPECT_EQ(classifier.openZeroPriceLifecycles(), 1U);

    classifier.classify(makeEvent(te::EventKind::remove, 42, 6'484'011));
    EXPECT_EQ(classifier.openZeroPriceLifecycles(), 0U);
}

// A modify does not end a lifecycle, so the id must survive it.
TEST(BitstampClassifier, KeepsTrackingAcrossAModify) {
    te::BitstampEventClassifier classifier;

    classifier.classify(makeEvent(te::EventKind::add, 42, 0));
    classifier.classify(makeEvent(te::EventKind::modify, 42, 6'484'011));

    EXPECT_EQ(classifier.openZeroPriceLifecycles(), 1U);
}

// After a lifecycle is retired, the same id reused for a real priced order must reach the book.
// If the classifier failed to release the id, that order would be silently dropped forever.
TEST(BitstampClassifier, StopsExcludingAnIdOnceItsLifecycleHasEnded) {
    te::BitstampEventClassifier classifier;

    classifier.classify(makeEvent(te::EventKind::add, 42, 0));
    classifier.classify(makeEvent(te::EventKind::remove, 42, 6'484'011));

    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::add, 42, 6'484'011)),
              te::EventDisposition::apply_to_book);
    EXPECT_EQ(classifier.stats().appliedToBook, 1U);
}

TEST(BitstampClassifier, TracksSeveralOverlappingLifecyclesIndependently) {
    te::BitstampEventClassifier classifier;

    classifier.classify(makeEvent(te::EventKind::add, 1, 0));
    classifier.classify(makeEvent(te::EventKind::add, 2, 0));
    classifier.classify(makeEvent(te::EventKind::add, 3, 6'484'011));  // a real order
    EXPECT_EQ(classifier.openZeroPriceLifecycles(), 2U);

    classifier.classify(makeEvent(te::EventKind::remove, 1, 6'484'011));
    EXPECT_EQ(classifier.openZeroPriceLifecycles(), 1U);

    // id 2 is still excluded; id 3 was never excluded.
    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::remove, 2, 6'484'011)),
              te::EventDisposition::zero_price_lifecycle);
    EXPECT_EQ(classifier.classify(makeEvent(te::EventKind::remove, 3, 6'484'011)),
              te::EventDisposition::apply_to_book);
    EXPECT_EQ(classifier.openZeroPriceLifecycles(), 0U);
}

// ---- counting
//
// Every event must be accounted for exactly once, the same invariant the Slice 1 recorder
// enforces on its own counters. An event that increments neither counter has vanished silently.

TEST(BitstampClassifier, CountsEveryEventExactlyOnce) {
    te::BitstampEventClassifier classifier;

    classifier.classify(makeEvent(te::EventKind::add, 1, 0));           // zero-price
    classifier.classify(makeEvent(te::EventKind::modify, 1, 100));      // zero-price
    classifier.classify(makeEvent(te::EventKind::remove, 1, 100));      // zero-price
    classifier.classify(makeEvent(te::EventKind::add, 2, 6'484'011));   // book
    classifier.classify(makeEvent(te::EventKind::remove, 2, 6'484'011));// book

    const auto& stats = classifier.stats();
    EXPECT_EQ(stats.zeroPriceLifecycle, 3U);
    EXPECT_EQ(stats.appliedToBook, 2U);
    EXPECT_EQ(stats.appliedToBook + stats.zeroPriceLifecycle, 5U);
}

TEST(BitstampClassifier, StartsWithZeroedCounters) {
    const te::BitstampEventClassifier classifier;

    EXPECT_EQ(classifier.stats().appliedToBook, 0U);
    EXPECT_EQ(classifier.stats().zeroPriceLifecycle, 0U);
    EXPECT_EQ(classifier.openZeroPriceLifecycles(), 0U);
}

// A sell-side price-zero lifecycle behaves identically; nothing here is side-specific.
TEST(BitstampClassifier, AppliesTheSameRuleToBothSides) {
    te::BitstampEventClassifier classifier;

    te::OrderEvent sellZero = makeEvent(te::EventKind::add, 7, 0);
    sellZero.side = te::Side::sell;

    EXPECT_EQ(classifier.classify(sellZero), te::EventDisposition::zero_price_lifecycle);
    EXPECT_EQ(classifier.openZeroPriceLifecycles(), 1U);
}
