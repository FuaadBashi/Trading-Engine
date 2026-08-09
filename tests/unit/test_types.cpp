// Slice 1. Write the test BEFORE the implementation.
#include <gtest/gtest.h>

#include <cstdint>
#include <te/core/types.hpp>
#include <type_traits>

TEST(PriceStorage, PriceStoresTicks) {
    const te::Price price{6'516'869};

    EXPECT_EQ(price.ticks, 6'516'869);
}

TEST(QtyStorage, QtyStoresUnits) {
    const te::Qty qty{15'253'549};

    EXPECT_EQ(qty.units, 15'253'549);
}

TEST(OrderIdStorage, OrderIdPreservesLargeValue) {
    // A real 16-digit ID from the capture. It fits in uint64_t and is below 2^53.
    // ADR 0005 still requires checked id_str parsing so future larger IDs and generic
    // JSON pipelines cannot silently lose integer precision.
    const te::OrderId id{2'037'293'133'250'560ULL};

    EXPECT_EQ(id.value, 2'037'293'133'250'560ULL);
}

TEST(TypeProperties, CoreTypesAreTriviallyCopyable) {
    // These values can move through fixed-capacity queues without custom copy behavior.
    // Trivial copyability does not define portable serialization: record writing must
    // still control field order, padding, endianness and format version explicitly.
    static_assert(std::is_trivially_copyable_v<te::Price>);
    static_assert(std::is_trivially_copyable_v<te::Qty>);
    static_assert(std::is_trivially_copyable_v<te::OrderId>);
    static_assert(std::is_trivially_copyable_v<te::Side>);

    // Side is pinned to one byte so the record layout does not shift if enumerators
    // are added later.
    static_assert(std::is_same_v<std::underlying_type_t<te::Side>, std::uint8_t>);
}

TEST(TypeProperties, DistinctTypesDoNotInterconvert) {
    // The point of separate structs rather than aliases for std::int64_t: passing a Qty
    // where a Price belongs must not compile. If either of these ever becomes
    // constructible from the other, the guardrail in ADR 0004 has silently gone.
    static_assert(!std::is_convertible_v<te::Qty, te::Price>);
    static_assert(!std::is_convertible_v<te::Price, te::Qty>);
    static_assert(!std::is_convertible_v<te::Price, std::int64_t>);
}

TEST(PriceComparison, EqualTickValuesCompareEqual) {
    const te::Price first{6'516'869};
    const te::Price same{6'516'869};
    const te::Price different{6'516'870};

    EXPECT_EQ(first, same);
    EXPECT_NE(first, different);
}

TEST(QtyComparison, EqualUnitValuesCompareEqual) {
    const te::Qty first{15'253'549};
    const te::Qty same{15'253'549};
    const te::Qty different{15'253'550};

    EXPECT_EQ(first, same);
    EXPECT_NE(first, different);
}

TEST(OrderIdComparison, EqualValuesCompareEqual) {
    const te::OrderId first{2'037'293'133'250'560ULL};
    const te::OrderId same{2'037'293'133'250'560ULL};
    const te::OrderId different{2'037'293'133'250'561ULL};

    EXPECT_EQ(first, same);
    EXPECT_NE(first, different);
}

TEST(PriceArithmetic, TickSumsAreExact) {
    // Twelve fills at 65168.69, accumulated both ways. This is the ADR 0004 argument
    // as an executable claim rather than an assertion in a document.

    // Demonstration only: 65168.69 is not represented exactly as a binary double.
    // Repeated floating-point addition can therefore differ from the mathematical
    // decimal result. Production correctness is enforced later at the decoder boundary,
    // where the decimal string must be parsed directly into integer ticks.
    double as_double = 0.0;
    for (int i = 0; i < 12; ++i) {
        as_double += 65168.69;
    }
    EXPECT_NE(as_double, 782024.28);

    // The same arithmetic in integer ticks. 6'516'869 ticks is exactly 65168.69 USD,
    // and integer addition cannot lose precision, so the total is exact.
    std::int64_t notional = 0;
    for (int i = 0; i < 12; ++i) {
        notional += te::Price{6'516'869}.ticks;
    }
    EXPECT_EQ(notional, 78'202'428);

    // Guard against the two assertions agreeing by accident: they must describe the
    // same quantity. 78'202'428 ticks scaled back to USD is 782024.28.
    EXPECT_NE(static_cast<double>(notional) / 100.0, as_double);
}
