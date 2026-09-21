#include <gtest/gtest.h>

#include <cstdint>

#include <te/engine/portfolio.hpp>

// Every expected number here was worked by hand before any implementation existed (TODO D1), and
// each row was checked two ways: equity must equal cash plus the position's market value, and
// realized plus unrealized must equal the change in equity. The rules are ADR 0014 D5.
//
// These tests are the specification. If the implementation disagrees, the implementation is wrong
// until the numbers are re-derived and this file is changed deliberately.
//
// Every case is DISABLED_ because Portfolio::applyFill is not written yet. They would all fail,
// and a suite that is red for days stops being a signal -- a real regression elsewhere would sit
// unnoticed among expected failures. Delete one DISABLED_ prefix as each case starts passing;
// that is the progress ladder through D2, roughly in the order below.
//
// Two of these pass even against the unimplemented stub, for the wrong reasons: the stub returns
// invalid_quantity for everything, and it returns a zero Money. They only become real evidence
// once applyFill exists.

namespace {

// Money is eight decimal places below one currency unit (D5 rule 9), which makes literal amounts
// unreadable: 100.50 is 10050000000. This builds them from whole units and hundredths so the test
// bodies show the same figures as the paper sheet. Integer-only on purpose -- no double may touch
// a money value anywhere, including in a test helper.
constexpr te::Money money(std::int64_t whole, std::int64_t hundredths = 0) {
    return te::Money{whole * 100'000'000 + hundredths * 1'000'000};
}

te::InstrumentSpec btcUsd() {
    return te::InstrumentSpec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
}

// Quantities are raw unit counts rather than realistic satoshi amounts. Position arithmetic and
// the basis split are both ratio-based, so the scale cancels; small numbers keep the sheet legible.
te::Fill buy(std::uint64_t id, std::int64_t quantity, te::Money notional, te::Money fee) {
    return te::Fill{
        .execution_id = te::ExecutionId{id},
        .side = te::Side::buy,
        .price = te::Price{100},
        .quantity = te::Qty{quantity},
        .notional = notional,
        .fee = fee,
    };
}

te::Fill sell(std::uint64_t id, std::int64_t quantity, te::Money notional, te::Money fee) {
    return te::Fill{
        .execution_id = te::ExecutionId{id},
        .side = te::Side::sell,
        .price = te::Price{100},
        .quantity = te::Qty{quantity},
        .notional = notional,
        .fee = fee,
    };
}

// Starting cash. The sheet begins every scenario from 1000 so the equity check is easy to read.
te::Portfolio openedAccount() {
    te::Portfolio portfolio{btcUsd()};
    return portfolio;
}

void expectAccount(const te::Portfolio& portfolio, te::Money cash, std::int64_t position,
                   te::Money basis, te::Money realized, te::Money fees) {
    EXPECT_EQ(portfolio.cash(), cash);
    EXPECT_EQ(portfolio.position().units, position);
    EXPECT_EQ(portfolio.basis(), basis);
    EXPECT_EQ(portfolio.realized(), realized);
    EXPECT_EQ(portfolio.feesPaid(), fees);
}

}  // namespace

// ------------------------------------------------------------------------------------------
// The long cycle: open, close half, close the rest. Cases 1-3 of the sheet.
// ------------------------------------------------------------------------------------------

TEST(Portfolio, DISABLED_OpeningALongFoldsTheFeeIntoBasis) {
    te::Portfolio portfolio = openedAccount();

    const auto outcome = portfolio.applyFill(buy(1, 2, money(200), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    EXPECT_TRUE(outcome.valueIf()->applied);
    // Opening never realizes: there is nothing closed to realize against (rule 8).
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(0));
    // Basis is 201, not 200. The fee was part of acquiring the position (rule 4), and it appears
    // in two places on purpose -- it left cash and it raised what the position cost.
    expectAccount(portfolio, money(-201), 2, money(201), money(0), money(1));
}

TEST(Portfolio, DISABLED_ClosingHalfALongRealizesNetOfTheFee) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(buy(1, 2, money(200), money(1))).hasValue());

    const auto outcome = portfolio.applyFill(sell(2, 1, money(110), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    // Half the receipt leaves with the unit sold: 201 splits into 100.50 and 100.50 (rule 12).
    // Realized is 109 received after the fee, less the 100.50 that unit cost.
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(8, 50));
    expectAccount(portfolio, money(-92), 1, money(100, 50), money(8, 50), money(2));
}

