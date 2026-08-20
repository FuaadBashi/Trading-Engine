#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <te/feed/bitstamp/snapshot.hpp>

namespace {

te::InstrumentSpec btcUsd() {
    return te::InstrumentSpec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
}

// The committed fixture, inline. Two bids sharing one price, two asks at distinct prices.
// Deliberately round numbers so every expected tick/unit value below can be checked by hand.
constexpr const char* kSample =
    R"({"timestamp":"1700000000","microtimestamp":"1700000000000000","bids":[["100.00","0.50000000","1000000000000001"],["100.00","0.75000000","1000000000000002"]],"asks":[["100.01","0.30000000","1000000000000003"],["100.02","0.60000000","1000000000000004"]]})";

}  // namespace

// ---- happy path: shape and ordering

TEST(BitstampSnapshot, ParsesEveryOrderFromBothSides) {
    const auto result = te::bitstamp::parseSnapshot(kSample, btcUsd());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->orders.size(), 4U);
}

TEST(BitstampSnapshot, ParsesMicrotimestampExactly) {
    const auto result = te::bitstamp::parseSnapshot(kSample, btcUsd());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->microtimestamp, 1'700'000'000'000'000ULL);
}

// Side is not carried in a row -- it comes from which array the row was found in. If those two
// loops were ever wired to the same JSON key, this is the test that catches it.
TEST(BitstampSnapshot, BidRowsBecomeBuyAndAskRowsBecomeSell) {
    const auto result = te::bitstamp::parseSnapshot(kSample, btcUsd());
    ASSERT_TRUE(result.hasValue());
    const auto& orders = result.valueIf()->orders;
    ASSERT_EQ(orders.size(), 4U);

    EXPECT_EQ(orders[0].side, te::Side::buy);
    EXPECT_EQ(orders[1].side, te::Side::buy);
    EXPECT_EQ(orders[2].side, te::Side::sell);
    EXPECT_EQ(orders[3].side, te::Side::sell);
}

