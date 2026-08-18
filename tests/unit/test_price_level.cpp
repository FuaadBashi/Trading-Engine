#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <limits>

#include <te/book/price_level.hpp>

TEST(PriceLevel, DefaultLevelIsEmptyAndHasZeroTotal) {
    const te::PriceLevel level;

    EXPECT_TRUE(level.isEmpty());
    EXPECT_EQ(level.totalQuantity(), te::Qty{0});
}

TEST(PriceLevel, AddStoresOrdersInFifoOrderAndUpdatesTotal) {
    te::PriceLevel level;

    const auto firstResult = level.addOrder(te::OrderId{101}, te::Qty{5});
    ASSERT_TRUE(firstResult.has_value());
    const te::OrderHandle first = *firstResult;

    const auto secondResult = level.addOrder(te::OrderId{102}, te::Qty{10});
    ASSERT_TRUE(secondResult.has_value());
    const te::OrderHandle second = *secondResult;

    EXPECT_FALSE(level.isEmpty());
    EXPECT_EQ(first->id, te::OrderId{101});
    EXPECT_EQ(first->qty, te::Qty{5});
    EXPECT_EQ(second->id, te::OrderId{102});
    EXPECT_EQ(second->qty, te::Qty{10});
    EXPECT_EQ(std::next(first), second);
    EXPECT_EQ(level.totalQuantity(), te::Qty{15});
}

TEST(PriceLevel, ChangeQtyDecreasePreservesEntryAndUpdatesTotal) {
    te::PriceLevel level;
    const auto firstResult = level.addOrder(te::OrderId{201}, te::Qty{30});
    const auto secondResult = level.addOrder(te::OrderId{202}, te::Qty{20});
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_TRUE(secondResult.has_value());
    const te::OrderHandle first = *firstResult;
    const te::OrderHandle second = *secondResult;

    ASSERT_TRUE(level.changeQty(first, te::Qty{10}));

    EXPECT_EQ(first->id, te::OrderId{201});
    EXPECT_EQ(first->qty, te::Qty{10});
    EXPECT_EQ(std::next(first), second);
    EXPECT_EQ(level.totalQuantity(), te::Qty{30});
}

TEST(PriceLevel, ChangeQtyIncreasePreservesEntryAndUpdatesTotal) {
    te::PriceLevel level;
    const auto firstResult = level.addOrder(te::OrderId{301}, te::Qty{10});
    const auto secondResult = level.addOrder(te::OrderId{302}, te::Qty{20});
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_TRUE(secondResult.has_value());
    const te::OrderHandle first = *firstResult;
    const te::OrderHandle second = *secondResult;

    ASSERT_TRUE(level.changeQty(first, te::Qty{25}));

    EXPECT_EQ(first->id, te::OrderId{301});
    EXPECT_EQ(first->qty, te::Qty{25});
    EXPECT_EQ(std::next(first), second);
    EXPECT_EQ(level.totalQuantity(), te::Qty{45});
}

TEST(PriceLevel, RemoveMiddleUsesStoredQtyAndKeepsOtherHandlesValid) {
    te::PriceLevel level;
    const auto firstResult = level.addOrder(te::OrderId{401}, te::Qty{5});
    const auto middleResult = level.addOrder(te::OrderId{402}, te::Qty{10});
    const auto lastResult = level.addOrder(te::OrderId{403}, te::Qty{15});
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_TRUE(middleResult.has_value());
    ASSERT_TRUE(lastResult.has_value());
    const te::OrderHandle first = *firstResult;
    const te::OrderHandle middle = *middleResult;
    const te::OrderHandle last = *lastResult;

    level.removeOrder(middle);

    EXPECT_FALSE(level.isEmpty());
    EXPECT_EQ(first->id, te::OrderId{401});
    EXPECT_EQ(last->id, te::OrderId{403});
    EXPECT_EQ(std::next(first), last);
    EXPECT_EQ(level.totalQuantity(), te::Qty{20});
}

TEST(PriceLevel, RemoveFinalOrderMakesLevelEmptyAndZero) {
    te::PriceLevel level;
    const auto result = level.addOrder(te::OrderId{501}, te::Qty{25});
    ASSERT_TRUE(result.has_value());

    level.removeOrder(*result);

    EXPECT_TRUE(level.isEmpty());
    EXPECT_EQ(level.totalQuantity(), te::Qty{0});
}

TEST(PriceLevel, AddOverflowFailsWithoutChangingState) {
    te::PriceLevel level;
    constexpr std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
    const auto existingResult = level.addOrder(te::OrderId{601}, te::Qty{maximum});
    ASSERT_TRUE(existingResult.has_value());
    const te::OrderHandle existing = *existingResult;

    const auto overflowResult = level.addOrder(te::OrderId{602}, te::Qty{1});

    EXPECT_FALSE(overflowResult.has_value());
    EXPECT_EQ(existing->id, te::OrderId{601});
    EXPECT_EQ(existing->qty, te::Qty{maximum});
    EXPECT_EQ(level.totalQuantity(), te::Qty{maximum});
}

TEST(PriceLevel, ChangeQtyOverflowFailsWithoutChangingState) {
    te::PriceLevel level;
    constexpr std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
    const auto firstResult = level.addOrder(te::OrderId{701}, te::Qty{maximum - 5});
    const auto secondResult = level.addOrder(te::OrderId{702}, te::Qty{5});
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_TRUE(secondResult.has_value());
    const te::OrderHandle second = *secondResult;

    const bool changed = level.changeQty(second, te::Qty{6});

    EXPECT_FALSE(changed);
    EXPECT_EQ(second->id, te::OrderId{702});
    EXPECT_EQ(second->qty, te::Qty{5});
    EXPECT_EQ(level.totalQuantity(), te::Qty{maximum});
}