TEST(Portfolio, DISABLED_ClosingTheRestAtALossLeavesBasisAndPositionAtZero) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(buy(1, 2, money(200), money(1))).hasValue());
    ASSERT_TRUE(portfolio.applyFill(sell(2, 1, money(110), money(1))).hasValue());

    const auto outcome = portfolio.applyFill(sell(3, 1, money(90), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    // 89 received after the fee against a cost of 100.50.
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(-11, 50));
    // Bought 2 for 200 and sold them for 200, so the trading made nothing and the whole -3 is the
    // three fees. Basis reaches zero exactly as position does (rule 7).
    expectAccount(portfolio, money(-3), 0, money(0), money(-3), money(3));
}

// ------------------------------------------------------------------------------------------
// The short cycle: the same arithmetic with the signs flipped. Cases 4-6 of the sheet.
// ------------------------------------------------------------------------------------------

TEST(Portfolio, DISABLED_OpeningAShortSubtractsTheFeeFromProceeds) {
    te::Portfolio portfolio = openedAccount();

    const auto outcome = portfolio.applyFill(sell(1, 2, money(200), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(0));
    // Basis is 199, not 201. A short receives money, so the fee makes it receive less (rule 4).
    // Adding the fee here would make a flat round trip report zero profit while both fees had
    // genuinely left cash -- basis must agree with the direction cash actually moved.
    expectAccount(portfolio, money(199), -2, money(199), money(0), money(1));
}

TEST(Portfolio, DISABLED_CoveringHalfAShortMirrorsTheLongCase) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(sell(1, 2, money(200), money(1))).hasValue());

    const auto outcome = portfolio.applyFill(buy(2, 1, money(90), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    // Got 99.50 for that unit, paid 91 to buy it back. Identical to the long partial close,
    // because a short is the same accounting with the signs reversed.
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(8, 50));
    expectAccount(portfolio, money(108), -1, money(99, 50), money(8, 50), money(2));
}

TEST(Portfolio, DISABLED_CoveringTheRestOfAShortClosesItOut) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(sell(1, 2, money(200), money(1))).hasValue());
    ASSERT_TRUE(portfolio.applyFill(buy(2, 1, money(90), money(1))).hasValue());

    const auto outcome = portfolio.applyFill(buy(3, 1, money(105), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    // Got 99.50, paid 106 to close. A loss on this fill, but the running total stays positive.
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(-6, 50));
    // Sold for 200, bought back for 195: 5 gross, less 3 fees.
    expectAccount(portfolio, money(2), 0, money(0), money(2), money(3));
}

// ------------------------------------------------------------------------------------------
// The reversal: one fill that closes a long and opens a short. Case 7 of the sheet.
// ------------------------------------------------------------------------------------------

TEST(Portfolio, DISABLED_SellingThroughFlatClosesTheLongAndOpensAShort) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(buy(1, 2, money(200), money(1))).hasValue());

    // Holding 2, selling 5: two units close the long, three open a short. The single fee of 1
    // splits by quantity, 2/5 to the closing part and 3/5 to the opening part (rule 6).
    const auto outcome = portfolio.applyFill(sell(2, 5, money(550), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    EXPECT_TRUE(outcome.valueIf()->reversed);
    // Closing half: 220 received less 0.40 of fee, against the whole basis of 201.
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(18, 60));
    // Opening half: 330 received less 0.60 of fee becomes the new short's basis.
    expectAccount(portfolio, money(349), -3, money(329, 40), money(18, 60), money(2));
}

TEST(Portfolio, DISABLED_BuyingThroughFlatClosesTheShortAndOpensALong) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(sell(1, 2, money(200), money(1))).hasValue());

    // The mirror of the case above, which is why it was never hand-worked: holding -2 and buying
    // 5 closes the short with two units and opens a long with three.
    const auto outcome = portfolio.applyFill(buy(2, 5, money(450), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    EXPECT_TRUE(outcome.valueIf()->reversed);
    // Closing half: got 199 for those two, pays 180 plus 0.40 of fee to buy them back.
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(18, 60));
    // Opening half: 270 paid plus 0.60 of fee becomes the new long's cost.
    expectAccount(portfolio, money(-251), 3, money(270, 60), money(18, 60), money(2));
}

// ------------------------------------------------------------------------------------------
// Duplicates. A redelivered fill is ignored, not rejected (rule 10).
// ------------------------------------------------------------------------------------------

TEST(Portfolio, DISABLED_ARepeatedExecutionIdIsIgnoredWithoutChangingTheAccount) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(buy(1, 2, money(200), money(1))).hasValue());

    const auto outcome = portfolio.applyFill(buy(1, 2, money(200), money(1)));

    // Success, not failure: redelivery after a reconnect is normal protocol behaviour.
    ASSERT_TRUE(outcome.hasValue());
    EXPECT_FALSE(outcome.valueIf()->applied);
    EXPECT_EQ(outcome.valueIf()->realizedDelta, money(0));
    // Unchanged from the single application above. Applying it twice would report owning 4.
    expectAccount(portfolio, money(-201), 2, money(201), money(0), money(1));
}