// ADR 0004's guarantee at the snapshot boundary: decimal text becomes exact integer ticks/units
// with no floating point anywhere in between.
TEST(BitstampSnapshot, ConvertsPriceAndQuantityToExactIntegers) {
    const auto result = te::bitstamp::parseSnapshot(kSample, btcUsd());
    ASSERT_TRUE(result.hasValue());
    const auto& orders = result.valueIf()->orders;
    ASSERT_EQ(orders.size(), 4U);

    EXPECT_EQ(orders[0].price, te::Price{10'000});        // "100.00" at scale 2
    EXPECT_EQ(orders[0].quantity, te::Qty{50'000'000});   // "0.50000000" at scale 8
    EXPECT_EQ(orders[0].order_id, te::OrderId{1'000'000'000'000'001ULL});

    EXPECT_EQ(orders[2].price, te::Price{10'001});        // "100.01"
    EXPECT_EQ(orders[2].quantity, te::Qty{30'000'000});   // "0.30000000"

    EXPECT_EQ(orders[3].price, te::Price{10'002});        // "100.02"
    EXPECT_EQ(orders[3].quantity, te::Qty{60'000'000});   // "0.60000000"
}

// L3, not L2: two orders at one price stay two orders. Summing them here would silently discard
// the per-order identity the whole queue-position model in Slice 4 depends on.
TEST(BitstampSnapshot, KeepsSamePriceOrdersSeparate) {
    const auto result = te::bitstamp::parseSnapshot(kSample, btcUsd());
    ASSERT_TRUE(result.hasValue());
    const auto& orders = result.valueIf()->orders;
    ASSERT_GE(orders.size(), 2U);

    EXPECT_EQ(orders[0].price, orders[1].price);
    EXPECT_NE(orders[0].order_id, orders[1].order_id);
    EXPECT_NE(orders[0].quantity, orders[1].quantity);
}

// Row order within a side is preserved, so a replay is deterministic. Note this is ordering only,
// NOT proven queue priority -- see BookSnapshot's own doc comment and ADR 0011.
TEST(BitstampSnapshot, PreservesRowOrderWithinASide) {
    const auto result = te::bitstamp::parseSnapshot(kSample, btcUsd());
    ASSERT_TRUE(result.hasValue());
    const auto& orders = result.valueIf()->orders;
    ASSERT_EQ(orders.size(), 4U);

    EXPECT_EQ(orders[0].order_id, te::OrderId{1'000'000'000'000'001ULL});
    EXPECT_EQ(orders[1].order_id, te::OrderId{1'000'000'000'000'002ULL});
    EXPECT_EQ(orders[2].order_id, te::OrderId{1'000'000'000'000'003ULL});
    EXPECT_EQ(orders[3].order_id, te::OrderId{1'000'000'000'000'004ULL});
}

TEST(BitstampSnapshot, AcceptsEmptySides) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1700000000000000","bids":[],"asks":[]})", btcUsd());

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.valueIf()->orders.empty());
    EXPECT_EQ(result.valueIf()->microtimestamp, 1'700'000'000'000'000ULL);
}

// ---- document-level failures

// simdjson On Demand is lazy: iterate() does not validate the whole document up front, so most
// garbage is only caught when a field is actually reached. Empty input is the case that fails at
// iterate() itself; everything else surfaces at the first field access instead. What matters is
// that all of it is rejected -- the specific code is less precise than it looks.
TEST(BitstampSnapshot, RejectsEmptyInputAsMalformedJson) {
    const auto result = te::bitstamp::parseSnapshot("", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::malformed_json);
}

TEST(BitstampSnapshot, RejectsGarbageInputWithoutAcceptingIt) {
    for (const char* garbage : {"this is not json", R"({"microtimestamp":)", "12345", "[1,2,3]",
                                R"({"hello":"world"})"}) {
        const auto result = te::bitstamp::parseSnapshot(garbage, btcUsd());
        EXPECT_FALSE(result.hasValue()) << "silently accepted: " << garbage;
    }
}

TEST(BitstampSnapshot, RejectsMissingMicrotimestamp) {
    const auto result = te::bitstamp::parseSnapshot(R"({"bids":[],"asks":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::missing_microtimestamp);
}

// Regression: this field was once dereferenced without checking whether parsing succeeded, so a
// non-numeric timestamp read through a null pointer instead of returning an error.
TEST(BitstampSnapshot, RejectsUnparseableMicrotimestampWithoutCrashing) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"not-a-number","bids":[],"asks":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::missing_microtimestamp);
}

TEST(BitstampSnapshot, RejectsMissingBids) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1700000000000000","asks":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::missing_bids);
}

TEST(BitstampSnapshot, RejectsMissingAsks) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1700000000000000","bids":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::missing_asks);
}

// ---- row-level failures

TEST(BitstampSnapshot, RejectsRowWithTooFewFields) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["100.00","0.50000000"]],"asks":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::wrong_field_count);
}

TEST(BitstampSnapshot, RejectsRowWithTooManyFields) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["100.00","0.50000000","1","extra"]],"asks":[]})",
        btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::wrong_field_count);
}

TEST(BitstampSnapshot, RejectsNonNumericPrice) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["not-a-price","0.50000000","1"]],"asks":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::invalid_price);
}

// Three price decimals against a scale of two is excess precision, not a roundable value. The
// parser must refuse rather than silently drop a digit.
TEST(BitstampSnapshot, RejectsPriceWithMorePrecisionThanTheScaleAllows) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["100.123","0.50000000","1"]],"asks":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::invalid_price);
}

TEST(BitstampSnapshot, RejectsNonNumericQuantity) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["100.00","lots","1"]],"asks":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::invalid_quantity);
}

TEST(BitstampSnapshot, RejectsNonNumericOrderId) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["100.00","0.50000000","abc"]],"asks":[]})", btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::invalid_order_id);
}

// ---- duplicate ids
//
// A snapshot is the book's starting state. Two rows claiming the same id make that state
// ambiguous, and the parser must not leave the caller to guess which copy is real.

