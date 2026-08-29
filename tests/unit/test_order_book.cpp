#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <te/book/order_book.hpp>

namespace {

te::OrderEvent makeEvent(te::EventKind kind,
                         std::uint64_t id,
                         std::int64_t price,
                         std::int64_t quantity,
                         te::Side side = te::Side::buy) {
    return te::OrderEvent{
        .order_id = te::OrderId{id},
        .price = te::Price{price},
        .quantity = te::Qty{quantity},
        .side = side,
        .kind = kind,
    };
}

void expectError(const te::Result<te::ApplyOutcome, te::ApplyError>& result,
                 te::ApplyError expected) {
    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), expected);
}

}  // namespace

TEST(OrderBook, EmptyBookHasNoBestPricesAndZeroQuantity) {
    const te::OrderBook book;

    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{0});
    EXPECT_EQ(book.qtyAt(te::Side::sell, te::Price{100}), te::Qty{0});
}

TEST(OrderBook, AddCreatesLevelAndUpdatesObservableState) {
    te::OrderBook book;

    const auto result = book.apply(makeEvent(te::EventKind::add, 1, 100, 5));

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_TRUE(result.valueIf()->createdLevel);
    EXPECT_FALSE(result.valueIf()->removedLevel);
    EXPECT_EQ(book.bestBid(), te::Price{100});
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{5});
}

TEST(OrderBook, AddAtExistingPriceDoesNotCreateAnotherLevel) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::add, 2, 100, 7));

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_FALSE(result.valueIf()->createdLevel);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{12});
}

TEST(OrderBook, DuplicateOrderIdFailsWithoutChangingBook) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::add, 1, 101, 7));

    expectError(result, te::ApplyError::duplicate_order_id);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{5});
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{101}), te::Qty{0});
}

TEST(OrderBook, AddRejectsInvalidPriceAndQuantity) {
    te::OrderBook book;

    const auto invalidPrice = book.apply(makeEvent(te::EventKind::add, 1, 0, 5));
    const auto invalidQuantity = book.apply(makeEvent(te::EventKind::add, 2, 100, 0));

    expectError(invalidPrice, te::ApplyError::invalid_price);
    expectError(invalidQuantity, te::ApplyError::invalid_quantity);
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST(OrderBook, SamePriceModifyReplacesQuantityWithoutChangingLevels) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::modify, 1, 100, 7));

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_FALSE(result.valueIf()->createdLevel);
    EXPECT_FALSE(result.valueIf()->removedLevel);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{7});
}

TEST(OrderBook, ModifyRejectsSideMismatchWithoutChangingBook) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());

    const auto result =
        book.apply(makeEvent(te::EventKind::modify, 1, 100, 7, te::Side::sell));

    expectError(result, te::ApplyError::side_mismatch);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{5});
    EXPECT_EQ(book.qtyAt(te::Side::sell, te::Price{100}), te::Qty{0});
}

TEST(OrderBook, PriceMoveCreatesTargetRemovesEmptyOldLevelAndUpdatesLocator) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());

    const auto moveResult = book.apply(makeEvent(te::EventKind::modify, 1, 101, 7));

    ASSERT_TRUE(moveResult.hasValue());
    ASSERT_NE(moveResult.valueIf(), nullptr);
    EXPECT_TRUE(moveResult.valueIf()->createdLevel);
    EXPECT_TRUE(moveResult.valueIf()->removedLevel);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{0});
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{101}), te::Qty{7});
    EXPECT_EQ(book.bestBid(), te::Price{101});

    const auto secondModify = book.apply(makeEvent(te::EventKind::modify, 1, 101, 8));
    ASSERT_TRUE(secondModify.hasValue());
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{101}), te::Qty{8});
}

TEST(OrderBook, PriceMoveIntoExistingLevelAggregatesQuantity) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 2, 101, 3)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::modify, 1, 101, 7));

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_FALSE(result.valueIf()->createdLevel);
    EXPECT_TRUE(result.valueIf()->removedLevel);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{0});
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{101}), te::Qty{10});
}