TEST(Portfolio, DISABLED_ADuplicateIsRecognisedByIdAloneEvenIfOtherFieldsDiffer) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(buy(1, 2, money(200), money(1))).hasValue());

    // Same execution id, different everything else. The id is the venue's statement of identity;
    // trusting the other fields instead would let a corrupted redelivery through.
    const auto outcome = portfolio.applyFill(sell(1, 7, money(999), money(5)));

    ASSERT_TRUE(outcome.hasValue());
    EXPECT_FALSE(outcome.valueIf()->applied);
    expectAccount(portfolio, money(-201), 2, money(201), money(0), money(1));
}

// ------------------------------------------------------------------------------------------
// Rejection. Every refused fill leaves the account completely unchanged (rule 11).
// ------------------------------------------------------------------------------------------

TEST(Portfolio, DISABLED_AZeroQuantityFillIsRejectedAndChangesNothing) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(buy(1, 2, money(200), money(1))).hasValue());

    const auto outcome = portfolio.applyFill(buy(2, 0, money(0), money(1)));

    ASSERT_FALSE(outcome.hasValue());
    ASSERT_NE(outcome.errorIf(), nullptr);
    EXPECT_EQ(*outcome.errorIf(), te::FillError::invalid_quantity);
    expectAccount(portfolio, money(-201), 2, money(201), money(0), money(1));
}

TEST(Portfolio, DISABLED_ANegativeQuantityFillIsRejected) {
    te::Portfolio portfolio = openedAccount();

    const auto outcome = portfolio.applyFill(buy(1, -3, money(300), money(1)));

    ASSERT_FALSE(outcome.hasValue());
    ASSERT_NE(outcome.errorIf(), nullptr);
    // Direction is carried by side, never by a negative quantity; allowing both would give two
    // ways to express a sell and one of them would eventually disagree with the other.
    EXPECT_EQ(*outcome.errorIf(), te::FillError::invalid_quantity);
    expectAccount(portfolio, money(0), 0, money(0), money(0), money(0));
}

TEST(Portfolio, DISABLED_ARejectedFillDoesNotConsumeItsExecutionId) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_FALSE(portfolio.applyFill(buy(1, 0, money(0), money(1))).hasValue());

    // The venue may legitimately reuse the id on a corrected message. Recording an id that was
    // never applied would silently swallow that correction.
    const auto outcome = portfolio.applyFill(buy(1, 2, money(200), money(1)));

    ASSERT_TRUE(outcome.hasValue());
    EXPECT_TRUE(outcome.valueIf()->applied);
    expectAccount(portfolio, money(-201), 2, money(201), money(0), money(1));
}

// ------------------------------------------------------------------------------------------
// Unrealized valuation. Needs an explicit mark and never touches the fill path.
// ------------------------------------------------------------------------------------------

TEST(Portfolio, DISABLED_UnrealizedIsZeroWhenFlat) {
    te::Portfolio portfolio = openedAccount();

    // No open position, so there is nothing to value at any price.
    EXPECT_EQ(portfolio.unrealizedAt(te::Price{100}), money(0));
    EXPECT_EQ(portfolio.unrealizedAt(te::Price{100000}), money(0));
}

TEST(Portfolio, DISABLED_AFreshLongIsUnderwaterByItsFee) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(buy(1, 2, money(200), money(1))).hasValue());

    // Marked at the price just traded, the position is worth 200 against a basis of 201. The gap
    // is exactly the fee, which is why a position opens slightly underwater.
    EXPECT_EQ(portfolio.unrealizedAt(te::Price{10000}), money(-1));
}

TEST(Portfolio, DISABLED_RealizedPlusUnrealizedAccountsForTheWholeEquityChange) {
    te::Portfolio portfolio = openedAccount();
    ASSERT_TRUE(portfolio.applyFill(buy(1, 2, money(200), money(1))).hasValue());
    ASSERT_TRUE(portfolio.applyFill(sell(2, 1, money(110), money(1))).hasValue());

    // The check applied to every row of the paper sheet. Marked at 110: one unit is worth 110
    // against a basis of 100.50, so unrealized is 9.50, and 8.50 realized plus 9.50 unrealized is
    // the 18 the account is up. If these two ever fail to reconcile, a number is wrong.
    EXPECT_EQ(portfolio.realized(), money(8, 50));
    EXPECT_EQ(portfolio.unrealizedAt(te::Price{11000}), money(9, 50));
}