TEST(BitstampSnapshot, RejectsDuplicateOrderIdOnTheSameSide) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["100.00","0.50000000","42"],["100.01","0.60000000","42"]],"asks":[]})",
        btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::duplicate_order_id);
}

TEST(BitstampSnapshot, RejectsDuplicateOrderIdAcrossSides) {
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["100.00","0.50000000","42"]],"asks":[["101.00","0.60000000","42"]]})",
        btcUsd());

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(*result.errorIf(), te::bitstamp::SnapshotError::duplicate_order_id);
}

// ---- scale is supplied, not assumed

// The same text parses to different integers under a different InstrumentSpec. Proves the parser
// reads scale from the spec rather than hardcoding BTC/USD's 2 and 8.
TEST(BitstampSnapshot, UsesTheSuppliedScalesRatherThanAssumingBtcUsd) {
    const te::InstrumentSpec synthetic{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 4,
        .quantity_decimals = 3,
    };
    const auto result = te::bitstamp::parseSnapshot(
        R"({"microtimestamp":"1","bids":[["100.00","0.500","7"]],"asks":[]})", synthetic);

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.valueIf()->orders.size(), 1U);
    EXPECT_EQ(result.valueIf()->orders[0].price, te::Price{1'000'000});  // "100.00" at scale 4
    EXPECT_EQ(result.valueIf()->orders[0].quantity, te::Qty{500});       // "0.500" at scale 3
}

// ---- the committed fixture file, and the real capture

TEST(BitstampSnapshot, ParsesTheCommittedFixtureFile) {
    const std::string path = std::string(TE_TEST_DATA_DIR) + "/bitstamp_snapshot_sample.json";
    std::ifstream in(path);
    ASSERT_TRUE(in.is_open()) << "fixture missing: " << path;
    std::stringstream buffer;
    buffer << in.rdbuf();

    const auto result = te::bitstamp::parseSnapshot(buffer.str(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->orders.size(), 4U);
    EXPECT_EQ(result.valueIf()->microtimestamp, 1'700'000'000'000'000ULL);
}

// The real 392KB group=2 snapshot: 4,174 bids and 4,563 asks, no duplicate ids anywhere. Skips
// cleanly when the capture is absent so a fresh clone still passes.
TEST(BitstampSnapshot, ParsesTheRealCaptureSnapshot) {
    const std::string path = std::string(TE_PROJECT_ROOT_DIR) +
                             "/data/raw/bitstamp-btcusd-20260809T100421Z/segment-0000.snapshot";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "capture not present: " << path;
    }
    std::ifstream in(path);
    ASSERT_TRUE(in.is_open());
    std::stringstream buffer;
    buffer << in.rdbuf();

    const auto result = te::bitstamp::parseSnapshot(buffer.str(), btcUsd());
    ASSERT_TRUE(result.hasValue());

    const auto& snapshot = *result.valueIf();
    EXPECT_EQ(snapshot.microtimestamp, 1'786'269'861'574'036ULL);
    EXPECT_EQ(snapshot.orders.size(), 8'737U);

    std::size_t buys = 0;
    std::size_t sells = 0;
    for (const te::bitstamp::SnapshotOrder& order : snapshot.orders) {
        if (order.side == te::Side::buy) {
            ++buys;
        } else {
            ++sells;
        }
    }
    EXPECT_EQ(buys, 4'174U);
    EXPECT_EQ(sells, 4'563U);

    // First row of the real bids array, field-exact.
    EXPECT_EQ(snapshot.orders[0].order_id, te::OrderId{2'037'492'791'283'712ULL});
    EXPECT_EQ(snapshot.orders[0].price, te::Price{6'484'011});      // "64840.11"
    EXPECT_EQ(snapshot.orders[0].quantity, te::Qty{22'992'207});    // "0.22992207"
    EXPECT_EQ(snapshot.orders[0].side, te::Side::buy);
}