TEST(OrderBook, FailedPriceMoveLeavesOldAndTargetLevelsUnchanged) {
    te::OrderBook book;
    constexpr std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 2, 101, maximum - 5)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::modify, 1, 101, 6));

    expectError(result, te::ApplyError::level_quantity_overflow);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{5});
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{101}), te::Qty{maximum - 5});
}

TEST(OrderBook, RemoveOneOfSeveralOrdersKeepsLevelAndUpdatesTotal) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 2, 100, 7)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::remove, 1, 100, 0));

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_FALSE(result.valueIf()->createdLevel);
    EXPECT_FALSE(result.valueIf()->removedLevel);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{7});
    EXPECT_EQ(book.bestBid(), te::Price{100});
}

TEST(OrderBook, RemoveFinalOrderErasesLevelAndReportsOutcome) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::remove, 1, 100, 0));

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_FALSE(result.valueIf()->createdLevel);
    EXPECT_TRUE(result.valueIf()->removedLevel);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{0});
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST(OrderBook, RemoveUsesStoredPriceInsteadOfReportedPrice) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::remove, 1, 999, 0));

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_TRUE(result.valueIf()->removedLevel);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{0});
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{999}), te::Qty{0});
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST(OrderBook, RemoveRejectsSideMismatchWithoutChangingBook) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());

    const auto result =
        book.apply(makeEvent(te::EventKind::remove, 1, 100, 0, te::Side::sell));

    expectError(result, te::ApplyError::side_mismatch);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{5});
    EXPECT_EQ(book.qtyAt(te::Side::sell, te::Price{100}), te::Qty{0});
}

TEST(OrderBook, RemoveUnknownOrderIdFailsWithoutChangingBook) {
    te::OrderBook book;

    const auto result = book.apply(makeEvent(te::EventKind::remove, 999, 100, 0));

    expectError(result, te::ApplyError::unknown_order_id);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST(OrderBook, RemovedOrderIdCanBeAddedAgain) {
    te::OrderBook book;
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::add, 1, 100, 5)).hasValue());
    ASSERT_TRUE(book.apply(makeEvent(te::EventKind::remove, 1, 100, 0)).hasValue());

    const auto result = book.apply(makeEvent(te::EventKind::add, 1, 101, 7));

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_TRUE(result.valueIf()->createdLevel);
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{100}), te::Qty{0});
    EXPECT_EQ(book.qtyAt(te::Side::buy, te::Price{101}), te::Qty{7});
}

// OrderBook is moved twice on every replay: out of bootstrap's Result, then into ReplayResult.
// Nothing tested that a POPULATED book survives it. The static_asserts in order_book.hpp only
// prove copy is deleted and move exists -- not that move is correct. It matters here because
// orderIndex_ stores OrderLocators holding list iterators into this book's own PriceLevels, so a
// move that relocated the nodes would leave every locator dangling. That would be silent until
// some later modify or remove followed a stale iterator, i.e. mid-replay, on real data.
TEST(OrderBook, MoveConstructionKeepsLocatorsUsableOnAPopulatedBook) {
    te::OrderBook source;
    ASSERT_TRUE(source.apply(makeEvent(te::EventKind::add, 1, 100, 5, te::Side::buy)).hasValue());
    ASSERT_TRUE(source.apply(makeEvent(te::EventKind::add, 2, 100, 3, te::Side::buy)).hasValue());
    ASSERT_TRUE(source.apply(makeEvent(te::EventKind::add, 3, 200, 7, te::Side::sell)).hasValue());

    const te::OrderBook moved = std::move(source);

    moved.validate();
    EXPECT_EQ(moved.levelCount(), 2U);
    EXPECT_EQ(moved.qtyAt(te::Side::buy, te::Price{100}), te::Qty{8});
    EXPECT_EQ(moved.qtyAt(te::Side::sell, te::Price{200}), te::Qty{7});
}

