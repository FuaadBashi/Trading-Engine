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