// The dangerous half: locators must still resolve for MUTATION, not just for reads. A read-only
// check would pass even if every iterator were dangling, because qtyAt goes through the price
// maps and never touches orderIndex_.
TEST(OrderBook, MovedBookCanStillModifyAndRemoveOrdersItInherited) {
    te::OrderBook source;
    ASSERT_TRUE(source.apply(makeEvent(te::EventKind::add, 1, 100, 5, te::Side::buy)).hasValue());
    ASSERT_TRUE(source.apply(makeEvent(te::EventKind::add, 2, 100, 3, te::Side::buy)).hasValue());
    ASSERT_TRUE(source.apply(makeEvent(te::EventKind::add, 3, 200, 7, te::Side::sell)).hasValue());

    te::OrderBook moved = std::move(source);

    // Follow an inherited locator to shrink an order in place.
    ASSERT_TRUE(moved.apply(makeEvent(te::EventKind::modify, 1, 100, 2, te::Side::buy)).hasValue());
    EXPECT_EQ(moved.qtyAt(te::Side::buy, te::Price{100}), te::Qty{5});

    // Follow another to erase a node the moved-from book allocated.
    ASSERT_TRUE(moved.apply(makeEvent(te::EventKind::remove, 2, 100, 3, te::Side::buy)).hasValue());
    EXPECT_EQ(moved.qtyAt(te::Side::buy, te::Price{100}), te::Qty{2});

    // Emptying a level must still remove the level itself.
    ASSERT_TRUE(moved.apply(makeEvent(te::EventKind::remove, 1, 100, 2, te::Side::buy)).hasValue());
    EXPECT_EQ(moved.levelCount(), 1U);
    moved.validate();
}

TEST(OrderBook, MoveAssignmentKeepsLocatorsUsableAndDiscardsTheOldBook) {
    te::OrderBook source;
    ASSERT_TRUE(source.apply(makeEvent(te::EventKind::add, 1, 100, 5, te::Side::buy)).hasValue());
    ASSERT_TRUE(source.apply(makeEvent(te::EventKind::add, 2, 200, 4, te::Side::sell)).hasValue());

    // The target already owns state, so assignment must release it rather than merge it.
    te::OrderBook target;
    ASSERT_TRUE(target.apply(makeEvent(te::EventKind::add, 9, 300, 1, te::Side::buy)).hasValue());

    target = std::move(source);

    target.validate();
    EXPECT_EQ(target.levelCount(), 2U);
    EXPECT_EQ(target.qtyAt(te::Side::buy, te::Price{300}), te::Qty{});
    ASSERT_TRUE(target.apply(makeEvent(te::EventKind::remove, 1, 100, 5, te::Side::buy)).hasValue());
    EXPECT_EQ(target.levelCount(), 1U);
}

// Two moves is what a real replay does, so prove the book survives a chain of them.
TEST(OrderBook, SurvivesTheDoubleMoveAReplayPerforms) {
    te::OrderBook first;
    ASSERT_TRUE(first.apply(makeEvent(te::EventKind::add, 1, 100, 5, te::Side::buy)).hasValue());
    ASSERT_TRUE(first.apply(makeEvent(te::EventKind::add, 2, 100, 3, te::Side::buy)).hasValue());

    te::OrderBook second = std::move(first);
    te::OrderBook third = std::move(second);

    third.validate();
    ASSERT_TRUE(third.apply(makeEvent(te::EventKind::modify, 2, 100, 1, te::Side::buy)).hasValue());
    EXPECT_EQ(third.qtyAt(te::Side::buy, te::Price{100}), te::Qty{6});
    EXPECT_EQ(third.digest(), [] {
        te::OrderBook direct;
        direct.apply(makeEvent(te::EventKind::add, 1, 100, 5, te::Side::buy));
        direct.apply(makeEvent(te::EventKind::add, 2, 100, 1, te::Side::buy));
        return direct.digest();
    }()) << "a moved book must hash identically to one built in place";
}
